#pragma once
#include <cmath>
#include <vector>
#include "VulkanGizmo.h"
#include "VulkanBuffer.h"

// Mesh builders shared by the editor gizmos (VulkanGizmo, VulkanLightGizmo).
namespace GizmoGeometry {
inline constexpr float kPi = 3.14159265358979f;

using Vertices = std::vector<GizmoVertex>;
using Indices = std::vector<uint16_t>;

inline void OrthonormalBasis(const Vec3& n, Vec3& u, Vec3& v) {
    const Vec3 helper = (std::abs(n.x) < 0.9f) ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    u = vnorm(vcross(n, helper));
    v = vnorm(vcross(n, u));
}

inline uint16_t Base(const Vertices& vertices) {
    return static_cast<uint16_t>(vertices.size());
}

inline void AddCylinder(Vertices& vertices, Indices& indices, const Vec3& a, const Vec3& b,
                 float radius, const Vec3& color, float handle, int sides = 12) {
    const Vec3 dir = vnorm(vsub(b, a));
    Vec3 u, v;
    OrthonormalBasis(dir, u, v);
    const uint16_t base = Base(vertices);
    for (int i = 0; i < sides; ++i) {
        const float angle = i * 2.0f * kPi / sides;
        const Vec3 n = vadd(vscale(u, std::cos(angle)), vscale(v, std::sin(angle)));
        vertices.push_back({vadd(a, vscale(n, radius)), n, color, handle});
        vertices.push_back({vadd(b, vscale(n, radius)), n, color, handle});
    }
    for (int i = 0; i < sides; ++i) {
        const int next = (i + 1) % sides;
        const uint16_t i0 = base + i * 2, i1 = i0 + 1;
        const uint16_t i2 = base + next * 2, i3 = i2 + 1;
        indices.insert(indices.end(), {i0, i1, i2, i2, i1, i3});
    }
}

inline void AddCone(Vertices& vertices, Indices& indices, const Vec3& baseCenter, const Vec3& tip,
             float radius, const Vec3& color, float handle, int sides = 16) {
    const Vec3 dir = vnorm(vsub(tip, baseCenter));
    Vec3 u, v;
    OrthonormalBasis(dir, u, v);
    const uint16_t ring = Base(vertices);
    for (int i = 0; i < sides; ++i) {
        const float angle = i * 2.0f * kPi / sides;
        const Vec3 radial = vadd(vscale(u, std::cos(angle)), vscale(v, std::sin(angle)));
        const Vec3 n = vnorm(vadd(radial, vscale(dir, 0.5f)));
        vertices.push_back({vadd(baseCenter, vscale(radial, radius)), n, color, handle});
    }
    const uint16_t tipIndex = Base(vertices);
    vertices.push_back({tip, dir, color, handle});
    const uint16_t capCenter = Base(vertices);
    vertices.push_back({baseCenter, vscale(dir, -1.0f), color, handle});
    for (int i = 0; i < sides; ++i) {
        const uint16_t a = ring + i;
        const uint16_t b = ring + (i + 1) % sides;
        indices.insert(indices.end(), {a, tipIndex, b, a, b, capCenter});
    }
}

inline void AddBox(Vertices& vertices, Indices& indices, const Vec3& center, float size,
            const Vec3& color, float handle) {
    const float h = size * 0.5f;
    const Vec3 normals[6] = { Vec3(1,0,0), Vec3(-1,0,0), Vec3(0,1,0),
                              Vec3(0,-1,0), Vec3(0,0,1), Vec3(0,0,-1) };
    for (const Vec3& n : normals) {
        Vec3 u, v;
        OrthonormalBasis(n, u, v);
        const Vec3 faceCenter = vadd(center, vscale(n, h));
        const uint16_t base = Base(vertices);
        const float corners[4][2] = { {-1, -1}, {1, -1}, {1, 1}, {-1, 1} };
        for (const auto& c : corners) {
            const Vec3 p = vadd(faceCenter, vadd(vscale(u, c[0] * h), vscale(v, c[1] * h)));
            vertices.push_back({p, n, color, handle});
        }
        indices.insert(indices.end(), {base, static_cast<uint16_t>(base + 1),
                                       static_cast<uint16_t>(base + 2), base,
                                       static_cast<uint16_t>(base + 2),
                                       static_cast<uint16_t>(base + 3)});
    }
}

inline void AddTorus(Vertices& vertices, Indices& indices, const Vec3& axis, float radius,
              float tube, const Vec3& color, float handle, int segments = 96, int sides = 8) {
    Vec3 u, v;
    OrthonormalBasis(axis, u, v);
    const uint16_t base = Base(vertices);
    for (int i = 0; i <= segments; ++i) {
        const float a = i * 2.0f * kPi / segments;
        const Vec3 radial = vadd(vscale(u, std::cos(a)), vscale(v, std::sin(a)));
        const Vec3 ringPos = vscale(radial, radius);
        for (int j = 0; j < sides; ++j) {
            const float b = j * 2.0f * kPi / sides;
            const Vec3 n = vadd(vscale(radial, std::cos(b)), vscale(axis, std::sin(b)));
            vertices.push_back({vadd(ringPos, vscale(n, tube)), n, color, handle});
        }
    }
    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < sides; ++j) {
            const uint16_t a = base + i * sides + j;
            const uint16_t b = base + i * sides + (j + 1) % sides;
            const uint16_t c = a + sides;
            const uint16_t d = b + sides;
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }
}

// Flat ring in the XY plane, facing +Z.
inline void AddAnnulus(Vertices& vertices, Indices& indices, float inner, float outer,
                const Vec3& color, float handle, int segments = 96) {
    const uint16_t base = Base(vertices);
    for (int i = 0; i <= segments; ++i) {
        const float a = i * 2.0f * kPi / segments;
        const Vec3 radial(std::cos(a), std::sin(a), 0.0f);
        vertices.push_back({vscale(radial, inner), Vec3(0, 0, 1), color, handle});
        vertices.push_back({vscale(radial, outer), Vec3(0, 0, 1), color, handle});
    }
    for (int i = 0; i < segments; ++i) {
        const uint16_t i0 = base + i * 2, i1 = i0 + 1, i2 = i0 + 2, i3 = i0 + 3;
        indices.insert(indices.end(), {i0, i1, i2, i2, i1, i3});
    }
}

// Filled disc in the XY plane, facing +Z.
inline void AddDisc(Vertices& vertices, Indices& indices, float radius, const Vec3& color,
             float handle, int segments) {
    const uint16_t center = Base(vertices);
    vertices.push_back({Vec3(0, 0, 0), Vec3(0, 0, 1), color, handle});
    for (int i = 0; i <= segments; ++i) {
        const float a = i * 2.0f * kPi / segments;
        vertices.push_back({Vec3(std::cos(a) * radius, std::sin(a) * radius, 0.0f),
                            Vec3(0, 0, 1), color, handle});
    }
    for (int i = 0; i < segments; ++i) {
        indices.insert(indices.end(), {center, static_cast<uint16_t>(center + 1 + i),
                                       static_cast<uint16_t>(center + 2 + i)});
    }
}

inline void AddPlaneSquare(Vertices& vertices, Indices& indices, const Vec3& a, const Vec3& b,
                    float inner, float outer, const Vec3& color, float handle) {
    const uint16_t base = Base(vertices);
    const Vec3 normal = vnorm(vcross(a, b));
    vertices.push_back({vadd(vscale(a, inner), vscale(b, inner)), normal, color, handle});
    vertices.push_back({vadd(vscale(a, outer), vscale(b, inner)), normal, color, handle});
    vertices.push_back({vadd(vscale(a, outer), vscale(b, outer)), normal, color, handle});
    vertices.push_back({vadd(vscale(a, inner), vscale(b, outer)), normal, color, handle});
    indices.insert(indices.end(), {base, static_cast<uint16_t>(base + 1),
                                   static_cast<uint16_t>(base + 2), base,
                                   static_cast<uint16_t>(base + 2),
                                   static_cast<uint16_t>(base + 3)});
}

inline void UploadGeometry(VulkanBuffer*& vertexBuffer, VulkanBuffer*& indexBuffer, uint32_t& indexCount,
                    VmaAllocator allocator, Vertices& vertices, Indices& indices) {
    indexCount = static_cast<uint32_t>(indices.size());
    vertexBuffer = new VulkanBuffer();
    vertexBuffer->Create(allocator, vertices.size() * sizeof(GizmoVertex),
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
                         vertices.data());
    indexBuffer = new VulkanBuffer();
    indexBuffer->Create(allocator, indices.size() * sizeof(uint16_t),
                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
                        indices.data());
}

} // namespace GizmoGeometry
