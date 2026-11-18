/**
 * @file AssetIntake.h
 * @brief Imports a downloaded asset pack (zip, folder or model file) into a project: models, materials applied
 *        through prefabs, leftover textures and documents, a licence record and the generated credits module.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus {

/// Where an asset came from and the terms it is used under; written as credits.json beside the asset.
struct AssetCredit {
    std::string title;
    std::string source;     ///< "Sketchfab", "Fab", ...
    std::string sourceUrl;
    std::string author;
    std::string authorUrl;
    std::string license;
    std::string licenseUrl;
};

struct AssetIntakeRequest {
    std::filesystem::path source;      ///< .zip, folder, or one .fbx/.glb/.gltf/.obj
    std::filesystem::path projectRoot;
    std::string targetFolder;          ///< portable folder under assets/; made unique when it already exists
    AssetCredit credit;                ///< no credits.json when title, author and license are all empty
};

struct AssetIntakeResult {
    std::string folder; ///< portable folder the pack was imported into
    std::vector<std::string> prefabs;
    std::vector<std::string> models;
    std::vector<std::string> materials;
    std::vector<std::string> otherFiles;
    std::vector<std::string> warnings;
};

class AssetIntake {
public:
    static bool Import(const AssetIntakeRequest& request, AssetIntakeResult& outResult, std::string& outError);

    /// Imports a pack again into the folder it was imported to, from source or else the source it was imported from
    /// (import.json), replacing its models, materials and prefabs in place. Prefabs keep their guid and the ids of
    /// nodes whose names match, so placed copies, their overrides and nested links pick up the new version. Files the
    /// new version no longer has are left alone.
    static bool Reimport(const std::filesystem::path& projectRoot, const std::string& folder,
                         const std::filesystem::path& source, AssetIntakeResult& outResult, std::string& outError);

    /// The source a pack folder was imported from, or empty when the folder has no import.json.
    static std::filesystem::path RecordedSource(const std::filesystem::path& projectRoot, const std::string& folder);

    /// Rewrites assets/scripts/ImportedCredits.lua from every assets/**/credits.json.
    static bool RegenerateCredits(const std::filesystem::path& projectRoot, std::string& outError);

    /// Extracts a ZIP (stored or deflate, no ZIP64) into destination, refusing entries that escape it.
    static bool ExtractZip(const std::filesystem::path& zip, const std::filesystem::path& destination,
                           std::string& outError);
};

} // namespace Hephaestus
