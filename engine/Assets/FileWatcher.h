#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "AssetDatabase.h"

class FileWatcher {
public:
    using ChangeCallback = std::function<void(const std::string& assetId, const std::filesystem::path& sourcePath)>;

    FileWatcher();
    ~FileWatcher();

    void Start(AssetDatabase* db, ChangeCallback callback);
    void Stop();

    void SetDebounce(std::chrono::milliseconds debounce);
    void SetStabilityWait(std::chrono::milliseconds wait);

private:
    void Run();

    std::atomic<bool> running_{false};
    std::thread thread_;
    AssetDatabase* db_ = nullptr;
    ChangeCallback callback_;

    std::chrono::milliseconds debounce_{500};
    std::chrono::milliseconds stabilityWait_{200};

    struct WatchState {
        AssetFingerprint fingerprint{};
        std::chrono::steady_clock::time_point lastChange{};
        bool pending = false;
    };

    std::mutex stateMutex_;
    std::unordered_map<std::string, WatchState> states_;
};
