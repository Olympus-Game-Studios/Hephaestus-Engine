/**
 * @file GltfDocument.cpp
 * @brief glTF 2.0 / GLB document loading and accessor reading shared by the static and skeletal importers.
 */

#include "GltfDocument.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

using json = nlohmann::json;

namespace Hephaestus::Gltf {
namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int Base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

bool DecodeDataUri(const std::string& uri, std::vector<uint8_t>& out) {
    const size_t comma = uri.find(',');
    if (comma == std::string::npos || uri.find(";base64", 0) > comma) {
        return false;
    }
    out.clear();
    out.reserve((uri.size() - comma) * 3 / 4);
    uint32_t accumulator = 0;
    int bits = 0;
    for (size_t i = comma + 1; i < uri.size(); ++i) {
        const char c = uri[i];
        if (c == '=') {
            break;
        }
        const int value = Base64Value(c);
        if (value < 0) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                continue;
            }
            return false;
        }
        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFFu));
        }
    }
    return true;
}

std::string DecodePercent(const std::string& uri) {
    std::string out;
    out.reserve(uri.size());
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size() &&
            std::isxdigit(static_cast<unsigned char>(uri[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(uri[i + 2]))) {
            out.push_back(static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(uri[i]);
        }
    }
    return out;
}

std::string ExtensionForMime(const std::string& mime) {
    if (mime == "image/png") return ".png";
    if (mime == "image/jpeg") return ".jpg";
    if (mime == "image/webp") return ".webp";
    if (mime == "image/ktx2") return ".ktx2";
    return {};
}

} // namespace


int ComponentSize(int componentType) {
    switch (componentType) {
    case kComponentByte:
    case kComponentUByte:
        return 1;
    case kComponentShort:
    case kComponentUShort:
        return 2;
    case kComponentUInt:
    case kComponentFloat:
        return 4;
    default:
        return 0;
    }
}

int TypeComponentCount(const std::string& type) {
    if (type == "SCALAR") {
        return 1;
    }
    if (type == "VEC2") {
        return 2;
    }
    if (type == "VEC3") {
        return 3;
    }
    if (type == "VEC4") {
        return 4;
    }
    if (type == "MAT2") {
        return 4;
    }
    if (type == "MAT3") {
        return 9;
    }
    if (type == "MAT4") {
        return 16;
    }
    return 0;
}


float ReadGLTFNumber(const json& value) {
    if (value.is_number_float()) {
        return value.get<float>();
    }
    if (value.is_number_integer()) {
        return static_cast<float>(value.get<std::int64_t>());
    }
    return 0.0f;
}

Mat4 Mat4FromGLTFQuaternion(float x, float y, float z, float w) {
    float len = std::sqrt(x * x + y * y + z * z + w * w);
    if (len > 0.0f) {
        x /= len;
        y /= len;
        z /= len;
        w /= len;
    }

    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    Mat4 r = Mat4::Identity();
    r.m[0] = 1.0f - 2.0f * (yy + zz);
    r.m[1] = 2.0f * (xy + wz);
    r.m[2] = 2.0f * (xz - wy);
    r.m[4] = 2.0f * (xy - wz);
    r.m[5] = 1.0f - 2.0f * (xx + zz);
    r.m[6] = 2.0f * (yz + wx);
    r.m[8] = 2.0f * (xz + wy);
    r.m[9] = 2.0f * (yz - wx);
    r.m[10] = 1.0f - 2.0f * (xx + yy);
    return r;
}

Mat4 GLTFNodeLocalMatrix(const json& node) {
    if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() >= 16) {
        Mat4 matrix = Mat4::Identity();
        for (int i = 0; i < 16; ++i) {
            matrix.m[i] = ReadGLTFNumber(node["matrix"][i]);
        }
        return matrix;
    }

    Vec3 translation(0.0f, 0.0f, 0.0f);
    Vec3 scale(1.0f, 1.0f, 1.0f);
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;

    if (node.contains("translation") && node["translation"].is_array() &&
        node["translation"].size() >= 3) {
        translation.x = ReadGLTFNumber(node["translation"][0]);
        translation.y = ReadGLTFNumber(node["translation"][1]);
        translation.z = ReadGLTFNumber(node["translation"][2]);
    }
    if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() >= 3) {
        scale.x = ReadGLTFNumber(node["scale"][0]);
        scale.y = ReadGLTFNumber(node["scale"][1]);
        scale.z = ReadGLTFNumber(node["scale"][2]);
    }
    if (node.contains("rotation") && node["rotation"].is_array() &&
        node["rotation"].size() >= 4) {
        qx = ReadGLTFNumber(node["rotation"][0]);
        qy = ReadGLTFNumber(node["rotation"][1]);
        qz = ReadGLTFNumber(node["rotation"][2]);
        qw = ReadGLTFNumber(node["rotation"][3]);
    }

    const Mat4 t = Mat4::Translation(translation);
    const Mat4 r = Mat4FromGLTFQuaternion(qx, qy, qz, qw);
    const Mat4 s = Mat4::Scale(scale);
    return Mat4::Multiply(t, Mat4::Multiply(r, s));
}


bool LoadGlbOrGltf(const std::filesystem::path& path,
                   json& root,
                   std::vector<std::vector<uint8_t>>& buffers,
                   std::string& errorMessage) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        errorMessage = "Failed to open glTF file: " + path.string();
        return false;
    }

    const std::string ext = ToLower(path.extension().string());
    if (ext == ".glb") {
        uint32_t magic = 0;
        file.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
        if (magic != 0x46546C67) {
            errorMessage = "Not a GLB file";
            return false;
        }
        uint32_t version = 0;
        uint32_t fileLength = 0;
        file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&fileLength), sizeof(uint32_t));
        (void)fileLength;
        if (version != 2) {
            errorMessage = "Only glTF 2.0 is supported";
            return false;
        }

        uint32_t chunkLength = 0;
        uint32_t chunkType = 0;
        file.read(reinterpret_cast<char*>(&chunkLength), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&chunkType), sizeof(uint32_t));
        if (chunkType != 0x4E4F534A) {
            errorMessage = "Expected JSON chunk in GLB file";
            return false;
        }
        std::string jsonContent(chunkLength, '\0');
        file.read(jsonContent.data(), static_cast<std::streamsize>(chunkLength));
        root = json::parse(jsonContent);

        std::vector<uint8_t> binChunk;
        if (file.peek() != EOF) {
            uint32_t binLength = 0;
            uint32_t binType = 0;
            file.read(reinterpret_cast<char*>(&binLength), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&binType), sizeof(uint32_t));
            if (binType != 0x004E4942) {
                errorMessage = "Expected BIN chunk in GLB file";
                return false;
            }
            binChunk.resize(binLength);
            file.read(reinterpret_cast<char*>(binChunk.data()),
                      static_cast<std::streamsize>(binLength));
        }

        const json bufferList = root.value("buffers", json::array());
        buffers.resize(bufferList.size());
        if (buffers.empty() && !binChunk.empty()) {
            buffers.push_back(std::move(binChunk));
        } else if (!buffers.empty()) {
            buffers[0] = std::move(binChunk);
        }
        return true;
    }

    if (ext == ".gltf") {
        std::string jsonContent((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
        root = json::parse(jsonContent);
        const json bufferList = root.value("buffers", json::array());
        buffers.resize(bufferList.size());
        for (size_t i = 0; i < bufferList.size(); ++i) {
            const json& buffer = bufferList[i];
            if (!buffer.contains("uri") || !buffer["uri"].is_string()) {
                errorMessage = "glTF buffer is missing an external uri";
                return false;
            }
            const std::string uri = buffer["uri"].get<std::string>();
            if (uri.rfind("data:", 0) == 0) {
                if (!DecodeDataUri(uri, buffers[i])) {
                    errorMessage = "glTF buffer has an invalid base64 data URI";
                    return false;
                }
                continue;
            }
            const std::filesystem::path binPath = path.parent_path() / DecodePercent(uri);
            std::ifstream binFile(binPath, std::ios::binary);
            if (!binFile.is_open()) {
                errorMessage = "Failed to open external binary file: " + uri;
                return false;
            }
            binFile.seekg(0, std::ios::end);
            const auto binSize = static_cast<size_t>(binFile.tellg());
            binFile.seekg(0, std::ios::beg);
            buffers[i].resize(binSize);
            binFile.read(reinterpret_cast<char*>(buffers[i].data()),
                         static_cast<std::streamsize>(binSize));
        }
        return true;
    }

    errorMessage = "Expected a .glb or .gltf file";
    return false;
}

bool OpenAccessor(const json& root,
                  const std::vector<std::vector<uint8_t>>& buffers,
                  int accessorIndex,
                  AccessorView& outView,
                  std::string& errorMessage) {
    const json accessors = root.value("accessors", json::array());
    if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) {
        errorMessage = "Accessor index out of range";
        return false;
    }
    const json& accessor = accessors[accessorIndex];
    if (accessor.contains("sparse")) {
        errorMessage = "Sparse glTF accessors are not supported";
        return false;
    }
    if (!accessor.contains("bufferView")) {
        errorMessage = "Accessor is missing bufferView";
        return false;
    }
    outView.count = accessor.value("count", 0);
    outView.componentType = accessor.value("componentType", 0);
    outView.normalized = accessor.value("normalized", false);
    const std::string type = accessor.value("type", "");
    outView.componentCount = TypeComponentCount(type);
    const int elemSize = ComponentSize(outView.componentType);
    if (outView.count <= 0 || outView.componentCount <= 0 || elemSize <= 0) {
        errorMessage = "Unsupported accessor type";
        return false;
    }

    const json bufferViews = root.value("bufferViews", json::array());
    const int bufferViewIndex = accessor["bufferView"].get<int>();
    if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) {
        errorMessage = "bufferView index out of range";
        return false;
    }
    const json& bufferView = bufferViews[bufferViewIndex];
    const int bufferIndex = bufferView.value("buffer", 0);
    if (bufferIndex < 0 || bufferIndex >= static_cast<int>(buffers.size())) {
        errorMessage = "buffer index out of range";
        return false;
    }
    const int viewOffset = bufferView.value("byteOffset", 0);
    const int accessorOffset = accessor.value("byteOffset", 0);
    const int viewLength = bufferView.value("byteLength", 0);
    const int packedStride = outView.componentCount * elemSize;
    outView.stride = bufferView.value("byteStride", packedStride);
    if (outView.stride < packedStride) {
        errorMessage = "Invalid accessor byteStride";
        return false;
    }

    const size_t start = static_cast<size_t>(viewOffset) + static_cast<size_t>(accessorOffset);
    const size_t needed = start + static_cast<size_t>(outView.stride) * static_cast<size_t>(outView.count - 1) +
                          static_cast<size_t>(packedStride);
    if (start > buffers[static_cast<size_t>(bufferIndex)].size() ||
        needed > buffers[static_cast<size_t>(bufferIndex)].size() ||
        (viewLength > 0 && accessorOffset + packedStride > viewLength)) {
        errorMessage = "Accessor data exceeds buffer size";
        return false;
    }
    outView.data = buffers[static_cast<size_t>(bufferIndex)].data() + start;
    return true;
}

float ReadNormalized(const AccessorView& view, int element, int component) {
    const uint8_t* ptr = view.data + static_cast<size_t>(element) * static_cast<size_t>(view.stride) +
                         static_cast<size_t>(component) * static_cast<size_t>(ComponentSize(view.componentType));
    switch (view.componentType) {
    case kComponentFloat: {
        float value = 0.0f;
        std::memcpy(&value, ptr, sizeof(float));
        return value;
    }
    case kComponentUByte: {
        const float raw = static_cast<float>(*ptr);
        return view.normalized ? (raw / 255.0f) : raw;
    }
    case kComponentByte: {
        const float raw = static_cast<float>(static_cast<int8_t>(*ptr));
        return view.normalized ? std::max(raw / 127.0f, -1.0f) : raw;
    }
    case kComponentUShort: {
        uint16_t raw = 0;
        std::memcpy(&raw, ptr, sizeof(uint16_t));
        return view.normalized ? (static_cast<float>(raw) / 65535.0f) : static_cast<float>(raw);
    }
    case kComponentShort: {
        int16_t raw = 0;
        std::memcpy(&raw, ptr, sizeof(int16_t));
        const float value = static_cast<float>(raw);
        return view.normalized ? std::max(value / 32767.0f, -1.0f) : value;
    }
    case kComponentUInt: {
        uint32_t raw = 0;
        std::memcpy(&raw, ptr, sizeof(uint32_t));
        return static_cast<float>(raw);
    }
    default:
        return 0.0f;
    }
}


bool ReadOptionalVecAttribute(const json& root,
                              const std::vector<std::vector<uint8_t>>& buffers,
                              const json& attributes,
                              const char* name,
                              int expectedCount,
                              int expectedComponents,
                              std::vector<float>& outValues,
                              std::string& errorMessage) {
    if (!attributes.contains(name)) {
        return true;
    }
    AccessorView view;
    if (!OpenAccessor(root, buffers, attributes[name].get<int>(), view, errorMessage)) {
        return false;
    }
    if (view.componentCount != expectedComponents || view.count < expectedCount) {
        errorMessage = std::string(name) + " accessor does not match POSITION count";
        return false;
    }
    outValues.resize(static_cast<size_t>(expectedCount * expectedComponents));
    for (int i = 0; i < expectedCount; ++i) {
        for (int c = 0; c < expectedComponents; ++c) {
            outValues[static_cast<size_t>(i * expectedComponents + c)] = ReadNormalized(view, i, c);
        }
    }
    return true;
}

ImageSource ReadTextureImage(const json& root,
                             const std::vector<std::vector<uint8_t>>& buffers,
                             const std::filesystem::path& gltfPath,
                             int textureIndex) {
    ImageSource out;
    const json textures = root.value("textures", json::array());
    if (textureIndex < 0 || textureIndex >= static_cast<int>(textures.size())) {
        return out;
    }
    const json& texture = textures[static_cast<size_t>(textureIndex)];
    int imageIndex = texture.value("source", -1);
    if (texture.contains("extensions") && texture["extensions"].is_object()) {
        for (const auto& [name, extension] : texture["extensions"].items()) {
            if (imageIndex < 0 && extension.is_object()) {
                imageIndex = extension.value("source", -1);
            }
        }
    }
    const json images = root.value("images", json::array());
    if (imageIndex < 0 || imageIndex >= static_cast<int>(images.size())) {
        return out;
    }
    const json& image = images[static_cast<size_t>(imageIndex)];
    const std::string mime = image.value("mimeType", "");
    if (image.contains("uri") && image["uri"].is_string()) {
        const std::string uri = image["uri"].get<std::string>();
        if (uri.rfind("data:", 0) == 0) {
            const size_t semicolon = uri.find(';');
            if (DecodeDataUri(uri, out.embedded)) {
                out.extension = ExtensionForMime(semicolon == std::string::npos ? mime : uri.substr(5, semicolon - 5));
            }
            if (out.extension.empty()) {
                out.embedded.clear();
            }
            return out;
        }
        const std::filesystem::path file = gltfPath.parent_path() / DecodePercent(uri);
        std::error_code ec;
        if (std::filesystem::is_regular_file(file, ec)) {
            out.file = file;
        }
        return out;
    }
    const int viewIndex = image.value("bufferView", -1);
    const json views = root.value("bufferViews", json::array());
    if (viewIndex < 0 || viewIndex >= static_cast<int>(views.size())) {
        return out;
    }
    const json& view = views[static_cast<size_t>(viewIndex)];
    const int bufferIndex = view.value("buffer", 0);
    const size_t offset = view.value("byteOffset", static_cast<size_t>(0));
    const size_t length = view.value("byteLength", static_cast<size_t>(0));
    if (bufferIndex < 0 || bufferIndex >= static_cast<int>(buffers.size()) ||
        offset + length > buffers[static_cast<size_t>(bufferIndex)].size()) {
        return out;
    }
    out.extension = ExtensionForMime(mime);
    if (!out.extension.empty()) {
        const uint8_t* begin = buffers[static_cast<size_t>(bufferIndex)].data() + offset;
        out.embedded.assign(begin, begin + length);
    }
    return out;
}

} // namespace Hephaestus::Gltf
