#include "HtmlPanelView.h"

#include "UltralightPlatformSetup.h"

#include "../../Gfx/VulkanDevice.h"
#include "../../Gfx/VulkanBuffer.h"
#include "../../Gfx/VulkanTexture.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <Ultralight/Bitmap.h>
#include <Ultralight/MouseEvent.h>
#include <Ultralight/Renderer.h>
#include <Ultralight/ScrollEvent.h>
#include <Ultralight/View.h>
#include <Ultralight/platform/Surface.h>

#include <AppCore/JSHelpers.h>

#include <algorithm>
#include <cstring>
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

  uint32_t width = 0;
  uint32_t height = 0;

  // Cached ImGui image rect for input routing
  ImVec2 lastMin{0, 0};
  ImVec2 lastMax{0, 0};

  // State for mouse button transitions
  bool lastMouseDown = false;

  void EnsureTextureCreated() {
    if (texture)
      return;
    texture = std::make_unique<VulkanTexture>();
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
                           VK_FORMAT_R8G8B8A8_SRGB,
                           VK_IMAGE_TILING_OPTIMAL,
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        return;
      }

      texture->CreateImageView(vkdev->device, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
      texture->CreateSampler(vkdev->device, vkdev->physicalDevice);

      // Reset descriptor set
      imguiTextureSet = VK_NULL_HANDLE;
    }

    bitmap->LockPixels();
    const uint8_t* src = reinterpret_cast<const uint8_t*>(bitmap->raw_pixels());
    const uint32_t rowBytes = bitmap->row_bytes();

    // Ultralight Bitmap is BGRA8 by default on desktop.
    // Convert to RGBA8 in a tight buffer.
    std::vector<uint8_t> rgba;
    rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);

    for (uint32_t y = 0; y < h; ++y) {
      const uint8_t* row = src + static_cast<size_t>(y) * rowBytes;
      uint8_t* out = rgba.data() + static_cast<size_t>(y) * w * 4;
      for (uint32_t x = 0; x < w; ++x) {
        const uint8_t b = row[x * 4 + 0];
        const uint8_t g = row[x * 4 + 1];
        const uint8_t r = row[x * 4 + 2];
        const uint8_t a = row[x * 4 + 3];
        out[x * 4 + 0] = r;
        out[x * 4 + 1] = g;
        out[x * 4 + 2] = b;
        out[x * 4 + 3] = a;
      }
    }

    bitmap->UnlockPixels();

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

    VulkanBuffer staging;
    staging.Create(vkdev->allocator,
                   imageSize,
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VMA_MEMORY_USAGE_CPU_ONLY);

    void* data = nullptr;
    vmaMapMemory(vkdev->allocator, staging.allocation, &data);
    std::memcpy(data, rgba.data(), static_cast<size_t>(imageSize));
    vmaUnmapMemory(vkdev->allocator, staging.allocation);

    // Transition and copy.
    auto cmd = BeginSingleTimeCommands(vkdev->device, vkdev->commandPool);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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
                           staging.buffer,
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

    EndSingleTimeCommands(vkdev->device, vkdev->commandPool, vkdev->graphicsQueue, cmd);

    staging.Destroy(vkdev->allocator);

    EnsureImGuiDescriptor();
  }

  static VkCommandBuffer BeginSingleTimeCommands(VkDevice device, VkCommandPool commandPool) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
  }

  static void EndSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
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
  impl_->width = std::max(1u, width);
  impl_->height = std::max(1u, height);
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

void HtmlPanelView::HandleImGuiInput() {
  if (!impl_->view)
    return;

  // Only handle mouse when the image is hovered.
  ImVec2 mouse = ImGui::GetMousePos();
  bool hovered = (mouse.x >= impl_->lastMin.x && mouse.x <= impl_->lastMax.x &&
                  mouse.y >= impl_->lastMin.y && mouse.y <= impl_->lastMax.y);

  if (!hovered)
    return;

  const float w = std::max(1.0f, impl_->lastMax.x - impl_->lastMin.x);
  const float h = std::max(1.0f, impl_->lastMax.y - impl_->lastMin.y);

  const float localXf = (mouse.x - impl_->lastMin.x) * (static_cast<float>(impl_->width) / w);
  const float localYf = (mouse.y - impl_->lastMin.y) * (static_cast<float>(impl_->height) / h);

  const int localX = static_cast<int>(std::clamp(localXf, 0.0f, static_cast<float>(impl_->width - 1)));
  const int localY = static_cast<int>(std::clamp(localYf, 0.0f, static_cast<float>(impl_->height - 1)));

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

} // namespace Hephaestus::HtmlUI
