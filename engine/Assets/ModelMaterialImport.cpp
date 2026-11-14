/**
 * @file ModelMaterialImport.cpp
 * @brief Source-model materials (glTF, FBX, loose texture sets) written as Hephaestus .hematerial assets.
 */

#include "ModelMaterialImport.h"

#include "AssetPathPolicy.h"
#include "FbxDocument.h"
#include "GltfDocument.h"
#include "../Scene/Components.h"

#include <nlohmann/json.hpp>
#include "stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../extern/glfw/deps/stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <system_error>

using json = nlohmann::json;

namespace Hephaestus {
namespace {

Vec3 ReadVec3(const json& value, Vec3 fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    return Vec3(Gltf::ReadGLTFNumber(value[0]), Gltf::ReadGLTFNumber(value[1]), Gltf::ReadGLTFNumber(value[2]));
}

SourceTexture FromImage(Gltf::ImageSource image) {
    SourceTexture texture;
    texture.embedded = std::move(image.embedded);
    texture.extension = std::move(image.extension);
    texture.file = std::move(image.file);
    return texture;
}

SourceTexture FromFbxTexture(const FbxTextureSource& source) {
    SourceTexture texture;
    texture.embedded = source.embedded;
    texture.extension = source.extension;
    texture.file = source.file;
    return texture;
}

std::string SourceKey(const SourceTexture& texture) {
    std::string key = texture.file.empty()
        ? "embedded:" + std::to_string(std::hash<std::string>{}(
              std::string(texture.embedded.begin(), texture.embedded.end()))) +
              ":" + std::to_string(texture.embedded.size())
        : std::filesystem::absolute(texture.file).lexically_normal().generic_string();
    if (texture.invert || texture.glossExponent > 0.0f || texture.flipGreen) {
        key += "|" + std::to_string(texture.channel) + (texture.invert ? "i" : "") + (texture.flipGreen ? "g" : "") +
               (texture.glossExponent > 0.0f ? "e" + std::to_string(texture.glossExponent) : "");
    }
    return key;
}

// Channel picks are left to the renderer (MaterialComponent::roughnessChannel and friends); only value changes
// need a new image.
bool NeedsConversion(const SourceTexture& texture) {
    return texture.invert || texture.glossExponent > 0.0f || texture.flipGreen;
}

uint32_t ChannelOf(const SourceTexture& texture) {
    return NeedsConversion(texture) || texture.channel < 0 ? 0u : static_cast<uint32_t>(std::min(texture.channel, 3));
}

std::string StemFor(const SourceTexture& texture, const std::string& stem, const char* map) {
    return stem + (texture.channel >= 0 && !NeedsConversion(texture) ? "_Packed" : map);
}

} // namespace

std::string SanitizeAssetName(std::string name) {
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|' || static_cast<unsigned char>(c) < 32) {
            c = '_';
        }
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
    while (!name.empty() && name.front() == ' ') {
        name.erase(name.begin());
    }
    return name.empty() ? std::string("Material") : name;
}

bool ReadGltfMaterials(const std::filesystem::path& path, std::vector<SourceMaterial>& outMaterials,
                       std::string& outError) {
    outMaterials.clear();
    json root;
    std::vector<std::vector<uint8_t>> buffers;
    try {
        if (!Gltf::LoadGlbOrGltf(path, root, buffers, outError)) {
            return false;
        }
    } catch (const std::exception& e) {
        outError = std::string("Failed to load glTF file: ") + e.what();
        return false;
    }
    auto texture = [&](const json& info) {
        return info.is_object() ? FromImage(Gltf::ReadTextureImage(root, buffers, path, info.value("index", -1)))
                                : SourceTexture{};
    };

    const json materials = root.value("materials", json::array());
    for (size_t i = 0; i < materials.size(); ++i) {
        const json& m = materials[i];
        SourceMaterial out;
        out.name = m.value("name", "Material " + std::to_string(i));
        const json extensions = m.value("extensions", json::object());
        if (extensions.contains("KHR_materials_pbrSpecularGlossiness")) {
            const json& sg = extensions["KHR_materials_pbrSpecularGlossiness"];
            const json diffuse = sg.value("diffuseFactor", json::array({1.0, 1.0, 1.0, 1.0}));
            out.baseColor = ReadVec3(diffuse, out.baseColor);
            out.opacity = diffuse.size() > 3 ? Gltf::ReadGLTFNumber(diffuse[3]) : 1.0f;
            out.baseColorTexture = texture(sg.value("diffuseTexture", json()));
            out.roughness = 1.0f - sg.value("glossinessFactor", 1.0f);
            out.roughnessTexture = texture(sg.value("specularGlossinessTexture", json()));
            out.roughnessTexture.channel = 3;
            out.roughnessTexture.invert = true;
            if (out.roughnessTexture.Exists()) {
                out.roughness = 1.0f;
            }
        } else {
            const json pbr = m.value("pbrMetallicRoughness", json::object());
            const json base = pbr.value("baseColorFactor", json::array({1.0, 1.0, 1.0, 1.0}));
            out.baseColor = ReadVec3(base, out.baseColor);
            out.opacity = base.size() > 3 ? Gltf::ReadGLTFNumber(base[3]) : 1.0f;
            out.metallic = pbr.value("metallicFactor", 1.0f);
            out.roughness = pbr.value("roughnessFactor", 1.0f);
            out.baseColorTexture = texture(pbr.value("baseColorTexture", json()));
            out.roughnessTexture = texture(pbr.value("metallicRoughnessTexture", json()));
            out.metallicTexture = out.roughnessTexture;
            out.roughnessTexture.channel = 1;
            out.metallicTexture.channel = 2;
        }
        out.normalTexture = texture(m.value("normalTexture", json()));
        out.aoTexture = texture(m.value("occlusionTexture", json()));
        out.aoTexture.channel = 0;
        out.emissive = ReadVec3(m.value("emissiveFactor", json()), out.emissive);
        if (extensions.contains("KHR_materials_emissive_strength")) {
            const float strength = extensions["KHR_materials_emissive_strength"].value("emissiveStrength", 1.0f);
            out.emissive = Vec3(out.emissive.x * strength, out.emissive.y * strength, out.emissive.z * strength);
        }
        const std::string alphaMode = m.value("alphaMode", "OPAQUE");
        out.blend = alphaMode == "MASK" ? SourceBlend::Mask
            : alphaMode == "BLEND"      ? SourceBlend::Blend
                                        : SourceBlend::Opaque;
        out.alphaCutoff = m.value("alphaCutoff", 0.5f);
        outMaterials.push_back(std::move(out));
    }
    return true;
}

SourceMaterial SourceMaterialFromFbx(const FbxMaterialInfo& info) {
    SourceMaterial out;
    out.name = info.name;
    out.baseColor = info.baseColor;
    out.metallic = info.metallic;
    out.roughness = info.roughness;
    out.baseColorTexture = FromFbxTexture(info.baseColorTexture);
    out.normalTexture = FromFbxTexture(info.normalTexture);
    out.roughnessTexture = FromFbxTexture(info.roughnessTexture);
    if (!out.roughnessTexture.Exists() && info.glossinessTexture.Exists()) {
        out.roughnessTexture = FromFbxTexture(info.glossinessTexture);
        out.roughnessTexture.glossExponent = std::max(info.shininessExponent, 1e-3f);
    }
    if (out.roughnessTexture.Exists()) {
        out.roughness = 1.0f;
    }
    out.metallicTexture = FromFbxTexture(info.metallicTexture);
    if (out.metallicTexture.Exists()) {
        out.metallic = 1.0f;
    }
    out.aoTexture = FromFbxTexture(info.aoTexture);
    return out;
}

MaterialAssetWriter::MaterialAssetWriter(std::filesystem::path projectRoot, std::filesystem::path folder)
    : m_projectRoot(std::move(projectRoot)), m_folder(std::move(folder)) {}

std::string MaterialAssetWriter::WriteTexture(const SourceTexture& texture, const std::string& stem, bool scalar) {
    if (!texture.Exists()) {
        return {};
    }
    const std::string key = SourceKey(texture);
    if (const auto found = m_written.find(key); found != m_written.end()) {
        return found->second;
    }
    std::error_code ec;
    std::filesystem::create_directories(m_folder, ec);
    std::filesystem::path target;
    if (!NeedsConversion(texture)) {
        if (!texture.embedded.empty()) {
            target = m_folder / (stem + texture.extension);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(texture.embedded.data()),
                      static_cast<std::streamsize>(texture.embedded.size()));
            if (!out) {
                return {};
            }
        } else {
            target = m_folder / (stem + texture.file.extension().string());
            if (!std::filesystem::equivalent(texture.file, target, ec)) {
                std::filesystem::copy_file(texture.file, target, std::filesystem::copy_options::overwrite_existing,
                                           ec);
                if (ec) {
                    return {};
                }
            }
        }
    } else {
        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = texture.embedded.empty()
            ? stbi_load(texture.file.string().c_str(), &width, &height, &channels, 4)
            : stbi_load_from_memory(texture.embedded.data(), static_cast<int>(texture.embedded.size()), &width,
                                    &height, &channels, 4);
        if (!pixels) {
            return {};
        }
        const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
        target = m_folder / (stem + ".png");
        bool written = false;
        if (scalar) {
            std::vector<stbi_uc> gray(count);
            const int channel = std::clamp(texture.channel < 0 ? 0 : texture.channel, 0, 3);
            for (size_t i = 0; i < count; ++i) {
                float value = static_cast<float>(pixels[i * 4 + static_cast<size_t>(channel)]) / 255.0f;
                if (texture.glossExponent > 0.0f) {
                    // Same conversion ufbx and Blender use for the exponent itself: roughness = 1 - sqrt(exponent) / 10.
                    value = std::clamp(1.0f - std::sqrt(texture.glossExponent * value) / 10.0f, 0.0f, 1.0f);
                } else if (texture.invert) {
                    value = 1.0f - value;
                }
                gray[i] = static_cast<stbi_uc>(std::lround(value * 255.0f));
            }
            written = stbi_write_png(target.string().c_str(), width, height, 1, gray.data(), width) != 0;
        } else {
            if (texture.flipGreen) {
                for (size_t i = 0; i < count; ++i) {
                    pixels[i * 4 + 1] = static_cast<stbi_uc>(255 - pixels[i * 4 + 1]);
                }
            }
            written = stbi_write_png(target.string().c_str(), width, height, 4, pixels, width * 4) != 0;
        }
        stbi_image_free(pixels);
        if (!written) {
            return {};
        }
    }
    std::string portable = AssetPathPolicy::ToPortableKey(m_projectRoot, target.generic_string());
    m_written.emplace(key, portable);
    return portable;
}

std::string MaterialAssetWriter::Write(const SourceMaterial& material, const std::string& stem,
                                       std::string& outError) {
    const std::string albedo = WriteTexture(material.baseColorTexture, stem + "_BaseColor", false);
    const std::string normal = WriteTexture(material.normalTexture, stem + "_Normal", false);
    const std::string roughness =
        WriteTexture(material.roughnessTexture, StemFor(material.roughnessTexture, stem, "_Roughness"), true);
    const std::string metallic =
        WriteTexture(material.metallicTexture, StemFor(material.metallicTexture, stem, "_Metallic"), true);
    const std::string ao = WriteTexture(material.aoTexture, StemFor(material.aoTexture, stem, "_AO"), true);
    const float emissiveStrength =
        std::max({material.emissive.x, material.emissive.y, material.emissive.z}) > 0.0f ? 1.0f : 0.0f;
    json out = {
        {"albedo", {material.baseColor.x, material.baseColor.y, material.baseColor.z}},
        {"metallic", std::clamp(material.metallic, 0.0f, 1.0f)},
        {"roughness", std::clamp(material.roughness, 0.0f, 1.0f)},
        {"ao", 1.0},
        {"emissive", {material.emissive.x, material.emissive.y, material.emissive.z}},
        {"emissiveStrength", emissiveStrength},
        {"opacity", std::clamp(material.opacity, 0.0f, 1.0f)},
        {"useAlbedoTexture", !albedo.empty()},
        {"albedoTexturePath", albedo},
        {"useNormalTexture", !normal.empty()},
        {"normalTexturePath", normal},
        {"useRoughnessTexture", !roughness.empty()},
        {"roughnessTexturePath", roughness},
        {"useMetallicTexture", !metallic.empty()},
        {"metallicTexturePath", metallic},
        {"useAoTexture", !ao.empty()},
        {"aoTexturePath", ao},
    };
    for (const auto& [key, texture] : {std::pair{"roughnessChannel", &material.roughnessTexture},
                                       std::pair{"metallicChannel", &material.metallicTexture},
                                       std::pair{"aoChannel", &material.aoTexture}}) {
        if (ChannelOf(*texture) != 0) {
            out[key] = ChannelOf(*texture);
        }
    }
    if (material.blend == SourceBlend::Blend) {
        out["blendMode"] = MaterialBlendModeName(MaterialBlendMode::AlphaBlend);
    } else if (material.blend == SourceBlend::Mask) {
        out["blendMode"] = MaterialBlendModeName(MaterialBlendMode::Masked);
        out["opacityMask"] = std::clamp(material.alphaCutoff, 0.0f, 1.0f);
    }
    std::error_code ec;
    std::filesystem::create_directories(m_folder, ec);
    const std::filesystem::path file = m_folder / (stem + ".hematerial");
    std::ofstream stream(file, std::ios::trunc);
    stream << out.dump(2) << '\n';
    if (!stream) {
        outError = "Could not write " + file.string();
        return {};
    }
    return AssetPathPolicy::ToPortableKey(m_projectRoot, file.generic_string());
}

} // namespace Hephaestus
