#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

#include "Vertex.h"

struct VulkanPipeline {
    VkDevice device{};
    VkRenderPass renderPass{};
    VkExtent2D extent{};
    VkDescriptorSetLayout descriptorSetLayout{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    VkPipeline pipelineBackface{};
    VkPipeline pipelineWireframe{};
    VkPipeline pipelineWireframeBackface{};
    VkPipeline outlinePipeline{};
    bool ownsDescriptorSetLayout = true;

    // extraSetLayouts follow the pipeline's own set 0 layout.
    void Init(VkDevice dev, VkRenderPass rp, VkExtent2D ext, bool enableWireframe = false,
              const std::vector<VkDescriptorSetLayout>& extraSetLayouts = {},
              const std::string& fragmentShader = "triangle.frag.spv");
    void CreateBillboardPipeline(VkDevice dev, VkRenderPass rp, VkExtent2D ext, VkDescriptorSetLayout descLayout);
    void CreateOutlinePipeline();
    void Cleanup();

    VkPipeline GetPipeline(bool backfaceCullingEnabled, bool wireframe = false) const;
    bool HasWireframePipeline() const {
        return pipelineWireframe != VK_NULL_HANDLE;
    }

private:
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);
};
