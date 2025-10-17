#pragma once
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <stdexcept>
#include <cstring>

struct VulkanBuffer {
    VkBuffer buffer{};
    VmaAllocation allocation{};

    void Create(VmaAllocator allocator, VkDeviceSize size,
                VkBufferUsageFlags usage,
                VmaMemoryUsage memoryUsage,
                void* data = nullptr) 
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = memoryUsage;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to create buffer!");
        }

        if (data != nullptr) {
            void* mappedData = nullptr;
            vmaMapMemory(allocator, allocation, &mappedData);
            std::memcpy(mappedData, data, static_cast<size_t>(size));
            vmaUnmapMemory(allocator, allocation);
        }
    }

    void Destroy(VmaAllocator allocator) {
        if (buffer)
            vmaDestroyBuffer(allocator, buffer, allocation);
    }
};
