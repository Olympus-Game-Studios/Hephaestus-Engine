#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include "vk_mem_alloc.h"
#include "NativeSurface.h"

struct VulkanSwapchain {
    VkDevice device{};
    VkPhysicalDevice physicalDevice{};
    VkSurfaceKHR surface{};
    VkExtent2D extent{};
    VkFormat format{};
    bool supportsCapture = false; // Surface permits TRANSFER_SRC on presentable images.
    VkSwapchainKHR swapchain{};
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    VkRenderPass renderPass{};
    std::vector<VkFramebuffer> framebuffers;

    // Depth resources
    VmaAllocator allocator{};
    VkFormat depthFormat{};
    VkImage depthImage{};
    VmaAllocation depthAllocation{};
    VkImageView depthImageView{};

    // Legacy GLFW-based initialization (runtime/legacy editor path)
    void Init(VkPhysicalDevice phys, VkDevice dev, VkSurfaceKHR surf, GLFWwindow* window,
              VmaAllocator alloc, bool vsync = true);
    void Recreate(GLFWwindow* window);
    
    // Native surface initialization for Qt editor (no GLFW dependency)
    void InitFromNativeSurface(VkPhysicalDevice phys, VkDevice dev, VkSurfaceKHR surf, 
                               uint32_t width, uint32_t height, VmaAllocator alloc);
    void RecreateFromNativeSurface(uint32_t width, uint32_t height);
    
    void Cleanup();
    
private:
    // Internal initialization shared by both paths
    void initInternal(uint32_t width, uint32_t height);
    bool m_vsync = true;
};
