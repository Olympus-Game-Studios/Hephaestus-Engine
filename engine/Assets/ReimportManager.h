#pragma once
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AssetDatabase.h"
#include "AssetImporters.h"
#include "DerivedDataCache.h"
#include "FileWatcher.h"

enum class ReimportPolicy {
    Manual,
    Auto
};

struct ReimportResult {
    bool success = false;
    std::string message;
    std::vector<std::string> warnings;
    std::vector<std::string> changedAssetIds;
};

struct CompletedReimport {
    ReimportResult result;
    AssetType assetType = AssetType::Unknown;
    std::string assetId;
    std::filesystem::path sourcePath;
    std::shared_ptr<MeshData> meshData;
};

class ReimportManager {
public:
    ReimportManager(AssetDatabase& db, DerivedDataCache& ddc);
    ~ReimportManager();

    void SetProjectRoot(const std::filesystem::path& projectRoot);

    void RegisterImporter(std::unique_ptr<IAssetImporter> importer);

    ReimportResult RequestReimport(const std::string& assetId, ReimportPolicy policy);
    ReimportResult RequestReimportByPath(const std::filesystem::path& sourcePath, ReimportPolicy policy = ReimportPolicy::Manual);
    ReimportResult ReimportAllChanged();
    // Reimports registered source files whose size or write time changes after this call.
    void StartAutoReimport();

    std::vector<CompletedReimport> DrainCompleted();

    void RegisterSourceIfMissing(const std::filesystem::path& sourcePath, AssetType type);

    using ReimportedCallback = std::function<void(const CompletedReimport&)>;
    void AddOnAssetReimported(ReimportedCallback callback);
    void NotifyAssetReimported(const CompletedReimport& completed);

private:
    struct Job {
        std::string assetId;
        ReimportPolicy policy = ReimportPolicy::Manual;
    };

    void WorkerLoop();
    void EnqueueJob(const Job& job);
    IAssetImporter* FindImporterForPath(const std::filesystem::path& sourcePath);

    AssetDatabase& db_;
    DerivedDataCache& ddc_;
    FileWatcher watcher_;

    std::vector<std::unique_ptr<IAssetImporter>> importers_;

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::queue<Job> jobs_;
    std::unordered_set<std::string> pending_;

    std::mutex completedMutex_;
    std::vector<CompletedReimport> completed_;

    std::mutex callbackMutex_;
    std::vector<ReimportedCallback> callbacks_;

    std::thread worker_;
    std::atomic<bool> running_{true};
};
