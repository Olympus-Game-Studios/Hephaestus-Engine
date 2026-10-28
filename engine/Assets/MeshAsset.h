#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>

#include "../Core/Math.h"
#include "../Gfx/Vertex.h"
#include "../Gfx/VulkanBuffer.h"

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    struct Submesh {
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        int materialSlot = 0;
        std::string name;
        AABB bounds{};
    };
    std::vector<Submesh> submeshes;
    AABB bounds{};

    void Clear() {
        vertices.clear();
        indices.clear();
        submeshes.clear();
        bounds = AABB();
    }
};

struct MeshGPU {
    std::string name;
    std::filesystem::path sourcePath;
    std::string sourceKey;
    VulkanBuffer vertexBuffer;
    VulkanBuffer indexBuffer;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    std::vector<MeshData::Submesh> submeshes;
    AABB bounds{};
    bool imported = false;
    bool skinned = false;
    uint32_t boneCount = 0;
    VulkanBuffer boneBuffer;
    VkDescriptorSet boneDescriptorSet = VK_NULL_HANDLE;
    // Editor only: object-space triangles for probe baking.
    std::vector<Vec3> cpuPositions;
    std::vector<uint32_t> cpuIndices;

    void Destroy(VmaAllocator allocator) {
        vertexBuffer.Destroy(allocator);
        indexBuffer.Destroy(allocator);
        boneBuffer.Destroy(allocator);
        vertexCount = 0;
        indexCount = 0;
        boneCount = 0;
        skinned = false;
        boneDescriptorSet = VK_NULL_HANDLE;
        submeshes.clear();
        cpuPositions.clear();
        cpuIndices.clear();
        imported = false;
        sourceKey.clear();
        sourcePath.clear();
    }
};
