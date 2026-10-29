#define VMA_IMPLEMENTATION
#include "VulkanDevice.h"
#include "vk_mem_alloc.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
#endif

void VulkanDevice::Init(GLFWwindow *window, VkInstance inInstance) {
  instance = inInstance;

  // Create window surface
  if (glfwCreateWindowSurface(instance, window, nullptr, &surface) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to create window surface");

  // Select GPU + create queues
  pickPhysicalDevice();
  findQueueFamilies(physicalDevice);
  createLogicalDevice();
  createCommandPool();
  createAllocator();

  std::cout << "Vulkan device initialized.\n";
}

void VulkanDevice::InitWithoutSurface(VkInstance inInstance) {
  instance = inInstance;
  surface = VK_NULL_HANDLE; // No surface yet

  std::cout << "[VulkanDevice] InitWithoutSurface: enumerating GPUs\n";

  // Select GPU
  pickPhysicalDevice();
  
  std::cout << "[VulkanDevice] InitWithoutSurface: creating logical device\n";
  
  // Find queue families without surface (graphics-only for now)
  // Present family will be validated when AttachToSurface is called
  uint32_t qCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, nullptr);
  std::vector<VkQueueFamilyProperties> props(qCount);
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, props.data());

  for (uint32_t i = 0; i < qCount; ++i) {
    if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && graphicsFamily == UINT32_MAX) {
      graphicsFamily = i;
      // Assume same family can present (validated later in AttachToSurface)
      presentFamily = i;
      break;
    }
  }
  
  if (graphicsFamily == UINT32_MAX) {
    throw std::runtime_error("No graphics queue family found");
  }
  
  createLogicalDevice();
  createCommandPool();
  createAllocator();

  std::cout << "Vulkan device initialized (without surface).\n";
}

bool VulkanDevice::AttachToSurface(const Hephaestus::NativeSurface& nativeSurface) {
  if (!nativeSurface.IsValid()) {
    std::cerr << "[VulkanDevice] AttachToSurface: Invalid native surface\n";
    return false;
  }

  // Destroy existing surface if any
  DestroySurface();

#ifdef _WIN32
  if (!createWin32Surface(nativeSurface.windowHandle)) {
    std::cerr << "[VulkanDevice] Failed to create Win32 surface\n";
    return false;
  }
#elif defined(__APPLE__)
  VkMetalSurfaceCreateInfoEXT createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
  createInfo.pLayer = nativeSurface.windowHandle;
  if (vkCreateMetalSurfaceEXT(instance, &createInfo, nullptr, &surface) != VK_SUCCESS) {
    std::cerr << "[VulkanDevice] Failed to create Metal surface\n";
    return false;
  }
#else
  std::cerr << "[VulkanDevice] AttachToSurface: Platform not supported yet\n";
  return false;
#endif

  // Validate that our queue family supports present on this surface
  VkBool32 presentSupport = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, presentFamily, surface, &presentSupport);
  if (!presentSupport) {
    std::cerr << "[VulkanDevice] Warning: Current queue family doesn't support present on this surface\n";
    // Try to find another queue family that supports present
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, nullptr);
    for (uint32_t i = 0; i < qCount; ++i) {
      vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
      if (presentSupport) {
        presentFamily = i;
        std::cout << "[VulkanDevice] Found alternative present queue family: " << i << "\n";
        break;
      }
    }
    if (!presentSupport) {
      std::cerr << "[VulkanDevice] No queue family supports present on this surface!\n";
      DestroySurface();
      return false;
    }
  }

  std::cout << "[VulkanDevice] Attached to native surface (" 
            << nativeSurface.width << "x" << nativeSurface.height << ")\n";
  return true;
}

bool VulkanDevice::createWin32Surface(void* hwnd) {
#ifdef _WIN32
  VkWin32SurfaceCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  createInfo.hwnd = static_cast<HWND>(hwnd);
  createInfo.hinstance = GetModuleHandle(nullptr);
  
  if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS) {
    return false;
  }
  return true;
#else
  (void)hwnd;
  return false;
#endif
}

void VulkanDevice::DestroySurface() {
  if (surface != VK_NULL_HANDLE) {
    // Note: Caller should ensure device is idle before calling this
    vkDestroySurfaceKHR(instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
    std::cout << "[VulkanDevice] Surface destroyed\n";
  }
}

void VulkanDevice::Cleanup() {
  // First wait for GPU to be idle (if device exists)
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
  }
  
  // Destroy surface before device
  DestroySurface();
  
  // Destroy allocator (uses device)
  if (allocator) {
    vmaDestroyAllocator(allocator);
    allocator = nullptr;
  }

  // Finally destroy device
  if (device != VK_NULL_HANDLE) {
    if (commandPool) {
      vkDestroyCommandPool(device, commandPool, nullptr);
      commandPool = VK_NULL_HANDLE;
    }
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
  }
}

// ---------- internals ----------

void VulkanDevice::pickPhysicalDevice() {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance, &count, nullptr);
  std::cout << "[VulkanDevice] Physical devices: " << count << "\n";
  if (count == 0)
    throw std::runtime_error("No Vulkan devices found!");

  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance, &count, devices.data());

  for (uint32_t i = 0; i < count; ++i) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(devices[i], &props);
    std::cout << "[VulkanDevice] GPU[" << i << "]: " << props.deviceName
              << " type=" << props.deviceType
              << " api=" << VK_VERSION_MAJOR(props.apiVersion) << "."
              << VK_VERSION_MINOR(props.apiVersion) << "."
              << VK_VERSION_PATCH(props.apiVersion) << "\n";
  }

  physicalDevice = devices[0];
  VkPhysicalDeviceProperties selected{};
  vkGetPhysicalDeviceProperties(physicalDevice, &selected);
  std::cout << "[VulkanDevice] Selected GPU: " << selected.deviceName << "\n";
}

void VulkanDevice::findQueueFamilies(VkPhysicalDevice pd) {
  uint32_t qCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
  std::vector<VkQueueFamilyProperties> props(qCount);
  vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, props.data());

  for (uint32_t i = 0; i < qCount; ++i) {
    if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
        graphicsFamily == UINT32_MAX)
      graphicsFamily = i;

    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &present);
    if (present && presentFamily == UINT32_MAX)
      presentFamily = i;
  }

  if (graphicsFamily == UINT32_MAX || presentFamily == UINT32_MAX)
    throw std::runtime_error("Required queue families not found");
}

void VulkanDevice::createLogicalDevice() {
  float priority = 1.0f;
  std::vector<VkDeviceQueueCreateInfo> qInfos;
  std::set<uint32_t> uniqueFamilies = {graphicsFamily, presentFamily};

  for (uint32_t family : uniqueFamilies) {
    VkDeviceQueueCreateInfo q{};
    q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    q.queueFamilyIndex = family;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    qInfos.push_back(q);
  }

  std::vector<const char *> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef __APPLE__
  deviceExtensions.push_back("VK_KHR_portability_subset");
#endif

  VkPhysicalDeviceFeatures supportedFeatures{};
  vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);

  if (!supportedFeatures.samplerAnisotropy)
    throw std::runtime_error("Required sampler anisotropy support missing");

  if (!supportedFeatures.shaderSampledImageArrayDynamicIndexing)
    throw std::runtime_error(
        "Required sampled image array dynamic indexing support missing");

  VkPhysicalDeviceFeatures features{};
  features.samplerAnisotropy = VK_TRUE;
  features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
  m_fillModeNonSolid = supportedFeatures.fillModeNonSolid == VK_TRUE;
  // Probe GI stores to rg16f images, which SPIR-V treats as an extended storage format.
  m_storageImageExtendedFormats = supportedFeatures.shaderStorageImageExtendedFormats == VK_TRUE;
  features.shaderStorageImageExtendedFormats = supportedFeatures.shaderStorageImageExtendedFormats;
  if (m_fillModeNonSolid) {
    features.fillModeNonSolid = VK_TRUE;
  }
  // Optional hardware ray queries for probe GI: needs Vulkan 1.2 buffer device addresses plus the KHR extensions.
  VkPhysicalDeviceProperties deviceProps{};
  vkGetPhysicalDeviceProperties(physicalDevice, &deviceProps);
  uint32_t extensionCount = 0;
  vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
  std::vector<VkExtensionProperties> available(extensionCount);
  vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, available.data());
  auto hasExtension = [&](const char* name) {
    return std::any_of(available.begin(), available.end(),
                       [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
  };
  VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  VkPhysicalDeviceVulkan12Features vulkan12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  accelFeatures.pNext = &rayQueryFeatures;
  vulkan12.pNext = &accelFeatures;
  VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  features2.pNext = &vulkan12;
  m_rayQuery = false;
  if (deviceProps.apiVersion >= VK_API_VERSION_1_2 &&
      hasExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
      hasExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
      hasExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)) {
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
    m_rayQuery = vulkan12.bufferDeviceAddress && accelFeatures.accelerationStructure && rayQueryFeatures.rayQuery;
  }
  // Lets a ray-tracing GPU run the path every other GPU takes (testing the fallbacks).
  if (const char* off = std::getenv("HEPHAESTUS_DISABLE_RAY_TRACING"); off && off[0] != '\0' && off[0] != '0') {
    m_rayQuery = false;
  }

  VkPhysicalDeviceRayQueryFeaturesKHR enableRayQuery{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  enableRayQuery.rayQuery = VK_TRUE;
  VkPhysicalDeviceAccelerationStructureFeaturesKHR enableAccel{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  enableAccel.accelerationStructure = VK_TRUE;
  enableAccel.pNext = &enableRayQuery;
  VkPhysicalDeviceVulkan12Features enable12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  enable12.bufferDeviceAddress = VK_TRUE;
  enable12.pNext = &enableAccel;
  VkPhysicalDeviceFeatures2 enable{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  enable.features = features;
  enable.pNext = &enable12;

  VkDeviceCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  ci.queueCreateInfoCount = static_cast<uint32_t>(qInfos.size());
  ci.pQueueCreateInfos = qInfos.data();
  if (m_rayQuery) {
    deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    ci.pNext = &enable;
  } else {
    ci.pEnabledFeatures = &features;
  }
  ci.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
  ci.ppEnabledExtensionNames = deviceExtensions.data();

  if (vkCreateDevice(physicalDevice, &ci, nullptr, &device) != VK_SUCCESS)
    throw std::runtime_error("Failed to create logical device");

  if (m_rayQuery) {
    m_rayTracing.createAccelerationStructure = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
    m_rayTracing.destroyAccelerationStructure = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
    m_rayTracing.getBuildSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));
    m_rayTracing.cmdBuildAccelerationStructures = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR"));
    m_rayTracing.getAccelerationStructureAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));
    std::cout << "[VulkanDevice] Hardware ray queries enabled\n";
  }

  vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
  vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
}

void VulkanDevice::createAllocator() {
  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.physicalDevice = physicalDevice;
  allocatorInfo.device = device;
  allocatorInfo.instance = instance;
  if (m_rayQuery) {
    allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
  }

  if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS)
    throw std::runtime_error("Failed to create VMA allocator");
}

void VulkanDevice::createCommandPool() {
  VkCommandPoolCreateInfo pool{};
  pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool.queueFamilyIndex = graphicsFamily;
  pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  if (vkCreateCommandPool(device, &pool, nullptr, &commandPool) != VK_SUCCESS)
    throw std::runtime_error("Failed to create command pool");
}
