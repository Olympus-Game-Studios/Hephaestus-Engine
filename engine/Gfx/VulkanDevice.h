#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <cstdint>
#include "vk_mem_alloc.h"
#include "NativeSurface.h"

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
    
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t presentFamily  = UINT32_MAX;

    // Legacy GLFW-based initialization (runtime/legacy editor path)
    void Init(GLFWwindow* window, VkInstance instance);
    
    // Two-phase initialization for Qt editor:
    // Phase 1: Initialize device without surface (call once at startup)
    void InitWithoutSurface(VkInstance instance);
    
    // Phase 2: Create/recreate surface from native handle (can be called multiple times)
    // Returns true on success. If a surface already exists, it will be destroyed first.
    bool AttachToSurface(const Hephaestus::NativeSurface& nativeSurface);
    
    // Destroy only the surface (for detaching from Qt viewport before shutdown)
    void DestroySurface();
    
    void Cleanup();

private:
    // Internal helper to create Win32 surface
    bool createWin32Surface(void* hwnd);

    void pickPhysicalDevice();
    void findQueueFamilies(VkPhysicalDevice pd);
    void createLogicalDevice();
    void createCommandPool();
};
