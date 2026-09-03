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

class FbxStaticImporter {
public:
    static bool Load(const std::filesystem::path& path,
                     FbxSpaceConversion spaceConversion,
                     MeshData& outMesh,
                     FbxStaticImportResult& outMeta,
                     std::string& outError);
};

} // namespace Hephaestus
