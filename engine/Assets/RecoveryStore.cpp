#include "RecoveryStore.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_map>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

std::filesystem::path RecoveryStore::RecoveryRoot(const std::filesystem::path& projectRoot) {
    return projectRoot / ".hephaestus" / "recovery";
}

bool RecoveryStore::IsProcessRunning(std::uint32_t pid) {
    if (pid == 0) {
        return false;
    }
#if defined(_WIN32)
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!handle) {
        return false;
    }
    DWORD exitCode = 0;
    const BOOL ok = GetExitCodeProcess(handle, &exitCode);
    CloseHandle(handle);
    return ok && exitCode == STILL_ACTIVE;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

bool RecoveryStore::parseIsoUtc(const std::string& iso, std::chrono::system_clock::time_point& out) {
    // Accepts YYYY-MM-DDTHH:MM:SSZ or YYYY-MM-DDTHH:MM:SS.mmmZ
    if (iso.size() < 20) {
        return false;
    }
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d",
                    &year, &month, &day, &hour, &minute, &second) != 6) {
        return false;
    }
    if (iso.find('Z') == std::string::npos && iso.find('z') == std::string::npos) {
        return false;
    }

    int millis = 0;
    const auto dot = iso.find('.');
    if (dot != std::string::npos) {
        std::sscanf(iso.c_str() + static_cast<std::ptrdiff_t>(dot) + 1, "%d", &millis);
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
#if defined(_WIN32)
    const time_t secs = _mkgmtime(&tm);
#else
    const time_t secs = timegm(&tm);
#endif
    if (secs == static_cast<time_t>(-1)) {
        return false;
    }
    out = std::chrono::system_clock::from_time_t(secs) + std::chrono::milliseconds(millis);
    return true;
}

bool RecoveryStore::isHeartbeatStale(const std::string& iso, std::chrono::seconds staleAfter) {
    std::chrono::system_clock::time_point hb;
    if (!parseIsoUtc(iso, hb)) {
        return true;
    }
    const auto age = std::chrono::system_clock::now() - hb;
    return age > staleAfter;
}

bool RecoveryStore::IsUncleanSession(const std::filesystem::path& sessionPath,
                                     std::chrono::seconds staleAfter) {
    std::error_code ec;
    if (!std::filesystem::exists(sessionPath, ec)) {
        return false;
    }

    std::ifstream in(sessionPath);
    if (!in.is_open()) {
        return false;
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (...) {
        return false;
    }

    const bool cleanExit = root.value("cleanExit", true);
    if (cleanExit) {
        return false;
    }

    const auto pid = root.value("pid", 0u);
    const std::string lastHeartbeat = root.value("lastHeartbeat", std::string{});
    const bool alive = IsProcessRunning(static_cast<std::uint32_t>(pid));
    const bool stale = lastHeartbeat.empty() || isHeartbeatStale(lastHeartbeat, staleAfter);
    if (!alive) {
        return true;
    }
    return stale;
}

std::string RecoveryStore::nowIsoUtc() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto secs = time_point_cast<seconds>(now);
    const auto ms = duration_cast<milliseconds>(now - secs).count();
    const std::time_t t = system_clock::to_time_t(secs);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return std::string(buf);
}

std::string RecoveryStore::sanitizeIdToken(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_') {
            out.push_back(static_cast<char>(ch));
        } else if (ch == '/' || ch == '\\' || ch == '.' || ch == ' ') {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "doc";
    }
    if (out.size() > 64) {
        out.resize(64);
    }
    return out;
}

bool RecoveryStore::writeAtomic(const std::filesystem::path& finalPath, const std::string& contents) const {
    std::error_code ec;
    std::filesystem::create_directories(finalPath.parent_path(), ec);

    std::filesystem::path tempPath = finalPath;
    tempPath += ".tmp";

    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!out.good()) {
            out.close();
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

    std::filesystem::rename(tempPath, finalPath, ec);
    if (ec) {
        // Windows: replace existing
        std::filesystem::remove(finalPath, ec);
        ec.clear();
        std::filesystem::rename(tempPath, finalPath, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }
    return true;
}

bool RecoveryStore::ensureDirs() const {
    if (recoveryRoot_.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(snapshotsDir_, ec);
    return std::filesystem::is_directory(snapshotsDir_, ec);
}

bool RecoveryStore::loadSession(const std::filesystem::path& path, SessionState& out) const {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    nlohmann::json root;
    try {
        in >> root;
    } catch (...) {
        return false;
    }
    out.pid = root.value("pid", 0u);
    out.startedAt = root.value("startedAt", std::string{});
    out.cleanExit = root.value("cleanExit", false);
    out.lastHeartbeat = root.value("lastHeartbeat", std::string{});
    out.projectRoot = root.value("projectRoot", std::string{});
    return true;
}

bool RecoveryStore::saveSession(const SessionState& state) const {
    nlohmann::json root = nlohmann::json::object();
    root["schema"] = "HephaestusRecoverySession";
    root["version"] = 1;
    root["pid"] = state.pid;
    root["startedAt"] = state.startedAt;
    root["cleanExit"] = state.cleanExit;
    root["lastHeartbeat"] = state.lastHeartbeat;
    root["projectRoot"] = state.projectRoot;
    return writeAtomic(sessionPath_, root.dump(2));
}

bool RecoveryStore::loadIndex(std::vector<IndexEntry>& out) const {
    out.clear();
    std::error_code ec;
    if (!std::filesystem::exists(indexPath_, ec)) {
        return true;
    }
    std::ifstream in(indexPath_);
    if (!in.is_open()) {
        return false;
    }
    nlohmann::json root;
    try {
        in >> root;
    } catch (...) {
        return false;
    }
    if (!root.contains("snapshots") || !root["snapshots"].is_array()) {
        return true;
    }
    for (const auto& item : root["snapshots"]) {
        IndexEntry entry;
        entry.id = item.value("id", std::string{});
        entry.documentId = item.value("documentId", std::string{});
        entry.sourcePath = item.value("sourcePath", std::string{});
        entry.fileName = item.value("fileName", std::string{});
        entry.timestamp = item.value("timestamp", std::string{});
        if (item.contains("sourceMtime") && !item["sourceMtime"].is_null()) {
            entry.sourceMtime = item.value("sourceMtime", std::string{});
        }
        if (!entry.id.empty() && !entry.fileName.empty()) {
            out.push_back(std::move(entry));
        }
    }
    return true;
}

bool RecoveryStore::saveIndex(const std::vector<IndexEntry>& entries) const {
    nlohmann::json root = nlohmann::json::object();
    root["schema"] = "HephaestusRecoveryIndex";
    root["version"] = 1;
    root["snapshots"] = nlohmann::json::array();
    for (const auto& entry : entries) {
        nlohmann::json item = nlohmann::json::object();
        item["id"] = entry.id;
        item["documentId"] = entry.documentId;
        item["sourcePath"] = entry.sourcePath;
        item["fileName"] = entry.fileName;
        item["timestamp"] = entry.timestamp;
        if (entry.sourceMtime) {
            item["sourceMtime"] = *entry.sourceMtime;
        } else {
            item["sourceMtime"] = nullptr;
        }
        root["snapshots"].push_back(std::move(item));
    }
    return writeAtomic(indexPath_, root.dump(2));
}

bool RecoveryStore::setPendingRecovery(bool pending) {
    pendingRecovery_ = pending;
    std::error_code ec;
    if (!pending) {
        std::filesystem::remove(pendingPath_, ec);
        return true;
    }
    nlohmann::json root = nlohmann::json::object();
    root["schema"] = "HephaestusRecoveryPending";
    root["version"] = 1;
    root["reason"] = "unclean_session";
    root["detectedAt"] = nowIsoUtc();
    return writeAtomic(pendingPath_, root.dump(2));
}

std::filesystem::path RecoveryStore::snapshotPathFor(const IndexEntry& entry) const {
    return snapshotsDir_ / entry.fileName;
}

bool RecoveryStore::BeginSession(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) {
        return false;
    }

    projectRoot_ = projectRoot.lexically_normal();
    recoveryRoot_ = RecoveryRoot(projectRoot_);
    snapshotsDir_ = recoveryRoot_ / "snapshots";
    sessionPath_ = recoveryRoot_ / "session.json";
    indexPath_ = recoveryRoot_ / "index.json";
    pendingPath_ = recoveryRoot_ / "recovery_pending.json";

    if (!ensureDirs()) {
        return false;
    }

    std::error_code ec;
    pendingRecovery_ = std::filesystem::exists(pendingPath_, ec);

    if (std::filesystem::exists(sessionPath_, ec)) {
        if (IsUncleanSession(sessionPath_, kDefaultStaleHeartbeat)) {
            std::vector<IndexEntry> existing;
            if (loadIndex(existing) && !existing.empty()) {
                setPendingRecovery(true);
            }
        }
    }

#if defined(_WIN32)
    const std::uint32_t pid = static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    const std::uint32_t pid = static_cast<std::uint32_t>(::getpid());
#endif

    const std::string now = nowIsoUtc();
    session_.pid = pid;
    session_.startedAt = now;
    session_.cleanExit = false;
    session_.lastHeartbeat = now;
    session_.projectRoot = projectRoot_.generic_string();

    return saveSession(session_);
}

bool RecoveryStore::Heartbeat() {
    if (!HasActiveSession()) {
        return false;
    }
    session_.lastHeartbeat = nowIsoUtc();
    session_.cleanExit = false;
    return saveSession(session_);
}

bool RecoveryStore::EndSessionClean() {
    if (!HasActiveSession()) {
        return false;
    }
    session_.cleanExit = true;
    session_.lastHeartbeat = nowIsoUtc();
    return saveSession(session_);
}

std::string RecoveryStore::WriteSceneSnapshot(const std::string& documentId,
                                              const std::string& sourcePath,
                                              const std::string& contentString,
                                              const std::optional<std::string>& sourceMtime) {
    if (!HasActiveSession() || documentId.empty()) {
        return {};
    }
    if (!ensureDirs()) {
        return {};
    }

    // Guard: never write under assets/ or with a real .hpscene extension as the destination.
    const std::string timestamp = nowIsoUtc();
    std::string compactTs;
    compactTs.reserve(timestamp.size());
    for (unsigned char ch : timestamp) {
        if (std::isalnum(ch)) {
            compactTs.push_back(static_cast<char>(ch));
        }
    }

    const std::string token = sanitizeIdToken(documentId);
    static std::atomic<std::uint32_t> sequence{0};
    const std::uint32_t seq = sequence.fetch_add(1, std::memory_order_relaxed);
    const std::string id = "scene_" + token + "_" + compactTs + "_" + std::to_string(seq);
    const std::string fileName = id + ".hpscene.snapshot";
    const std::filesystem::path dest = snapshotsDir_ / fileName;

    // Destination must stay under recovery/snapshots.
    std::error_code ec;
    const auto destNorm = dest.lexically_normal();
    const auto snapsNorm = snapshotsDir_.lexically_normal();
    const auto rel = destNorm.lexically_relative(snapsNorm);
    if (rel.empty() || *rel.begin() == "..") {
        return {};
    }

    if (!writeAtomic(dest, contentString)) {
        return {};
    }

    std::vector<IndexEntry> entries;
    if (!loadIndex(entries)) {
        std::filesystem::remove(dest, ec);
        return {};
    }

    IndexEntry entry;
    entry.id = id;
    entry.documentId = documentId;
    entry.sourcePath = sourcePath;
    entry.fileName = fileName;
    entry.timestamp = timestamp;
    entry.sourceMtime = sourceMtime;
    entries.push_back(std::move(entry));

    if (!saveIndex(entries)) {
        std::filesystem::remove(dest, ec);
        return {};
    }

    return id;
}

std::vector<RecoveryCandidate> RecoveryStore::ListRecoveryCandidates() const {
    std::vector<RecoveryCandidate> result;
    if (recoveryRoot_.empty()) {
        return result;
    }

    std::error_code ec;
    const bool pending = pendingRecovery_ || std::filesystem::exists(pendingPath_, ec);
    if (!pending) {
        return result;
    }

    std::vector<IndexEntry> entries;
    if (!loadIndex(entries)) {
        return result;
    }

    result.reserve(entries.size());
    for (const auto& entry : entries) {
        const auto path = snapshotPathFor(entry);
        if (!std::filesystem::exists(path, ec)) {
            continue;
        }
        RecoveryCandidate candidate;
        candidate.id = entry.id;
        candidate.documentId = entry.documentId;
        candidate.sourcePath = entry.sourcePath;
        candidate.fileName = entry.fileName;
        candidate.timestamp = entry.timestamp;
        candidate.sourceMtime = entry.sourceMtime;
        result.push_back(std::move(candidate));
    }
    return result;
}

std::optional<std::string> RecoveryStore::LoadSnapshotContent(const std::string& id) const {
    if (id.empty() || snapshotsDir_.empty()) {
        return std::nullopt;
    }

    std::vector<IndexEntry> entries;
    if (!loadIndex(entries)) {
        return std::nullopt;
    }

    for (const auto& entry : entries) {
        if (entry.id != id) {
            continue;
        }
        const auto path = snapshotPathFor(entry);
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            return std::nullopt;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }
    return std::nullopt;
}

bool RecoveryStore::DiscardSnapshot(const std::string& id) {
    if (id.empty() || snapshotsDir_.empty()) {
        return false;
    }

    std::vector<IndexEntry> entries;
    if (!loadIndex(entries)) {
        return false;
    }

    bool removed = false;
    std::error_code ec;
    std::vector<IndexEntry> kept;
    kept.reserve(entries.size());
    for (auto& entry : entries) {
        if (entry.id == id) {
            std::filesystem::remove(snapshotPathFor(entry), ec);
            removed = true;
            continue;
        }
        kept.push_back(std::move(entry));
    }

    if (!removed) {
        return false;
    }
    if (!saveIndex(kept)) {
        return false;
    }
    if (kept.empty()) {
        setPendingRecovery(false);
    }
    return true;
}

bool RecoveryStore::DiscardAll() {
    if (snapshotsDir_.empty()) {
        return false;
    }

    std::vector<IndexEntry> entries;
    loadIndex(entries);

    std::error_code ec;
    for (const auto& entry : entries) {
        std::filesystem::remove(snapshotPathFor(entry), ec);
    }

    // Also remove any orphan snapshot files.
    if (std::filesystem::exists(snapshotsDir_, ec)) {
        for (const auto& file : std::filesystem::directory_iterator(snapshotsDir_, ec)) {
            if (!file.is_regular_file(ec)) {
                continue;
            }
            const auto name = file.path().filename().string();
            if (name.size() >= 17 && name.find(".hpscene.snapshot") != std::string::npos) {
                std::filesystem::remove(file.path(), ec);
            }
        }
    }

    if (!saveIndex({})) {
        return false;
    }
    return setPendingRecovery(false);
}

bool RecoveryStore::Prune(std::size_t maxSnapshotsPerDoc,
                          std::optional<std::chrono::seconds> maxAge) {
    if (snapshotsDir_.empty()) {
        return false;
    }

    std::vector<IndexEntry> entries;
    if (!loadIndex(entries)) {
        return false;
    }

    const auto now = std::chrono::system_clock::now();
    std::error_code ec;

    std::vector<IndexEntry> filtered;
    filtered.reserve(entries.size());
    for (auto& entry : entries) {
        if (maxAge) {
            std::chrono::system_clock::time_point ts;
            if (!parseIsoUtc(entry.timestamp, ts) || (now - ts) > *maxAge) {
                std::filesystem::remove(snapshotPathFor(entry), ec);
                continue;
            }
        }
        filtered.push_back(std::move(entry));
    }

    // Newest first per document, keep maxSnapshotsPerDoc.
    std::stable_sort(filtered.begin(), filtered.end(),
                     [](const IndexEntry& a, const IndexEntry& b) {
                         return a.timestamp > b.timestamp;
                     });

    std::unordered_map<std::string, std::size_t> counts;
    std::vector<IndexEntry> kept;
    kept.reserve(filtered.size());
    for (auto& entry : filtered) {
        std::size_t& count = counts[entry.documentId];
        if (count >= maxSnapshotsPerDoc) {
            std::filesystem::remove(snapshotPathFor(entry), ec);
            continue;
        }
        ++count;
        kept.push_back(std::move(entry));
    }

    // Restore chronological order in index (oldest first).
    std::stable_sort(kept.begin(), kept.end(),
                     [](const IndexEntry& a, const IndexEntry& b) {
                         return a.timestamp < b.timestamp;
                     });

    return saveIndex(kept);
}
