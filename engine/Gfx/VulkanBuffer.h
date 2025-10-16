#pragma once
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <stdexcept>

struct VulkanBuffer {
    VkBuffer buffer{};
    VmaAllocation allocation{};

    void Create(VmaAllocator allocator, VkDeviceSize size,
                VkBufferUsageFlags usage,
                VmaMemoryUsage memoryUsage) 
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = memoryUsage;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                            &buffer, &allocation, nullptr) != VK_SUCCESS)
            throw std::runtime_error("Failed to create buffer");
    }

    void Destroy(VmaAllocator allocator) {
        if (buffer)
            vmaDestroyBuffer(allocator, buffer, allocation);
    }
};
