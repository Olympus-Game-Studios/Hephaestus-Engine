#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include "vk_mem_alloc.h"

struct VulkanSwapchain {
    VkDevice device{};
    VkPhysicalDevice physicalDevice{};
    VkSurfaceKHR surface{};
    VkExtent2D extent{};
    VkFormat format{};
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

    void Init(VkPhysicalDevice phys, VkDevice dev, VkSurfaceKHR surf, GLFWwindow* window, VmaAllocator alloc);
    void Recreate(GLFWwindow* window);
    void Cleanup();
};
