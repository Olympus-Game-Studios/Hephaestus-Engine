#include "FileWatcher.h"

#include <thread>

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() {
    Stop();
}

void FileWatcher::Start(AssetDatabase* db, ChangeCallback callback) {
    if (running_) {
        return;
    }
    db_ = db;
    callback_ = std::move(callback);
    running_ = true;
    thread_ = std::thread(&FileWatcher::Run, this);
}

void FileWatcher::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void FileWatcher::SetDebounce(std::chrono::milliseconds debounce) {
    debounce_ = debounce;
}

void FileWatcher::SetStabilityWait(std::chrono::milliseconds wait) {
    stabilityWait_ = wait;
}

void FileWatcher::Run() {
    using namespace std::chrono;
    while (running_) {
        if (!db_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        auto records = db_->GetAllRecords();
        auto now = steady_clock::now();

        auto projectRoot = db_->GetProjectRoot();

        for (const auto& record : records) {
            if (record.sourcePath.empty()) {
                continue;
            }

            std::filesystem::path sourcePath = record.sourcePath;
            if (!projectRoot.empty() && sourcePath.is_relative()) {
                sourcePath = (projectRoot / sourcePath).lexically_normal();
            }
            std::error_code existsError;
            if (!std::filesystem::exists(sourcePath, existsError)) {
                continue;
            }
            auto fingerprint = AssetDatabase::BuildFastFingerprint(sourcePath);

            std::lock_guard<std::mutex> lock(stateMutex_);
            auto [stateIt, firstSeen] = states_.try_emplace(record.assetId);
            auto& state = stateIt->second;
            if (firstSeen) {
                state.fingerprint = fingerprint;
                continue;
            }
            bool changed = (fingerprint.sizeBytes != state.fingerprint.sizeBytes) ||
                           (fingerprint.lastWriteTime != state.fingerprint.lastWriteTime);

            if (changed) {
                state.fingerprint = fingerprint;
                state.lastChange = now;
                state.pending = true;
                continue;
            }

            if (state.pending) {
                auto elapsed = duration_cast<milliseconds>(now - state.lastChange);
                if (elapsed >= debounce_) {
                    if (elapsed >= stabilityWait_) {
                        state.pending = false;
                        if (callback_) {
                            callback_(record.assetId, sourcePath);
                        }
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
