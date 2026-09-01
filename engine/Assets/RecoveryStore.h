/**
 * @file RecoveryStore.h
 * @brief Non-Qt crash-recovery store under <project>/.hephaestus/recovery/.
 *
 * Persists scene document snapshots and session heartbeats so the editor can
 * offer restore after an unclean exit. Never writes into real .hpscene assets.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct RecoveryCandidate {
    std::string id;              ///< Stable snapshot id (matches index + filename stem)
    std::string documentId;      ///< Editor document id
    std::string sourcePath;      ///< Original scene path (informational only)
    std::string fileName;        ///< Relative name under snapshots/
    std::string timestamp;       ///< ISO-8601 write time
    std::optional<std::string> sourceMtime; ///< Optional source file mtime (ISO or opaque)
};

class RecoveryStore {
public:
    static constexpr std::chrono::seconds kDefaultStaleHeartbeat{120};

    /** Absolute recovery root: <project>/.hephaestus/recovery */
    static std::filesystem::path RecoveryRoot(const std::filesystem::path& projectRoot);

    /**
     * Start a session for projectRoot. Detects unclean previous sessions,
     * preserves snapshots for ListRecoveryCandidates, then writes a new session.json.
     */
    bool BeginSession(const std::filesystem::path& projectRoot);

    /** Update lastHeartbeat in session.json (atomic). */
    bool Heartbeat();

    /** Mark cleanExit=true and flush session.json. */
    bool EndSessionClean();

    /**
     * Write a scene snapshot under snapshots/ (never into .hpscene assets).
     * @return Snapshot id on success, empty string on failure.
     */
    std::string WriteSceneSnapshot(const std::string& documentId,
                                   const std::string& sourcePath,
                                   const std::string& contentString,
                                   const std::optional<std::string>& sourceMtime = std::nullopt);

    /** Candidates from a previous unclean session (or recovery_pending flag). */
    std::vector<RecoveryCandidate> ListRecoveryCandidates() const;

    std::optional<std::string> LoadSnapshotContent(const std::string& id) const;

    bool DiscardSnapshot(const std::string& id);
    bool DiscardAll();

    /**
     * Keep at most maxSnapshotsPerDoc newest snapshots per documentId.
     * Optionally drop entries older than maxAge.
     */
    bool Prune(std::size_t maxSnapshotsPerDoc,
               std::optional<std::chrono::seconds> maxAge = std::nullopt);

    bool HasActiveSession() const { return !projectRoot_.empty(); }
    bool HasPendingRecovery() const { return pendingRecovery_; }
    const std::filesystem::path& GetProjectRoot() const { return projectRoot_; }

    /** Best-effort: true if PID appears to be a live process (Windows). */
    static bool IsProcessRunning(std::uint32_t pid);

    /** True when cleanExit=false and (PID dead or heartbeat older than staleAfter). */
    static bool IsUncleanSession(const std::filesystem::path& sessionPath,
                                 std::chrono::seconds staleAfter = kDefaultStaleHeartbeat);

private:
    struct SessionState {
        std::uint32_t pid = 0;
        std::string startedAt;
        bool cleanExit = false;
        std::string lastHeartbeat;
        std::string projectRoot;
    };

    struct IndexEntry {
        std::string id;
        std::string documentId;
        std::string sourcePath;
        std::string fileName;
        std::string timestamp;
        std::optional<std::string> sourceMtime;
    };

    std::filesystem::path projectRoot_;
    std::filesystem::path recoveryRoot_;
    std::filesystem::path snapshotsDir_;
    std::filesystem::path sessionPath_;
    std::filesystem::path indexPath_;
    std::filesystem::path pendingPath_;
    bool pendingRecovery_ = false;
    SessionState session_;

    bool ensureDirs() const;
    bool loadSession(const std::filesystem::path& path, SessionState& out) const;
    bool saveSession(const SessionState& state) const;
    bool loadIndex(std::vector<IndexEntry>& out) const;
    bool saveIndex(const std::vector<IndexEntry>& entries) const;
    bool writeAtomic(const std::filesystem::path& finalPath, const std::string& contents) const;
    bool setPendingRecovery(bool pending);
    std::filesystem::path snapshotPathFor(const IndexEntry& entry) const;
    static std::string nowIsoUtc();
    static std::string sanitizeIdToken(const std::string& value);
    static bool parseIsoUtc(const std::string& iso, std::chrono::system_clock::time_point& out);
    static bool isHeartbeatStale(const std::string& iso, std::chrono::seconds staleAfter);
};
