#include "BuiltinMeshes.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

#include "EngineFrameRenderer.h"
#include "../Assets/SkeletalModelImporter.h"
#include "../Core/Math.h"

namespace Hephaestus {

namespace {

MeshData BuildCube()
{
    MeshData cubeMesh;
    const float s = 0.5f;
    cubeMesh.vertices = {
        {{-s, -s,  s}, { 0,  0,  1}, {1, 1, 1}, {0, 0}},
        {{ s, -s,  s}, { 0,  0,  1}, {1, 1, 1}, {1, 0}},
        {{ s,  s,  s}, { 0,  0,  1}, {1, 1, 1}, {1, 1}},
        {{-s,  s,  s}, { 0,  0,  1}, {1, 1, 1}, {0, 1}},
        {{ s, -s, -s}, { 0,  0, -1}, {1, 1, 1}, {0, 0}},
        {{-s, -s, -s}, { 0,  0, -1}, {1, 1, 1}, {1, 0}},
        {{-s,  s, -s}, { 0,  0, -1}, {1, 1, 1}, {1, 1}},
        {{ s,  s, -s}, { 0,  0, -1}, {1, 1, 1}, {0, 1}},
        {{-s,  s,  s}, { 0,  1,  0}, {1, 1, 1}, {0, 0}},
        {{ s,  s,  s}, { 0,  1,  0}, {1, 1, 1}, {1, 0}},
        {{ s,  s, -s}, { 0,  1,  0}, {1, 1, 1}, {1, 1}},
        {{-s,  s, -s}, { 0,  1,  0}, {1, 1, 1}, {0, 1}},
        {{-s, -s, -s}, { 0, -1,  0}, {1, 1, 1}, {0, 0}},
        {{ s, -s, -s}, { 0, -1,  0}, {1, 1, 1}, {1, 0}},
        {{ s, -s,  s}, { 0, -1,  0}, {1, 1, 1}, {1, 1}},
        {{-s, -s,  s}, { 0, -1,  0}, {1, 1, 1}, {0, 1}},
        {{ s, -s,  s}, { 1,  0,  0}, {1, 1, 1}, {0, 0}},
        {{ s, -s, -s}, { 1,  0,  0}, {1, 1, 1}, {1, 0}},
        {{ s,  s, -s}, { 1,  0,  0}, {1, 1, 1}, {1, 1}},
        {{ s,  s,  s}, { 1,  0,  0}, {1, 1, 1}, {0, 1}},
        {{-s, -s, -s}, {-1,  0,  0}, {1, 1, 1}, {0, 0}},
        {{-s, -s,  s}, {-1,  0,  0}, {1, 1, 1}, {1, 0}},
        {{-s,  s,  s}, {-1,  0,  0}, {1, 1, 1}, {1, 1}},
        {{-s,  s, -s}, {-1,  0,  0}, {1, 1, 1}, {0, 1}},
    };
    for (uint32_t face = 0; face < 6; ++face) {
        const uint32_t base = face * 4;
        cubeMesh.indices.insert(cubeMesh.indices.end(),
                                {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }
    cubeMesh.bounds = AABB(Vec3(-s, -s, -s), Vec3(s, s, s));
    return cubeMesh;
}

MeshData BuildPlane()
{
    MeshData planeMesh;
    const float p = 0.75f;
    planeMesh.vertices = {
        {{-p, 0.0f, -p}, {0, 1, 0}, {1, 1, 1}, {0, 0}},
        {{ p, 0.0f, -p}, {0, 1, 0}, {1, 1, 1}, {1, 0}},
        {{ p, 0.0f,  p}, {0, 1, 0}, {1, 1, 1}, {1, 1}},
        {{-p, 0.0f,  p}, {0, 1, 0}, {1, 1, 1}, {0, 1}},
    };
    planeMesh.indices = {0, 1, 2, 0, 2, 3};
    planeMesh.bounds = AABB(Vec3(-p, 0.0f, -p), Vec3(p, 0.0f, p));
    return planeMesh;
}

MeshData BuildSphere()
{
    MeshData sphereMesh;
    const int stacks = 16;
    const int slices = 32;
    const float radius = 0.5f;
    for (int i = 0; i <= stacks; ++i) {
        const float phi = static_cast<float>(i) / stacks * 3.14159265f;
        for (int j = 0; j <= slices; ++j) {
            const float theta = static_cast<float>(j) / slices * 2.0f * 3.14159265f;
            const float x = radius * sinf(phi) * cosf(theta);
            const float y = radius * cosf(phi);
            const float z = radius * sinf(phi) * sinf(theta);
            const Vec3 normal = vnorm(Vec3(x, y, z));
            const float u = static_cast<float>(j) / slices;
            const float v = static_cast<float>(i) / stacks;
            sphereMesh.vertices.push_back({{x, y, z}, {normal.x, normal.y, normal.z}, {1, 1, 1}, {u, v}});
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            const uint32_t first = i * (slices + 1) + j;
            const uint32_t second = first + slices + 1;
            sphereMesh.indices.insert(sphereMesh.indices.end(),
                                      {first, second, first + 1, second, second + 1, first + 1});
        }
    }
    sphereMesh.bounds = AABB(Vec3(-radius, -radius, -radius), Vec3(radius, radius, radius));
    return sphereMesh;
}

void AddSkinnedRestFixture(EngineFrameRenderer& renderer)
{
    SkeletalImportResult fixture;
    std::vector<Mat4> palette;
    SkeletalModelImporter::BuildRestPoseFixture(fixture, palette);
    if (fixture.meshes.empty()) {
        return;
    }
    const SkinnedMeshPart& part = fixture.meshes.front();
    std::vector<SkinnedVertex> verts(part.vertices.size());
    for (size_t i = 0; i < part.vertices.size(); ++i) {
        const auto& src = part.vertices[i];
        std::memcpy(verts[i].pos, src.pos, sizeof(src.pos));
        std::memcpy(verts[i].normal, src.normal, sizeof(src.normal));
        std::memcpy(verts[i].color, src.color, sizeof(src.color));
        std::memcpy(verts[i].texCoord, src.texCoord, sizeof(src.texCoord));
        std::memcpy(verts[i].joints, src.joints, sizeof(src.joints));
        std::memcpy(verts[i].weights, src.weights, sizeof(src.weights));
    }
    std::vector<MeshData::Submesh> submeshes;
    for (const auto& sub : part.submeshes) {
        MeshData::Submesh gpuSub{};
        gpuSub.indexOffset = sub.indexOffset;
        gpuSub.indexCount = sub.indexCount;
        gpuSub.materialSlot = sub.materialSlot;
        gpuSub.name = sub.name;
        submeshes.push_back(std::move(gpuSub));
    }
    renderer.AddSkinnedMesh("Skinned Rest Fixture", verts, part.indices, submeshes, fixture.bounds, palette,
                            std::filesystem::path("builtin://skinned_rest_fixture"), false);
}

} // namespace

bool BuildBuiltinMesh(const std::string& key, MeshData& outMesh)
{
    if (key == "builtin://cube") outMesh = BuildCube();
    else if (key == "builtin://plane") outMesh = BuildPlane();
    else if (key == "builtin://sphere") outMesh = BuildSphere();
    else return false;
    return true;
}

void AddBuiltinMeshes(EngineFrameRenderer& renderer)
{
    renderer.AddStaticMesh("Default Cube", BuildCube(), "builtin://cube", false);
    renderer.AddStaticMesh("Default Plane", BuildPlane(), "builtin://plane", false);
    renderer.AddStaticMesh("Default Sphere", BuildSphere(), "builtin://sphere", false);
    AddSkinnedRestFixture(renderer);
}

} // namespace Hephaestus
