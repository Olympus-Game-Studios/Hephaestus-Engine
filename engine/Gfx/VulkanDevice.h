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

    /** True when the logical device enabled fillModeNonSolid (wireframe rasterization). */
    bool SupportsFillModeNonSolid() const { return m_fillModeNonSolid; }

    // Extension entry points, loaded only when SupportsRayQuery().
    struct RayTracingFunctions {
        PFN_vkCreateAccelerationStructureKHR createAccelerationStructure = nullptr;
        PFN_vkDestroyAccelerationStructureKHR destroyAccelerationStructure = nullptr;
        PFN_vkGetAccelerationStructureBuildSizesKHR getBuildSizes = nullptr;
        PFN_vkCmdBuildAccelerationStructuresKHR cmdBuildAccelerationStructures = nullptr;
        PFN_vkGetAccelerationStructureDeviceAddressKHR getAccelerationStructureAddress = nullptr;
    };
    /** True when VK_KHR_ray_query, acceleration structures and buffer device addresses are enabled. */
    bool SupportsRayQuery() const { return m_rayQuery; }
    const RayTracingFunctions& RayTracing() const { return m_rayTracing; }

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
    void createAllocator();

    bool m_fillModeNonSolid = false;
    bool m_rayQuery = false;
    RayTracingFunctions m_rayTracing;
};
