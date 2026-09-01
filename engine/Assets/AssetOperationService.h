/**
 * @file AssetOperationService.h
 * @brief Transactional rename / move / trash for project assets (non-Qt).
 *
 * Uses AssetReferenceIndex for preflight referrers and structured rewrite,
 * AssetPathPolicy for portable keys, and OpenDocumentRegistry for open-path
 * retargeting. Trash lives under .hephaestus/trash/; rollback backups under
 * .hephaestus/transactions/.
 */

#pragma once

#include "AssetReferenceIndex.h"
#include "OpenDocumentRegistry.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace Hephaestus {

struct AssetOperationPreflight {
    bool ok = false;
    bool isExternal = false;
    bool destinationExists = false;
    std::string oldPortablePath;
    std::string newPortablePath;
    std::string absoluteOldPath;
    std::string absoluteNewPath;
    std::vector<AssetReference> referrers;
    std::vector<std::string> warnings;
    std::string error;
};

struct AssetOperationResult {
    bool success = false;
    std::string opId;
    std::string error;
    AssetReferenceRewriteResult rewrite;
    std::vector<std::string> warnings;
};

struct TrashEntryInfo {
    std::string opId;
    std::string timestamp;
    std::vector<std::string> originalPortablePaths;
    int referrerCount = 0;
    std::string note;
};

/**
 * @brief Safe filesystem asset operations with reference rewrite + trash.
 */
class AssetOperationService {
public:
    void SetProjectRoot(const std::filesystem::path& projectRoot);
    std::filesystem::path GetProjectRoot() const;

    void SetReferenceIndex(AssetReferenceIndex* index);
    AssetReferenceIndex* GetReferenceIndex() const;

    void SetDocumentRegistry(OpenDocumentRegistry* registry);
    OpenDocumentRegistry* GetDocumentRegistry() const;

    /** Validate rename/move; gathers referrers via AssetReferenceIndex. */
    AssetOperationPreflight Preflight(const std::string& oldPath,
                                      const std::string& newPath) const;

    AssetOperationResult RenameAsset(const std::string& oldPath, const std::string& newPath);
    AssetOperationResult MoveAsset(const std::string& oldPath, const std::string& newPath);

    /**
     * Move a project asset under .hephaestus/trash/<opId>/. Never trashes
     * AbsoluteExternal sources.
     */
    AssetOperationResult MoveToTrash(const std::string& path);

    AssetOperationResult RestoreFromTrash(const std::string& opId);
    AssetOperationResult PermanentDeleteTrash(const std::string& opId);
    std::vector<TrashEntryInfo> ListTrash() const;

    static std::filesystem::path TrashRoot(const std::filesystem::path& projectRoot);
    static std::filesystem::path TransactionsRoot(const std::filesystem::path& projectRoot);

private:
    AssetOperationResult renameOrMove(const std::string& oldPath,
                                      const std::string& newPath,
                                      bool isMove);
    AssetOperationResult executeRename(const AssetOperationPreflight& preflight);
    void refreshAssetManager() const;
    void rescanIndex() const;
    void retargetOpenDocuments(const std::string& oldPortable,
                               const std::string& newPortable) const;

    bool ensureDir(const std::filesystem::path& dir, std::string* errorOut) const;
    bool writeTextFile(const std::filesystem::path& path,
                       const std::string& text,
                       std::string* errorOut) const;
    bool readTextFile(const std::filesystem::path& path,
                      std::string& out,
                      std::string* errorOut) const;
    bool copyPathRecursive(const std::filesystem::path& from,
                           const std::filesystem::path& to,
                           std::string* errorOut) const;
    bool removePathRecursive(const std::filesystem::path& path, std::string* errorOut) const;

    std::string makeOpId() const;
    std::string nowIsoUtc() const;
    std::string toPortable(const std::string& path) const;
    std::filesystem::path toAbsolute(const std::string& path) const;
    bool isExternalSource(const std::string& path) const;

    bool writeTrashManifest(const std::filesystem::path& trashDir,
                            const TrashEntryInfo& info,
                            const std::vector<AssetReference>& referrers,
                            std::string* errorOut) const;
    bool readTrashManifest(const std::filesystem::path& trashDir,
                           TrashEntryInfo& info,
                           std::string* errorOut) const;

    /** Best-effort backup of referrer docs + source before mutating. */
    bool createTransactionBackup(const std::string& opId,
                                 const std::filesystem::path& sourceAbs,
                                 const std::string& sourcePortable,
                                 const std::vector<AssetReference>& referrers,
                                 std::filesystem::path& backupRootOut,
                                 std::string* errorOut) const;
    bool rollbackFromBackup(const std::filesystem::path& backupRoot,
                            std::string* errorOut) const;
    void discardTransactionBackup(const std::filesystem::path& backupRoot) const;

    mutable std::mutex m_mutex;
    std::filesystem::path m_projectRoot;
    AssetReferenceIndex* m_index = nullptr;
    OpenDocumentRegistry* m_documents = nullptr;
};

} // namespace Hephaestus
