#pragma once

#include <filesystem>
#include <optional>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>
#include "../Gfx/VulkanTexture.h"
#include "imgui.h"

class VulkanDevice;

namespace HephaestusEngine {

// Project metadata stored alongside each project
struct ProjectMetadata {
    std::string name;
    std::string description;
    std::string engineVersion = "1.0.0";
    std::chrono::system_clock::time_point lastModified;
    std::chrono::system_clock::time_point created;
    std::vector<std::string> tags;
    std::string screenshotPath;  // Relative path to project screenshot
    bool isFavorite = false;
    
    // Serialize/deserialize methods
    bool SaveToFile(const std::filesystem::path& projectRoot) const;
    bool LoadFromFile(const std::filesystem::path& projectRoot);
};

// Project template for quick project creation
struct ProjectTemplate {
    std::string name;
    std::string description;
    std::string iconKey;
    std::vector<std::string> folders;  // Folders to create
    bool include3DDefaults = false;
    bool include2DDefaults = false;
};

// Project info for display in the browser
struct ProjectInfo {
    std::filesystem::path path;
    ProjectMetadata metadata;
    bool isValid = true;
    std::string errorMessage;
    
    // UI state
    ImTextureID thumbnail = (ImTextureID)0;
    bool thumbnailLoaded = false;
};

// View mode for project display
enum class ProjectViewMode {
    Grid,
    List
};

// Sort mode for projects
enum class ProjectSortMode {
    LastModified,
    Name,
    Created,
    Favorites
};

class ProjectManager {
public:
    ProjectManager() = default;
    ~ProjectManager() = default;

    // Initialize the project manager with Vulkan resources
    void Initialize(::VulkanDevice* device);
    void Shutdown();

    // Main draw function - returns selected project path if user opens a project
    std::optional<std::filesystem::path> Draw();

    // Set the projects root directory
    void SetProjectsRoot(const std::filesystem::path& root);
    
    // Get current projects root
    std::filesystem::path GetProjectsRoot() const { return m_ProjectsRoot; }

    // Refresh the project list
    void RefreshProjects();

    // Get available templates
    const std::vector<ProjectTemplate>& GetTemplates() const { return m_Templates; }

private:
    // UI Drawing functions
    void DrawHeader();
    void DrawToolbar();
    void DrawProjectsView();
    void DrawProjectCard(const ProjectInfo& project, bool inGrid);
    void DrawProjectListItem(const ProjectInfo& project);
    void DrawNewProjectDialog();
    void DrawImportProjectDialog();
    void DrawProjectContextMenu(const ProjectInfo& project);
    void DrawSidebar();
    
    // Project operations
    bool CreateProject(const std::string& name, const std::string& description, 
                      const ProjectTemplate* templatePtr = nullptr);
    bool DeleteProject(const std::filesystem::path& projectPath);
    bool ImportProject(const std::filesystem::path& projectPath);
    void OpenProjectLocation(const std::filesystem::path& projectPath);
    
    // Metadata operations
    void SaveProjectMetadata(const std::filesystem::path& projectPath, const ProjectMetadata& metadata);
    ProjectMetadata LoadProjectMetadata(const std::filesystem::path& projectPath);
    
    // Thumbnail loading
    void LoadProjectThumbnail(ProjectInfo& project);
    void CreateDefaultThumbnail(ProjectInfo& project);
    
    // Filtering and sorting
    void ApplyFilterAndSort();
    bool PassesFilter(const ProjectInfo& project) const;
    
    // Template initialization
    void InitializeTemplates();
    
    // Icon loading
    void LoadIcons();
    ImTextureID GetIcon(const std::string& iconKey);

private:
    // Vulkan resources
    ::VulkanDevice* m_Device = nullptr;
    std::unordered_map<std::string, VulkanTexture> m_Icons;
    std::unordered_map<std::filesystem::path, VulkanTexture> m_Thumbnails;
    
    // Project data
    std::filesystem::path m_ProjectsRoot;
    std::vector<ProjectInfo> m_AllProjects;
    std::vector<ProjectInfo*> m_FilteredProjects;  // Pointers to filtered projects
    std::vector<ProjectInfo*> m_RecentProjects;     // Pointers to recent projects
    
    // Templates
    std::vector<ProjectTemplate> m_Templates;
    
    // UI State
    ProjectViewMode m_ViewMode = ProjectViewMode::Grid;
    ProjectSortMode m_SortMode = ProjectSortMode::LastModified;
    char m_SearchBuffer[256] = {};
    std::string m_FilterTag;
    
    // Selected project
    ProjectInfo* m_SelectedProject = nullptr;
    ProjectInfo* m_ContextMenuProject = nullptr;
    bool m_RequestProjectOpen = false;
    
    // Dialog states
    bool m_ShowNewProjectDialog = false;
    bool m_ShowImportDialog = false;
    char m_NewProjectNameBuffer[128] = {};
    char m_NewProjectDescBuffer[512] = {};
    const ProjectTemplate* m_SelectedTemplate = nullptr;
    
    // Status messages
    std::string m_StatusMessage;
    std::string m_ErrorMessage;
    float m_MessageTimer = 0.0f;
    
    // Grid view settings
    float m_ThumbnailSize = 260.0f;
    float m_GridPadding = 20.0f;
    
    // Colors (Godot/Unreal inspired)
    ImVec4 m_ColorAccent = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);      // Blue
    ImVec4 m_ColorAccentHover = ImVec4(0.36f, 0.69f, 1.0f, 1.0f);
    ImVec4 m_ColorSuccess = ImVec4(0.32f, 0.80f, 0.60f, 1.0f);     // Green
    ImVec4 m_ColorWarning = ImVec4(0.90f, 0.70f, 0.30f, 1.0f);     // Yellow
    ImVec4 m_ColorError = ImVec4(0.90f, 0.36f, 0.36f, 1.0f);       // Red
    ImVec4 m_ColorCardBg = ImVec4(0.18f, 0.20f, 0.25f, 1.0f);      // Dark card
    ImVec4 m_ColorCardHover = ImVec4(0.22f, 0.24f, 0.29f, 1.0f);
    ImVec4 m_ColorSidebar = ImVec4(0.14f, 0.16f, 0.20f, 1.0f);
};

} // namespace HephaestusEngine
