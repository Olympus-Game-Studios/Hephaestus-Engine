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

    void Init(VkDevice dev, VkRenderPass rp, VkExtent2D ext);
    void CreateBillboardPipeline(VkDevice dev, VkRenderPass rp, VkExtent2D ext, VkDescriptorSetLayout descLayout);
    void Cleanup();

private:
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);
};
