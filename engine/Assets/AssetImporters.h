#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "AssetDatabase.h"
#include "MeshAsset.h"

struct TextureImportData {
    int width = 0;
    int height = 0;
    int channels = 0;
    bool isHDR = false;
};

struct AudioImportData {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
};

struct ImportResult {
    bool success = false;
    std::string error;
    std::vector<std::string> warnings;

    AssetType assetType = AssetType::Unknown;
    AssetFingerprint fingerprint{};
    std::shared_ptr<MeshData> meshData;
    std::shared_ptr<TextureImportData> textureData;
    std::shared_ptr<AudioImportData> audioData;
    std::vector<uint8_t> derivedPayload;
};

struct ImportContext {
    std::filesystem::path sourcePath;
    AssetRecord record;
};

class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;
    virtual const char* GetImporterId() const = 0;
    virtual const char* GetVersion() const = 0;
    virtual bool CanImport(const std::filesystem::path& sourcePath) const = 0;
    virtual ImportResult Import(const ImportContext& context) = 0;
};

class TextureImporter final : public IAssetImporter {
public:
    const char* GetImporterId() const override { return "TextureImporter"; }
    const char* GetVersion() const override { return "1"; }
    bool CanImport(const std::filesystem::path& sourcePath) const override;
    ImportResult Import(const ImportContext& context) override;
};

class StaticMeshImporter final : public IAssetImporter {
public:
    const char* GetImporterId() const override { return "StaticMeshImporter"; }
    const char* GetVersion() const override { return "1"; }
    bool CanImport(const std::filesystem::path& sourcePath) const override;
    ImportResult Import(const ImportContext& context) override;
};

class AudioImporter final : public IAssetImporter {
public:
    const char* GetImporterId() const override { return "AudioImporter"; }
    const char* GetVersion() const override { return "1"; }
    bool CanImport(const std::filesystem::path& sourcePath) const override;
    ImportResult Import(const ImportContext& context) override;
};
