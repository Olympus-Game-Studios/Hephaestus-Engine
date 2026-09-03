#include "AssetOperationService.h"

#include "AssetManager.h"
#include "AssetPathPolicy.h"
#include "CharacterAsset.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace Hephaestus {
namespace {

using json = nlohmann::json;

bool StartsWithInsensitive(const std::string& value, const std::string& prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

std::filesystem::path AssetOperationService::TrashRoot(const std::filesystem::path& projectRoot) {
    return projectRoot / ".hephaestus" / "trash";
}

std::filesystem::path AssetOperationService::TransactionsRoot(
    const std::filesystem::path& projectRoot) {
    return projectRoot / ".hephaestus" / "transactions";
}

void AssetOperationService::SetProjectRoot(const std::filesystem::path& projectRoot) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_projectRoot = projectRoot;
}

std::filesystem::path AssetOperationService::GetProjectRoot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_projectRoot;
}

void AssetOperationService::SetReferenceIndex(AssetReferenceIndex* index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_index = index;
}

AssetReferenceIndex* AssetOperationService::GetReferenceIndex() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_index;
}

void AssetOperationService::SetDocumentRegistry(OpenDocumentRegistry* registry) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_documents = registry;
}

OpenDocumentRegistry* AssetOperationService::GetDocumentRegistry() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_documents;
}

std::string AssetOperationService::toPortable(const std::string& path) const {
    return AssetPathPolicy::ToPortableKey(m_projectRoot, path);
}

std::filesystem::path AssetOperationService::toAbsolute(const std::string& path) const {
    const std::string portable = toPortable(path);
    auto resolved = AssetPathPolicy::ResolvePortableKey(m_projectRoot, portable.empty() ? path : portable);
    if (!resolved.empty()) {
        return resolved;
    }
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return p;
    }
    return m_projectRoot / p;
}

bool AssetOperationService::isExternalSource(const std::string& path) const {
    const auto classification = AssetPathPolicy::Classify(m_projectRoot, path);
    return classification.kind == AssetPathKind::AbsoluteExternal;
}

std::string AssetOperationService::makeOpId() const {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 0xffff);
    std::ostringstream oss;
    oss << "op_" << ms << "_" << std::hex << dist(gen);
    return oss.str();
}

std::string AssetOperationService::nowIsoUtc() const {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t t = clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

bool AssetOperationService::ensureDir(const std::filesystem::path& dir, std::string* errorOut) const {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        if (errorOut) {
            *errorOut = "Failed to create directory: " + dir.string() + " (" + ec.message() + ")";
        }
        return false;
    }
    return true;
}

bool AssetOperationService::writeTextFile(const std::filesystem::path& path,
                                          const std::string& text,
                                          std::string* errorOut) const {
    if (!ensureDir(path.parent_path(), errorOut)) {
        return false;
    }
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (errorOut) {
                *errorOut = "Failed to write temp file: " + tmp;
            }
            return false;
        }
        out << text;
        if (!out) {
            if (errorOut) {
                *errorOut = "Failed while writing: " + tmp;
            }
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            if (errorOut) {
                *errorOut = "Failed to finalize write: " + path.string();
            }
            return false;
        }
    }
    return true;
}

bool AssetOperationService::readTextFile(const std::filesystem::path& path,
                                         std::string& out,
                                         std::string* errorOut) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (errorOut) {
            *errorOut = "Failed to read: " + path.string();
        }
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool AssetOperationService::copyPathRecursive(const std::filesystem::path& from,
                                              const std::filesystem::path& to,
                                              std::string* errorOut) const {
    std::error_code ec;
    if (!std::filesystem::exists(from, ec)) {
        if (errorOut) {
            *errorOut = "Source missing: " + from.string();
        }
        return false;
    }
    if (std::filesystem::is_directory(from, ec)) {
        if (!ensureDir(to, errorOut)) {
            return false;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(from, ec)) {
            const auto rel = std::filesystem::relative(entry.path(), from, ec);
            const auto dest = to / rel;
            if (entry.is_directory()) {
                if (!ensureDir(dest, errorOut)) {
                    return false;
                }
            } else if (entry.is_regular_file()) {
                if (!ensureDir(dest.parent_path(), errorOut)) {
                    return false;
                }
                std::filesystem::copy_file(entry.path(), dest,
                                           std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    if (errorOut) {
                        *errorOut = "Copy failed: " + entry.path().string() + " (" + ec.message() + ")";
                    }
                    return false;
                }
            }
        }
        return true;
    }

    if (!ensureDir(to.parent_path(), errorOut)) {
        return false;
    }
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        if (errorOut) {
            *errorOut = "Copy failed: " + from.string() + " (" + ec.message() + ")";
        }
        return false;
    }
    return true;
}

bool AssetOperationService::removePathRecursive(const std::filesystem::path& path,
                                                std::string* errorOut) const {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec) {
        if (errorOut) {
            *errorOut = "Remove failed: " + path.string() + " (" + ec.message() + ")";
        }
        return false;
    }
    return true;
}

void AssetOperationService::refreshAssetManager() const {
    auto& mgr = AssetManager::GetInstance();
    if (mgr.HasValidAssetRoot()) {
        mgr.Refresh();
    }
}

void AssetOperationService::rescanIndex() const {
    AssetReferenceIndex* index = nullptr;
    std::filesystem::path root;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        index = m_index;
        root = m_projectRoot;
    }
    if (index && !root.empty()) {
        index->ScanProject(root);
    }
}

void AssetOperationService::retargetOpenDocuments(const std::string& oldPortable,
                                                 const std::string& newPortable) const {
    OpenDocumentRegistry* docs = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        docs = m_documents;
    }
    if (docs) {
        docs->RetargetPath(oldPortable, newPortable);
    }
}

AssetOperationPreflight AssetOperationService::Preflight(const std::string& oldPath,
                                                         const std::string& newPath) const {
    AssetOperationPreflight result;

    std::filesystem::path projectRoot;
    AssetReferenceIndex* index = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        projectRoot = m_projectRoot;
        index = m_index;
    }

    if (projectRoot.empty()) {
        result.error = "Project root not set";
        return result;
    }
    if (oldPath.empty()) {
        result.error = "Source path is empty";
        return result;
    }

    result.isExternal = isExternalSource(oldPath);
    result.oldPortablePath = toPortable(oldPath);
    if (result.oldPortablePath.empty()) {
        result.oldPortablePath = AssetPathPolicy::NormalizeSeparators(oldPath);
    }
    result.absoluteOldPath =
        AssetPathPolicy::NormalizeSeparators(toAbsolute(oldPath).string());

    if (!newPath.empty()) {
        result.newPortablePath = toPortable(newPath);
        if (result.newPortablePath.empty()) {
            result.newPortablePath = AssetPathPolicy::NormalizeSeparators(newPath);
        }
        result.absoluteNewPath =
            AssetPathPolicy::NormalizeSeparators(toAbsolute(newPath).string());
    }

    std::error_code ec;
    if (!std::filesystem::exists(result.absoluteOldPath, ec)) {
        result.error = "Source does not exist: " + result.absoluteOldPath;
        return result;
    }

    if (!newPath.empty()) {
        if (AssetPathPolicy::KeysEqual(result.oldPortablePath, result.newPortablePath)) {
            result.error = "Source and destination paths are the same";
            return result;
        }
        if (std::filesystem::exists(result.absoluteNewPath, ec)) {
            result.destinationExists = true;
            result.error = "Destination already exists: " + result.newPortablePath;
            return result;
        }
        // Destination must stay under project for portable ops
        const auto newClass = AssetPathPolicy::Classify(projectRoot, result.absoluteNewPath);
        if (newClass.kind == AssetPathKind::AbsoluteExternal) {
            result.error = "Destination is outside the project";
            return result;
        }
    }

    if (index) {
        result.referrers = index->GetReferrers(result.oldPortablePath);
    } else {
        result.warnings.push_back("AssetReferenceIndex not attached; referrer rewrite skipped");
    }

    result.ok = true;
    return result;
}

bool AssetOperationService::createTransactionBackup(
    const std::string& opId,
    const std::filesystem::path& sourceAbs,
    const std::string& sourcePortable,
    const std::vector<AssetReference>& referrers,
    std::filesystem::path& backupRootOut,
    std::string* errorOut) const {
    backupRootOut = TransactionsRoot(m_projectRoot) / opId;
    if (!ensureDir(backupRootOut, errorOut)) {
        return false;
    }

    json manifest = json::object();
    manifest["opId"] = opId;
    manifest["timestamp"] = nowIsoUtc();
    manifest["sourcePortable"] = sourcePortable;
    manifest["files"] = json::array();

    auto backupOne = [&](const std::filesystem::path& abs,
                         const std::string& portableLabel) -> bool {
        if (abs.empty() || !std::filesystem::exists(abs)) {
            return true;
        }
        const std::string safeName =
            AssetPathPolicy::NormalizeSeparators(portableLabel.empty() ? abs.filename().string()
                                                                        : portableLabel);
        std::string stamped = safeName;
        for (char& c : stamped) {
            if (c == '/' || c == '\\' || c == ':') {
                c = '_';
            }
        }
        const auto dest = backupRootOut / "files" / stamped;
        if (!copyPathRecursive(abs, dest, errorOut)) {
            return false;
        }
        json entry = json::object();
        entry["portable"] = portableLabel;
        entry["absolute"] = AssetPathPolicy::NormalizeSeparators(abs.string());
        entry["backup"] = AssetPathPolicy::NormalizeSeparators(
            std::filesystem::relative(dest, backupRootOut).string());
        manifest["files"].push_back(std::move(entry));
        return true;
    };

    if (!backupOne(sourceAbs, sourcePortable)) {
        return false;
    }

    for (const auto& ref : referrers) {
        const auto abs = AssetPathPolicy::ResolvePortableKey(m_projectRoot, ref.documentPath);
        if (!backupOne(abs, ref.documentPath)) {
            return false;
        }
    }

    return writeTextFile(backupRootOut / "manifest.json", manifest.dump(2), errorOut);
}

bool AssetOperationService::rollbackFromBackup(const std::filesystem::path& backupRoot,
                                               std::string* errorOut) const {
    std::string text;
    if (!readTextFile(backupRoot / "manifest.json", text, errorOut)) {
        return false;
    }
    json root;
    try {
        root = json::parse(text);
    } catch (const std::exception& ex) {
        if (errorOut) {
            *errorOut = std::string("Invalid transaction manifest: ") + ex.what();
        }
        return false;
    }

    if (!root.contains("files") || !root["files"].is_array()) {
        if (errorOut) {
            *errorOut = "Transaction manifest missing files[]";
        }
        return false;
    }

    for (const auto& entry : root["files"]) {
        const std::string absolute = entry.value("absolute", "");
        const std::string backupRel = entry.value("backup", "");
        if (absolute.empty() || backupRel.empty()) {
            continue;
        }
        const auto backupPath = backupRoot / backupRel;
        if (!copyPathRecursive(backupPath, absolute, errorOut)) {
            return false;
        }
    }
    return true;
}

void AssetOperationService::discardTransactionBackup(const std::filesystem::path& backupRoot) const {
    if (backupRoot.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove_all(backupRoot, ec);
}

AssetOperationResult AssetOperationService::executeRename(const AssetOperationPreflight& preflight) {
    AssetOperationResult result;
    result.opId = makeOpId();

    std::filesystem::path backupRoot;
    std::string error;

    if (!createTransactionBackup(result.opId,
                                 preflight.absoluteOldPath,
                                 preflight.oldPortablePath,
                                 preflight.referrers,
                                 backupRoot,
                                 &error)) {
        // Best-effort: continue without backup if creation fails, but warn.
        result.warnings.push_back("Transaction backup failed: " + error);
        backupRoot.clear();
        error.clear();
    }

    // Ensure parent of destination exists
    {
        std::filesystem::path dest(preflight.absoluteNewPath);
        if (!ensureDir(dest.parent_path(), &error)) {
            result.error = error;
            discardTransactionBackup(backupRoot);
            return result;
        }
    }

    std::error_code ec;
    std::filesystem::rename(preflight.absoluteOldPath, preflight.absoluteNewPath, ec);
    if (ec) {
        // Fallback: copy + remove for cross-device
        if (!copyPathRecursive(preflight.absoluteOldPath, preflight.absoluteNewPath, &error) ||
            !removePathRecursive(preflight.absoluteOldPath, &error)) {
            result.error = "Filesystem rename failed: " + ec.message();
            if (!backupRoot.empty()) {
                std::string rollbackError;
                rollbackFromBackup(backupRoot, &rollbackError);
            }
            discardTransactionBackup(backupRoot);
            return result;
        }
    }

    AssetReferenceIndex* index = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        index = m_index;
    }

    if (index) {
        result.rewrite =
            index->RewriteReferences(preflight.oldPortablePath, preflight.newPortablePath);
        if (!result.rewrite.success) {
            result.warnings.insert(result.warnings.end(), result.rewrite.warnings.begin(),
                                   result.rewrite.warnings.end());
            for (const auto& e : result.rewrite.errors) {
                result.warnings.push_back(e);
            }
            // Attempt rollback of filesystem + documents
            if (!backupRoot.empty()) {
                std::string rollbackError;
                if (rollbackFromBackup(backupRoot, &rollbackError)) {
                    result.error = "Reference rewrite failed; rolled back. " +
                                   (result.rewrite.errors.empty()
                                        ? std::string()
                                        : result.rewrite.errors.front());
                    discardTransactionBackup(backupRoot);
                    rescanIndex();
                    refreshAssetManager();
                    return result;
                }
                result.warnings.push_back("Rollback after rewrite failure also failed: " +
                                          rollbackError);
            }
            result.error = "Reference rewrite failed";
            discardTransactionBackup(backupRoot);
            refreshAssetManager();
            rescanIndex();
            return result;
        }
    }

    retargetOpenDocuments(preflight.oldPortablePath, preflight.newPortablePath);

    if (CharacterOwnedSource::IsCharacterAssetPortableKey(preflight.oldPortablePath) ||
        CharacterOwnedSource::IsCharacterAssetPortableKey(preflight.newPortablePath)) {
        std::string relocateError;
        if (!CharacterOwnedSource::RelocateAfterCharacterMove(GetProjectRoot(),
                                                              preflight.oldPortablePath,
                                                              preflight.newPortablePath,
                                                              relocateError)) {
            if (!backupRoot.empty()) {
                std::string rollbackError;
                rollbackFromBackup(backupRoot, &rollbackError);
            }
            result.error = relocateError.empty() ? "Failed to move character owned sources"
                                                 : relocateError;
            discardTransactionBackup(backupRoot);
            refreshAssetManager();
            rescanIndex();
            return result;
        }
        AssetReferenceIndex* ownedIndex = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ownedIndex = m_index;
        }
        if (ownedIndex) {
            const std::string oldOwned =
                CharacterOwnedSource::OwnedDirectoryPortableKey(preflight.oldPortablePath);
            const std::string newOwned =
                CharacterOwnedSource::OwnedDirectoryPortableKey(preflight.newPortablePath);
            if (!oldOwned.empty() && oldOwned != newOwned) {
                const auto ownedRewrite =
                    ownedIndex->RewriteReferences(oldOwned + "/mesh.glb", newOwned + "/mesh.glb");
                if (!ownedRewrite.success) {
                    result.warnings.push_back("Owned mesh path rewrite reported errors");
                }
                const std::string oldLua =
                    oldOwned + "/" +
                    CharacterOwnedSource::ControllerScriptFileName(
                        CharacterOwnedSource::CharacterStem(preflight.oldPortablePath));
                const std::string newLua =
                    newOwned + "/" +
                    CharacterOwnedSource::ControllerScriptFileName(
                        CharacterOwnedSource::CharacterStem(preflight.newPortablePath));
                ownedIndex->RewriteReferences(oldLua, newLua);
            }
        }
    }

    refreshAssetManager();
    rescanIndex();
    discardTransactionBackup(backupRoot);

    result.success = true;
    return result;
}

AssetOperationResult AssetOperationService::renameOrMove(const std::string& oldPath,
                                                         const std::string& newPath,
                                                         bool /*isMove*/) {
    AssetOperationResult result;
    const AssetOperationPreflight preflight = Preflight(oldPath, newPath);
    if (!preflight.ok) {
        result.error = preflight.error;
        result.warnings = preflight.warnings;
        return result;
    }
    if (preflight.isExternal) {
        // Renaming external sources on disk is allowed only if destination stays
        // within project classification for new path; still refuse moving external
        // provenance files into trash elsewhere. Rename of external is refused.
        result.error = "Cannot rename/move external (non-project) sources via AssetOperationService";
        return result;
    }
    return executeRename(preflight);
}

AssetOperationResult AssetOperationService::RenameAsset(const std::string& oldPath,
                                                        const std::string& newPath) {
    return renameOrMove(oldPath, newPath, false);
}

AssetOperationResult AssetOperationService::MoveAsset(const std::string& oldPath,
                                                      const std::string& newPath) {
    return renameOrMove(oldPath, newPath, true);
}

bool AssetOperationService::writeTrashManifest(const std::filesystem::path& trashDir,
                                               const TrashEntryInfo& info,
                                               const std::vector<AssetReference>& referrers,
                                               std::string* errorOut) const {
    json root = json::object();
    root["opId"] = info.opId;
    root["timestamp"] = info.timestamp;
    root["note"] = info.note;
    root["originalPortablePaths"] = info.originalPortablePaths;
    root["referrerCount"] = info.referrerCount;

    json refs = json::array();
    for (const auto& ref : referrers) {
        json item = json::object();
        item["documentPath"] = ref.documentPath;
        item["fieldKind"] = ToString(ref.fieldKind);
        item["structuredLocation"] = ref.structuredLocation;
        item["referencedPath"] = ref.referencedPath;
        item["isAdvisory"] = ref.isAdvisory;
        refs.push_back(std::move(item));
    }
    root["referrers"] = std::move(refs);

    return writeTextFile(trashDir / "manifest.json", root.dump(2), errorOut);
}

bool AssetOperationService::readTrashManifest(const std::filesystem::path& trashDir,
                                              TrashEntryInfo& info,
                                              std::string* errorOut) const {
    std::string text;
    if (!readTextFile(trashDir / "manifest.json", text, errorOut)) {
        return false;
    }
    json root;
    try {
        root = json::parse(text);
    } catch (const std::exception& ex) {
        if (errorOut) {
            *errorOut = std::string("Invalid trash manifest: ") + ex.what();
        }
        return false;
    }

    info.opId = root.value("opId", trashDir.filename().string());
    info.timestamp = root.value("timestamp", "");
    info.note = root.value("note", "");
    info.referrerCount = root.value("referrerCount", 0);
    info.originalPortablePaths.clear();
    if (root.contains("originalPortablePaths") && root["originalPortablePaths"].is_array()) {
        for (const auto& p : root["originalPortablePaths"]) {
            if (p.is_string()) {
                info.originalPortablePaths.push_back(p.get<std::string>());
            }
        }
    }
    return true;
}

AssetOperationResult AssetOperationService::MoveToTrash(const std::string& path) {
    AssetOperationResult result;

    std::filesystem::path projectRoot;
    AssetReferenceIndex* index = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        projectRoot = m_projectRoot;
        index = m_index;
    }

    if (projectRoot.empty()) {
        result.error = "Project root not set";
        return result;
    }
    if (path.empty()) {
        result.error = "Path is empty";
        return result;
    }
    if (isExternalSource(path)) {
        result.error = "Refusing to trash external (non-project) sources";
        return result;
    }

    const std::string portable = toPortable(path);
    const auto absolute = toAbsolute(path);
    std::error_code ec;
    if (!std::filesystem::exists(absolute, ec)) {
        result.error = "Source does not exist: " + absolute.string();
        return result;
    }

    // Refuse if under .hephaestus already
    const std::string normPortable = AssetPathPolicy::NormalizeSeparators(portable);
    if (StartsWithInsensitive(normPortable, ".hephaestus/")) {
        result.error = "Cannot trash paths under .hephaestus/";
        return result;
    }

    std::vector<AssetReference> referrers;
    if (index) {
        referrers = index->GetReferrers(portable);
    }

    result.opId = makeOpId();
    const auto trashDir = TrashRoot(projectRoot) / result.opId;
    const auto payloadDir = trashDir / "payload";

    std::string error;
    if (!ensureDir(payloadDir, &error)) {
        result.error = error;
        return result;
    }

    // Preserve portable-relative layout under payload/
    const auto payloadTarget = payloadDir / std::filesystem::path(normPortable);
    if (!ensureDir(payloadTarget.parent_path(), &error)) {
        result.error = error;
        return result;
    }

    std::filesystem::rename(absolute, payloadTarget, ec);
    if (ec) {
        if (!copyPathRecursive(absolute, payloadTarget, &error) ||
            !removePathRecursive(absolute, &error)) {
            result.error = "Failed to move asset into trash: " + ec.message();
            removePathRecursive(trashDir, nullptr);
            return result;
        }
    }

    TrashEntryInfo info;
    info.opId = result.opId;
    info.timestamp = nowIsoUtc();
    info.originalPortablePaths = {portable};
    info.referrerCount = static_cast<int>(referrers.size());
    info.note = "Moved to trash";

    if (CharacterOwnedSource::IsCharacterAssetPortableKey(portable)) {
        const std::string ownedDir = CharacterOwnedSource::OwnedDirectoryPortableKey(portable);
        const auto ownedAbs = toAbsolute(ownedDir);
        std::error_code ownedEc;
        if (!ownedDir.empty() && std::filesystem::exists(ownedAbs, ownedEc)) {
            const auto ownedPayload =
                payloadDir / std::filesystem::path(AssetPathPolicy::NormalizeSeparators(ownedDir));
            if (!ensureDir(ownedPayload.parent_path(), &error)) {
                std::error_code restoreEc;
                std::filesystem::rename(payloadTarget, absolute, restoreEc);
                result.error = error;
                return result;
            }
            std::filesystem::rename(ownedAbs, ownedPayload, ownedEc);
            if (ownedEc) {
                if (!copyPathRecursive(ownedAbs, ownedPayload, &error) ||
                    !removePathRecursive(ownedAbs, &error)) {
                    std::error_code restoreEc;
                    std::filesystem::rename(payloadTarget, absolute, restoreEc);
                    result.error = "Failed to move character owned sources into trash: " + error;
                    return result;
                }
            }
            info.originalPortablePaths.push_back(ownedDir);
        }
    }

    if (!writeTrashManifest(trashDir, info, referrers, &error)) {
        // Best-effort restore
        std::error_code restoreEc;
        std::filesystem::rename(payloadTarget, absolute, restoreEc);
        result.error = "Failed to write trash manifest: " + error;
        return result;
    }

    // Open docs that pointed at this path: leave registry entry but paths are stale;
    // callers may unregister. We do not rewrite references on trash (assets go missing).
    refreshAssetManager();
    rescanIndex();

    result.success = true;
    if (!referrers.empty()) {
        result.warnings.push_back(std::to_string(referrers.size()) +
                                  " referrer(s) still point at trashed asset");
    }
    return result;
}

AssetOperationResult AssetOperationService::RestoreFromTrash(const std::string& opId) {
    AssetOperationResult result;
    result.opId = opId;

    std::filesystem::path projectRoot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        projectRoot = m_projectRoot;
    }
    if (projectRoot.empty()) {
        result.error = "Project root not set";
        return result;
    }
    if (opId.empty()) {
        result.error = "opId is empty";
        return result;
    }

    const auto trashDir = TrashRoot(projectRoot) / opId;
    std::error_code ec;
    if (!std::filesystem::exists(trashDir, ec)) {
        result.error = "Trash entry not found: " + opId;
        return result;
    }

    TrashEntryInfo info;
    std::string error;
    if (!readTrashManifest(trashDir, info, &error)) {
        result.error = error;
        return result;
    }

    const auto payloadDir = trashDir / "payload";
    if (!std::filesystem::exists(payloadDir, ec)) {
        result.error = "Trash payload missing for " + opId;
        return result;
    }

    for (const auto& portable : info.originalPortablePaths) {
        const auto dest = toAbsolute(portable);
        const auto src = payloadDir / std::filesystem::path(
                                          AssetPathPolicy::NormalizeSeparators(portable));
        if (!std::filesystem::exists(src, ec)) {
            result.error = "Missing payload for " + portable;
            return result;
        }
        if (std::filesystem::exists(dest, ec)) {
            result.error = "Cannot restore; destination exists: " + portable;
            return result;
        }
        if (!ensureDir(dest.parent_path(), &error)) {
            result.error = error;
            return result;
        }
        std::filesystem::rename(src, dest, ec);
        if (ec) {
            if (!copyPathRecursive(src, dest, &error)) {
                result.error = "Restore rename failed: " + ec.message();
                return result;
            }
        }
    }

    if (!removePathRecursive(trashDir, &error)) {
        result.warnings.push_back("Restored but failed to clear trash entry: " + error);
    }

    refreshAssetManager();
    rescanIndex();
    result.success = true;
    return result;
}

AssetOperationResult AssetOperationService::PermanentDeleteTrash(const std::string& opId) {
    AssetOperationResult result;
    result.opId = opId;

    std::filesystem::path projectRoot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        projectRoot = m_projectRoot;
    }
    if (projectRoot.empty()) {
        result.error = "Project root not set";
        return result;
    }
    if (opId.empty()) {
        result.error = "opId is empty";
        return result;
    }

    const auto trashDir = TrashRoot(projectRoot) / opId;
    std::error_code ec;
    if (!std::filesystem::exists(trashDir, ec)) {
        result.error = "Trash entry not found: " + opId;
        return result;
    }

    std::string error;
    if (!removePathRecursive(trashDir, &error)) {
        result.error = error;
        return result;
    }
    result.success = true;
    return result;
}

std::vector<TrashEntryInfo> AssetOperationService::ListTrash() const {
    std::vector<TrashEntryInfo> entries;
    std::filesystem::path projectRoot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        projectRoot = m_projectRoot;
    }
    if (projectRoot.empty()) {
        return entries;
    }

    const auto root = TrashRoot(projectRoot);
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
        return entries;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (!entry.is_directory()) {
            continue;
        }
        TrashEntryInfo info;
        std::string error;
        if (readTrashManifest(entry.path(), info, &error)) {
            entries.push_back(std::move(info));
        }
    }
    return entries;
}

} // namespace Hephaestus
