#pragma once
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "AssetTypes.h"

struct AssetRecord {
    std::string assetId;
    AssetType assetType = AssetType::Unknown;
    std::string sourcePath;
    AssetFingerprint fingerprint{};
    std::string importerId;
    std::string importerVersion;
    std::string importSettingsHash;
    std::string derivedDataKey;
    uint64_t lastImportTime = 0;
    bool lastImportSuccess = false;
    std::string lastImportMessage;
};

class AssetDatabase {
public:
    bool Load(const std::filesystem::path& projectRoot);
    bool Save();

    std::optional<AssetRecord> GetById(const std::string& assetId) const;
    std::optional<AssetRecord> GetBySourcePath(const std::filesystem::path& sourcePath) const;

    AssetRecord GetOrCreateForSource(const std::filesystem::path& sourcePath,
                                     AssetType type,
                                     const std::string& importerId,
                                     const std::string& importerVersion,
                                     const std::string& importSettingsHash);

    bool UpdateRecord(const AssetRecord& record);

    std::vector<AssetRecord> GetAllRecords() const;

    std::filesystem::path GetProjectRoot() const;

    static std::string NormalizeSourcePath(const std::filesystem::path& sourcePath,
                                           const std::filesystem::path& projectRoot);

    static AssetFingerprint BuildFastFingerprint(const std::filesystem::path& sourcePath);
    static std::string ComputeContentHash(const std::filesystem::path& sourcePath);
    static std::string BuildDerivedDataKey(const AssetRecord& record, const AssetFingerprint& fingerprint);

private:
    std::filesystem::path projectRoot_;
    std::filesystem::path dbPath_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, AssetRecord> recordsById_;
    std::unordered_map<std::string, std::string> sourcePathToId_;

    std::string GenerateAssetId();
};
