#pragma once

#include "VulkanDevice.h"

#include <vulkan/vulkan.h>

#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// Shader loading and one-off GPU work shared by the probe GI passes (ProbeGI, ProbeCascades).
namespace Hephaestus::ProbeGpu {

inline VkShaderModule LoadShader(VkDevice device, const std::string& name) {
    const std::string candidates[] = {"shaders/" + name, "../shaders/" + name, "build/Release/shaders/" + name,
                                      "../build/Release/shaders/" + name};
    for (const auto& path : candidates) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) continue;
        const size_t size = static_cast<size_t>(file.tellg());
        std::vector<char> code(size);
        file.seekg(0);
        file.read(code.data(), static_cast<std::streamsize>(size));
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &info, nullptr, &module) == VK_SUCCESS) {
            return module;
        }
        return VK_NULL_HANDLE;
    }
    std::cerr << "[ProbeGI] Missing shader " << name << "\n";
    return VK_NULL_HANDLE;
}

inline void SubmitOnce(VulkanDevice* device, const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = device->commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device->device, &alloc, &cmd);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    record(cmd);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(device->graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(device->graphicsQueue);
    vkFreeCommandBuffers(device->device, device->commandPool, 1, &cmd);
}

} // namespace Hephaestus::ProbeGpu
