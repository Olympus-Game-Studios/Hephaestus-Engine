/**
 * @file AssetManager.cpp
 * @brief Implementation of core asset management system.
 */

#include "AssetManager.h"
#include "AssetMountRegistry.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>

namespace Hephaestus {

AssetManager& AssetManager::GetInstance() {
    static AssetManager instance;
    return instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

void AssetManager::SetProjectRoot(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_projectRoot = std::filesystem::path(path);
    std::cout << "[AssetManager] Project root set: " << path << "\n";
}

void AssetManager::SetAssetRoot(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_assetRoot = std::filesystem::path(path);
    std::cout << "[AssetManager] Asset root set: " << path << "\n";
}

void AssetManager::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_projectRoot.clear();
    m_assetRoot.clear();
    m_assetIndex.clear();
    std::cout << "[AssetManager] Cleared\n";
    
    if (m_onAssetChanged) {
        m_onAssetChanged();
    }
}

std::string AssetManager::GetProjectRoot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_projectRoot.string();
}

std::string AssetManager::GetAssetRoot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_assetRoot.string();
}

bool AssetManager::HasValidAssetRoot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return (!m_assetRoot.empty() && std::filesystem::exists(m_assetRoot)) ||
           !AssetMountRegistry::Instance().Mounts().empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Browsing API
// ─────────────────────────────────────────────────────────────────────────────

bool AssetManager::IsBrowserHiddenName(const std::string& name) {
    return !name.empty() && name.front() == '.';
}

bool AssetManager::IsBrowserHiddenRelativePath(const std::string& relativePath) {
    if (relativePath.empty()) {
        return false;
    }
    size_t start = 0;
    while (start < relativePath.size()) {
        size_t slash = relativePath.find('/', start);
        const size_t end = (slash == std::string::npos) ? relativePath.size() : slash;
        const std::string part = relativePath.substr(start, end - start);
        if (IsBrowserHiddenName(part)) {
            return true;
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return false;
}

std::vector<DirectoryEntry> AssetManager::ListDirectory(const std::string& relativePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<DirectoryEntry> result;
    
    const auto mounted = AssetMountRegistry::Instance().Resolve(relativePath);
    if (!mounted && (m_assetRoot.empty() || !std::filesystem::exists(m_assetRoot))) {
        return result;
    }
    
    std::filesystem::path targetPath = mounted ? *mounted : m_assetRoot;
    if (!mounted && !relativePath.empty()) {
        targetPath = m_assetRoot / relativePath;
    }
    
    if (!std::filesystem::exists(targetPath) || !std::filesystem::is_directory(targetPath)) {
        return result;
    }
    
    std::error_code ec;
    std::vector<DirectoryEntry> folders;
    std::vector<DirectoryEntry> files;
    
    for (const auto& entry : std::filesystem::directory_iterator(targetPath, ec)) {
        if (ec) continue;

        DirectoryEntry dirEntry;
        dirEntry.name = entry.path().filename().string();
        if (IsBrowserHiddenName(dirEntry.name)) {
            continue;
        }
        
        // Build relative path
        std::string relPath = relativePath.empty() 
            ? dirEntry.name 
            : relativePath + "/" + dirEntry.name;
        dirEntry.relativePath = relPath;
        
        if (entry.is_directory()) {
            dirEntry.isDirectory = true;
            dirEntry.type = AssetType::Folder;
            folders.push_back(std::move(dirEntry));
        } else if (entry.is_regular_file()) {
            dirEntry.isDirectory = false;
            dirEntry.type = AssetTypeFromExtension(entry.path().extension().string());
            dirEntry.sizeBytes = entry.file_size(ec);
            files.push_back(std::move(dirEntry));
        }
    }

    if (relativePath.empty()) {
        for (const auto& mount : AssetMountRegistry::Instance().Mounts()) {
            if (!mount.scheme.starts_with("plugin://")) continue;
            DirectoryEntry plugin;
            plugin.name = "Plugin - " + mount.scheme.substr(std::string("plugin://").size());
            plugin.relativePath = mount.scheme;
            plugin.type = AssetType::Folder;
            plugin.isDirectory = true;
            folders.push_back(std::move(plugin));
        }
    }
    
    // Sort folders and files alphabetically
    auto sortByName = [](const DirectoryEntry& a, const DirectoryEntry& b) {
        return a.name < b.name;
    };
    std::sort(folders.begin(), folders.end(), sortByName);
    std::sort(files.begin(), files.end(), sortByName);
    
    // Folders first, then files
    result.reserve(folders.size() + files.size());
    result.insert(result.end(), folders.begin(), folders.end());
    result.insert(result.end(), files.begin(), files.end());
    
    return result;
}

std::optional<AssetInfo> AssetManager::GetAssetInfo(const std::string& relativePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // First check the index
    auto it = m_assetIndex.find(relativePath);
    if (it != m_assetIndex.end()) {
        return it->second;
    }
    
    // Build info on-demand if not indexed
    const auto mounted = AssetMountRegistry::Instance().Resolve(relativePath);
    if (!mounted && m_assetRoot.empty()) {
        return std::nullopt;
    }
    
    std::filesystem::path absPath = mounted ? *mounted : m_assetRoot / relativePath;
    if (!std::filesystem::exists(absPath)) {
        return std::nullopt;
    }
    
    return buildAssetInfo(absPath, relativePath);
}

std::string AssetManager::GetParentPath(const std::string& relativePath) const {
    if (relativePath.empty()) {
        return "";
    }
    
    if (relativePath.starts_with("plugin://")) {
        const auto slash = relativePath.find('/', std::string("plugin://").size());
        if (slash == std::string::npos) return "";
        const auto last = relativePath.rfind('/');
        return last == slash ? relativePath.substr(0, slash) : relativePath.substr(0, last);
    }
    std::filesystem::path p(relativePath);
    if (p.has_parent_path()) {
        std::string parent = p.parent_path().string();
        // Normalize path separators
        std::replace(parent.begin(), parent.end(), '\\', '/');
        // Handle case where parent is just "."
        if (parent == "." || parent == "./") {
            return "";
        }
        return parent;
    }
    
    return "";
}

std::string AssetManager::ToAbsolutePath(const std::string& relativePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (const auto mounted = AssetMountRegistry::Instance().Resolve(relativePath)) {
        return mounted->string();
    }
    if (m_assetRoot.empty()) {
        return "";
    }
    
    if (relativePath.empty()) {
        return m_assetRoot.string();
    }
    
    return (m_assetRoot / relativePath).string();
}

bool AssetManager::PathExists(const std::string& relativePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (const auto mounted = AssetMountRegistry::Instance().Resolve(relativePath)) {
        return std::filesystem::exists(*mounted);
    }
    if (m_assetRoot.empty()) {
        return false;
    }
    
    std::filesystem::path absPath = relativePath.empty() 
        ? m_assetRoot 
        : m_assetRoot / relativePath;
    
    return std::filesystem::exists(absPath);
}

bool AssetManager::IsDirectory(const std::string& relativePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (const auto mounted = AssetMountRegistry::Instance().Resolve(relativePath)) {
        return std::filesystem::is_directory(*mounted);
    }
    if (m_assetRoot.empty()) {
        return false;
    }
    
    std::filesystem::path absPath = relativePath.empty() 
        ? m_assetRoot 
        : m_assetRoot / relativePath;
    
    return std::filesystem::is_directory(absPath);
}

// ─────────────────────────────────────────────────────────────────────────────
// Indexing
// ─────────────────────────────────────────────────────────────────────────────

void AssetManager::Refresh() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        m_assetIndex.clear();
        
        if (m_assetRoot.empty() || !std::filesystem::exists(m_assetRoot)) {
            std::cout << "[AssetManager] Refresh skipped - no valid asset root\n";
            return;
        }
        
        std::cout << "[AssetManager] Refreshing asset index from: " << m_assetRoot << "\n";
        
        scanDirectory(m_assetRoot, "");
        
        std::cout << "[AssetManager] Indexed " << m_assetIndex.size() << " assets\n";
    }
    
    // Callback outside lock
    if (m_onAssetChanged) {
        m_onAssetChanged();
    }
}

std::vector<AssetInfo> AssetManager::GetAllAssets() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<AssetInfo> result;
    result.reserve(m_assetIndex.size());
    
    for (const auto& [path, info] : m_assetIndex) {
        if (IsBrowserHiddenRelativePath(path)) {
            continue;
        }
        result.push_back(info);
    }
    
    return result;
}

std::vector<AssetInfo> AssetManager::GetAssetsByType(AssetType type) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<AssetInfo> result;
    
    for (const auto& [path, info] : m_assetIndex) {
        if (info.type == type) {
            result.push_back(info);
        }
    }
    
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────

void AssetManager::SetAssetChangedCallback(AssetChangedCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onAssetChanged = std::move(cb);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private Helpers
// ─────────────────────────────────────────────────────────────────────────────

void AssetManager::scanDirectory(const std::filesystem::path& dir, const std::string& relativeBase) {
    std::error_code ec;
    
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) continue;
        
        std::string name = entry.path().filename().string();
        std::string relativePath = relativeBase.empty() ? name : relativeBase + "/" + name;
        
        if (entry.is_directory()) {
            // Index the folder
            AssetInfo folderInfo = buildAssetInfo(entry.path(), relativePath);
            m_assetIndex[relativePath] = folderInfo;
            
            // Recurse
            scanDirectory(entry.path(), relativePath);
        } else if (entry.is_regular_file()) {
            AssetInfo fileInfo = buildAssetInfo(entry.path(), relativePath);
            m_assetIndex[relativePath] = fileInfo;
        }
    }
}

AssetInfo AssetManager::buildAssetInfo(const std::filesystem::path& absolutePath, 
                                        const std::string& relativePath) const {
    AssetInfo info;
    info.id = computeAssetId(relativePath);
    info.name = absolutePath.filename().string();
    info.relativePath = relativePath;
    info.absolutePath = absolutePath.string();
    
    std::error_code ec;
    if (std::filesystem::is_directory(absolutePath)) {
        info.isDirectory = true;
        info.type = AssetType::Folder;
    } else {
        info.isDirectory = false;
        info.type = AssetTypeFromExtension(absolutePath.extension().string());
        info.sizeBytes = std::filesystem::file_size(absolutePath, ec);
        
        auto ftime = std::filesystem::last_write_time(absolutePath, ec);
        if (!ec) {
            // Convert to system time
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            info.lastModifiedTime = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count()
            );
        }
    }
    
    return info;
}

std::string AssetManager::computeAssetId(const std::string& relativePath) const {
    // Simple hash-based ID
    std::hash<std::string> hasher;
    size_t hash = hasher(relativePath);
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "asset_%016zx", hash);
    return std::string(buffer);
}

} // namespace Hephaestus
