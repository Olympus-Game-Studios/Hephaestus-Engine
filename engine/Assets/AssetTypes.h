#pragma once
#include <cstdint>
#include <string>

enum class AssetType {
    Unknown = 0,
    Texture,
    StaticMesh,
    Audio
};

struct AssetFingerprint {
    uint64_t sizeBytes = 0;
    uint64_t lastWriteTime = 0;
    std::string contentHash;
};

inline const char* ToString(AssetType type) {
    switch (type) {
        case AssetType::Texture: return "Texture";
        case AssetType::StaticMesh: return "StaticMesh";
        case AssetType::Audio: return "Audio";
        default: return "Unknown";
    }
}

inline AssetType AssetTypeFromString(const std::string& value) {
    if (value == "Texture") return AssetType::Texture;
    if (value == "StaticMesh") return AssetType::StaticMesh;
    if (value == "Audio") return AssetType::Audio;
    return AssetType::Unknown;
}
