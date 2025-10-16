#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

struct Vertex {
    float pos[3];
    float color[3];

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attribs(2);
        attribs[0].binding = 0;
        attribs[0].location = 0;
        attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribs[0].offset = offsetof(Vertex, pos);

        attribs[1].binding = 0;
        attribs[1].location = 1;
        attribs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribs[1].offset = offsetof(Vertex, color);
        return attribs;
    }
};

struct VulkanPipeline {
    VkDevice device{};
    VkRenderPass renderPass{};
    VkExtent2D extent{};
    VkDescriptorSetLayout descriptorSetLayout{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};

    void Init(VkDevice dev, VkRenderPass rp, VkExtent2D ext);
    void Cleanup();

private:
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);
};
