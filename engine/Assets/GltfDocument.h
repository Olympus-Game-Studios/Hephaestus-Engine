/**
 * @file GltfDocument.h
 * @brief glTF 2.0 / GLB document loading and accessor reading shared by the static and skeletal importers.
 */

#pragma once

#include "../Core/Math.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus::Gltf {

constexpr int kComponentByte = 5120;
constexpr int kComponentUByte = 5121;
constexpr int kComponentShort = 5122;
constexpr int kComponentUShort = 5123;
constexpr int kComponentUInt = 5125;
constexpr int kComponentFloat = 5126;

int ComponentSize(int componentType);
int TypeComponentCount(const std::string& type);
float ReadGLTFNumber(const nlohmann::json& value);
Mat4 Mat4FromGLTFQuaternion(float x, float y, float z, float w);
Mat4 GLTFNodeLocalMatrix(const nlohmann::json& node);

/// Loads a .glb or .gltf with every buffer (GLB chunk, external file or base64 data URI).
bool LoadGlbOrGltf(const std::filesystem::path& path,
                   nlohmann::json& root,
                   std::vector<std::vector<uint8_t>>& buffers,
                   std::string& errorMessage);

struct AccessorView {
    const uint8_t* data = nullptr;
    int count = 0;
    int componentType = 0;
    int componentCount = 0;
    int stride = 0;
    bool normalized = false;
};

bool OpenAccessor(const nlohmann::json& root,
                  const std::vector<std::vector<uint8_t>>& buffers,
                  int accessorIndex,
                  AccessorView& outView,
                  std::string& errorMessage);

float ReadNormalized(const AccessorView& view, int element, int component);

/// Reads NAME into expectedCount * expectedComponents floats; succeeds without output when NAME is absent.
bool ReadOptionalVecAttribute(const nlohmann::json& root,
                              const std::vector<std::vector<uint8_t>>& buffers,
                              const nlohmann::json& attributes,
                              const char* name,
                              int expectedCount,
                              int expectedComponents,
                              std::vector<float>& outValues,
                              std::string& errorMessage);

/// An image as either bytes (GLB bufferView or data URI) or a file beside the glTF.
struct ImageSource {
    std::vector<uint8_t> embedded;
    std::string extension; ///< ".png", ".jpg", ... for embedded bytes
    std::filesystem::path file;
    bool Exists() const { return !embedded.empty() || !file.empty(); }
};

/// Resolves textures[textureIndex] to its image. Returns an empty source when it cannot.
ImageSource ReadTextureImage(const nlohmann::json& root,
                             const std::vector<std::vector<uint8_t>>& buffers,
                             const std::filesystem::path& gltfPath,
                             int textureIndex);

} // namespace Hephaestus::Gltf
