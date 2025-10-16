#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <cstdint>
#include "vk_mem_alloc.h"

class VulkanDevice {
public:
    VkInstance instance{};
    VkPhysicalDevice physicalDevice{};
    VkDevice device{};
    VkQueue graphicsQueue{};
    VkQueue presentQueue{};
    VkSurfaceKHR surface{};
    VkCommandPool commandPool{};
    VmaAllocator allocator{}; // ✅ VMA allocator

    void Init(GLFWwindow* window, VkInstance instance);
    void Cleanup();

private:
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t presentFamily  = UINT32_MAX;

    void pickPhysicalDevice();
    void findQueueFamilies(VkPhysicalDevice pd);
    void createLogicalDevice();
    void createCommandPool();
};
