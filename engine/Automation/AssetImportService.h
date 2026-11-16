#pragma once

/**
 * @file AssetImportService.h
 * @brief Narrow, Qt-free abstraction over the editor asset import pipeline.
 *
 * Keeping this interface free of the concrete EditorAssetPipeline header lets
 * the automation core stay free of the renderer/Vulkan include chain. The
 * editor host supplies an implementation backed by EditorAssetPipeline and
 * ReimportManager; tests can supply a deterministic fake.
 */

#include "Assets/AssetIntake.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus::Automation {

struct AssetImportStatus {
    bool success = false;
    std::string assetType;
    std::string message;
    std::vector<std::string> warnings;
};

class IAssetImportService {
public:
    virtual ~IAssetImportService() = default;

    /** True when the source extension maps to a registered importer. */
    virtual bool CanImport(const std::string& absoluteSource, std::string& outTypeName) = 0;

    /** Register a copied source and queue an import; false when queueing fails. */
    virtual bool QueueImport(const std::string& absoluteSource, std::string& outError) = 0;

    /** Queue a reimport of an existing source; false when queueing fails. */
    virtual bool QueueReimport(const std::string& absoluteSource, std::string& outError) = 0;

    /** Non-blocking completion poll. True when a completion was observed. */
    virtual bool PollCompletion(const std::string& absoluteSource, AssetImportStatus& outStatus) = 0;

    /** Import a downloaded pack with its materials applied through prefabs (AssetIntake). */
    virtual bool ImportPack(const AssetIntakeRequest& request, AssetIntakeResult& outResult, std::string& outError)
    {
        (void)request;
        (void)outResult;
        outError = "Asset pack import is unavailable for this host";
        return false;
    }

    /** Import a pack again into its folder, updating it in place (AssetIntake::Reimport). */
    virtual bool ReimportPack(const std::filesystem::path& projectRoot, const std::string& folder,
                              const std::filesystem::path& source, AssetIntakeResult& outResult, std::string& outError)
    {
        (void)projectRoot;
        (void)folder;
        (void)source;
        (void)outResult;
        outError = "Asset pack reimport is unavailable for this host";
        return false;
    }
};

} // namespace Hephaestus::Automation
