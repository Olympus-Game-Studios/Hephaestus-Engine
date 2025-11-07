#include "ProjectManager.h"
#include "../Gfx/VulkanDevice.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <iomanip>

// Platform-specific includes for opening folders
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#elif defined(__linux__)
#include <stdlib.h>
#endif

namespace HephaestusEngine {

// ============================================================================
// ProjectMetadata Implementation
// ============================================================================

bool ProjectMetadata::SaveToFile(const std::filesystem::path& projectRoot) const {
    std::filesystem::path metadataPath = projectRoot / ".hephaestus" / "project.meta";
    
    try {
        std::filesystem::create_directories(metadataPath.parent_path());
        
        std::ofstream file(metadataPath);
        if (!file.is_open()) {
            return false;
        }
        
        // Simple key-value format
        file << "name=" << name << "\n";
        file << "description=" << description << "\n";
        file << "engineVersion=" << engineVersion << "\n";
        
        auto lastModTime = std::chrono::system_clock::to_time_t(lastModified);
        auto createdTime = std::chrono::system_clock::to_time_t(created);
        file << "lastModified=" << lastModTime << "\n";
        file << "created=" << createdTime << "\n";
        
        file << "isFavorite=" << (isFavorite ? "1" : "0") << "\n";
        file << "screenshot=" << screenshotPath << "\n";
        
        // Tags
        if (!tags.empty()) {
            file << "tags=";
            for (size_t i = 0; i < tags.size(); ++i) {
                file << tags[i];
                if (i < tags.size() - 1) file << ",";
            }
            file << "\n";
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjectMetadata::LoadFromFile(const std::filesystem::path& projectRoot) {
    std::filesystem::path metadataPath = projectRoot / ".hephaestus" / "project.meta";
    
    if (!std::filesystem::exists(metadataPath)) {
        return false;
    }
    
    try {
        std::ifstream file(metadataPath);
        if (!file.is_open()) {
            return false;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            size_t pos = line.find('=');
            if (pos == std::string::npos) continue;
            
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            
            if (key == "name") {
                name = value;
            } else if (key == "description") {
                description = value;
            } else if (key == "engineVersion") {
                engineVersion = value;
            } else if (key == "lastModified") {
                std::time_t time = std::stoll(value);
                lastModified = std::chrono::system_clock::from_time_t(time);
            } else if (key == "created") {
                std::time_t time = std::stoll(value);
                created = std::chrono::system_clock::from_time_t(time);
            } else if (key == "isFavorite") {
                isFavorite = (value == "1");
            } else if (key == "screenshot") {
                screenshotPath = value;
            } else if (key == "tags") {
                tags.clear();
                std::stringstream ss(value);
                std::string tag;
                while (std::getline(ss, tag, ',')) {
                    tags.push_back(tag);
                }
            }
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// ProjectManager Implementation
// ============================================================================

void ProjectManager::Initialize(::VulkanDevice* device) {
    m_Device = device;
    InitializeTemplates();
    LoadIcons();
}

void ProjectManager::Shutdown() {
    // Cleanup textures
    for (auto& [key, texture] : m_Icons) {
        // VulkanTexture::Destroy needs allocator and device
        // We'll skip cleanup for now or add proper resource management later
    }
    m_Icons.clear();
    
    for (auto& [path, texture] : m_Thumbnails) {
        // Same as above
    }
    m_Thumbnails.clear();
}

void ProjectManager::SetProjectsRoot(const std::filesystem::path& root) {
    m_ProjectsRoot = root;
    RefreshProjects();
}

void ProjectManager::RefreshProjects() {
    m_AllProjects.clear();
    m_FilteredProjects.clear();
    m_RecentProjects.clear();
    m_SelectedProject = nullptr;
    
    if (!std::filesystem::exists(m_ProjectsRoot)) {
        std::filesystem::create_directories(m_ProjectsRoot);
        return;
    }
    
    // Scan for projects
    try {
        for (const auto& entry : std::filesystem::directory_iterator(m_ProjectsRoot)) {
            if (!entry.is_directory()) continue;
            
            ProjectInfo info;
            info.path = entry.path();
            
            // Try to load metadata
            info.metadata.LoadFromFile(info.path);
            
            // If no metadata exists, create default
            if (info.metadata.name.empty()) {
                info.metadata.name = info.path.filename().string();
                info.metadata.description = "No description available";
                info.metadata.created = std::chrono::system_clock::now();
                info.metadata.lastModified = std::chrono::system_clock::now();
            }
            
            // Update last modified from filesystem
            auto ftime = std::filesystem::last_write_time(info.path);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            info.metadata.lastModified = sctp;
            
            m_AllProjects.push_back(info);
        }
    } catch (const std::exception& e) {
        m_ErrorMessage = std::string("Error scanning projects: ") + e.what();
    }
    
    ApplyFilterAndSort();
}

void ProjectManager::ApplyFilterAndSort() {
    m_FilteredProjects.clear();
    m_RecentProjects.clear();
    
    // Filter
    for (auto& project : m_AllProjects) {
        if (PassesFilter(project)) {
            m_FilteredProjects.push_back(&project);
        }
    }
    
    // Sort
    auto sortFunc = [this](ProjectInfo* a, ProjectInfo* b) {
        switch (m_SortMode) {
            case ProjectSortMode::Name:
                return a->metadata.name < b->metadata.name;
            case ProjectSortMode::Created:
                return a->metadata.created > b->metadata.created;
            case ProjectSortMode::LastModified:
                return a->metadata.lastModified > b->metadata.lastModified;
            case ProjectSortMode::Favorites:
                if (a->metadata.isFavorite != b->metadata.isFavorite) {
                    return a->metadata.isFavorite;
                }
                return a->metadata.lastModified > b->metadata.lastModified;
            default:
                return false;
        }
    };
    
    std::sort(m_FilteredProjects.begin(), m_FilteredProjects.end(), sortFunc);
    
    // Get recent projects (top 5)
    auto tempRecent = m_FilteredProjects;
    std::sort(tempRecent.begin(), tempRecent.end(), [](ProjectInfo* a, ProjectInfo* b) {
        return a->metadata.lastModified > b->metadata.lastModified;
    });
    
    size_t maxRecent = std::min(static_cast<size_t>(5), tempRecent.size());
    for (size_t i = 0; i < maxRecent; ++i) {
        m_RecentProjects.push_back(tempRecent[i]);
    }
}

bool ProjectManager::PassesFilter(const ProjectInfo& project) const {
    // Search filter
    if (m_SearchBuffer[0] != '\0') {
        std::string search = m_SearchBuffer;
        std::transform(search.begin(), search.end(), search.begin(), ::tolower);
        
        std::string name = project.metadata.name;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        
        if (name.find(search) == std::string::npos) {
            return false;
        }
    }
    
    // Tag filter
    if (!m_FilterTag.empty()) {
        bool hasTag = false;
        for (const auto& tag : project.metadata.tags) {
            if (tag == m_FilterTag) {
                hasTag = true;
                break;
            }
        }
        if (!hasTag) return false;
    }
    
    return true;
}

std::optional<std::filesystem::path> ProjectManager::Draw() {
    std::optional<std::filesystem::path> selectedProject;
    
    // Update message timer
    if (m_MessageTimer > 0.0f) {
        m_MessageTimer -= ImGui::GetIO().DeltaTime;
    }
    
    // Main window
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | 
                                  ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus |
                                  ImGuiWindowFlags_NoNavFocus |
                                  ImGuiWindowFlags_NoDocking;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    
    if (ImGui::Begin("##ProjectManagerWindow", nullptr, windowFlags)) {
        DrawHeader();
        
        ImGui::BeginChild("##MainContent", ImVec2(0, 0), false);
        
        // Layout: Sidebar (left) + Main content (right)
        float sidebarWidth = 250.0f;
        
        ImGui::PushStyleColor(ImGuiCol_ChildBg, m_ColorSidebar);
        ImGui::BeginChild("##Sidebar", ImVec2(sidebarWidth, 0), true, ImGuiWindowFlags_NoScrollbar);
        DrawSidebar();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        
        ImGui::SameLine();
        
        ImGui::BeginChild("##ProjectsArea", ImVec2(0, 0), false);
        DrawToolbar();
        ImGui::Separator();
        DrawProjectsView();
        ImGui::EndChild();
        
        ImGui::EndChild();
    }
    ImGui::End();
    
    ImGui::PopStyleVar(3);
    
    // Dialogs
    if (m_ShowNewProjectDialog) {
        DrawNewProjectDialog();
    }
    
    if (m_ShowImportDialog) {
        DrawImportProjectDialog();
    }
    
    // Check if user selected a project
    if (m_SelectedProject && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        selectedProject = m_SelectedProject->path;
        m_RequestProjectOpen = false;
    }

    if (!selectedProject && m_RequestProjectOpen && m_SelectedProject) {
        selectedProject = m_SelectedProject->path;
        m_RequestProjectOpen = false;
    }
    
    return selectedProject;
}

void ProjectManager::DrawHeader() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.14f, 0.18f, 1.0f));
    
    ImGui::BeginChild("##Header", ImVec2(0, 60), true);
    
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
    
    // Title
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Assume first font is larger
    ImGui::TextColored(m_ColorAccent, "HEPHAESTUS ENGINE");
    ImGui::PopFont();
    
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
    ImGui::TextDisabled("Project Manager");
    
    ImGui::SameLine(ImGui::GetWindowWidth() - 150);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5);
    
    // New Project button
    ImGui::PushStyleColor(ImGuiCol_Button, m_ColorAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_ColorAccentHover);
    if (ImGui::Button("+ New Project", ImVec2(140, 30))) {
        m_ShowNewProjectDialog = true;
        m_ErrorMessage.clear();
        m_StatusMessage.clear();
        memset(m_NewProjectNameBuffer, 0, sizeof(m_NewProjectNameBuffer));
        memset(m_NewProjectDescBuffer, 0, sizeof(m_NewProjectDescBuffer));
        m_SelectedTemplate = nullptr;
    }
    ImGui::PopStyleColor(2);
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void ProjectManager::DrawSidebar() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
    
    ImGui::TextColored(m_ColorAccent, "QUICK ACCESS");
    ImGui::Spacing();
    
    // Recent Projects
    if (ImGui::Selectable("Recent", m_FilterTag.empty() && m_SortMode == ProjectSortMode::LastModified)) {
        m_FilterTag.clear();
        m_SortMode = ProjectSortMode::LastModified;
        memset(m_SearchBuffer, 0, sizeof(m_SearchBuffer));
        ApplyFilterAndSort();
    }
    
    // Favorites
    if (ImGui::Selectable("Favorites", m_SortMode == ProjectSortMode::Favorites)) {
        m_FilterTag.clear();
        m_SortMode = ProjectSortMode::Favorites;
        memset(m_SearchBuffer, 0, sizeof(m_SearchBuffer));
        ApplyFilterAndSort();
    }
    
    // All Projects
    if (ImGui::Selectable("All Projects", m_FilterTag.empty() && m_SortMode == ProjectSortMode::Name)) {
        m_FilterTag.clear();
        m_SortMode = ProjectSortMode::Name;
        memset(m_SearchBuffer, 0, sizeof(m_SearchBuffer));
        ApplyFilterAndSort();
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Actions
    ImGui::TextColored(m_ColorAccent, "ACTIONS");
    ImGui::Spacing();
    
    if (ImGui::Selectable("Import Project...")) {
        m_ShowImportDialog = true;
    }
    
    if (ImGui::Selectable("Open Projects Folder")) {
        OpenProjectLocation(m_ProjectsRoot);
    }
    
    if (ImGui::Selectable("Refresh")) {
        RefreshProjects();
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Project count
    ImGui::TextDisabled("Total Projects: %zu", m_AllProjects.size());
    
    ImGui::PopStyleVar();
}

void ProjectManager::DrawToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
    
    // Search box
    ImGui::SetNextItemWidth(300);
    if (ImGui::InputTextWithHint("##Search", "Search projects...", m_SearchBuffer, sizeof(m_SearchBuffer))) {
        ApplyFilterAndSort();
    }
    
    ImGui::SameLine();
    
    // Sort dropdown
    ImGui::SetNextItemWidth(150);
    const char* sortModes[] = { "Last Modified", "Name", "Created", "Favorites" };
    int currentSort = static_cast<int>(m_SortMode);
    if (ImGui::Combo("##Sort", &currentSort, sortModes, IM_ARRAYSIZE(sortModes))) {
        m_SortMode = static_cast<ProjectSortMode>(currentSort);
        ApplyFilterAndSort();
    }
    
    ImGui::SameLine();
    
    // View mode toggle
    ImGui::PushStyleColor(ImGuiCol_Button, m_ViewMode == ProjectViewMode::Grid ? m_ColorAccent : ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Grid")) {
        m_ViewMode = ProjectViewMode::Grid;
    }
    ImGui::PopStyleColor();
    
    ImGui::SameLine(0, 2);
    
    ImGui::PushStyleColor(ImGuiCol_Button, m_ViewMode == ProjectViewMode::List ? m_ColorAccent : ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("List")) {
        m_ViewMode = ProjectViewMode::List;
    }
    ImGui::PopStyleColor();
    
    // Status messages
    if (m_MessageTimer > 0.0f) {
        ImGui::SameLine();
        if (!m_ErrorMessage.empty()) {
            ImGui::TextColored(m_ColorError, "%s", m_ErrorMessage.c_str());
        } else if (!m_StatusMessage.empty()) {
            ImGui::TextColored(m_ColorSuccess, "%s", m_StatusMessage.c_str());
        }
    }
    
    ImGui::PopStyleVar();
}

void ProjectManager::DrawProjectsView() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(m_GridPadding, m_GridPadding));
    
    ImGui::BeginChild("##ProjectsScroll", ImVec2(0, 0), false);
    
    if (m_FilteredProjects.empty()) {
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() / 2 - 50);
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("No projects found.");
        ImGui::Spacing();
        ImGui::Text("Click 'New Project' to get started!");
        ImGui::Spacing();
        ImGui::Separator();
    } else {
        if (m_ViewMode == ProjectViewMode::Grid) {
            // Grid layout
            float windowWidth = ImGui::GetContentRegionAvail().x;
            int columns = std::max(1, static_cast<int>((windowWidth + m_GridPadding) / (m_ThumbnailSize + m_GridPadding)));
            
            int col = 0;
            for (auto* project : m_FilteredProjects) {
                if (col > 0) {
                    ImGui::SameLine();
                }
                
                DrawProjectCard(*project, true);
                
                col++;
                if (col >= columns) {
                    col = 0;
                }
            }
        } else {
            // List layout
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 2));
            
            for (auto* project : m_FilteredProjects) {
                DrawProjectListItem(*project);
            }
            
            ImGui::PopStyleVar();
        }
    }
    
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void ProjectManager::DrawProjectCard(const ProjectInfo& project, bool inGrid) {
    ImGui::BeginGroup();
    
    ImVec2 cardSize(m_ThumbnailSize, m_ThumbnailSize + 80);
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    
    // Background
    bool isSelected = (m_SelectedProject == &project);
    ImVec4 bgColor = isSelected ? m_ColorAccentHover : m_ColorCardBg;
    
    if (ImGui::IsMouseHoveringRect(cursorPos, ImVec2(cursorPos.x + cardSize.x, cursorPos.y + cardSize.y))) {
        bgColor = isSelected ? m_ColorAccentHover : m_ColorCardHover;
    }
    
    ImGui::GetWindowDrawList()->AddRectFilled(
        cursorPos,
        ImVec2(cursorPos.x + cardSize.x, cursorPos.y + cardSize.y),
        ImGui::ColorConvertFloat4ToU32(bgColor),
        4.0f
    );
    
    ImGui::InvisibleButton(("##Card" + project.path.string()).c_str(), cardSize);
    
    if (ImGui::IsItemClicked()) {
        m_SelectedProject = const_cast<ProjectInfo*>(&project);
    }
    
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        // Will be handled in main Draw() function
    }
    
    if (ImGui::BeginPopupContextItem(("##Ctx" + project.path.string()).c_str())) {
        m_ContextMenuProject = const_cast<ProjectInfo*>(&project);
        DrawProjectContextMenu(project);
        ImGui::EndPopup();
    }
    
    // Draw thumbnail
    ImVec2 thumbnailPos = ImVec2(cursorPos.x + 8, cursorPos.y + 8);
    ImVec2 thumbnailSize(m_ThumbnailSize - 16, m_ThumbnailSize - 16 - 60);
    
    if (project.thumbnail) {
        ImGui::GetWindowDrawList()->AddImage(
            project.thumbnail,
            thumbnailPos,
            ImVec2(thumbnailPos.x + thumbnailSize.x, thumbnailPos.y + thumbnailSize.y)
        );
    } else {
        // Default thumbnail background
        ImGui::GetWindowDrawList()->AddRectFilled(
            thumbnailPos,
            ImVec2(thumbnailPos.x + thumbnailSize.x, thumbnailPos.y + thumbnailSize.y),
            IM_COL32(40, 45, 55, 255),
            2.0f
        );
        
        // Draw project icon
        ImVec2 iconPos = ImVec2(
            thumbnailPos.x + thumbnailSize.x / 2 - 24,
            thumbnailPos.y + thumbnailSize.y / 2 - 24
        );
        
        ImGui::GetWindowDrawList()->AddText(
            ImGui::GetFont(),
            48.0f,
            iconPos,
            IM_COL32(100, 110, 130, 255),
            ""
        );
    }
    
    // Favorite icon
    if (project.metadata.isFavorite) {
        ImVec2 favPos = ImVec2(cursorPos.x + cardSize.x - 30, cursorPos.y + 10);
        ImGui::GetWindowDrawList()->AddText(
            ImGui::GetFont(),
            20.0f,
            favPos,
            IM_COL32(255, 200, 0, 255),
            ""
        );
    }
    
    // Project name
    ImVec2 textPos = ImVec2(cursorPos.x + 8, cursorPos.y + m_ThumbnailSize - 60);
    ImGui::SetCursorScreenPos(textPos);
    
    std::string displayName = project.metadata.name;
    if (displayName.length() > 20) {
        displayName = displayName.substr(0, 17) + "...";
    }
    
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::Text("%s", displayName.c_str());
    ImGui::PopFont();
    
    // Last modified
    ImGui::SetCursorScreenPos(ImVec2(textPos.x, textPos.y + 25));
    auto time = std::chrono::system_clock::to_time_t(project.metadata.lastModified);
    std::tm tm = *std::localtime(&time);
    char timeStr[32];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d", &tm);
    ImGui::TextDisabled("%s", timeStr);
    
    ImGui::EndGroup();
}

void ProjectManager::DrawProjectListItem(const ProjectInfo& project) {
    bool isSelected = (m_SelectedProject == &project);
    
    if (ImGui::Selectable(("##ListItem" + project.path.string()).c_str(), isSelected, 
                         ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 32))) {
        m_SelectedProject = const_cast<ProjectInfo*>(&project);
    }
    
    if (ImGui::BeginPopupContextItem(("##CtxList" + project.path.string()).c_str())) {
        m_ContextMenuProject = const_cast<ProjectInfo*>(&project);
        DrawProjectContextMenu(project);
        ImGui::EndPopup();
    }
    
    ImVec2 pos = ImGui::GetItemRectMin();
    pos.x += 8;
    pos.y += 8;
    
    // Favorite star
    if (project.metadata.isFavorite) {
        ImGui::SetCursorScreenPos(pos);
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "");
        pos.x += 24;
    }
    
    // Name
    ImGui::SetCursorScreenPos(pos);
    ImGui::Text("%s", project.metadata.name.c_str());
    
    // Last modified (right-aligned)
    auto time = std::chrono::system_clock::to_time_t(project.metadata.lastModified);
    std::tm tm = *std::localtime(&time);
    char timeStr[64];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M", &tm);
    
    ImVec2 timeSize = ImGui::CalcTextSize(timeStr);
    ImGui::SameLine(ImGui::GetWindowWidth() - timeSize.x - 16);
    ImGui::TextDisabled("%s", timeStr);
}

void ProjectManager::DrawProjectContextMenu(const ProjectInfo& project) {
    if (ImGui::MenuItem("Open")) {
        m_SelectedProject = const_cast<ProjectInfo*>(&project);
        m_RequestProjectOpen = true;
    }
    
    ImGui::Separator();
    
    bool isFav = project.metadata.isFavorite;
    if (ImGui::MenuItem(isFav ? "Remove from Favorites" : "Add to Favorites")) {
        auto& meta = const_cast<ProjectMetadata&>(project.metadata);
        meta.isFavorite = !meta.isFavorite;
        meta.SaveToFile(project.path);
        ApplyFilterAndSort();
    }
    
    ImGui::Separator();
    
    if (ImGui::MenuItem("Open Folder")) {
        OpenProjectLocation(project.path);
    }
    
    if (ImGui::MenuItem("Edit Details...")) {
        // TODO: Implement project details editor
    }
    
    ImGui::Separator();
    
    ImGui::PushStyleColor(ImGuiCol_Text, m_ColorError);
    if (ImGui::MenuItem("Delete Project...")) {
        // TODO: Add confirmation dialog
        // DeleteProject(project.path);
    }
    ImGui::PopStyleColor();
}

void ProjectManager::DrawNewProjectDialog() {
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
    
    if (ImGui::Begin("Create New Project", &m_ShowNewProjectDialog, flags)) {
        ImGui::Text("Project Name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ProjectName", m_NewProjectNameBuffer, sizeof(m_NewProjectNameBuffer));
        
        ImGui::Spacing();
        ImGui::Text("Description:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##ProjectDesc", m_NewProjectDescBuffer, sizeof(m_NewProjectDescBuffer), 
                                  ImVec2(-1, 80));
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::Text("Choose a Template:");
        
        ImGui::BeginChild("##Templates", ImVec2(0, 200), true);
        
        // Empty project option
        bool isEmptySelected = (m_SelectedTemplate == nullptr);
        if (ImGui::Selectable("Empty Project", isEmptySelected)) {
            m_SelectedTemplate = nullptr;
        }
        if (isEmptySelected) {
            ImGui::Indent();
            ImGui::TextDisabled("Start with a blank project");
            ImGui::Unindent();
        }
        
        // Template options
        for (const auto& tmpl : m_Templates) {
            bool isSelected = (m_SelectedTemplate == &tmpl);
            if (ImGui::Selectable(tmpl.name.c_str(), isSelected)) {
                m_SelectedTemplate = &tmpl;
            }
            if (isSelected) {
                ImGui::Indent();
                ImGui::TextDisabled("%s", tmpl.description.c_str());
                ImGui::Unindent();
            }
        }
        
        ImGui::EndChild();
        
        ImGui::Spacing();
        
        // Error/Status messages
        if (!m_ErrorMessage.empty()) {
            ImGui::TextColored(m_ColorError, "%s", m_ErrorMessage.c_str());
        }
        
        ImGui::Separator();
        
        // Buttons
        ImGui::PushStyleColor(ImGuiCol_Button, m_ColorAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_ColorAccentHover);
        if (ImGui::Button("Create Project", ImVec2(150, 30))) {
            std::string name = m_NewProjectNameBuffer;
            std::string desc = m_NewProjectDescBuffer;
            
            if (name.empty()) {
                m_ErrorMessage = "Project name cannot be empty";
            } else if (CreateProject(name, desc, m_SelectedTemplate)) {
                m_ShowNewProjectDialog = false;
                m_StatusMessage = "Project created successfully!";
                m_MessageTimer = 3.0f;
                RefreshProjects();
            }
        }
        ImGui::PopStyleColor(2);
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 30))) {
            m_ShowNewProjectDialog = false;
        }
    }
    ImGui::End();
}

void ProjectManager::DrawImportProjectDialog() {
    ImGui::SetNextWindowSize(ImVec2(500, 200), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    if (ImGui::Begin("Import Project", &m_ShowImportDialog)) {
        ImGui::TextWrapped("To import an existing project, copy or move the project folder into:");
        ImGui::Spacing();
        ImGui::TextColored(m_ColorAccent, "%s", m_ProjectsRoot.string().c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("Then click 'Refresh' to see the imported project.");
        
        ImGui::Separator();
        
        if (ImGui::Button("Open Projects Folder", ImVec2(200, 30))) {
            OpenProjectLocation(m_ProjectsRoot);
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(100, 30))) {
            m_ShowImportDialog = false;
        }
    }
    ImGui::End();
}

bool ProjectManager::CreateProject(const std::string& name, const std::string& description, 
                                  const ProjectTemplate* templatePtr) {
    try {
        // Sanitize name
        std::string sanitizedName = name;
        for (char& c : sanitizedName) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != ' ' && c != '_' && c != '-') {
                c = '_';
            }
        }
        
        std::filesystem::path projectPath = m_ProjectsRoot / sanitizedName;
        
        if (std::filesystem::exists(projectPath)) {
            m_ErrorMessage = "A project with this name already exists";
            return false;
        }
        
        // Create project directory
        std::filesystem::create_directories(projectPath);
        
        // Create standard folders
        std::vector<std::string> folders = {
            "assets",
            "assets/models",
            "assets/textures",
            "assets/materials",
            "assets/scenes",
            "assets/scripts",
            "assets/audio",
            "assets/prefabs",
            ".hephaestus"
        };
        
        // Add template-specific folders
        if (templatePtr) {
            folders.insert(folders.end(), templatePtr->folders.begin(), templatePtr->folders.end());
        }
        
        for (const auto& folder : folders) {
            std::filesystem::create_directories(projectPath / folder);
        }
        
        // Create metadata
        ProjectMetadata metadata;
        metadata.name = name;
        metadata.description = description.empty() ? "No description" : description;
        metadata.engineVersion = "1.0.0";
        metadata.created = std::chrono::system_clock::now();
        metadata.lastModified = metadata.created;
        
        if (templatePtr) {
            metadata.tags.push_back(templatePtr->name);
        }
        
        metadata.SaveToFile(projectPath);
        
        return true;
    } catch (const std::exception& e) {
        m_ErrorMessage = std::string("Failed to create project: ") + e.what();
        return false;
    }
}

void ProjectManager::OpenProjectLocation(const std::filesystem::path& projectPath) {
#ifdef _WIN32
    ShellExecuteW(nullptr, L"explore", projectPath.wstring().c_str(), nullptr, nullptr, SW_SHOW);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + projectPath.string() + "\"";
    system(cmd.c_str());
#elif defined(__linux__)
    std::string cmd = "xdg-open \"" + projectPath.string() + "\"";
    system(cmd.c_str());
#endif
}

void ProjectManager::InitializeTemplates() {
    // 3D Project Template
    ProjectTemplate template3D;
    template3D.name = "3D Project";
    template3D.description = "A template for 3D games and applications with sample assets";
    template3D.include3DDefaults = true;
    template3D.folders = { "assets/meshes", "assets/animations" };
    m_Templates.push_back(template3D);
    
    // 2D Project Template
    ProjectTemplate template2D;
    template2D.name = "2D Project";
    template2D.description = "A template for 2D games with sprite support";
    template2D.include2DDefaults = true;
    template2D.folders = { "assets/sprites", "assets/tilemaps" };
    m_Templates.push_back(template2D);
    
    // Blank Template
    ProjectTemplate templateBlank;
    templateBlank.name = "Minimal Project";
    templateBlank.description = "Minimal project structure with only essential folders";
    m_Templates.push_back(templateBlank);
}

void ProjectManager::LoadIcons() {
    // TODO: Load actual icon textures using VulkanTexture
    // For now, we'll rely on text-based icons in the UI
}

ImTextureID ProjectManager::GetIcon(const std::string& iconKey) {
    // TODO: Return actual texture ID from m_Icons map
    (void)iconKey;
    return (ImTextureID)0;
}

} // namespace HephaestusEngine
