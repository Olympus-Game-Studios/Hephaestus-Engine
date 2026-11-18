/**
 * @file FbxStaticImporter.h
 * @brief ufbx scene → MeshData for static meshes.
 */

#pragma once

#include "FbxImportSettings.h"
#include "MeshAsset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus {

struct FbxStaticImportResult {
    bool containsSkin = false;
    std::vector<std::string> warnings;
};

/// One mesh node of an FBX: its geometry with the node's rotation and scale baked in, placed relative to origin.
struct FbxMeshPart {
    std::string name;
    MeshData mesh;
    Vec3 origin{0.0f, 0.0f, 0.0f}; ///< the node's world position, where the part sits in the original file
};

class FbxStaticImporter {
public:
    /// Every mesh node as its own part (for importing a kit of pieces as separate meshes).
    static bool LoadParts(const std::filesystem::path& path,
                          FbxSpaceConversion spaceConversion,
                          std::vector<FbxMeshPart>& outParts,
                          FbxStaticImportResult& outMeta,
                          std::string& outError);

    static bool Load(const std::filesystem::path& path,
                     FbxSpaceConversion spaceConversion,
                     MeshData& outMesh,
                     FbxStaticImportResult& outMeta,
                     std::string& outError);
};

} // namespace Hephaestus
