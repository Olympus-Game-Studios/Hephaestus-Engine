#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>

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

    void Init(VkPhysicalDevice phys, VkDevice dev, VkSurfaceKHR surf, GLFWwindow* window);
    void Cleanup();
};
