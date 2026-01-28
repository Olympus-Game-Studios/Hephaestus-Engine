#include "AssetDatabase.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

namespace {
uint64_t ToUnixMillis(const std::filesystem::file_time_type& tp) {
    using namespace std::chrono;
    auto systemTime = time_point_cast<milliseconds>(tp - std::filesystem::file_time_type::clock::now() + system_clock::now());
    return static_cast<uint64_t>(systemTime.time_since_epoch().count());
}

std::string HashToHex(uint64_t value) {
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << value;
    return oss.str();
}

uint64_t Fnv1a64(const uint8_t* data, size_t size) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}
}

bool AssetDatabase::Load(const std::filesystem::path& projectRoot) {
    std::lock_guard<std::mutex> lock(mutex_);

    projectRoot_ = projectRoot.lexically_normal();
    if (projectRoot_.empty()) {
        return false;
    }

    dbPath_ = projectRoot_ / ".hephaestus" / "assetdb.json";
    std::error_code ec;
    std::filesystem::create_directories(dbPath_.parent_path(), ec);

    recordsById_.clear();
    sourcePathToId_.clear();

    if (!std::filesystem::exists(dbPath_, ec)) {
        return Save();
    }

    std::ifstream in(dbPath_);
    if (!in.is_open()) {
        return false;
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (...) {
        return false;
    }

    if (!root.is_object() || !root.contains("assets")) {
        return false;
    }

    for (const auto& entry : root["assets"]) {
        if (!entry.is_object()) {
            continue;
        }
        AssetRecord record;
        record.assetId = entry.value("assetId", "");
        record.assetType = AssetTypeFromString(entry.value("assetType", "Unknown"));
        record.sourcePath = entry.value("sourcePath", "");
        record.importerId = entry.value("importerId", "");
        record.importerVersion = entry.value("importerVersion", "");
        record.importSettingsHash = entry.value("importSettingsHash", "");
        record.derivedDataKey = entry.value("derivedDataKey", "");
        record.lastImportTime = entry.value("lastImportTime", 0ull);
        record.lastImportSuccess = entry.value("lastImportSuccess", false);
        record.lastImportMessage = entry.value("lastImportMessage", "");

        auto fingerprint = entry.value("fingerprint", nlohmann::json::object());
        record.fingerprint.sizeBytes = fingerprint.value("sizeBytes", 0ull);
        record.fingerprint.lastWriteTime = fingerprint.value("lastWriteTime", 0ull);
        record.fingerprint.contentHash = fingerprint.value("contentHash", "");

        if (record.assetId.empty() || record.sourcePath.empty()) {
            continue;
        }
        recordsById_[record.assetId] = record;
        sourcePathToId_[record.sourcePath] = record.assetId;
    }

    return true;
}

bool AssetDatabase::Save() {
    if (projectRoot_.empty() || dbPath_.empty()) {
        return false;
    }

    nlohmann::json root = nlohmann::json::object();
    root["schema"] = "HephaestusAssetDB";
    root["version"] = 1;
    root["assets"] = nlohmann::json::array();

    for (const auto& [id, record] : recordsById_) {
        nlohmann::json entry = nlohmann::json::object();
        entry["assetId"] = record.assetId;
        entry["assetType"] = ToString(record.assetType);
        entry["sourcePath"] = record.sourcePath;
        entry["importerId"] = record.importerId;
        entry["importerVersion"] = record.importerVersion;
        entry["importSettingsHash"] = record.importSettingsHash;
        entry["derivedDataKey"] = record.derivedDataKey;
        entry["lastImportTime"] = record.lastImportTime;
        entry["lastImportSuccess"] = record.lastImportSuccess;
        entry["lastImportMessage"] = record.lastImportMessage;

        nlohmann::json fingerprint = nlohmann::json::object();
        fingerprint["sizeBytes"] = record.fingerprint.sizeBytes;
        fingerprint["lastWriteTime"] = record.fingerprint.lastWriteTime;
        fingerprint["contentHash"] = record.fingerprint.contentHash;
        entry["fingerprint"] = fingerprint;

        root["assets"].push_back(std::move(entry));
    }

    std::error_code ec;
    std::filesystem::create_directories(dbPath_.parent_path(), ec);

    std::filesystem::path tempPath = dbPath_;
    tempPath += ".tmp";

    std::ofstream out(tempPath);
    if (!out.is_open()) {
        return false;
    }

    try {
        out << root.dump(2);
    } catch (...) {
        return false;
    }
    out.close();

    std::filesystem::rename(tempPath, dbPath_, ec);
    if (ec) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    return true;
}

std::optional<AssetRecord> AssetDatabase::GetById(const std::string& assetId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = recordsById_.find(assetId);
    if (it == recordsById_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<AssetRecord> AssetDatabase::GetBySourcePath(const std::filesystem::path& sourcePath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = NormalizeSourcePath(sourcePath, projectRoot_);
    auto it = sourcePathToId_.find(key);
    if (it == sourcePathToId_.end()) {
        return std::nullopt;
    }
    auto recordIt = recordsById_.find(it->second);
    if (recordIt == recordsById_.end()) {
        return std::nullopt;
    }
    return recordIt->second;
}

AssetRecord AssetDatabase::GetOrCreateForSource(const std::filesystem::path& sourcePath,
                                                AssetType type,
                                                const std::string& importerId,
                                                const std::string& importerVersion,
                                                const std::string& importSettingsHash) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = NormalizeSourcePath(sourcePath, projectRoot_);
    auto existing = sourcePathToId_.find(key);
    if (existing != sourcePathToId_.end()) {
        return recordsById_[existing->second];
    }

    AssetRecord record;
    record.assetId = GenerateAssetId();
    record.assetType = type;
    record.sourcePath = key;
    record.importerId = importerId;
    record.importerVersion = importerVersion;
    record.importSettingsHash = importSettingsHash;

    recordsById_[record.assetId] = record;
    sourcePathToId_[record.sourcePath] = record.assetId;
    Save();
    return record;
}

bool AssetDatabase::UpdateRecord(const AssetRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (record.assetId.empty()) {
        return false;
    }
    recordsById_[record.assetId] = record;
    if (!record.sourcePath.empty()) {
        sourcePathToId_[record.sourcePath] = record.assetId;
    }
    return Save();
}

std::vector<AssetRecord> AssetDatabase::GetAllRecords() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AssetRecord> records;
    records.reserve(recordsById_.size());
    for (const auto& [id, record] : recordsById_) {
        records.push_back(record);
    }
    return records;
}

std::filesystem::path AssetDatabase::GetProjectRoot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return projectRoot_;
}

std::string AssetDatabase::NormalizeSourcePath(const std::filesystem::path& sourcePath,
                                               const std::filesystem::path& projectRoot) {
    std::filesystem::path normalized = sourcePath.lexically_normal();
    if (projectRoot.empty()) {
        return normalized.generic_string();
    }
    std::error_code ec;
    std::filesystem::path relative = std::filesystem::relative(normalized, projectRoot, ec);
    if (!ec && !relative.empty() && relative.generic_string().find("..") == std::string::npos) {
        return relative.generic_string();
    }
    return normalized.generic_string();
}

AssetFingerprint AssetDatabase::BuildFastFingerprint(const std::filesystem::path& sourcePath) {
    AssetFingerprint fingerprint{};
    std::error_code ec;
    if (!std::filesystem::exists(sourcePath, ec)) {
        return fingerprint;
    }

    fingerprint.sizeBytes = static_cast<uint64_t>(std::filesystem::file_size(sourcePath, ec));
    if (ec) {
        fingerprint.sizeBytes = 0;
    }

    auto writeTime = std::filesystem::last_write_time(sourcePath, ec);
    if (!ec) {
        fingerprint.lastWriteTime = ToUnixMillis(writeTime);
    }

    return fingerprint;
}

std::string AssetDatabase::ComputeContentHash(const std::filesystem::path& sourcePath) {
    std::ifstream file(sourcePath, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    std::vector<uint8_t> buffer(1024 * 256);
    uint64_t hash = 14695981039346656037ull;
    while (file.good()) {
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        std::streamsize bytes = file.gcount();
        if (bytes <= 0) {
            break;
        }
        hash ^= Fnv1a64(buffer.data(), static_cast<size_t>(bytes));
        hash *= 1099511628211ull;
    }
    return HashToHex(hash);
}

std::string AssetDatabase::BuildDerivedDataKey(const AssetRecord& record, const AssetFingerprint& fingerprint) {
    std::ostringstream oss;
    oss << record.assetId << "|" << record.importerId << "|" << record.importerVersion
        << "|" << record.importSettingsHash << "|" << fingerprint.sizeBytes
        << "|" << fingerprint.lastWriteTime << "|" << fingerprint.contentHash;
    std::string composite = oss.str();
    uint64_t hash = Fnv1a64(reinterpret_cast<const uint8_t*>(composite.data()), composite.size());
    return HashToHex(hash);
}

std::string AssetDatabase::GenerateAssetId() {
    static uint64_t counter = 0;
    auto now = std::chrono::system_clock::now();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::ostringstream oss;
    oss << std::hex << millis << "-" << counter++;
    return oss.str();
}
