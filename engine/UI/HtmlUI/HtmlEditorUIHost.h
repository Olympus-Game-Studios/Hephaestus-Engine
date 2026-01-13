#pragma once

#include "EditorMessageBridge.h"
#include "HtmlPanelView.h"

#include <Ultralight/RefPtr.h>
#include <Ultralight/Renderer.h>

#include <memory>
#include <filesystem>
#include <functional>
#include <string>

#include <vulkan/vulkan.h>

class VulkanDevice;
struct GLFWwindow;
class Scene;

namespace Hephaestus::HtmlUI {

class HtmlEditorUIHost {
public:
  HtmlEditorUIHost();
  ~HtmlEditorUIHost();

  void Init(VulkanDevice* vkdev, VkDescriptorPool imguiDescriptorPool);

  // Provide current editor state. Call each frame (or whenever changed).
  void SetScene(Scene* scene);
  void SetDetailsLockedFlag(bool* detailsLocked);
  void SetAssetsRoot(std::filesystem::path assetsRoot);

  // Callback for when an asset is opened. Called with the asset path.
  using AssetOpenCallback = std::function<void(const std::filesystem::path&)>;
  void SetAssetOpenCallback(AssetOpenCallback callback);
  void SetAssetCreateCallback(AssetOpenCallback callback);

  // Update Ultralight and upload panel textures.
  void UpdateAndSync(float dt);

  // Draw editor panels (transitional: uses ImGui docking windows as host).
  void DrawDockedPanels();
  void DrawOutlinerPanel();
  void DrawDetailsPanel();
  void DrawAssetBrowserPanel();
  void DrawLogConsolePanel();

  EditorMessageBridge& Bridge();

private:
  VulkanDevice* vkdev_ = nullptr;
  VkDescriptorPool imguiDescriptorPool_ = nullptr;

  Scene* scene_ = nullptr;
  bool* detailsLocked_ = nullptr;
  std::filesystem::path assetsRoot_;
  std::string lastSelectedAssetRel_;
  uint32_t lastSelectedId_ = 0;

  // Last transform pushed to the UI for the current selection.
  uint32_t lastTransformId_ = 0;
  float lastTransform_[9] = {0};
  bool hasLastTransform_ = false;

  AssetOpenCallback assetOpenCallback_;
  AssetOpenCallback assetCreateCallback_;

  ultralight::RefPtr<ultralight::Renderer> renderer_;

  EditorMessageBridge bridge_;

  std::unique_ptr<HtmlPanelView> outliner_;
  std::unique_ptr<HtmlPanelView> details_;
  std::unique_ptr<HtmlPanelView> assetBrowser_;
  std::unique_ptr<HtmlPanelView> logConsole_;
};

} // namespace Hephaestus::HtmlUI
