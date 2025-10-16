#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>

struct VulkanDevice {
    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physicalDevice{};
    VkDevice device{};
    VkQueue graphicsQueue{};
    VkQueue presentQueue{};
    VkSwapchainKHR swapchain{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkFormat swapchainImageFormat{};
    VkExtent2D swapchainExtent{};

    void Init(GLFWwindow* window, VkInstance instance);
    void Cleanup();                     // <- Make sure this line exists
};
