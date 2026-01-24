#pragma once

#include "EditorMessageBridge.h"
#include "HtmlPanelView.h"

#include <Ultralight/RefPtr.h>
#include <Ultralight/Renderer.h>

#include <memory>
#include <filesystem>
#include <functional>
#include <string>
#include <optional>
#include <utility>
#include <unordered_map>

#include "../../Scene/Json.h"

#include "../EditorSpawnPreset.h"

#include <vulkan/vulkan.h>

class VulkanDevice;
struct GLFWwindow;
class Scene;
class ScriptEngine;

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
  void SetScriptEngine(ScriptEngine* scriptEngine);
  void SetBlueprintMode(bool enabled);

  // Callback for when an asset is opened. Called with the asset path.
  using AssetOpenCallback = std::function<void(const std::filesystem::path&)>;
  void SetAssetOpenCallback(AssetOpenCallback callback);
  void SetAssetCreateCallback(AssetOpenCallback callback);

  // Update Ultralight and upload panel textures.
  void UpdateAndSync(float dt);

  // Draw editor panels (transitional: uses ImGui docking windows as host).
  void DrawDockedPanels();
  void DrawOutlinerPanel();
  void DrawAddActorsPanel();
  void DrawDetailsPanel();
  void DrawAssetBrowserPanel();
  void DrawBlueprintEditorPanel();
  void DrawLogConsolePanel();

  // Open (or focus) a blueprint document in the Blueprint Editor panel.
  void OpenBlueprintEditor(const std::filesystem::path& path);

  // Returns true if there are any open blueprint documents.
  bool HasOpenBlueprints() const { return !blueprintDocs_.empty(); }

  // Consumes a spawn request originating from the HTML Add Actors panel.
  std::optional<EditorSpawnPreset> ConsumeAddActorsSpawnRequest();
  std::optional<std::string> ConsumeAddActorsSpawnMeshRequest();
  std::optional<std::pair<std::string, uint32_t>> ConsumeOutlinerAssetDropRequest();

  // Returns true if the mouse is hovering over any HTML editor panel.
  bool IsAnyPanelHovered() const;

  EditorMessageBridge& Bridge();

private:
  void ApplyThemeToPanels();
  VulkanDevice* vkdev_ = nullptr;
  VkDescriptorPool imguiDescriptorPool_ = nullptr;

  Scene* scene_ = nullptr;
  ScriptEngine* scriptEngine_ = nullptr;
  bool* detailsLocked_ = nullptr;
  std::filesystem::path assetsRoot_;
  bool blueprintMode_ = false;
  bool themeDirty_ = true;
  std::string lastSelectedAssetRel_;
  uint32_t lastSelectedId_ = 0;
  uint32_t lastSelectionCount_ = 0;
  uint64_t lastSelectionHash_ = 0;

  // Last transform pushed to the UI for the current selection.
  uint32_t lastTransformId_ = 0;
  float lastTransform_[9] = {0};
  bool hasLastTransform_ = false;

  AssetOpenCallback assetOpenCallback_;
  AssetOpenCallback assetCreateCallback_;

  ultralight::RefPtr<ultralight::Renderer> renderer_;

  EditorMessageBridge bridge_;

  std::unique_ptr<HtmlPanelView> outliner_;
  std::unique_ptr<HtmlPanelView> addActors_;
  std::unique_ptr<HtmlPanelView> details_;
  std::unique_ptr<HtmlPanelView> assetBrowser_;
  std::unique_ptr<HtmlPanelView> blueprintEditor_;
  std::unique_ptr<HtmlPanelView> logConsole_;

  // Tracks whether each host ImGui panel is currently visible (ie. not collapsed).
  bool outlinerVisible_ = false;
  bool addActorsVisible_ = false;
  bool detailsVisible_ = false;
  bool assetBrowserVisible_ = false;
  bool blueprintEditorVisible_ = false;
  bool logConsoleVisible_ = false;

  struct BlueprintDocument {
    std::filesystem::path path;
    JsonValue root;
    bool dirty = false;
    std::string error;
  };

  std::unordered_map<std::string, BlueprintDocument> blueprintDocs_;
  std::vector<std::string> blueprintTabOrder_;

  // State pushed from HTML UI (consumed by the editor loop).
  std::optional<EditorSpawnPreset> pendingAddActorsSpawn_;
  std::optional<EditorSpawnPreset> pendingAddActorsDrag_;
  std::optional<std::string> pendingAddActorsSpawnMeshRel_;
  std::optional<std::string> pendingAssetDrag_;
  std::optional<std::pair<std::string, uint32_t>> pendingOutlinerAssetDrop_;

  bool LoadBlueprintDocumentFromDisk(const std::filesystem::path& path,
                                     BlueprintDocument& outDoc,
                                     std::string& outError);
};

} // namespace Hephaestus::HtmlUI
