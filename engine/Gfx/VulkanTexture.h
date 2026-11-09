#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <vector>

/**
 * VulkanTexture - Manages a Vulkan texture (image + view + sampler)
 * Supports loading from PNG/JPG files using stb_image
 */
class VulkanTexture {
public:
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 1;

    // Create texture from file
    bool LoadFromFile(
        VmaAllocator allocator,
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        const std::string& filepath,
        bool generateMipmaps = false,
        bool srgb = true); ///< false for data textures (normal, roughness, metallic, AO)

    // Create HDR texture from file (HDRI, EXR)
    bool LoadHDRFromFile(
        VmaAllocator allocator,
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        const std::string& filepath,
        std::vector<float>* outPixels = nullptr);

    // Create 1x1 solid color texture
    bool CreateSolidColor(
        VmaAllocator allocator,
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        uint32_t rgba);

    // Create empty texture
    bool Create(
        VmaAllocator allocator,
        VkDevice device,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties);

    // Create image view
    bool CreateImageView(VkDevice device, VkFormat format, VkImageAspectFlags aspectFlags);

    // Create sampler
    bool CreateSampler(VkDevice device, VkPhysicalDevice physicalDevice);

    // Cleanup
    void Destroy(VmaAllocator allocator, VkDevice device);

private:
    // Helper to transition image layout
    void TransitionImageLayout(
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        VkImageLayout oldLayout,
        VkImageLayout newLayout);

    // Helper to copy buffer to image
    void CopyBufferToImage(
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        VkBuffer buffer,
        uint32_t width,
        uint32_t height);

    // Helper to generate mipmaps
    void GenerateMipmaps(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        VkFormat imageFormat,
        int32_t texWidth,
        int32_t texHeight);
};
