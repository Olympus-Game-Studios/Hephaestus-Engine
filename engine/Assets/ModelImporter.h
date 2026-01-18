#pragma once
#include <filesystem>
#include <string>

#include "MeshAsset.h"

namespace ModelImporter {
    bool IsSupportedExtension(const std::filesystem::path& path);
    bool LoadOBJ(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage);
    bool LoadFBX(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage);
    bool LoadGLTF(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage);
    bool LoadModel(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage);
}
