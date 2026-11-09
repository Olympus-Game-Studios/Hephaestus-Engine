/**
 * @file FbxDocument.h
 * @brief RAII ufbx scene loader with shared Hephaestus space-conversion options.
 */

#pragma once

#include "FbxImportSettings.h"

#include "../Core/Math.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ufbx_scene;
struct ufbx_matrix;
struct ufbx_vec3;
struct ufbx_quat;
struct ufbx_string;
struct ufbx_blob;
struct ufbx_node;

namespace Hephaestus {

struct FbxLoadSettings {
    FbxSpaceConversion spaceConversion = FbxSpaceConversion::Auto;
    bool skeletalMesh = false;
};

class FbxDocument {
public:
    static constexpr uint64_t kMaxFileBytes = 256ull * 1024ull * 1024ull;

    FbxDocument() = default;
    ~FbxDocument();

    FbxDocument(const FbxDocument&) = delete;
    FbxDocument& operator=(const FbxDocument&) = delete;

    bool Load(const std::filesystem::path& path,
              const FbxLoadSettings& settings,
              std::string& outError);

    ufbx_scene* Scene() const { return m_scene; }
    FbxSpaceConversion ResolvedSpaceConversion() const { return m_resolved; }
    bool ContainsSkin() const;
    bool ContainsBlendShapes() const;

    static Mat4 ToMat4(const ufbx_matrix& matrix);
    static Vec3 ToVec3(const ufbx_vec3& value);
    static Vec4 ToQuat(const ufbx_quat& value);
    static std::string ToString(const ufbx_string& value);
    static std::string StripBoneNamespace(const std::string& name, bool enable);
    static std::string DetectImageExtension(const ufbx_blob& content, const std::string& filenameHint);
    static FbxSpaceConversion ResolveSpaceConversion(FbxSpaceConversion requested, int exporter);

private:
    ufbx_scene* m_scene = nullptr;
    FbxSpaceConversion m_resolved = FbxSpaceConversion::ModifyGeometry;
};

/// An FBX texture as either embedded bytes or a file found next to the FBX.
struct FbxTextureSource {
    std::vector<uint8_t> embedded;
    std::string extension; ///< ".png", ".jpg", ... for embedded bytes
    std::filesystem::path file;
    bool Exists() const { return !embedded.empty() || !file.empty(); }
};

struct FbxMaterialInfo {
    std::string name;
    Vec3 baseColor{1.0f, 1.0f, 1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    FbxTextureSource baseColorTexture;
    FbxTextureSource normalTexture;
    FbxTextureSource roughnessTexture;   ///< roughness map, used as is
    FbxTextureSource glossinessTexture;  ///< glossiness map (Mixamo, Phong exporters): roughness = 1 - gloss
};

/// Materials of an FBX with their base colour and normal textures (embedded or beside the file).
bool ReadFbxMaterials(const std::filesystem::path& path,
                      std::vector<FbxMaterialInfo>& outMaterials,
                      std::string& outError);

bool FbxFileContainsSkin(const std::filesystem::path& path, bool& outHasSkin, std::string& outError);

} // namespace Hephaestus
