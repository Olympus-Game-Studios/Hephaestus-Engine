#include "HtmlPanelView.h"

#include "UltralightPlatformSetup.h"

#include "../../Gfx/VulkanDevice.h"
#include "../../Gfx/VulkanBuffer.h"
#include "../../Gfx/VulkanTexture.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <Ultralight/Bitmap.h>
#include <Ultralight/MouseEvent.h>
#include <Ultralight/KeyEvent.h>
#include <Ultralight/Renderer.h>
#include <Ultralight/ScrollEvent.h>
#include <Ultralight/View.h>
#include <Ultralight/platform/Surface.h>

#include <AppCore/JSHelpers.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <vector>

namespace Hephaestus::HtmlUI {

struct HtmlPanelView::Impl : public ultralight::LoadListener {
  std::string debugName;
  EditorMessageBridge* bridge = nullptr;

  VulkanDevice* vkdev = nullptr;
  VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;

  ultralight::RefPtr<ultralight::View> view;

  std::unique_ptr<VulkanTexture> texture;
  VkDescriptorSet imguiTextureSet = VK_NULL_HANDLE;

  static constexpr uint32_t kUploadSlots = 3;
  struct UploadSlot {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VulkanBuffer staging;
    VkDeviceSize stagingSize = 0;
    bool created = false;
  };

  std::array<UploadSlot, kUploadSlots> uploadSlots{};
  uint32_t uploadSlotIndex = 0;

  // Track current image layout for correct barriers.
  VkImageLayout textureLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkFormat textureFormat = VK_FORMAT_B8G8R8A8_SRGB;

  uint32_t width = 0;
  uint32_t height = 0;

  // Resize throttling to avoid GPU stalls during aggressive window drags.
  uint32_t pendingWidth = 0;
  uint32_t pendingHeight = 0;
  uint32_t lastRequestedWidth = 0;
  uint32_t lastRequestedHeight = 0;
  bool hasPendingResize = false;
  std::chrono::steady_clock::time_point lastResizeRequestTime{};
  std::chrono::steady_clock::time_point lastResizeAppliedTime{};

  // Cached ImGui image rect for input routing
  ImVec2 lastMin{0, 0};
  ImVec2 lastMax{0, 0};

  // State for mouse button transitions
  bool lastMouseDown = false;       // Left mouse button
  bool lastRightMouseDown = false;  // Right mouse button

  // Whether this view currently owns keyboard focus
  bool focused = false;

  ~Impl() {
    if (!vkdev || vkdev->device == VK_NULL_HANDLE)
      return;

    // Ensure no in-flight upload is still using our resources.
    for (auto& slot : uploadSlots) {
      if (slot.fence != VK_NULL_HANDLE) {
        vkWaitForFences(vkdev->device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
      }
    }

    for (auto& slot : uploadSlots) {
      if (slot.created) {
        slot.staging.Destroy(vkdev->allocator);
      }
      if (slot.fence != VK_NULL_HANDLE) {
        vkDestroyFence(vkdev->device, slot.fence, nullptr);
        slot.fence = VK_NULL_HANDLE;
      }
    }

    // Command buffers were allocated from the shared command pool.
    // Free them after we're sure they are no longer in use.
    std::array<VkCommandBuffer, kUploadSlots> bufs{};
    uint32_t count = 0;
    for (auto& slot : uploadSlots) {
      if (slot.cmd != VK_NULL_HANDLE) {
        bufs[count++] = slot.cmd;
        slot.cmd = VK_NULL_HANDLE;
      }
    }
    if (count > 0) {
      vkFreeCommandBuffers(vkdev->device, vkdev->commandPool, count, bufs.data());
    }
  }

  void EnsureTextureCreated() {
    if (texture)
      return;
    texture = std::make_unique<VulkanTexture>();
  }

  void EnsureUploadSlotsCreated() {
    if (!vkdev || vkdev->device == VK_NULL_HANDLE)
      return;

    bool anyMissing = false;
    for (auto& slot : uploadSlots) {
      if (slot.cmd == VK_NULL_HANDLE || slot.fence == VK_NULL_HANDLE) {
        anyMissing = true;
        break;
      }
    }
    if (!anyMissing)
      return;

    // Create fences (signaled initially so first use doesn't block).
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (auto& slot : uploadSlots) {
      if (slot.fence == VK_NULL_HANDLE) {
        vkCreateFence(vkdev->device, &fenceInfo, nullptr, &slot.fence);
      }
    }

    // Allocate command buffers.
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = vkdev->commandPool;
    allocInfo.commandBufferCount = kUploadSlots;

    std::array<VkCommandBuffer, kUploadSlots> allocated{};
    if (vkAllocateCommandBuffers(vkdev->device, &allocInfo, allocated.data()) == VK_SUCCESS) {
      for (uint32_t i = 0; i < kUploadSlots; ++i) {
        uploadSlots[i].cmd = allocated[i];
      }
    }
  }

  UploadSlot& AcquireUploadSlot() {
    EnsureUploadSlotsCreated();
    UploadSlot& slot = uploadSlots[uploadSlotIndex % kUploadSlots];
    uploadSlotIndex = (uploadSlotIndex + 1) % kUploadSlots;

    if (vkdev && vkdev->device != VK_NULL_HANDLE && slot.fence != VK_NULL_HANDLE) {
      // Wait only for this slot's previous upload, not the entire queue.
      vkWaitForFences(vkdev->device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
      vkResetFences(vkdev->device, 1, &slot.fence);
    }

    if (vkdev && vkdev->device != VK_NULL_HANDLE && slot.cmd != VK_NULL_HANDLE) {
      vkResetCommandBuffer(slot.cmd, 0);
    }
    return slot;
  }

  void EnsureImGuiDescriptor() {
    if (imguiTextureSet != VK_NULL_HANDLE)
      return;
    if (!texture || texture->imageView == VK_NULL_HANDLE || texture->sampler == VK_NULL_HANDLE)
      return;

    imguiTextureSet = ImGui_ImplVulkan_AddTexture(texture->sampler,
                                                  texture->imageView,
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  virtual void OnDOMReady(ultralight::View* caller,
                          uint64_t frame_id,
                          bool is_main_frame,
                          const ultralight::String& url) override {
    (void)frame_id;
    (void)is_main_frame;
    (void)url;

    using namespace ultralight;

    RefPtr<JSContext> context = caller->LockJSContext();
    SetJSContext(context->ctx());

    JSObject global = JSGlobalObject();

    // Expose: window.hepSend(messageString) -> returns responseString
    global["hepSend"] = JSCallbackWithRetval([this](const JSObject&, const JSArgs& args) -> JSValue {
      if (args.size() < 1) {
        return JSValue("{\"ok\":false,\"error\":\"missing_arg\"}");
      }

      ultralight::String msgUL = args[0].ToString();
      std::string msg = msgUL.utf8().data();

      auto resp = bridge ? bridge->HandleFromJs(msg) : std::optional<std::string>{};
      if (!resp) {
        return JSValue("{\"ok\":false,\"error\":\"no_response\"}");
      }
      return JSValue(resp->c_str());
    });

    // Publish bridge version
    global["hepBridgeVersion"] = JSValue(1);
  }

  void FireMouse(ultralight::MouseEvent::Type type, int x, int y,
                 ultralight::MouseEvent::Button button) {
    if (!view)
      return;
    ultralight::MouseEvent evt;
    evt.type = type;
    evt.x = x;
    evt.y = y;
    evt.button = button;
    view->FireMouseEvent(evt);
  }

  static unsigned BuildUltralightModifiers(const ImGuiIO& io) {
    unsigned mods = 0;
    if (io.KeyAlt)
      mods |= ultralight::KeyEvent::kMod_AltKey;
    if (io.KeyCtrl)
      mods |= ultralight::KeyEvent::kMod_CtrlKey;
    if (io.KeyShift)
      mods |= ultralight::KeyEvent::kMod_ShiftKey;
    if (io.KeySuper)
      mods |= ultralight::KeyEvent::kMod_MetaKey;
    return mods;
  }

  void FireKeyRaw(ultralight::KeyEvent::Type type, int virtualKeyCode, int nativeKeyCode, unsigned modifiers) {
    if (!view)
      return;
    ultralight::KeyEvent evt;
    evt.type = type;
    evt.virtual_key_code = virtualKeyCode;
    evt.native_key_code = nativeKeyCode;
    evt.modifiers = modifiers;
    ultralight::GetKeyIdentifierFromVirtualKeyCode(evt.virtual_key_code, evt.key_identifier);
    evt.is_system_key = false;
    view->FireKeyEvent(evt);
  }

  void FireChar(char32_t codepoint) {
    if (!view)
      return;
    ultralight::KeyEvent evt;
    evt.type = ultralight::KeyEvent::kType_Char;
    ultralight::String text = ultralight::String32((const char32_t*)&codepoint, 1);
    evt.text = text;
    evt.unmodified_text = text;
    view->FireKeyEvent(evt);
  }

  void FireScroll(int x, int y, int dx, int dy) {
    if (!view)
      return;
    ultralight::ScrollEvent evt;
    evt.type = ultralight::ScrollEvent::kType_ScrollByPixel;
    evt.delta_x = static_cast<int>(dx);
    evt.delta_y = static_cast<int>(dy);
    (void)x;
    (void)y;
    view->FireScrollEvent(evt);
  }

  void SyncTextureToGpu() {
    if (!view)
      return;

    // Ensure view surface exists and is BitmapSurface.
    auto* surface = static_cast<ultralight::BitmapSurface*>(view->surface());
    if (!surface)
      return;

    // Avoid re-uploading when nothing changed.
    if (surface->dirty_bounds().IsEmpty() && texture && texture->image) {
      return;
    }

    ultralight::RefPtr<ultralight::Bitmap> bitmap = surface->bitmap();
    if (!bitmap)
      return;

    EnsureTextureCreated();

    const uint32_t w = bitmap->width();
    const uint32_t h = bitmap->height();

    if (w == 0 || h == 0)
      return;

    // (Re)create GPU texture if needed.
    if (!texture->image || texture->width != w || texture->height != h) {
      texture->Destroy(vkdev->allocator, vkdev->device);

      // Create a texture we can upload to (transfer dst) and sample.
      // We keep it SRGB to match most UI assets.
      if (!texture->Create(vkdev->allocator, vkdev->device, w, h,
                           textureFormat,
                           VK_IMAGE_TILING_OPTIMAL,
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        return;
      }

      texture->CreateImageView(vkdev->device, textureFormat, VK_IMAGE_ASPECT_COLOR_BIT);
      texture->CreateSampler(vkdev->device, vkdev->physicalDevice);

      textureLayout = VK_IMAGE_LAYOUT_UNDEFINED;

      // Reset descriptor set
      imguiTextureSet = VK_NULL_HANDLE;
    }

    bitmap->LockPixels();
    const uint8_t* src = reinterpret_cast<const uint8_t*>(bitmap->raw_pixels());
    const uint32_t rowBytes = bitmap->row_bytes();

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

    UploadSlot& slot = AcquireUploadSlot();

    // Ensure staging buffer capacity.
    if (!slot.created || slot.stagingSize < imageSize) {
      if (slot.created) {
        slot.staging.Destroy(vkdev->allocator);
      }
      slot.staging.Create(vkdev->allocator,
                          imageSize,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VMA_MEMORY_USAGE_CPU_ONLY);
      slot.stagingSize = imageSize;
      slot.created = true;
    }

    // Pack rows tightly into staging buffer (Ultralight often has row padding).
    void* mapped = nullptr;
    vmaMapMemory(vkdev->allocator, slot.staging.allocation, &mapped);
    uint8_t* dst = reinterpret_cast<uint8_t*>(mapped);
    const size_t tightRowBytes = static_cast<size_t>(w) * 4;
    for (uint32_t y = 0; y < h; ++y) {
      const uint8_t* row = src + static_cast<size_t>(y) * rowBytes;
      std::memcpy(dst + static_cast<size_t>(y) * tightRowBytes, row, tightRowBytes);
    }
    vmaUnmapMemory(vkdev->allocator, slot.staging.allocation);

    bitmap->UnlockPixels();

    // We've captured the pixels for this frame; clear the dirty region so we can
    // skip future uploads until the view changes again.
    surface->ClearDirtyBounds();

    // Record transition + copy.
    VkCommandBuffer cmd = slot.cmd;
    if (cmd == VK_NULL_HANDLE)
      return;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = textureLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = (textureLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                  ? VK_ACCESS_SHADER_READ_BIT
                  : 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    VkPipelineStageFlags srcStage =
      (textureLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    vkCmdPipelineBarrier(cmd,
               srcStage,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {w, h, 1};

    vkCmdCopyBufferToImage(cmd,
                           slot.staging.buffer,
                           texture->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);

    VkImageMemoryBarrier barrier2 = barrier;
    barrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier2);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(vkdev->graphicsQueue, 1, &submitInfo, slot.fence);

    textureLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    EnsureImGuiDescriptor();
  }
};

HtmlPanelView::HtmlPanelView(std::string debugName,
                             EditorMessageBridge* bridge,
                             VulkanDevice* vkdev,
                             VkDescriptorPool imguiDescriptorPool)
    : impl_(std::make_unique<Impl>()) {
  impl_->debugName = std::move(debugName);
  impl_->bridge = bridge;
  impl_->vkdev = vkdev;
  impl_->imguiDescriptorPool = imguiDescriptorPool;
}

HtmlPanelView::~HtmlPanelView() {
  if (impl_->texture) {
    impl_->texture->Destroy(impl_->vkdev->allocator, impl_->vkdev->device);
  }
}

void HtmlPanelView::Create(ultralight::RefPtr<ultralight::Renderer> renderer,
                           uint32_t width,
                           uint32_t height,
                           const std::string& url) {
  impl_->width = width;
  impl_->height = height;

  ultralight::ViewConfig config;
  config.is_transparent = true;
  impl_->view = renderer->CreateView(width, height, config, nullptr);
  impl_->view->set_load_listener(impl_.get());
  impl_->view->LoadURL(url.c_str());
}

void HtmlPanelView::Resize(uint32_t width, uint32_t height) {
  const uint32_t clampedW = std::max(1u, width);
  const uint32_t clampedH = std::max(1u, height);

  if (impl_->width == clampedW && impl_->height == clampedH && !impl_->hasPendingResize) {
    return;
  }

  // Track the most recent request.
  auto now = std::chrono::steady_clock::now();
  if (impl_->lastRequestedWidth != clampedW || impl_->lastRequestedHeight != clampedH) {
    impl_->lastRequestedWidth = clampedW;
    impl_->lastRequestedHeight = clampedH;
    impl_->lastResizeRequestTime = now;
  }

  impl_->pendingWidth = clampedW;
  impl_->pendingHeight = clampedH;
  impl_->hasPendingResize = true;

  constexpr auto kResizeMinInterval = std::chrono::milliseconds(50);
  constexpr auto kResizeDebounce = std::chrono::milliseconds(120);

  const auto sinceApplied = now - impl_->lastResizeAppliedTime;
  const auto sinceRequest = now - impl_->lastResizeRequestTime;
  const bool requestStable = sinceRequest >= kResizeDebounce;
  const bool rateAllowed = sinceApplied >= kResizeMinInterval;

  if (!requestStable && !rateAllowed) {
    return;
  }

  impl_->width = impl_->pendingWidth;
  impl_->height = impl_->pendingHeight;
  impl_->hasPendingResize = false;
  impl_->lastResizeAppliedTime = now;

  if (impl_->view) {
    impl_->view->Resize(impl_->width, impl_->height);
  }
}

void HtmlPanelView::SyncTextureToGpu() { impl_->SyncTextureToGpu(); }

void HtmlPanelView::EvaluateScript(const std::string& script) {
  if (!impl_->view)
    return;
  impl_->view->EvaluateScript(script.c_str());
}

void HtmlPanelView::DrawImGuiImage() {
  if (impl_->imguiTextureSet == VK_NULL_HANDLE) {
    ImGui::TextUnformatted("(HTML view not ready)");
    return;
  }

  ImVec2 avail = ImGui::GetContentRegionAvail();
  if (avail.x < 1.0f)
    avail.x = 1.0f;
  if (avail.y < 1.0f)
    avail.y = 1.0f;

  ImGui::Image(reinterpret_cast<ImTextureID>(impl_->imguiTextureSet), avail);

  impl_->lastMin = ImGui::GetItemRectMin();
  impl_->lastMax = ImGui::GetItemRectMax();
}

bool HtmlPanelView::IsHovered() const {
  ImVec2 mouse = ImGui::GetMousePos();
  return (mouse.x >= impl_->lastMin.x && mouse.x <= impl_->lastMax.x &&
          mouse.y >= impl_->lastMin.y && mouse.y <= impl_->lastMax.y);
}

void HtmlPanelView::HandleImGuiInput() {
  if (!impl_->view)
    return;

  // Only handle mouse when the image is hovered.
  ImVec2 mouse = ImGui::GetMousePos();
  bool hovered = (mouse.x >= impl_->lastMin.x && mouse.x <= impl_->lastMax.x &&
                  mouse.y >= impl_->lastMin.y && mouse.y <= impl_->lastMax.y);

  // Focus management: click inside to focus, click outside to unfocus.
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    if (hovered) {
      impl_->focused = true;
      impl_->view->Focus();
    } else if (impl_->focused) {
      impl_->focused = false;
      impl_->view->Unfocus();
    }
  }

  // If not hovered and not focused, do nothing.
  if (!hovered && !impl_->focused)
    return;

  const float w = std::max(1.0f, impl_->lastMax.x - impl_->lastMin.x);
  const float h = std::max(1.0f, impl_->lastMax.y - impl_->lastMin.y);

  const float localXf = (mouse.x - impl_->lastMin.x) * (static_cast<float>(impl_->width) / w);
  const float localYf = (mouse.y - impl_->lastMin.y) * (static_cast<float>(impl_->height) / h);

  const int localX = static_cast<int>(std::clamp(localXf, 0.0f, static_cast<float>(impl_->width - 1)));
  const int localY = static_cast<int>(std::clamp(localYf, 0.0f, static_cast<float>(impl_->height - 1)));

  // Mouse/scroll only when hovered (keyboard can be focused without hover).
  if (hovered) {
    // Mouse move
    impl_->FireMouse(ultralight::MouseEvent::kType_MouseMoved,
                     localX,
                     localY,
                     ultralight::MouseEvent::kButton_None);

    // Left button
    bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (down && !impl_->lastMouseDown) {
      impl_->FireMouse(ultralight::MouseEvent::kType_MouseDown,
                       localX,
                       localY,
                       ultralight::MouseEvent::kButton_Left);
    } else if (!down && impl_->lastMouseDown) {
      impl_->FireMouse(ultralight::MouseEvent::kType_MouseUp,
                       localX,
                       localY,
                       ultralight::MouseEvent::kButton_Left);
    }
    impl_->lastMouseDown = down;

    // Right button (for context menu support)
    bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (rightDown && !impl_->lastRightMouseDown) {
      impl_->FireMouse(ultralight::MouseEvent::kType_MouseDown,
                       localX,
                       localY,
                       ultralight::MouseEvent::kButton_Right);
    } else if (!rightDown && impl_->lastRightMouseDown) {
      impl_->FireMouse(ultralight::MouseEvent::kType_MouseUp,
                       localX,
                       localY,
                       ultralight::MouseEvent::kButton_Right);
    }
    impl_->lastRightMouseDown = rightDown;

    // Scroll
    ImVec2 wheel = ImGui::GetIO().MouseWheelH != 0 || ImGui::GetIO().MouseWheel != 0
                       ? ImVec2(ImGui::GetIO().MouseWheelH, ImGui::GetIO().MouseWheel)
                       : ImVec2(0, 0);
    if (wheel.x != 0.0f || wheel.y != 0.0f) {
      // ImGui wheel is lines, not pixels; scale a bit.
      const int dx = static_cast<int>(wheel.x * 60.0f);
      const int dy = static_cast<int>(wheel.y * 60.0f);
      impl_->FireScroll(localX, localY, dx, dy);
    }
  }

  // Keyboard + text input when focused.
  if (impl_->focused) {
    ImGuiIO& io = ImGui::GetIO();
    const unsigned mods = HtmlPanelView::Impl::BuildUltralightModifiers(io);

    auto sendKey = [&](ImGuiKey key, int ulVKey) {
      if (ImGui::IsKeyPressed(key, true)) {
        impl_->FireKeyRaw(ultralight::KeyEvent::kType_RawKeyDown, ulVKey, 0, mods);

        // Synthesize Char events for Enter/Tab like Ultralight samples.
        if (ulVKey == ultralight::KeyCodes::GK_RETURN) {
          impl_->FireChar(U'\r');
        } else if (ulVKey == ultralight::KeyCodes::GK_TAB) {
          impl_->FireChar(U'\t');
        }
      }
      if (ImGui::IsKeyReleased(key)) {
        impl_->FireKeyRaw(ultralight::KeyEvent::kType_KeyUp, ulVKey, 0, mods);
      }
    };

    sendKey(ImGuiKey_Backspace, ultralight::KeyCodes::GK_BACK);
    sendKey(ImGuiKey_Delete, ultralight::KeyCodes::GK_DELETE);
    sendKey(ImGuiKey_Enter, ultralight::KeyCodes::GK_RETURN);
    sendKey(ImGuiKey_KeypadEnter, ultralight::KeyCodes::GK_RETURN);
    sendKey(ImGuiKey_Tab, ultralight::KeyCodes::GK_TAB);
    sendKey(ImGuiKey_Escape, ultralight::KeyCodes::GK_ESCAPE);
    sendKey(ImGuiKey_LeftArrow, ultralight::KeyCodes::GK_LEFT);
    sendKey(ImGuiKey_RightArrow, ultralight::KeyCodes::GK_RIGHT);
    sendKey(ImGuiKey_UpArrow, ultralight::KeyCodes::GK_UP);
    sendKey(ImGuiKey_DownArrow, ultralight::KeyCodes::GK_DOWN);
    sendKey(ImGuiKey_Home, ultralight::KeyCodes::GK_HOME);
    sendKey(ImGuiKey_End, ultralight::KeyCodes::GK_END);

    // Basic clipboard accelerators (Ctrl+A/C/V/X)
    sendKey(ImGuiKey_A, ultralight::KeyCodes::GK_A);
    sendKey(ImGuiKey_C, ultralight::KeyCodes::GK_C);
    sendKey(ImGuiKey_V, ultralight::KeyCodes::GK_V);
    sendKey(ImGuiKey_X, ultralight::KeyCodes::GK_X);

    // Send text input
    for (int n = 0; n < io.InputQueueCharacters.Size; n++) {
      ImWchar c = io.InputQueueCharacters[n];
      if (c == 0)
        continue;
      impl_->FireChar(static_cast<char32_t>(c));
    }
    io.InputQueueCharacters.resize(0);
  }
}

} // namespace Hephaestus::HtmlUI
