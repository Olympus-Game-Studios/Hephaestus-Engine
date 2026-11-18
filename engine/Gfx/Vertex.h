#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstddef>
#include <cstdint>

struct Vertex {
    float pos[3];
    float normal[3];
    float color[3];
    float texCoord[2];
    float lightmapUV[2];

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attribs(5);
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

        attribs[4].binding = 0;
        attribs[4].location = 4;
        attribs[4].format = VK_FORMAT_R32G32_SFLOAT;
        attribs[4].offset = offsetof(Vertex, lightmapUV);
        return attribs;
    }
};

/// GPU vertex for skinned meshes. Locations 0–3 match `Vertex`; 4–5 are palette joints/weights.
struct SkinnedVertex {
    float pos[3];
    float normal[3];
    float color[3];
    float texCoord[2];
    uint16_t joints[4]; ///< Eval / GPU palette indices
    float weights[4];

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(SkinnedVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attribs(6);
        attribs[0].binding = 0;
        attribs[0].location = 0;
        attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribs[0].offset = offsetof(SkinnedVertex, pos);

        attribs[1].binding = 0;
        attribs[1].location = 1;
        attribs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribs[1].offset = offsetof(SkinnedVertex, normal);

        attribs[2].binding = 0;
        attribs[2].location = 2;
        attribs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribs[2].offset = offsetof(SkinnedVertex, color);

        attribs[3].binding = 0;
        attribs[3].location = 3;
        attribs[3].format = VK_FORMAT_R32G32_SFLOAT;
        attribs[3].offset = offsetof(SkinnedVertex, texCoord);

        attribs[4].binding = 0;
        attribs[4].location = 4;
        attribs[4].format = VK_FORMAT_R16G16B16A16_UINT;
        attribs[4].offset = offsetof(SkinnedVertex, joints);

        attribs[5].binding = 0;
        attribs[5].location = 5;
        attribs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attribs[5].offset = offsetof(SkinnedVertex, weights);
        return attribs;
    }
};
