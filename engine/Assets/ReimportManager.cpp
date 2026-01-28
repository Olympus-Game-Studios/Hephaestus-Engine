#include "ReimportManager.h"

#include <chrono>
#include <ctime>
#include <iostream>

ReimportManager::ReimportManager(AssetDatabase& db, DerivedDataCache& ddc)
    : db_(db), ddc_(ddc) {
    watcher_.SetDebounce(std::chrono::milliseconds(500));
    watcher_.SetStabilityWait(std::chrono::milliseconds(200));

    watcher_.Start(&db_, [this](const std::string& assetId, const std::filesystem::path&) {
        RequestReimport(assetId, ReimportPolicy::Auto);
    });

    worker_ = std::thread(&ReimportManager::WorkerLoop, this);
}

ReimportManager::~ReimportManager() {
    running_ = false;
    queueCv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    watcher_.Stop();
}

void ReimportManager::SetProjectRoot(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) {
        return;
    }
    db_.Load(projectRoot);
    ddc_.SetRoot(projectRoot);
}

void ReimportManager::RegisterImporter(std::unique_ptr<IAssetImporter> importer) {
    if (!importer) {
        return;
    }
    importers_.push_back(std::move(importer));
}

ReimportResult ReimportManager::RequestReimport(const std::string& assetId, ReimportPolicy policy) {
    if (assetId.empty()) {
        return {false, "Invalid asset ID", {}, {}};
    }

    Job job{assetId, policy};
    EnqueueJob(job);
    return {true, "Queued reimport", {}, {assetId}};
}

ReimportResult ReimportManager::RequestReimportByPath(const std::filesystem::path& sourcePath, ReimportPolicy policy) {
    if (sourcePath.empty()) {
        return {false, "Missing source path", {}, {}};
    }

    auto existing = db_.GetBySourcePath(sourcePath);
    if (!existing) {
        IAssetImporter* importer = FindImporterForPath(sourcePath);
        if (!importer) {
            return {false, "No importer for source file", {}, {}};
        }

        AssetType type = AssetType::Unknown;
        if (dynamic_cast<TextureImporter*>(importer)) type = AssetType::Texture;
        else if (dynamic_cast<StaticMeshImporter*>(importer)) type = AssetType::StaticMesh;
        else if (dynamic_cast<AudioImporter*>(importer)) type = AssetType::Audio;

        AssetRecord record = db_.GetOrCreateForSource(sourcePath, type,
                                                      importer->GetImporterId(),
                                                      importer->GetVersion(),
                                                      "default");
        existing = record;
    }

    return RequestReimport(existing->assetId, policy);
}

ReimportResult ReimportManager::ReimportAllChanged() {
    auto records = db_.GetAllRecords();
    std::vector<std::string> changed;
    auto projectRoot = db_.GetProjectRoot();

    for (const auto& record : records) {
        if (record.sourcePath.empty()) {
            continue;
        }
        std::filesystem::path path = record.sourcePath;
        if (!projectRoot.empty() && path.is_relative()) {
            path = (projectRoot / path).lexically_normal();
        }
        auto fingerprint = AssetDatabase::BuildFastFingerprint(path);
        if (fingerprint.sizeBytes != record.fingerprint.sizeBytes ||
            fingerprint.lastWriteTime != record.fingerprint.lastWriteTime) {
            EnqueueJob({record.assetId, ReimportPolicy::Manual});
            changed.push_back(record.assetId);
        }
    }

    if (changed.empty()) {
        return {true, "No changed assets", {}, {}};
    }

    return {true, "Queued reimport for changed assets", {}, changed};
}

std::vector<CompletedReimport> ReimportManager::DrainCompleted() {
    std::lock_guard<std::mutex> lock(completedMutex_);
    std::vector<CompletedReimport> out;
    out.swap(completed_);
    return out;
}

void ReimportManager::RegisterSourceIfMissing(const std::filesystem::path& sourcePath, AssetType type) {
    if (sourcePath.empty()) {
        return;
    }
    auto existing = db_.GetBySourcePath(sourcePath);
    if (existing) {
        return;
    }

    IAssetImporter* importer = FindImporterForPath(sourcePath);
    if (!importer) {
        return;
    }

    db_.GetOrCreateForSource(sourcePath, type, importer->GetImporterId(), importer->GetVersion(), "default");
}

void ReimportManager::AddOnAssetReimported(ReimportedCallback callback) {
    if (!callback) {
        return;
    }
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbacks_.push_back(std::move(callback));
}

void ReimportManager::NotifyAssetReimported(const CompletedReimport& completed) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    for (const auto& cb : callbacks_) {
        if (cb) {
            cb(completed);
        }
    }
}

void ReimportManager::WorkerLoop() {
    while (running_) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [&]() { return !running_ || !jobs_.empty(); });
            if (!running_) {
                return;
            }
            job = jobs_.front();
            jobs_.pop();
            pending_.erase(job.assetId);
        }

        auto recordOpt = db_.GetById(job.assetId);
        if (!recordOpt) {
            continue;
        }
        AssetRecord record = *recordOpt;
        if (record.sourcePath.empty()) {
            continue;
        }

        std::filesystem::path sourcePath = record.sourcePath;
        auto projectRoot = db_.GetProjectRoot();
        if (!projectRoot.empty() && sourcePath.is_relative()) {
            sourcePath = (projectRoot / sourcePath).lexically_normal();
        }
        IAssetImporter* importer = FindImporterForPath(sourcePath);
        if (!importer) {
            CompletedReimport completed;
            completed.assetId = record.assetId;
            completed.assetType = record.assetType;
            completed.sourcePath = sourcePath;
            completed.result.success = false;
            completed.result.message = "No importer available";
            completed.result.changedAssetIds = {record.assetId};
            std::lock_guard<std::mutex> lock(completedMutex_);
            completed_.push_back(std::move(completed));
            continue;
        }

        ImportContext context{sourcePath, record};
        ImportResult importResult = importer->Import(context);

        CompletedReimport completed;
        completed.assetId = record.assetId;
        completed.assetType = importResult.assetType;
        completed.sourcePath = sourcePath;
        completed.result.changedAssetIds = {record.assetId};
        completed.result.warnings = importResult.warnings;

        if (!importResult.success) {
            completed.result.success = false;
            completed.result.message = importResult.error.empty() ? "Import failed" : importResult.error;

            record.lastImportSuccess = false;
            record.lastImportMessage = completed.result.message;
            record.lastImportTime = static_cast<uint64_t>(std::time(nullptr));
            db_.UpdateRecord(record);

            std::lock_guard<std::mutex> lock(completedMutex_);
            completed_.push_back(std::move(completed));
            continue;
        }

        record.assetType = importResult.assetType;
        record.importerId = importer->GetImporterId();
        record.importerVersion = importer->GetVersion();
        record.importSettingsHash = record.importSettingsHash.empty() ? "default" : record.importSettingsHash;

        if (importResult.fingerprint.contentHash.empty()) {
            importResult.fingerprint.contentHash = AssetDatabase::ComputeContentHash(sourcePath);
        }
        record.fingerprint = importResult.fingerprint;
        record.derivedDataKey = AssetDatabase::BuildDerivedDataKey(record, record.fingerprint);

        if (!importResult.derivedPayload.empty()) {
            ddc_.Store(record.derivedDataKey, importResult.derivedPayload);
        }

        record.lastImportSuccess = true;
        record.lastImportMessage = "OK";
        record.lastImportTime = static_cast<uint64_t>(std::time(nullptr));
        db_.UpdateRecord(record);

        completed.meshData = importResult.meshData;
        completed.result.success = true;
        completed.result.message = "Reimported";

        std::lock_guard<std::mutex> lock(completedMutex_);
        completed_.push_back(std::move(completed));
    }
}

void ReimportManager::EnqueueJob(const Job& job) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (pending_.count(job.assetId) > 0) {
        return;
    }
    pending_.insert(job.assetId);
    jobs_.push(job);
    queueCv_.notify_one();
}

IAssetImporter* ReimportManager::FindImporterForPath(const std::filesystem::path& sourcePath) {
    for (const auto& importer : importers_) {
        if (importer && importer->CanImport(sourcePath)) {
            return importer.get();
        }
    }
    return nullptr;
}
