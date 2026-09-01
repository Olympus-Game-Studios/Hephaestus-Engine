#pragma once

#include <QString>

namespace Hephaestus::Tools {

class ProjectSettingsManager;

/**
 * @brief Shared blank-project creation used by the launcher and editor.
 *
 * Creates the standard Hephaestus folder layout, optional default scene,
 * project.heproject (+ legacy .hephaestus copy), and optional default HDRI.
 */
class ProjectCreationService {
public:
    struct Result {
        QString projectDir;
        QString projectFilePath;
        QString defaultSceneRelativePath;
    };

    /**
     * @brief Create a blank project under parentDir/name.
     * @param parentDir Parent folder that will contain the new project directory.
     * @param name Project folder / display name.
     * @param engineVersion Optional engine version string (defaults to VersionInfo).
     * @param createDefaultScene When true, writes assets/scenes/default.hpscene.
     * @param outResult Optional result paths on success.
     * @param outError Optional human-readable error on failure.
     */
    static bool CreateBlankProject(const QString& parentDir,
                                   const QString& name,
                                   const QString& engineVersion = QString(),
                                   bool createDefaultScene = true,
                                   Result* outResult = nullptr,
                                   QString* outError = nullptr);

    /**
     * @brief Persist a project-relative last/default scene path via ProjectSettingsManager.
     */
    static bool SetLastSceneRelative(ProjectSettingsManager& settings,
                                     const QString& relativeScenePath);
};

} // namespace Hephaestus::Tools
