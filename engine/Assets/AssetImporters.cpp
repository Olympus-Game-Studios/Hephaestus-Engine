#include "AssetImporters.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include "ModelImporter.h"

#include "stb_image.h"

namespace {
std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool HasExtension(const std::filesystem::path& path, const std::vector<std::string>& exts) {
    std::string ext = ToLower(path.extension().string());
    for (const auto& candidate : exts) {
        if (ext == candidate) {
            return true;
        }
    }
    return false;
}

bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& outBytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    outBytes.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(outBytes.data()), size);
    return true;
}

bool ValidateWavHeader(const std::vector<uint8_t>& bytes, AudioImportData& outData) {
    if (bytes.size() < 44) {
        return false;
    }
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return false;
    }
    uint16_t channels = *reinterpret_cast<const uint16_t*>(bytes.data() + 22);
    uint32_t sampleRate = *reinterpret_cast<const uint32_t*>(bytes.data() + 24);
    uint16_t bitsPerSample = *reinterpret_cast<const uint16_t*>(bytes.data() + 34);
    outData.channels = channels;
    outData.sampleRate = sampleRate;
    outData.bitsPerSample = bitsPerSample;
    return true;
}

bool ValidateOggHeader(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 4) {
        return false;
    }
    return std::memcmp(bytes.data(), "OggS", 4) == 0;
}

void AppendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

std::vector<uint8_t> SerializeMeshData(const MeshData& mesh) {
    std::vector<uint8_t> bytes;
    uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());
    uint32_t submeshCount = static_cast<uint32_t>(mesh.submeshes.size());

    AppendBytes(bytes, &vertexCount, sizeof(vertexCount));
    AppendBytes(bytes, &indexCount, sizeof(indexCount));
    AppendBytes(bytes, &submeshCount, sizeof(submeshCount));
    if (vertexCount > 0) {
        AppendBytes(bytes, mesh.vertices.data(), sizeof(Vertex) * vertexCount);
    }
    if (indexCount > 0) {
        AppendBytes(bytes, mesh.indices.data(), sizeof(uint32_t) * indexCount);
    }

    for (const auto& sub : mesh.submeshes) {
        AppendBytes(bytes, &sub.indexOffset, sizeof(sub.indexOffset));
        AppendBytes(bytes, &sub.indexCount, sizeof(sub.indexCount));
        AppendBytes(bytes, &sub.materialSlot, sizeof(sub.materialSlot));

        uint32_t nameLen = static_cast<uint32_t>(sub.name.size());
        AppendBytes(bytes, &nameLen, sizeof(nameLen));
        if (nameLen > 0) {
            AppendBytes(bytes, sub.name.data(), nameLen);
        }

        AppendBytes(bytes, &sub.bounds, sizeof(sub.bounds));
    }

    AppendBytes(bytes, &mesh.bounds, sizeof(mesh.bounds));
    return bytes;
}
}

bool TextureImporter::CanImport(const std::filesystem::path& sourcePath) const {
    static const std::vector<std::string> exts = {".png", ".jpg", ".jpeg", ".tga", ".exr", ".hdr"};
    return HasExtension(sourcePath, exts);
}

ImportResult TextureImporter::Import(const ImportContext& context) {
    ImportResult result;
    result.assetType = AssetType::Texture;
    result.fingerprint = AssetDatabase::BuildFastFingerprint(context.sourcePath);

    int width = 0;
    int height = 0;
    int channels = 0;
    bool isHDR = false;

    std::string ext = ToLower(context.sourcePath.extension().string());
    if (ext == ".exr" || ext == ".hdr") {
        isHDR = true;
    }

    if (isHDR) {
        float* pixels = stbi_loadf(context.sourcePath.string().c_str(), &width, &height, &channels, 0);
        if (!pixels) {
            result.error = "Failed to load HDR texture";
            return result;
        }
        stbi_image_free(pixels);
    } else {
        unsigned char* pixels = stbi_load(context.sourcePath.string().c_str(), &width, &height, &channels, 0);
        if (!pixels) {
            result.error = "Failed to load texture";
            return result;
        }
        stbi_image_free(pixels);
    }

    auto meta = std::make_shared<TextureImportData>();
    meta->width = width;
    meta->height = height;
    meta->channels = channels;
    meta->isHDR = isHDR;
    result.textureData = std::move(meta);

    ReadFileBytes(context.sourcePath, result.derivedPayload);
    result.success = true;
    return result;
}

bool StaticMeshImporter::CanImport(const std::filesystem::path& sourcePath) const {
    return ModelImporter::IsSupportedExtension(sourcePath);
}

ImportResult StaticMeshImporter::Import(const ImportContext& context) {
    ImportResult result;
    result.assetType = AssetType::StaticMesh;
    result.fingerprint = AssetDatabase::BuildFastFingerprint(context.sourcePath);

    auto meshData = std::make_shared<MeshData>();
    std::string error;
    if (!ModelImporter::LoadModel(context.sourcePath, *meshData, error)) {
        result.error = error.empty() ? "Failed to import mesh" : error;
        return result;
    }

    if (meshData->vertices.empty() || meshData->indices.empty()) {
        result.error = "Mesh import produced empty geometry";
        return result;
    }

    result.meshData = meshData;
    result.derivedPayload = SerializeMeshData(*meshData);
    result.success = true;
    return result;
}

bool AudioImporter::CanImport(const std::filesystem::path& sourcePath) const {
    static const std::vector<std::string> exts = {".wav", ".ogg"};
    return HasExtension(sourcePath, exts);
}

ImportResult AudioImporter::Import(const ImportContext& context) {
    ImportResult result;
    result.assetType = AssetType::Audio;
    result.fingerprint = AssetDatabase::BuildFastFingerprint(context.sourcePath);

    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(context.sourcePath, bytes)) {
        result.error = "Failed to read audio file";
        return result;
    }

    auto meta = std::make_shared<AudioImportData>();
    std::string ext = ToLower(context.sourcePath.extension().string());
    if (ext == ".wav") {
        if (!ValidateWavHeader(bytes, *meta)) {
            result.error = "Invalid WAV header";
            return result;
        }
    } else if (ext == ".ogg") {
        if (!ValidateOggHeader(bytes)) {
            result.error = "Invalid OGG header";
            return result;
        }
    }

    result.audioData = std::move(meta);
    result.derivedPayload = std::move(bytes);
    result.success = true;
    return result;
}
