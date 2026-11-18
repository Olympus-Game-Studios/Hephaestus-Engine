#pragma once

#include "MeshAsset.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Hephaestus {

// Model files loaded for collision and navigation, shared by everything that needs a mesh's triangles. Each file is
// loaded once and loaded again when it changes on disk. Safe to use from any thread.
class MeshGeometryCache {
public:
    // The mesh at a MeshRenderer's meshAssetPath (absolute, project-relative or under assets/), or null when it
    // cannot be found or loaded. Built-in meshes ("builtin://") have no file and return null.
    static std::shared_ptr<const MeshData> Get(const std::string& meshAssetPath,
                                               const std::filesystem::path& projectRoot = {});
    // Where a meshAssetPath points: the project root first, then the working and executable directories and their
    // parents, then every project under a "projects" folder.
    static std::filesystem::path Resolve(const std::string& meshAssetPath, const std::filesystem::path& projectRoot = {});
    static void Clear();
};

} // namespace Hephaestus
