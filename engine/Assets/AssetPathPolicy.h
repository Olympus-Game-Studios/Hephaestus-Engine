/**
 * @file AssetPathPolicy.h
 * @brief Canonical portable asset path helpers for authored references.
 *
 * Authored content stores project-relative keys (assets/..., .engine/...,
 * scripts/..., builtin://...). Absolute paths under the project root are
 * converted to portable keys on save. External absolute paths remain absolute
 * and are classified as non-portable importer/provenance references.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus {

enum class AssetPathKind {
    Empty,
    Builtin,
    PortableProjectRelative, ///< assets/, .engine/, scripts/, or other in-project relative
    AbsoluteInProject,       ///< Absolute path that resolves under projectRoot
    AbsoluteExternal,        ///< Absolute path outside the project
    LegacyFilename,          ///< Bare filename / unknown relative key
    Unknown
};

struct AssetPathClassification {
    AssetPathKind kind = AssetPathKind::Unknown;
    std::string portableKey;   ///< Best portable key when available
    std::string normalized;    ///< Forward-slash normalized input
    bool isPortable = false;
};

/**
 * @brief Shared path policy for serialization, resolution, and indexing.
 */
class AssetPathPolicy {
public:
    /** Normalize separators and lexical form without resolving against disk. */
    static std::string NormalizeSeparators(const std::string& path);

    /**
     * @brief Convert a path to a portable authored key when possible.
     * @param projectRoot Project root (may be empty; marker fallback still applies)
     * @param path Raw path from UI, disk, or serialized data
     * @return Portable key, builtin:// passthrough, or original normalized path
     */
    static std::string ToPortableKey(const std::filesystem::path& projectRoot,
                                     const std::string& path);

    /**
     * @brief Resolve a portable key or legacy absolute path to an absolute filesystem path.
     * Does not invent files; returns empty string when input is empty.
     */
    static std::filesystem::path ResolvePortableKey(const std::filesystem::path& projectRoot,
                                                    const std::string& key);

    /** Classify a path for portability reports and indexing. */
    static AssetPathClassification Classify(const std::filesystem::path& projectRoot,
                                            const std::string& path);

    /** True when path is already a portable authored key prefix. */
    static bool IsPortableKey(const std::string& path);

    /** Case-insensitive compare of portable keys (Windows-friendly). */
    static bool KeysEqual(const std::string& a, const std::string& b);

    /** Collect known project markers used when remapping absolute paths. */
    static const std::vector<std::string>& ProjectMarkers();

    static bool IsPluginContentKey(const std::string& key);
    static std::string PluginIdFromContentKey(const std::string& key);
    static std::string UnresolvedPluginContentMessage(const std::string& key);
};

} // namespace Hephaestus
