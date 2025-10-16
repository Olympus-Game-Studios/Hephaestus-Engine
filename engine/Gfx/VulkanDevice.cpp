#include "VulkanDevice.h"
#include <stdexcept>
#include <iostream>
#include <vector>

void VulkanDevice::Init(GLFWwindow* window, VkInstance inInstance)
{
    instance = inInstance;

    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface");

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
        throw std::runtime_error("No Vulkan devices found!");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    physicalDevice = devices[0];

    const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueCreate{};
    queueCreate.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreate.queueFamilyIndex = 0;
    queueCreate.queueCount = 1;
    queueCreate.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures features{};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreate;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &features;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
        throw std::runtime_error("Failed to create logical device");

    vkGetDeviceQueue(device, 0, 0, &graphicsQueue);
    presentQueue = graphicsQueue;

    std::cout << "Vulkan device initialized.\n";
}

// ✅ Add this cleanup function exactly as declared in the header
void VulkanDevice::Cleanup()
{
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        for (auto view : swapchainImageViews)
            vkDestroyImageView(device, view, nullptr);
        vkDestroyDevice(device, nullptr);
    }

    if (surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(instance, surface, nullptr);
}
