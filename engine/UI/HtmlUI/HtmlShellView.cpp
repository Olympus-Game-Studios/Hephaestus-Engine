#include "HtmlShellView.h"

#include <Ultralight/Bitmap.h>
#include <Ultralight/MouseEvent.h>
#include <Ultralight/KeyEvent.h>
#include <Ultralight/ScrollEvent.h>
#include <Ultralight/platform/Surface.h>
#include <Ultralight/JavaScript.h>
#include <AppCore/JSHelpers.h>
#include <JavaScriptCore/JSValueRef.h>
#include <JavaScriptCore/JSStringRef.h>
#include <iostream>

#include "UltralightPlatformSetup.h"

namespace Hephaestus::HtmlUI {

struct ShellViewListener : public ultralight::ViewListener {
  void OnAddConsoleMessage(ultralight::View* caller,
                           const ultralight::ConsoleMessage& message) override {
    (void)caller;
    const char* levelStr = "LOG";
    switch (message.level()) {
      case ultralight::kMessageLevel_Warning: levelStr = "WARN"; break;
      case ultralight::kMessageLevel_Error: levelStr = "ERROR"; break;
      case ultralight::kMessageLevel_Debug: levelStr = "DEBUG"; break;
      case ultralight::kMessageLevel_Info: levelStr = "INFO"; break;
      default: break;
    }
    std::cerr << "[SHELL JS " << levelStr << "] " << message.message().utf8().data() << std::endl;
  }
};

struct ShellLoadListener : public ultralight::LoadListener {
  HtmlShellView* owner = nullptr;
  void OnDOMReady(ultralight::View* caller,
                  uint64_t frame_id,
                  bool is_main_frame,
                  const ultralight::String& url) override {
    (void)frame_id;
    (void)is_main_frame;
    (void)url;
    if (!owner) return;

    using namespace ultralight;
    RefPtr<JSContext> context = caller->LockJSContext();
    SetJSContext(context->ctx());
    JSObject global = JSGlobalObject();

    // Bridge function that JS can call to communicate with C++
    // JS calls: hep.callCpp(name, payloadObject) 
    // We convert to message format: {v:1, name:"...", payload:{...}}
    auto bridgeFunc = JSCallbackWithRetval([this](const JSObject&, const JSArgs& args) -> JSValue {
      if (args.size() < 1) {
        return JSValue("{\"ok\":false,\"error\":\"missing_name\"}");
      }
      ultralight::String nameUL = args[0].ToString();
      std::string name = nameUL.utf8().data();
      
      // Convert payload argument to JSON string using JSC API
      std::string payloadStr = "{}";
      if (args.size() >= 2) {
        JSContextRef ctx = GetJSContext();
        JSValueRef valRef = args[1];
        JSStringRef jsonStr = JSValueCreateJSONString(ctx, valRef, 0, nullptr);
        if (jsonStr) {
          size_t bufSize = JSStringGetMaximumUTF8CStringSize(jsonStr);
          std::string buf(bufSize, '\0');
          JSStringGetUTF8CString(jsonStr, &buf[0], bufSize);
          payloadStr = buf.c_str(); // Trim to actual length
          JSStringRelease(jsonStr);
        }
      }
      
      // Format as message for the bridge: {v:1, name:"...", payload:{...}}
      std::string msg = "{\"v\":1,\"name\":\"" + name + "\",\"payload\":" + payloadStr + "}";
      auto resp = owner->Bridge() ? owner->Bridge()->HandleFromJs(msg) : std::optional<std::string>{};
      if (!resp) {
        return JSValue("{\"ok\":false,\"error\":\"no_response\"}");
      }
      return JSValue(resp->c_str());
    });

    // Legacy hepSend (takes full message JSON)
    global["hepSend"] = JSCallbackWithRetval([this](const JSObject&, const JSArgs& args) -> JSValue {
      if (args.size() < 1) {
        return JSValue("{\"ok\":false,\"error\":\"missing_arg\"}");
      }
      ultralight::String msgUL = args[0].ToString();
      std::string msg = msgUL.utf8().data();
      auto resp = owner->Bridge() ? owner->Bridge()->HandleFromJs(msg) : std::optional<std::string>{};
      if (!resp) {
        return JSValue("{\"ok\":false,\"error\":\"no_response\"}");
      }
      return JSValue(resp->c_str());
    });

    // Create hep object with callCpp method for new API
    // Note: ui_common.js may have already set window.hep.callCpp that properly parses JSON
    // We store the native one as _nativeCallCpp and let ui_common.js wrapper handle it
    JSObject hepObj;
    
    // Check if hep object already exists (from ui_common.js)
    JSValue existingHep = global["hep"];
    if (existingHep.IsObject()) {
      hepObj = existingHep.ToObject();
    }
    
    // Store native function - ui_common.js will use this if available
    hepObj["_nativeCallCpp"] = bridgeFunc;
    
    // Only set callCpp if not already defined (ui_common.js version parses JSON properly)
    JSValue existingCallCpp = hepObj["callCpp"];
    if (existingCallCpp.IsUndefined() || existingCallCpp.IsNull()) {
      hepObj["callCpp"] = bridgeFunc;
    }
    
    global["hep"] = JSValue(hepObj);

    global["hepBridgeVersion"] = JSValue(1);
  }
};

HtmlShellView::HtmlShellView(std::string debugName,
                             EditorMessageBridge* bridge,
                             VulkanDevice* vkdev)
    : debugName_(std::move(debugName)), bridge_(bridge), vkdev_(vkdev) {
}

HtmlShellView::~HtmlShellView() {
  if (texture_) {
    texture_->Destroy(vkdev_->allocator, vkdev_->device);
  }
  if (upload_.created) {
    upload_.staging.Destroy(vkdev_->allocator);
  }
  if (upload_.fence != VK_NULL_HANDLE) {
    vkDestroyFence(vkdev_->device, upload_.fence, nullptr);
  }
}

void HtmlShellView::Create(ultralight::RefPtr<ultralight::Renderer> renderer,
                           uint32_t width,
                           uint32_t height,
                           const std::string& url) {
  renderer_ = renderer;
  width_ = width;
  height_ = height;

  ultralight::ViewConfig config;
  config.is_transparent = true;
  view_ = renderer_->CreateView(width, height, config, nullptr);

  auto* listener = new ShellLoadListener();
  listener->owner = this;
  view_->set_load_listener(listener);
  
  // Set view listener for console messages
  auto* viewListener = new ShellViewListener();
  view_->set_view_listener(viewListener);

  view_->LoadURL(url.c_str());
}

void HtmlShellView::Resize(uint32_t width, uint32_t height) {
  const uint32_t clampedW = std::max(1u, width);
  const uint32_t clampedH = std::max(1u, height);
  if (width_ == clampedW && height_ == clampedH) return;
  width_ = clampedW;
  height_ = clampedH;
  if (view_) view_->Resize(width_, height_);
}

void HtmlShellView::UpdateAndSync() {
  if (!renderer_) return;
  renderer_->Update();
  renderer_->Render();
}

void HtmlShellView::EnsureUploadSlot() {
  if (upload_.created) return;

  VkCommandBufferAllocateInfo alloc{};
  alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc.commandPool = vkdev_->commandPool;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = 1;
  vkAllocateCommandBuffers(vkdev_->device, &alloc, &upload_.cmd);

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled so first wait passes
  vkCreateFence(vkdev_->device, &fenceInfo, nullptr, &upload_.fence);

  upload_.created = true;
}

void HtmlShellView::EnsureTextureCreated(uint32_t w, uint32_t h) {
  if (!texture_) {
    texture_ = std::make_unique<VulkanTexture>();
  }

  if (texture_->image && texture_->width == w && texture_->height == h) {
    return;
  }

  texture_->Destroy(vkdev_->allocator, vkdev_->device);

  texture_->Create(vkdev_->allocator, vkdev_->device, w, h,
                   textureFormat_,
                   VK_IMAGE_TILING_OPTIMAL,
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  texture_->CreateImageView(vkdev_->device, textureFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
  texture_->CreateSampler(vkdev_->device, vkdev_->physicalDevice);

  textureLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
}

void HtmlShellView::SyncTextureToGpu() {
  if (!view_) return;

  auto* surface = static_cast<ultralight::BitmapSurface*>(view_->surface());
  if (!surface) return;

  if (surface->dirty_bounds().IsEmpty() && texture_ && texture_->image) return;

  ultralight::RefPtr<ultralight::Bitmap> bitmap = surface->bitmap();
  if (!bitmap) return;

  const uint32_t w = bitmap->width();
  const uint32_t h = bitmap->height();
  if (w == 0 || h == 0) return;

  EnsureTextureCreated(w, h);
  EnsureUploadSlot();

  bitmap->LockPixels();
  const uint8_t* src = reinterpret_cast<const uint8_t*>(bitmap->raw_pixels());
  const uint32_t rowBytes = bitmap->row_bytes();

  VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

  if (upload_.stagingSize < imageSize) {
    if (upload_.staging.buffer) upload_.staging.Destroy(vkdev_->allocator);
    upload_.staging.Create(vkdev_->allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VMA_MEMORY_USAGE_CPU_ONLY);
    upload_.stagingSize = imageSize;
  }

  void* mapped = nullptr;
  vmaMapMemory(vkdev_->allocator, upload_.staging.allocation, &mapped);
  uint8_t* dst = reinterpret_cast<uint8_t*>(mapped);
  const size_t tightRowBytes = static_cast<size_t>(w) * 4;
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t* row = src + static_cast<size_t>(y) * rowBytes;
    std::memcpy(dst + static_cast<size_t>(y) * tightRowBytes, row, tightRowBytes);
  }
  vmaUnmapMemory(vkdev_->allocator, upload_.staging.allocation);
  bitmap->UnlockPixels();

  surface->ClearDirtyBounds();

  vkWaitForFences(vkdev_->device, 1, &upload_.fence, VK_TRUE, UINT64_MAX);
  vkResetFences(vkdev_->device, 1, &upload_.fence);
  vkResetCommandBuffer(upload_.cmd, 0);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(upload_.cmd, &beginInfo);

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = textureLayout_;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = texture_->image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = (textureLayout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                            ? VK_ACCESS_SHADER_READ_BIT
                            : 0;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  VkPipelineStageFlags srcStage =
      (textureLayout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
          ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
          : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

  vkCmdPipelineBarrier(upload_.cmd,
                       srcStage,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0,
                       0, nullptr,
                       0, nullptr,
                       1, &barrier);

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

  vkCmdCopyBufferToImage(upload_.cmd,
                         upload_.staging.buffer,
                         texture_->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1,
                         &region);

  VkImageMemoryBarrier barrier2 = barrier;
  barrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(upload_.cmd,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       0,
                       0, nullptr,
                       0, nullptr,
                       1, &barrier2);

  vkEndCommandBuffer(upload_.cmd);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &upload_.cmd;

  vkQueueSubmit(vkdev_->graphicsQueue, 1, &submitInfo, upload_.fence);
  textureLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void HtmlShellView::EvaluateScript(const std::string& script) {
  if (!view_) return;
  view_->EvaluateScript(script.c_str());
}

void HtmlShellView::FireMouseMove(int x, int y) {
  if (!view_) return;
  ultralight::MouseEvent evt;
  evt.type = ultralight::MouseEvent::kType_MouseMoved;
  evt.x = x;
  evt.y = y;
  evt.button = ultralight::MouseEvent::kButton_None;
  view_->FireMouseEvent(evt);
}

void HtmlShellView::FireMouseDown(int x, int y, int button) {
  if (!view_) return;
  ultralight::MouseEvent evt;
  evt.type = ultralight::MouseEvent::kType_MouseDown;
  evt.x = x;
  evt.y = y;
  evt.button = button == 1 ? ultralight::MouseEvent::kButton_Left : ultralight::MouseEvent::kButton_Right;
  view_->FireMouseEvent(evt);
}

void HtmlShellView::FireMouseUp(int x, int y, int button) {
  if (!view_) return;
  ultralight::MouseEvent evt;
  evt.type = ultralight::MouseEvent::kType_MouseUp;
  evt.x = x;
  evt.y = y;
  evt.button = button == 1 ? ultralight::MouseEvent::kButton_Left : ultralight::MouseEvent::kButton_Right;
  view_->FireMouseEvent(evt);
}

void HtmlShellView::FireScroll(int x, int y, int dx, int dy) {
  if (!view_) return;
  ultralight::ScrollEvent evt;
  evt.type = ultralight::ScrollEvent::kType_ScrollByPixel;
  evt.delta_x = dx;
  evt.delta_y = dy;
  (void)x;
  (void)y;
  view_->FireScrollEvent(evt);
}

void HtmlShellView::FireKey(int virtualKeyCode, bool down, unsigned modifiers) {
  if (!view_) return;
  ultralight::KeyEvent evt;
  evt.type = down ? ultralight::KeyEvent::kType_RawKeyDown : ultralight::KeyEvent::kType_KeyUp;
  evt.virtual_key_code = virtualKeyCode;
  evt.native_key_code = 0;
  evt.modifiers = modifiers;
  ultralight::GetKeyIdentifierFromVirtualKeyCode(evt.virtual_key_code, evt.key_identifier);
  evt.is_system_key = false;
  view_->FireKeyEvent(evt);
}

void HtmlShellView::FireChar(char32_t codepoint) {
  if (!view_) return;
  ultralight::KeyEvent evt;
  evt.type = ultralight::KeyEvent::kType_Char;
  ultralight::String text = ultralight::String32((const char32_t*)&codepoint, 1);
  evt.text = text;
  evt.unmodified_text = text;
  view_->FireKeyEvent(evt);
}

} // namespace Hephaestus::HtmlUI
