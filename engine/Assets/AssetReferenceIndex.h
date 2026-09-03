/**
 * @file AssetReferenceIndex.h
 * @brief Format-aware forward/reverse asset reference index (non-Qt).
 *
 * Indexes authored path references in scenes, prefabs, materials, HUI, and
 * project settings using AssetPathPolicy canonical keys. Supports structured
 * rewrite of known JSON fields for later AssetOperationService use.
 */

#pragma once

#include "AssetPathPolicy.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hephaestus {

enum class AssetReferenceFieldKind {
    MeshSource,
    AppliedMaterialPath,
    TexturePath,
    ScriptPath,
    ClipPath,
    HdriPath,
    PrefabPath,
    ResourcePath,
    ImageId,
    BackgroundImageId,
    Controller,
    DefaultScene,
    LastScene,
    DefaultInputProfile,
    CharacterPath,
    NestedAssetPath, ///< Heuristic assets/-like string (advisory)
    Unknown
};

const char* ToString(AssetReferenceFieldKind kind);

struct AssetReference {
    std::string documentPath;       ///< Portable key of the referring document
    AssetReferenceFieldKind fieldKind = AssetReferenceFieldKind::Unknown;
    std::string structuredLocation; ///< JSON-ish path within the document
    std::string referencedPath;     ///< Portable key of the referenced asset
    bool isAdvisory = false;        ///< Heuristic / non-authoritative reference
};

struct AssetReferenceRewriteResult {
    bool success = true;
    int documentsTouched = 0;
    int referencesRewritten = 0;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

/**
 * @brief In-memory dependency / referrer index for authored asset documents.
 */
class AssetReferenceIndex {
public:
    void Clear();

    /** Full scan of assets/ plus project settings / .heproject files. */
    void ScanProject(const std::filesystem::path& projectRoot);

    /** Rescan a single document (absolute or project-relative). */
    void ScanDocument(const std::filesystem::path& absoluteOrRelativeDocumentPath);

    /** Forward dependencies: references originating from this document. */
    std::vector<AssetReference> GetDependencies(const std::string& assetPath) const;

    /** Reverse referrers: documents that reference this asset. */
    std::vector<AssetReference> GetReferrers(const std::string& assetPath) const;

    /**
     * Rewrite known structured path fields across indexed referrers on disk,
     * then rescan touched documents. Returns success/error details.
     */
    AssetReferenceRewriteResult RewriteReferences(const std::string& oldPath,
                                                  const std::string& newPath);

    /**
     * Update known JSON string fields in one document when KeysEqual matches.
     * Does not modify the index. Returns false on I/O or unrecoverable parse failure.
     */
    static bool RewritePathInDocument(const std::filesystem::path& documentPath,
                                      const std::filesystem::path& projectRoot,
                                      const std::string& oldPath,
                                      const std::string& newPath,
                                      int* rewriteCountOut = nullptr,
                                      std::string* errorOut = nullptr);

    std::vector<std::string> GetDiagnostics() const;
    void ClearDiagnostics();

    void SetProjectRoot(const std::filesystem::path& projectRoot);
    std::filesystem::path GetProjectRoot() const;

private:
    using RefMap = std::unordered_map<std::string, std::vector<AssetReference>>;

    void clearUnlocked();
    void addDiagnostic(std::string message);
    std::string makeLookupKey(const std::string& path) const;
    std::string toDocumentKey(const std::filesystem::path& absolutePath) const;
    std::filesystem::path resolveDocumentPath(const std::filesystem::path& path) const;

    void removeDocumentUnlocked(const std::string& documentKey);
    void addReferenceUnlocked(AssetReference ref);
    void scanDocumentUnlocked(const std::filesystem::path& absolutePath);

    mutable std::mutex m_mutex;
    std::filesystem::path m_projectRoot;
    RefMap m_dependencies; ///< lookup key -> refs from that document
    RefMap m_referrers;    ///< lookup key -> refs pointing at that asset
    std::vector<std::string> m_diagnostics;
};

} // namespace Hephaestus
