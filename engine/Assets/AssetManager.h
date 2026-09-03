/**
 * @file AssetManager.h
 * @brief Core asset management and browsing system.
 *
 * AssetManager indexes project assets for browsing and provides:
 * - Directory listing with type detection
 * - Asset metadata queries
 * - Filesystem-independent interface (no Qt dependencies)
 *
 * Used by both Qt and ImGui editors.
 */

#pragma once

#include "AssetTypes.h"
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hephaestus {

/**
 * @brief Information about an indexed asset.
 */
struct AssetInfo {
    std::string id;                     ///< Unique ID (hash of relative path)
    std::string name;                   ///< Filename without path
    std::string relativePath;           ///< Path relative to asset root
    std::string absolutePath;           ///< Full filesystem path
    AssetType type = AssetType::Unknown;
    bool isDirectory = false;
    uint64_t sizeBytes = 0;
    uint64_t lastModifiedTime = 0;      ///< File modification time
};

/**
 * @brief Directory listing entry for browser display.
 */
struct DirectoryEntry {
    std::string name;
    std::string relativePath;
    AssetType type = AssetType::Unknown;
    bool isDirectory = false;
    uint64_t sizeBytes = 0;
};

/**
 * @brief Callback for asset change notifications.
 */
using AssetChangedCallback = std::function<void()>;

/**
 * @brief Core asset management - indexes and queries project assets.
 *
 * This class provides:
 * - Project/asset root configuration
 * - Directory listing for browser
 * - Asset type detection from extensions
 * - Manual refresh/rescan capability
 *
 * Thread-safe for concurrent access.
 */
class AssetManager {
public:
    static AssetManager& GetInstance();

    // Non-copyable
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // ─────────────────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Set the project root directory.
     * @param path Path to project root (folder containing .heproject)
     */
    void SetProjectRoot(const std::string& path);

    /**
     * @brief Set the asset root directory.
     * @param path Path to asset root (typically <projectRoot>/assets)
     */
    void SetAssetRoot(const std::string& path);

    /**
     * @brief Clear all roots and cached data (called on project close).
     */
    void Clear();

    /**
     * @brief Get the project root directory.
     */
    std::string GetProjectRoot() const;

    /**
     * @brief Get the asset root directory.
     */
    std::string GetAssetRoot() const;

    /**
     * @brief Check if asset root is configured and exists.
     */
    bool HasValidAssetRoot() const;

    // ─────────────────────────────────────────────────────────────────────────
    // Browsing API
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief List contents of a directory.
     * @param relativePath Relative path from asset root (empty = root)
     * @return List of entries (folders first, then files sorted by name)
     */
    std::vector<DirectoryEntry> ListDirectory(const std::string& relativePath = "") const;

    /** True when a directory/file name should be hidden from the Asset Browser. */
    static bool IsBrowserHiddenName(const std::string& name);

    /** True when any path component is hidden (e.g. `.source`). */
    static bool IsBrowserHiddenRelativePath(const std::string& relativePath);

    /**
     * @brief Get asset info for a relative path.
     * @param relativePath Path relative to asset root
     * @return Asset info if found
     */
    std::optional<AssetInfo> GetAssetInfo(const std::string& relativePath) const;

    /**
     * @brief Get the parent directory path.
     * @param relativePath Current path
     * @return Parent path (empty string if already at root)
     */
    std::string GetParentPath(const std::string& relativePath) const;

    /**
     * @brief Convert relative path to absolute path.
     */
    std::string ToAbsolutePath(const std::string& relativePath) const;

    /**
     * @brief Check if a path exists in the asset tree.
     */
    bool PathExists(const std::string& relativePath) const;

    /**
     * @brief Check if path is a directory.
     */
    bool IsDirectory(const std::string& relativePath) const;

    // ─────────────────────────────────────────────────────────────────────────
    // Indexing
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Scan the asset root and rebuild the index.
     * Call this after SetAssetRoot() or to refresh.
     */
    void Refresh();

    /**
     * @brief Get all indexed assets (for search/filtering).
     */
    std::vector<AssetInfo> GetAllAssets() const;

    /**
     * @brief Get assets of a specific type.
     */
    std::vector<AssetInfo> GetAssetsByType(AssetType type) const;

    // ─────────────────────────────────────────────────────────────────────────
    // Callbacks
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Set callback for when assets change (after Refresh).
     */
    void SetAssetChangedCallback(AssetChangedCallback cb);

private:
    AssetManager() = default;
    ~AssetManager() = default;

    void scanDirectory(const std::filesystem::path& dir, const std::string& relativeBase);
    AssetInfo buildAssetInfo(const std::filesystem::path& absolutePath, const std::string& relativePath) const;
    std::string computeAssetId(const std::string& relativePath) const;

    mutable std::mutex m_mutex;
    
    std::filesystem::path m_projectRoot;
    std::filesystem::path m_assetRoot;
    
    // Indexed assets by relative path
    std::unordered_map<std::string, AssetInfo> m_assetIndex;
    
    AssetChangedCallback m_onAssetChanged;
};

} // namespace Hephaestus
