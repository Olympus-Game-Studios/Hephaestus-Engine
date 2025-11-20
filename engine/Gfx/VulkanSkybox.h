#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <vector>
#include "../Gfx/VulkanTexture.h"

/**
 * VulkanSkybox - Renders environment maps as background
 */
class VulkanSkybox {
public:
    VkDevice device = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::string engineRoot = "..";
    
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;
    
    VulkanTexture environmentTexture;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    uint32_t frameCount = 0;

    bool hasHDRI = false;

    void Init(VkDevice dev, VkRenderPass rp, VkExtent2D extent, VmaAllocator allocator,
              VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue,
              uint32_t inFrameCount, const std::string& engineRoot = "..");
    
    bool LoadHDRI(const std::string& filepath, VmaAllocator allocator, VkPhysicalDevice physicalDevice,
                  VkCommandPool commandPool, VkQueue graphicsQueue);
    
    void UpdateDescriptorSet(uint32_t frameIndex, VkBuffer uniformBuffer);
    
    void Draw(VkCommandBuffer cmd, uint32_t frameIndex);
    
    void Cleanup(VmaAllocator allocator);

private:
    void CreateCubeVertexBuffer(VmaAllocator allocator);
    void CreateDescriptorSetLayout();
    void CreatePipeline(VkExtent2D extent);
    VkShaderModule CreateShaderModule(const std::vector<char>& code);
    static std::vector<char> ReadFile(const std::string& filename);
};
