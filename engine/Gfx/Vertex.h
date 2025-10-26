#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstddef>

struct Vertex {
    float pos[3];
    float normal[3];
    float color[3];
    float texCoord[2];

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attribs(4);
        attribs[0].binding = 0;
        attribs[0].location = 0;
        attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribs[0].offset = offsetof(Vertex, pos);

        attribs[1].binding = 0;
        attribs[1].location = 1;
        attribs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribs[1].offset = offsetof(Vertex, normal);

        attribs[2].binding = 0;
        attribs[2].location = 2;
        attribs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribs[2].offset = offsetof(Vertex, color);

        attribs[3].binding = 0;
        attribs[3].location = 3;
        attribs[3].format = VK_FORMAT_R32G32_SFLOAT;
        attribs[3].offset = offsetof(Vertex, texCoord);
        return attribs;
    }
};
