#pragma once

#include <memory>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../Scene/Json.h"
#include <Ultralight/RefPtr.h>
#include <Ultralight/Renderer.h>

#include "EditorMessageBridge.h"
#include "HtmlShellView.h"
#include "../../Gfx/VulkanDevice.h"

class Scene;

namespace Hephaestus::HtmlUI {

class HtmlShellUIHost {
public:
  HtmlShellUIHost();
  ~HtmlShellUIHost();

  void Init(VulkanDevice* vkdev,
            VkRenderPass renderPass,
            VkExtent2D extent,
            const std::filesystem::path& baseDir);
  void OnSwapchainRecreated(VkRenderPass renderPass, VkExtent2D extent);
  void UpdateAndSync(float dt);
  void Draw(VkCommandBuffer cmd, VkExtent2D extent);

  EditorMessageBridge& Bridge();

  void SetScene(Scene* scenePtr);
  void SetAssetsRoot(std::filesystem::path assetsRoot);
  void SetViewportRect(int x, int y, int w, int h);
  struct ViewportRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
  };
  ViewportRect GetViewportRect() const;

  void OnMouseMove(double x, double y);
  void OnMouseButton(int button, int action, int mods, double x, double y);
  void OnScroll(double x, double y, double dx, double dy);
  void OnKey(int key, int scancode, int action, int mods);
  void OnChar(unsigned int codepoint);

  bool IsPointInViewport(double x, double y) const;

  struct BlueprintDocument {
    std::filesystem::path path;
    JsonValue root;
    bool dirty = false;
    std::string error;
  };

private:
  void CreatePipeline(VkRenderPass renderPass, VkExtent2D extent);
  void DestroyPipeline();
  void EnsureDescriptorSet();
  static std::vector<char> ReadFile(const std::string& filename);
  VkShaderModule CreateShaderModule(const std::vector<char>& code);

  EditorMessageBridge bridge_;
  VulkanDevice* vkdev_ = nullptr;

  std::unique_ptr<HtmlShellView> shell_;
  ultralight::RefPtr<ultralight::Renderer> renderer_;

  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
  VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;

  VkExtent2D extent_{};
  std::filesystem::path assetsRoot_;
  Scene* scene_ = nullptr;
  std::string lastSelectedAssetRel_;

  std::unordered_map<std::string, BlueprintDocument> blueprintDocs_;
  std::vector<std::string> blueprintTabOrder_;

  ViewportRect viewportRect_;
};

} // namespace Hephaestus::HtmlUI
