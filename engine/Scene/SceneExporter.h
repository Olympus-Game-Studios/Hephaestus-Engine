#pragma once

#include <filesystem>
#include <string>
#include <vector>

class Scene;

/**
 * SceneExporter handles exporting a scene and all its referenced assets
 * to a distributable runtime package. It collects all assets referenced
 * by the scene (meshes, textures, materials, scripts) and copies them
 * to the output directory along with the scene file.
 */
class SceneExporter {
public:
    struct ExportOptions {
        std::filesystem::path outputDirectory;
        std::string projectName;
        std::filesystem::path projectRootOverride;
        std::filesystem::path engineRootOverride;
        bool includeShaders = true;
        bool createManifest = true;
        bool compressToArchive = false; // Optional for future enhancement
    };

    struct ExportResult {
        bool success = false;
        std::string errorMessage;
        std::vector<std::filesystem::path> exportedAssets;
        std::filesystem::path manifestPath;
    };

    /**
     * Export a scene and all its assets to the specified directory.
     * @param scene The scene to export
     * @param options Export configuration options
     * @return Result containing success status and list of exported files
     */
    static ExportResult Export(const Scene& scene, const ExportOptions& options);

private:
    /**
     * Collect all asset paths referenced by the scene.
     */
    static std::vector<std::filesystem::path> CollectAssetPaths(const Scene& scene);

    /**
     * Copy an asset file to the export directory, preserving relative structure.
     */
    static bool CopyAsset(const std::filesystem::path& sourcePath,
                          const std::filesystem::path& outputDir,
                          const std::filesystem::path& projectRoot,
                          std::string& outError);

    /**
     * Create a manifest file listing all exported assets and metadata.
     */
    static bool CreateManifest(const std::filesystem::path& manifestPath,
                               const std::string& projectName,
                               const std::vector<std::filesystem::path>& assets,
                               std::string& outError);
};
