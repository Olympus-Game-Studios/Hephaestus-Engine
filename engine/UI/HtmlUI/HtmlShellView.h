#pragma once

#include <Ultralight/RefPtr.h>
#include <Ultralight/Renderer.h>
#include <Ultralight/View.h>
#include <string>
#include <memory>

#include "../../Gfx/VulkanDevice.h"
#include "../../Gfx/VulkanTexture.h"
#include "../../Gfx/VulkanBuffer.h"
#include "EditorMessageBridge.h"

namespace Hephaestus::HtmlUI {

class HtmlShellView {
public:
  HtmlShellView(std::string debugName,
                EditorMessageBridge* bridge,
                VulkanDevice* vkdev);
  ~HtmlShellView();

  void Create(ultralight::RefPtr<ultralight::Renderer> renderer,
              uint32_t width,
              uint32_t height,
              const std::string& url);
  void Resize(uint32_t width, uint32_t height);
  void UpdateAndSync();
  void SyncTextureToGpu();

  void EvaluateScript(const std::string& script);

  void FireMouseMove(int x, int y);
  void FireMouseDown(int x, int y, int button);
  void FireMouseUp(int x, int y, int button);
  void FireScroll(int x, int y, int dx, int dy);
  void FireKey(int virtualKeyCode, bool down, unsigned modifiers);
  void FireChar(char32_t codepoint);

  VulkanTexture* GetTexture() const { return texture_.get(); }
  uint32_t Width() const { return width_; }
  uint32_t Height() const { return height_; }
  EditorMessageBridge* Bridge() const { return bridge_; }

private:
  struct UploadSlot {
    VulkanBuffer staging;
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkDeviceSize stagingSize = 0;
    bool created = false;
  };

  void EnsureUploadSlot();
  void EnsureTextureCreated(uint32_t w, uint32_t h);

  std::string debugName_;
  EditorMessageBridge* bridge_ = nullptr;
  VulkanDevice* vkdev_ = nullptr;

  ultralight::RefPtr<ultralight::Renderer> renderer_;
  ultralight::RefPtr<ultralight::View> view_;

  std::unique_ptr<VulkanTexture> texture_;
  VkFormat textureFormat_ = VK_FORMAT_B8G8R8A8_SRGB;
  VkImageLayout textureLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

  UploadSlot upload_;

  uint32_t width_ = 0;
  uint32_t height_ = 0;
};

} // namespace Hephaestus::HtmlUI
