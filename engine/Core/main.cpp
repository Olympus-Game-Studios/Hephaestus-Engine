#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cstring>
#include "../Gfx/VulkanDevice.h"
#include "../Gfx/VulkanSwapchain.h"

#ifdef _DEBUG
const bool enableValidationLayers = true;
#else
const bool enableValidationLayers = false;
#endif

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    std::cerr << "[VULKAN] " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    return func ? func(instance, pCreateInfo, pAllocator, pMessenger)
                : VK_ERROR_EXTENSION_NOT_PRESENT;
}

void populateDebugCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

int main()
{
    // ── GLFW window ───────────────────────────────
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Hephaestus Engine", nullptr, nullptr);

    // ── Vulkan instance ───────────────────────────
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hephaestus Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Hephaestus Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        populateDebugCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    }

    VkInstance instance;
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan instance.\n";
        return -1;
    }

    VkDebugUtilsMessengerEXT debugMessenger;
    if (enableValidationLayers) {
        populateDebugCreateInfo(debugCreateInfo);
        CreateDebugUtilsMessengerEXT(instance, &debugCreateInfo, nullptr, &debugMessenger);
    }

    // ── Device setup ───────────────────────────────
    VulkanDevice vkdev;
    vkdev.Init(window, instance);

    VulkanSwapchain swap;
    swap.Init(vkdev.physicalDevice, vkdev.device, vkdev.surface, window);

    // ── Main loop ─────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Acquire next image
        uint32_t imageIndex;
        vkAcquireNextImageKHR(vkdev.device, swap.swapchain, UINT64_MAX,
                              VK_NULL_HANDLE, VK_NULL_HANDLE, &imageIndex);

        // ── Create a temporary command pool and command buffer ─────────────
        VkCommandPool cmdPool;
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = 0; // (TODO: replace with actual graphics family index)
        vkCreateCommandPool(vkdev.device, &poolInfo, nullptr, &cmdPool);

        VkCommandBuffer cmdBuf;
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(vkdev.device, &allocInfo, &cmdBuf);

        // ── Record clear command ──────────────────────────────────────────
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmdBuf, &beginInfo);

        VkClearValue clearColor = { {0.1f, 0.2f, 0.3f, 1.0f} }; // teal

        VkRenderPassBeginInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderInfo.renderPass = swap.renderPass;
        renderInfo.framebuffer = swap.framebuffers[imageIndex];
        renderInfo.renderArea.offset = {0, 0};
        renderInfo.renderArea.extent = swap.extent;
        renderInfo.clearValueCount = 1;
        renderInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(cmdBuf, &renderInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(cmdBuf);
        vkEndCommandBuffer(cmdBuf);

        // ── Submit and present ────────────────────────────────────────────
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        vkQueueSubmit(vkdev.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(vkdev.graphicsQueue);

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swap.swapchain;
        presentInfo.pImageIndices = &imageIndex;
        vkQueuePresentKHR(vkdev.presentQueue, &presentInfo);
        vkQueueWaitIdle(vkdev.presentQueue);

        // Cleanup per-frame temp command buffer
        vkFreeCommandBuffers(vkdev.device, cmdPool, 1, &cmdBuf);
        vkDestroyCommandPool(vkdev.device, cmdPool, nullptr);
    }

    // ── Cleanup ───────────────────────────────────
    swap.Cleanup();
    vkdev.Cleanup();
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
