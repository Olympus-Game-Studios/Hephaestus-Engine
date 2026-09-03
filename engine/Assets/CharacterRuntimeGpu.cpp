#include "CharacterRuntime.h"

#include "../Animation/Skeleton.h"
#include "../Gfx/EngineFrameRenderer.h"
#include "../Gfx/Vertex.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"

namespace Hephaestus {
namespace {

std::vector<SkinnedVertex> ToSkinnedVertices(const SkeletalImportResult& imported) {
    std::vector<SkinnedVertex> vertices;
    vertices.reserve(imported.vertices.size());
    for (const SkinnedVertexData& src : imported.vertices) {
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

std::vector<MeshData::Submesh> ToSubmeshes(const SkeletalImportResult& imported) {
    std::vector<MeshData::Submesh> submeshes;
    submeshes.reserve(imported.submeshes.size());
    for (const SkinnedSubmeshData& src : imported.submeshes) {
        MeshData::Submesh dst;
        dst.indexOffset = src.indexOffset;
        dst.indexCount = src.indexCount;
        dst.materialSlot = src.materialSlot;
        dst.name = src.name;
        dst.bounds = imported.bounds;
        submeshes.push_back(std::move(dst));
    }
    return submeshes;
}

} // namespace

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

    const std::string key = character.characterPath;
    if (forceReload) {
        renderer->InvalidateMesh(key);
    }
    size_t handle = renderer->GetMeshHandle(key);
    if (handle == SIZE_MAX) {
        std::vector<Mat4> palette;
        SkeletonBuilder::FillRestBonePalette(data.imported.skeleton, palette);
        const std::vector<SkinnedVertex> vertices = ToSkinnedVertices(data.imported);
        const std::vector<MeshData::Submesh> submeshes = ToSubmeshes(data.imported);
        if (vertices.empty() || data.imported.indices.empty()) {
            outError = "Character mesh has no geometry.";
            return false;
        }
        handle = renderer->AddSkinnedMesh(data.asset.name.empty() ? "Character" : data.asset.name,
                                          vertices, data.imported.indices, submeshes,
                                          data.imported.bounds, palette, key, true);
        if (handle == SIZE_MAX) {
            outError = "Failed to upload character mesh.";
            return false;
        }
        renderer->AddMeshAlias(key, handle);
    }

    mesh.meshHandle = handle;
    mesh.localBounds = data.imported.bounds;
    if (const MeshGPU* gpu = renderer->GetMesh(handle)) {
        const int subCount =
            gpu->submeshes.empty() ? 1 : static_cast<int>(gpu->submeshes.size());
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
