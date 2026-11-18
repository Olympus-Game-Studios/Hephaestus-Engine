#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "MeshAsset.h"

namespace ModelImporter {
    bool IsSupportedExtension(const std::filesystem::path& path);
    bool LoadOBJ(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage);
    bool LoadFBX(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage);
    bool LoadGLTF(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage);
    /// outSlotMaterials[slot] is the glTF material index behind each submesh slot, or -1 for none.
    bool LoadGLTF(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage,
                  std::vector<int>* outSlotMaterials);
    /// Only meshes of nodes whose name (or, unnamed, their mesh's name) passes includeNode are loaded.
    bool LoadGLTF(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage,
                  std::vector<int>* outSlotMaterials, const std::function<bool(const std::string&)>& includeNode);
    bool LoadModel(const std::filesystem::path& path, MeshData& outMesh, std::string& errorMessage);
}
