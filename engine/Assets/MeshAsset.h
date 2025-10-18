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
    AABB bounds{};

    void Clear() {
        vertices.clear();
        indices.clear();
        bounds = AABB();
    }
};

struct MeshGPU {
    std::string name;
    std::filesystem::path sourcePath;
    VulkanBuffer vertexBuffer;
    VulkanBuffer indexBuffer;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    AABB bounds{};
    bool imported = false;

    void Destroy(VmaAllocator allocator) {
        vertexBuffer.Destroy(allocator);
        indexBuffer.Destroy(allocator);
        vertexCount = 0;
        indexCount = 0;
        imported = false;
    }
};
