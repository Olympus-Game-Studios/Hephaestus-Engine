/**
 * @file FbxDocument.cpp
 * @brief ufbx scene load/free, space conversion, and small conversion helpers.
 */

#include "FbxDocument.h"

#include <ufbx.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>

namespace Hephaestus {
namespace {

std::string LowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string FormatUfbxError(const ufbx_error& error) {
    if (error.description.length > 0 && error.description.data) {
        return std::string(error.description.data, error.description.length);
    }
    return "Failed to load FBX.";
}

} // namespace

FbxDocument::~FbxDocument() {
    if (m_scene) {
        ufbx_free_scene(m_scene);
        m_scene = nullptr;
    }
}

FbxSpaceConversion FbxDocument::ResolveSpaceConversion(FbxSpaceConversion requested, int exporter) {
    if (requested == FbxSpaceConversion::AdjustTransforms) {
        return FbxSpaceConversion::AdjustTransforms;
    }
    if (requested == FbxSpaceConversion::ModifyGeometry) {
        return FbxSpaceConversion::ModifyGeometry;
    }
    if (exporter == UFBX_EXPORTER_BLENDER_BINARY || exporter == UFBX_EXPORTER_BLENDER_ASCII) {
        return FbxSpaceConversion::AdjustTransforms;
    }
    return FbxSpaceConversion::ModifyGeometry;
}

bool FbxDocument::Load(const std::filesystem::path& path,
                       const FbxLoadSettings& settings,
                       std::string& outError) {
    outError.clear();
    if (m_scene) {
        ufbx_free_scene(m_scene);
        m_scene = nullptr;
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        outError = "FBX file not found.";
        return false;
    }
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (!ec && size > kMaxFileBytes) {
        outError = "FBX file is too large to import (maximum 256 MB).";
        return false;
    }

    const auto u8 = path.u8string();
    const std::string pathUtf8(reinterpret_cast<const char*>(u8.c_str()), u8.size());

    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;
    opts.ignore_missing_external_files = true;
    opts.load_external_files = false;
    opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
    if (settings.skeletalMesh) {
        opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_PRESERVE;
    } else {
        opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    }

    ufbx_error error;
    ufbx_scene* peek = nullptr;
    if (settings.spaceConversion == FbxSpaceConversion::Auto) {
        ufbx_load_opts peekOpts = opts;
        peekOpts.ignore_all_content = true;
        peek = ufbx_load_file(pathUtf8.c_str(), &peekOpts, &error);
        const int exporter = peek ? static_cast<int>(peek->metadata.exporter) : UFBX_EXPORTER_UNKNOWN;
        m_resolved = ResolveSpaceConversion(settings.spaceConversion, exporter);
        if (peek) {
            ufbx_free_scene(peek);
        } else {
            // Fall through to a full load so the real error is reported.
            m_resolved = FbxSpaceConversion::ModifyGeometry;
        }
    } else {
        m_resolved = ResolveSpaceConversion(settings.spaceConversion, UFBX_EXPORTER_UNKNOWN);
    }

    opts.space_conversion = (m_resolved == FbxSpaceConversion::AdjustTransforms)
                                ? UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS
                                : UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

    m_scene = ufbx_load_file(pathUtf8.c_str(), &opts, &error);
    if (!m_scene) {
        outError = FormatUfbxError(error);
        return false;
    }
    return true;
}

bool FbxDocument::ContainsSkin() const {
    if (!m_scene) {
        return false;
    }
    return m_scene->skin_deformers.count > 0;
}

bool FbxDocument::ContainsBlendShapes() const {
    if (!m_scene) {
        return false;
    }
    return m_scene->blend_deformers.count > 0;
}

Mat4 FbxDocument::ToMat4(const ufbx_matrix& matrix) {
    Mat4 out = Mat4::Identity();
    out.m[0] = static_cast<float>(matrix.m00);
    out.m[1] = static_cast<float>(matrix.m10);
    out.m[2] = static_cast<float>(matrix.m20);
    out.m[4] = static_cast<float>(matrix.m01);
    out.m[5] = static_cast<float>(matrix.m11);
    out.m[6] = static_cast<float>(matrix.m21);
    out.m[8] = static_cast<float>(matrix.m02);
    out.m[9] = static_cast<float>(matrix.m12);
    out.m[10] = static_cast<float>(matrix.m22);
    out.m[12] = static_cast<float>(matrix.m03);
    out.m[13] = static_cast<float>(matrix.m13);
    out.m[14] = static_cast<float>(matrix.m23);
    return out;
}

Vec3 FbxDocument::ToVec3(const ufbx_vec3& value) {
    return Vec3(static_cast<float>(value.x), static_cast<float>(value.y),
                static_cast<float>(value.z));
}

Vec4 FbxDocument::ToQuat(const ufbx_quat& value) {
    return Vec4(static_cast<float>(value.x), static_cast<float>(value.y),
                static_cast<float>(value.z), static_cast<float>(value.w));
}

std::string FbxDocument::ToString(const ufbx_string& value) {
    if (!value.data || value.length == 0) {
        return {};
    }
    return std::string(value.data, value.length);
}

std::string FbxDocument::StripBoneNamespace(const std::string& name, bool enable) {
    if (!enable) {
        return name;
    }
    const auto colon = name.find_last_of(':');
    if (colon == std::string::npos || colon + 1 >= name.size()) {
        return name;
    }
    return name.substr(colon + 1);
}

std::string FbxDocument::DetectImageExtension(const ufbx_blob& content,
                                              const std::string& filenameHint) {
    const std::string hint = LowerCopy(filenameHint);
    auto hintExt = [&hint]() -> std::string {
        const auto dot = hint.find_last_of('.');
        if (dot == std::string::npos) {
            return {};
        }
        return hint.substr(dot);
    }();

    const uint8_t* bytes = static_cast<const uint8_t*>(content.data);
    const size_t size = content.size;
    std::string magic;
    if (bytes && size >= 8 && bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E &&
        bytes[3] == 0x47) {
        magic = ".png";
    } else if (bytes && size >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        magic = ".jpg";
    } else if (bytes && size >= 2 && bytes[0] == 'B' && bytes[1] == 'M') {
        magic = ".bmp";
    } else if (bytes && size >= 18 && bytes[1] <= 1 && (bytes[2] == 2 || bytes[2] == 10)) {
        magic = ".tga";
    }

    if (!magic.empty()) {
        return magic;
    }
    if (hintExt == ".png" || hintExt == ".jpg" || hintExt == ".jpeg" || hintExt == ".tga" ||
        hintExt == ".bmp" || hintExt == ".tif" || hintExt == ".tiff") {
        return hintExt == ".jpeg" ? ".jpg" : hintExt;
    }
    return {};
}

namespace {

FbxTextureSource ResolveFbxTexture(const ufbx_texture* texture, const std::filesystem::path& fbxPath)
{
    FbxTextureSource source;
    if (!texture) {
        return source;
    }
    const std::string relative = FbxDocument::ToString(texture->relative_filename);
    const std::string named = relative.empty() ? FbxDocument::ToString(texture->filename) : relative;
    if (texture->content.size > 0) {
        source.extension = FbxDocument::DetectImageExtension(texture->content, named);
        if (!source.extension.empty()) {
            const auto* bytes = static_cast<const uint8_t*>(texture->content.data);
            source.embedded.assign(bytes, bytes + texture->content.size);
            return source;
        }
    }
    std::vector<std::filesystem::path> candidates;
    const std::string absolute = FbxDocument::ToString(texture->absolute_filename);
    if (!absolute.empty()) {
        candidates.emplace_back(absolute);
    }
    if (!named.empty()) {
        const std::filesystem::path fileName = std::filesystem::path(named).filename();
        candidates.push_back(fbxPath.parent_path() / named);
        candidates.push_back(fbxPath.parent_path() / fileName);
        candidates.push_back(fbxPath.parent_path() / (fbxPath.stem().string() + ".fbm") / fileName);
    }
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            source.file = candidate;
            break;
        }
    }
    return source;
}

} // namespace

bool ReadFbxMaterials(const std::filesystem::path& path,
                      std::vector<FbxMaterialInfo>& outMaterials,
                      std::string& outError)
{
    outMaterials.clear();
    FbxDocument document;
    if (!document.Load(path, FbxLoadSettings{}, outError)) {
        return false;
    }
    const ufbx_scene* scene = document.Scene();
    for (size_t i = 0; i < scene->materials.count; ++i) {
        const ufbx_material* material = scene->materials.data[i];
        if (!material) {
            continue;
        }
        FbxMaterialInfo info;
        info.name = FbxDocument::ToString(material->name);
        const ufbx_material_map& base = material->pbr.base_color;
        if (base.has_value) {
            info.baseColor = Vec3(static_cast<float>(base.value_vec4.x), static_cast<float>(base.value_vec4.y),
                                  static_cast<float>(base.value_vec4.z));
        }
        if (material->pbr.roughness.has_value) {
            info.roughness = std::clamp(static_cast<float>(material->pbr.roughness.value_real), 0.05f, 1.0f);
        }
        if (material->pbr.metalness.has_value) {
            info.metallic = std::clamp(static_cast<float>(material->pbr.metalness.value_real), 0.0f, 1.0f);
        }
        info.baseColorTexture = ResolveFbxTexture(
            base.texture ? base.texture : material->fbx.diffuse_color.texture, path);
        const ufbx_texture* normal = material->pbr.normal_map.texture ? material->pbr.normal_map.texture
                                                                       : material->fbx.normal_map.texture;
        info.normalTexture = ResolveFbxTexture(normal, path);
        // A ShininessExponent map is glossiness for FBX SDK exporters (Mixamo, Maya, Max); Blender writes its
        // roughness map there and reads it back as roughness.
        const ufbx_texture* shininess = material->fbx.specular_exponent.texture;
        const bool blender = scene->metadata.exporter == UFBX_EXPORTER_BLENDER_BINARY ||
                             scene->metadata.exporter == UFBX_EXPORTER_BLENDER_ASCII;
        const ufbx_texture* roughness = material->pbr.roughness.texture;
        const ufbx_texture* gloss = material->pbr.glossiness.texture;
        if (roughness && (material->features.roughness_as_glossiness.enabled || (roughness == shininess && !blender))) {
            gloss = roughness;
            roughness = nullptr;
        }
        info.roughnessTexture = ResolveFbxTexture(roughness, path);
        info.glossinessTexture = ResolveFbxTexture(gloss, path);
        if (info.baseColorTexture.Exists()) {
            // The texture carries the colour; legacy diffuse factors would only darken it.
            info.baseColor = Vec3(1.0f, 1.0f, 1.0f);
        }
        outMaterials.push_back(std::move(info));
    }
    return true;
}

bool FbxFileContainsSkin(const std::filesystem::path& path, bool& outHasSkin, std::string& outError) {
    outHasSkin = false;
    FbxDocument document;
    FbxLoadSettings settings;
    if (!document.Load(path, settings, outError)) {
        return false;
    }
    outHasSkin = document.ContainsSkin();
    return true;
}

} // namespace Hephaestus
