#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "AssetDatabase.h"
#include "DerivedDataCache.h"
#include "ReimportManager.h"

/**
 * @brief Editor-facing owner of AssetDatabase, DerivedDataCache, and ReimportManager.
 *
 * Starts FileWatcher auto-reimport for registered sources when initialized.
 */
class EditorAssetPipeline {
public:
    EditorAssetPipeline() = default;
    ~EditorAssetPipeline();

    EditorAssetPipeline(const EditorAssetPipeline&) = delete;
    EditorAssetPipeline& operator=(const EditorAssetPipeline&) = delete;

    bool Initialize(const std::filesystem::path& projectRoot);
    void Shutdown();

    bool IsInitialized() const { return initialized_; }

    /** Register a newly copied source file in the asset DB if missing. */
    void RegisterCopiedAsset(const std::filesystem::path& path);

    ReimportResult RequestReimportByPath(const std::filesystem::path& sourcePath,
                                         ReimportPolicy policy = ReimportPolicy::Manual);
    ReimportResult ReimportAllChanged();

    std::vector<CompletedReimport> DrainCompleted();

    AssetDatabase& GetDatabase() { return db_; }
    const AssetDatabase& GetDatabase() const { return db_; }
    ReimportManager* GetReimportManager() { return reimport_.get(); }

private:
    void registerDefaultImporters();

    AssetDatabase db_;
    DerivedDataCache ddc_;
    std::unique_ptr<ReimportManager> reimport_;
    bool initialized_ = false;
};
