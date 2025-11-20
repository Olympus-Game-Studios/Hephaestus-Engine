#define VMA_IMPLEMENTATION
#include "VulkanDevice.h"
#include "vk_mem_alloc.h"
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

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

  // ---- Create VMA allocator ----
  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.physicalDevice = physicalDevice;
  allocatorInfo.device = device;
  allocatorInfo.instance = instance;

  if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS)
    throw std::runtime_error("Failed to create VMA allocator");

  std::cout << "Vulkan device initialized.\n";
}

void VulkanDevice::Cleanup() {
  if (allocator)
    vmaDestroyAllocator(allocator);

  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
    if (commandPool)
      vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDevice(device, nullptr);
  }

  if (surface)
    vkDestroySurfaceKHR(instance, surface, nullptr);
}

// ---------- internals ----------

void VulkanDevice::pickPhysicalDevice() {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance, &count, nullptr);
  if (count == 0)
    throw std::runtime_error("No Vulkan devices found!");

  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance, &count, devices.data());

  for (auto pd : devices) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(pd, &props);
    physicalDevice = pd;
    std::cout << "GPU: " << props.deviceName << "\n";
    return;
  }

  throw std::runtime_error("Failed to select a physical device");
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
  VkDeviceCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  ci.queueCreateInfoCount = static_cast<uint32_t>(qInfos.size());
  ci.pQueueCreateInfos = qInfos.data();
  ci.pEnabledFeatures = &features;
  ci.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
  ci.ppEnabledExtensionNames = deviceExtensions.data();

  if (vkCreateDevice(physicalDevice, &ci, nullptr, &device) != VK_SUCCESS)
    throw std::runtime_error("Failed to create logical device");

  vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
  vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
}

void VulkanDevice::createCommandPool() {
  VkCommandPoolCreateInfo pool{};
  pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool.queueFamilyIndex = graphicsFamily;
  pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  if (vkCreateCommandPool(device, &pool, nullptr, &commandPool) != VK_SUCCESS)
    throw std::runtime_error("Failed to create command pool");
}
