/**
 * @file ModelMaterialImport.h
 * @brief Source-model materials (glTF, FBX, loose texture sets) written as Hephaestus .hematerial assets.
 */

#pragma once

#include "../Core/Math.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Hephaestus {

struct FbxMaterialInfo;

/// A source texture as bytes or a file, plus how a scalar map is taken from it. Packed maps are referenced as they
/// are with the material's channel set; only inverted, glossiness and DirectX normal maps are rewritten.
struct SourceTexture {
    std::vector<uint8_t> embedded;
    std::string extension; ///< ".png", ".jpg", ... for embedded bytes
    std::filesystem::path file;
    int channel = -1;          ///< 0..3 reads that channel of a packed map; -1 reads red
    bool invert = false;       ///< glossiness → roughness
    float glossExponent = 0.0f; ///< > 0: Phong glossiness map scaling this ShininessExponent (FBX SDK exporters)
    bool flipGreen = false;    ///< DirectX normal map → OpenGL
    bool Exists() const { return !embedded.empty() || !file.empty(); }
};

enum class SourceBlend { Opaque, Blend };

struct SourceMaterial {
    std::string name;
    Vec3 baseColor{1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    Vec3 emissive{0.0f, 0.0f, 0.0f};
    SourceBlend blend = SourceBlend::Opaque;
    SourceTexture baseColorTexture;
    SourceTexture normalTexture;
    SourceTexture roughnessTexture;
    SourceTexture metallicTexture;
    SourceTexture aoTexture;
};

/// glTF materials, index-aligned with the file's materials[] array.
bool ReadGltfMaterials(const std::filesystem::path& path, std::vector<SourceMaterial>& outMaterials,
                       std::string& outError);

SourceMaterial SourceMaterialFromFbx(const FbxMaterialInfo& info);

/// Writes .hematerial files and their textures into one folder; a texture used by several materials is written once.
class MaterialAssetWriter {
public:
    MaterialAssetWriter(std::filesystem::path projectRoot, std::filesystem::path folder);

    /// Returns the material's portable path, or empty with outError set.
    std::string Write(const SourceMaterial& material, const std::string& stem, std::string& outError);

    /// Portable path of a texture written as is or converted; empty when it cannot be read or written.
    std::string WriteTexture(const SourceTexture& texture, const std::string& stem, bool scalar);

    /// Source files written so far, for callers that copy whatever else a pack contains.
    const std::map<std::string, std::string>& Written() const { return m_written; }

private:
    std::filesystem::path m_projectRoot;
    std::filesystem::path m_folder;
    std::map<std::string, std::string> m_written;
};

/// File-name safe version of a material or asset name.
std::string SanitizeAssetName(std::string name);

} // namespace Hephaestus
