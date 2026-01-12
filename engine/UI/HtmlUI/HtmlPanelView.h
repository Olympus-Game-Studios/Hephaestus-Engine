#pragma once

#include "EditorMessageBridge.h"

#include <memory>
#include <string>

#include <Ultralight/RefPtr.h>
#include <Ultralight/Renderer.h>
#include <Ultralight/View.h>

#include <vulkan/vulkan.h>

class VulkanDevice;
class VulkanTexture;
struct GLFWwindow;

// NOTE: VulkanDevice is declared as a class in engine/Gfx/VulkanDevice.h.
// MSVC name mangling encodes class-vs-struct, so our forward declaration must match.

namespace Hephaestus::HtmlUI {

class HtmlPanelView {
public:
  HtmlPanelView(std::string debugName,
                EditorMessageBridge* bridge,
                VulkanDevice* vkdev,
                VkDescriptorPool imguiDescriptorPool);

  ~HtmlPanelView();

  void Create(ultralight::RefPtr<ultralight::Renderer> renderer,
              uint32_t width,
              uint32_t height,
              const std::string& url);

  void Resize(uint32_t width, uint32_t height);

  // Upload rendered pixels to GPU and ensure ImGui descriptor set exists.
  void SyncTextureToGpu();

  void EvaluateScript(const std::string& script);

  // Draw the panel contents inside the current ImGui window.
  void DrawImGuiImage();

  // Forward basic mouse input when the ImGui image is hovered.
  void HandleImGuiInput();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace Hephaestus::HtmlUI
