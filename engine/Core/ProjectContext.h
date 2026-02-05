/**
 * @file ProjectContext.h
 * @brief Shared project state - single source of truth for active project.
 *
 * This class holds project-wide state including:
 * - Project file path and root directory
 * - Project name and metadata
 * - Asset root directory
 * - Recent projects list
 * 
 * Used by both Qt editor and legacy ImGui editor to ensure consistent state.
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <chrono>

namespace Hephaestus {

/**
 * @brief Project metadata loaded from .heproject file.
 */
struct ProjectData {
    int formatVersion = 1;
    std::string name;
    std::string description;
    std::string engineVersion = "1.0.0";
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point lastModified;
    bool isFavorite = false;
    std::string screenshotPath;
    std::vector<std::string> tags;
    std::string lastScene;  // Optional: last opened scene
};

/**
 * @brief Callback type for project state changes.
 */
using ProjectChangedCallback = std::function<void()>;

/**
 * @brief Single source of truth for project state.
 *
 * ProjectContext tracks the currently open project and provides
 * methods to load/close projects. It also manages recent projects.
 */
class ProjectContext {
public:
    static constexpr int kMaxRecentProjects = 10;

    ProjectContext();
    ~ProjectContext();

    // Non-copyable
    ProjectContext(const ProjectContext&) = delete;
    ProjectContext& operator=(const ProjectContext&) = delete;

    // ─────────────────────────────────────────────────────────────────────────
    // Project Management
    // ─────────────────────────────────────────────────────────────────────────
    
    /**
     * @brief Load a project from a .heproject file.
     * @param projectFilePath Path to the .heproject file
     * @param outError Error message if loading fails
     * @return true if loading succeeded
     */
    bool LoadProject(const std::string& projectFilePath, std::string& outError);

    /**
     * @brief Close the current project.
     */
    void CloseProject();

    /**
     * @brief Check if a project is currently open.
     */
    bool HasProject() const { return m_hasProject; }

    /**
     * @brief Get the project file path (.heproject file).
     */
    const std::string& GetProjectFilePath() const { return m_projectFilePath; }

    /**
     * @brief Get the project root directory (folder containing .heproject).
     */
    const std::string& GetProjectRootDir() const { return m_projectRootDir; }

    /**
     * @brief Get the project name.
     */
    const std::string& GetProjectName() const { return m_projectData.name; }

    /**
     * @brief Get the asset root directory for this project.
     * Typically <projectRoot>/assets
     */
    const std::string& GetAssetRootDir() const { return m_assetRootDir; }

    /**
     * @brief Get the full project metadata.
     */
    const ProjectData& GetProjectData() const { return m_projectData; }

    /**
     * @brief Get the last opened scene path (if any).
     */
    const std::string& GetLastScene() const { return m_projectData.lastScene; }

    // ─────────────────────────────────────────────────────────────────────────
    // Recent Projects
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Get the list of recent project paths.
     */
    const std::vector<std::string>& GetRecentProjects() const { return m_recentProjects; }

    /**
     * @brief Add a project to the recent list.
     * @param projectFilePath Path to the .heproject file
     */
    void AddToRecentProjects(const std::string& projectFilePath);

    /**
     * @brief Clear all recent projects.
     */
    void ClearRecentProjects();

    /**
     * @brief Load recent projects from persistent storage.
     * @param filePath Path to the settings file (or empty for default)
     */
    void LoadRecentProjectsFromFile(const std::string& filePath = "");

    /**
     * @brief Save recent projects to persistent storage.
     * @param filePath Path to the settings file (or empty for default)
     */
    void SaveRecentProjectsToFile(const std::string& filePath = "") const;

    // ─────────────────────────────────────────────────────────────────────────
    // Callbacks
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Set callback for project state changes (open/close).
     */
    void SetProjectChangedCallback(ProjectChangedCallback cb) { m_onProjectChanged = std::move(cb); }

private:
    bool parseProjectFile(const std::string& content, std::string& outError);
    std::string getDefaultSettingsPath() const;

    bool m_hasProject = false;
    std::string m_projectFilePath;
    std::string m_projectRootDir;
    std::string m_assetRootDir;
    ProjectData m_projectData;

    std::vector<std::string> m_recentProjects;

    ProjectChangedCallback m_onProjectChanged;
};

} // namespace Hephaestus
