#include "CharacterRuntime.h"

#include "../Animation/Skeleton.h"
#include "../Gfx/EngineFrameRenderer.h"
#include "../Gfx/Vertex.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"

namespace Hephaestus {
namespace {

std::string MeshPartSourceKey(const std::string& characterPath, size_t partIndex) {
    if (partIndex == 0) {
        return characterPath;
    }
    return characterPath + "#" + std::to_string(partIndex);
}

std::vector<SkinnedVertex> ToSkinnedVertices(const SkinnedMeshPart& part) {
    std::vector<SkinnedVertex> vertices;
    vertices.reserve(part.vertices.size());
    for (const SkinnedVertexData& src : part.vertices) {
        SkinnedVertex dst{};
        dst.pos[0] = src.pos[0];
        dst.pos[1] = src.pos[1];
        dst.pos[2] = src.pos[2];
        dst.normal[0] = src.normal[0];
        dst.normal[1] = src.normal[1];
        dst.normal[2] = src.normal[2];
        dst.color[0] = src.color[0];
        dst.color[1] = src.color[1];
        dst.color[2] = src.color[2];
        dst.texCoord[0] = src.texCoord[0];
        dst.texCoord[1] = src.texCoord[1];
        dst.joints[0] = src.joints[0];
        dst.joints[1] = src.joints[1];
        dst.joints[2] = src.joints[2];
        dst.joints[3] = src.joints[3];
        dst.weights[0] = src.weights[0];
        dst.weights[1] = src.weights[1];
        dst.weights[2] = src.weights[2];
        dst.weights[3] = src.weights[3];
        vertices.push_back(dst);
    }
    return vertices;
}

std::vector<MeshData::Submesh> ToSubmeshes(const SkinnedMeshPart& part, int slotBase) {
    std::vector<MeshData::Submesh> submeshes;
    submeshes.reserve(part.submeshes.size());
    for (size_t i = 0; i < part.submeshes.size(); ++i) {
        const SkinnedSubmeshData& src = part.submeshes[i];
        MeshData::Submesh dst;
        dst.indexOffset = src.indexOffset;
        dst.indexCount = src.indexCount;
        dst.materialSlot = slotBase + static_cast<int>(i);
        dst.name = src.name;
        dst.bounds = part.bounds;
        submeshes.push_back(std::move(dst));
    }
    return submeshes;
}

} // namespace

void CharacterRuntime::InvalidateGpuMeshes(EngineFrameRenderer* renderer,
                                           const std::string& portableCharacterPath) {
    if (!renderer || portableCharacterPath.empty()) {
        return;
    }
    renderer->WaitGpuIdle();
    renderer->InvalidateMesh(portableCharacterPath);
    for (size_t i = 1; i < 64; ++i) {
        const std::string partKey = MeshPartSourceKey(portableCharacterPath, i);
        if (renderer->GetMeshHandle(partKey) == SIZE_MAX) {
            break;
        }
        renderer->InvalidateMesh(partKey);
    }
}

bool CharacterRuntime::UploadVisual(Entity entity,
                                    const std::filesystem::path& projectRoot,
                                    EngineFrameRenderer* renderer,
                                    std::string& outError,
                                    bool forceReload) {
    outError.clear();
    if (!renderer) {
        return true;
    }
    if (!entity || !entity.HasComponent<CharacterComponent>() ||
        !entity.HasComponent<MeshRendererComponent>()) {
        return true;
    }

    auto& character = entity.GetComponent<CharacterComponent>();
    auto& mesh = entity.GetComponent<MeshRendererComponent>();
    if (!::IsRuntimeGeneratedVisual(mesh) || character.characterPath.empty()) {
        return true;
    }

    CharacterRuntimeData data;
    if (!Load(projectRoot, character.characterPath, data, outError)) {
        return false;
    }
    if (data.imported.meshes.empty()) {
        outError = "Character mesh has no geometry.";
        return false;
    }

    const std::string key = character.characterPath;
    bool needsUpload = forceReload;
    if (!needsUpload) {
        for (size_t i = 0; i < data.imported.meshes.size(); ++i) {
            if (renderer->GetMeshHandle(MeshPartSourceKey(key, i)) == SIZE_MAX) {
                needsUpload = true;
                break;
            }
        }
        if (!needsUpload &&
            renderer->GetMeshHandle(MeshPartSourceKey(key, data.imported.meshes.size())) != SIZE_MAX) {
            needsUpload = true;
        }
    }
    if (needsUpload) {
        InvalidateGpuMeshes(renderer, key);
        std::vector<Mat4> palette;
        SkeletonBuilder::FillRestBonePalette(data.imported.skeleton, palette);
        int slotBase = 0;
        std::vector<size_t> handles;
        handles.reserve(data.imported.meshes.size());
        for (size_t i = 0; i < data.imported.meshes.size(); ++i) {
            const SkinnedMeshPart& part = data.imported.meshes[i];
            const std::vector<SkinnedVertex> vertices = ToSkinnedVertices(part);
            const std::vector<MeshData::Submesh> submeshes = ToSubmeshes(part, slotBase);
            if (vertices.empty() || part.indices.empty()) {
                outError = "Character mesh has no geometry.";
                return false;
            }
            const std::string partName = data.asset.name.empty() ? "Character" : data.asset.name;
            const std::string gpuName =
                part.name.empty() ? partName : (partName + "/" + part.name);
            const size_t handle = renderer->AddSkinnedMesh(
                gpuName, vertices, part.indices, submeshes, part.bounds, palette,
                std::filesystem::path(MeshPartSourceKey(key, i)), true);
            if (handle == SIZE_MAX) {
                outError = "Failed to upload character mesh.";
                return false;
            }
            renderer->AddMeshAlias(MeshPartSourceKey(key, i), handle);
            handles.push_back(handle);
            slotBase += static_cast<int>(part.submeshes.empty() ? 1 : part.submeshes.size());
        }
        mesh.meshHandle = handles.front();
        mesh.meshPartHandles = std::move(handles);
    } else {
        std::vector<size_t> handles;
        handles.reserve(data.imported.meshes.size());
        for (size_t i = 0; i < data.imported.meshes.size(); ++i) {
            handles.push_back(renderer->GetMeshHandle(MeshPartSourceKey(key, i)));
        }
        mesh.meshHandle = handles.front();
        mesh.meshPartHandles = std::move(handles);
    }

    mesh.localBounds = data.imported.bounds;
    int subCount = 0;
    for (const SkinnedMeshPart& part : data.imported.meshes) {
        subCount += part.submeshes.empty() ? 1 : static_cast<int>(part.submeshes.size());
    }
    if (subCount > 0) {
        mesh.submeshCount = subCount;
    }
    return true;
}

bool CharacterRuntime::BindScene(Scene& scene,
                                 const std::filesystem::path& projectRoot,
                                 EngineFrameRenderer* renderer) {
    bool ok = true;
    auto view = scene.GetEntitiesWith<CharacterComponent>();
    for (auto handle : view) {
        Entity entity(handle, &scene);
        std::string error;
        if (!BindEntity(entity, projectRoot, false, error)) {
            ok = false;
            continue;
        }
        if (!UploadVisual(entity, projectRoot, renderer, error)) {
            ok = false;
        }
    }
    return ok;
}

} // namespace Hephaestus
