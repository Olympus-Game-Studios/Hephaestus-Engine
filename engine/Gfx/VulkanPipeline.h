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
    bool ownsDescriptorSetLayout = true;

    void Init(VkDevice dev, VkRenderPass rp, VkExtent2D ext);
    void CreateBillboardPipeline(VkDevice dev, VkRenderPass rp, VkExtent2D ext, VkDescriptorSetLayout descLayout);
    void Cleanup();

    VkPipeline GetPipeline(bool backfaceCullingEnabled) const;

private:
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);
};
