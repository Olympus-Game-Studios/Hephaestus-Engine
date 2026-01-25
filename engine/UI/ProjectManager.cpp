#if HEPHAESTUS_EDITOR_IMGUI
#include "ProjectManager.h"
#include "../Gfx/VulkanDevice.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_vulkan.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>

namespace {
constexpr const char* kDefaultProjectIconKey = "project-default";

std::filesystem::path FindEditorIconsRoot()
{
    namespace fs = std::filesystem;
    static fs::path cached;
    if (!cached.empty()) {
        return cached;
    }

    fs::path current = fs::current_path();
    for (int depth = 0; depth < 6 && !current.empty(); ++depth) {
        fs::path candidate = current / "editor" / "Icons";
        std::error_code existsErr;
        if (fs::exists(candidate, existsErr) && !existsErr) {
            cached = candidate;
            return cached;
        }
        if (!current.has_parent_path()) {
            break;
        }
        current = current.parent_path();
    }

    cached = fs::current_path() / "editor" / "Icons";
    return cached;
}



// Platform-specific includes for opening folders
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#elif defined(__linux__)
#include <stdlib.h>
#endif

namespace HephaestusEngine {

#ifdef _WIN32
static int CALLBACK BrowseFolderCallback(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
    if (uMsg == BFFM_INITIALIZED && lpData) {
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, lpData);
    }
    return 0;
}

static std::optional<std::filesystem::path> PickFolderDialog(const std::filesystem::path& initialPath)
{
    BROWSEINFOW bi{};
    bi.hwndOwner = nullptr;
    bi.lpszTitle = L"Select project location";
    bi.ulFlags = BIF_USENEWUI | BIF_RETURNONLYFSDIRS;
    wchar_t displayName[MAX_PATH] = {};
    bi.pszDisplayName = displayName;
    bi.lpfn = BrowseFolderCallback;

    std::wstring normalized = initialPath.lexically_normal().wstring();
    if (!normalized.empty()) {
        bi.lParam = reinterpret_cast<LPARAM>(normalized.c_str());
    }

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        return std::nullopt;
    }

    wchar_t selected[MAX_PATH] = {};
    bool gotPath = SHGetPathFromIDListW(pidl, selected) == TRUE;
    CoTaskMemFree(pidl);

    if (!gotPath) {
        return std::nullopt;
    }

    return std::filesystem::path(selected);
}
#endif

// ============================================================================
// ProjectMetadata Implementation
// ============================================================================

namespace {
    // Helper function to escape JSON string values
    std::string EscapeJsonString(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        result += buf;
                    } else {
                        result += c;
                    }
            }
        }
        return result;
    }

    // Helper function to unescape JSON string values
    std::string UnescapeJsonString(const std::string& str) {
        std::string result;
        for (size_t i = 0; i < str.length(); ++i) {
            if (str[i] == '\\' && i + 1 < str.length()) {
                switch (str[i + 1]) {
                    case '"': result += '"'; ++i; break;
                    case '\\': result += '\\'; ++i; break;
                    case 'b': result += '\b'; ++i; break;
                    case 'f': result += '\f'; ++i; break;
                    case 'n': result += '\n'; ++i; break;
                    case 'r': result += '\r'; ++i; break;
                    case 't': result += '\t'; ++i; break;
                    case 'u': {
                        if (i + 5 < str.length()) {
                            std::string hex = str.substr(i + 2, 4);
                            try {
                                int codepoint = std::stoi(hex, nullptr, 16);
                                if (codepoint < 128) {
                                    result += static_cast<char>(codepoint);
                                }
                                i += 5;
                            } catch (...) {
                                result += str[i + 1];
                                ++i;
                            }
                        } else {
                            result += str[i + 1];
                            ++i;
                        }
                        break;
                    }
                    default: result += str[i + 1]; ++i;
                }
            } else {
                result += str[i];
            }
        }
        return result;
    }
}

bool ProjectMetadata::SaveToFile(const std::filesystem::path& projectRoot) const {
    // Save as .heproject file at root (primary) and metadata file (legacy)
    std::filesystem::path heprojectPath = projectRoot / "project.heproject";
    std::filesystem::path legacyMetadataPath = projectRoot / ".hephaestus" / "project.meta";
    
    try {
        // Write the primary .heproject file
        std::ofstream heprojectFile(heprojectPath);
        if (!heprojectFile.is_open()) {
            return false;
        }
        
        auto lastModTime = std::chrono::system_clock::to_time_t(lastModified);
        auto createdTime = std::chrono::system_clock::to_time_t(created);
        
        // Write JSON format
        heprojectFile << "{\n";
        heprojectFile << "  \"formatVersion\": 1,\n";
        heprojectFile << "  \"name\": \"" << EscapeJsonString(name) << "\",\n";
        heprojectFile << "  \"description\": \"" << EscapeJsonString(description) << "\",\n";
        heprojectFile << "  \"engineVersion\": \"" << EscapeJsonString(engineVersion) << "\",\n";
        heprojectFile << "  \"created\": " << createdTime << ",\n";
        heprojectFile << "  \"lastModified\": " << lastModTime << ",\n";
        heprojectFile << "  \"isFavorite\": " << (isFavorite ? "true" : "false") << ",\n";
        heprojectFile << "  \"screenshot\": \"" << EscapeJsonString(screenshotPath) << "\",\n";
        heprojectFile << "  \"tags\": [";
        for (size_t i = 0; i < tags.size(); ++i) {
            heprojectFile << "\"" << EscapeJsonString(tags[i]) << "\"";
            if (i < tags.size() - 1) heprojectFile << ", ";
        }
        heprojectFile << "]\n";
        heprojectFile << "}\n";
        
        heprojectFile.close();
        
        // Also write legacy metadata file for backwards compatibility
        std::filesystem::create_directories(legacyMetadataPath.parent_path());
        std::ofstream file(legacyMetadataPath);
        if (file.is_open()) {
            file << "name=" << name << "\n";
            file << "description=" << description << "\n";
            file << "engineVersion=" << engineVersion << "\n";
            file << "lastModified=" << lastModTime << "\n";
            file << "created=" << createdTime << "\n";
            file << "isFavorite=" << (isFavorite ? "1" : "0") << "\n";
            file << "screenshot=" << screenshotPath << "\n";
            if (!tags.empty()) {
                file << "tags=";
                for (size_t i = 0; i < tags.size(); ++i) {
                    file << tags[i];
                    if (i < tags.size() - 1) file << ",";
                }
                file << "\n";
            }
            file.close();
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjectMetadata::LoadFromFile(const std::filesystem::path& projectRoot) {
    std::filesystem::path heprojectPath = projectRoot / "project.heproject";
    std::filesystem::path legacyMetadataPath = projectRoot / ".hephaestus" / "project.meta";
    
    // Try to load from .heproject first (primary format)
    if (std::filesystem::exists(heprojectPath)) {
        try {
            std::ifstream file(heprojectPath);
            if (!file.is_open()) {
                return false;
            }
            
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();
            
            // Simple JSON parser for our project file format
            // This is not a full JSON parser, but sufficient for our metadata format
            
            auto extractString = [&content](const std::string& key, std::string& out) -> bool {
                std::string searchKey = "\"" + key + "\": \"";
                size_t pos = content.find(searchKey);
                if (pos == std::string::npos) return false;
                
                pos += searchKey.length();
                size_t endPos = pos;
                while (endPos < content.length()) {
                    if (content[endPos] == '"' && content[endPos - 1] != '\\') {
                        break;
                    }
                    ++endPos;
                }
                
                if (endPos >= content.length()) return false;
                out = UnescapeJsonString(content.substr(pos, endPos - pos));
                return true;
            };
            
            auto extractNumber = [&content](const std::string& key, long long& out) -> bool {
                std::string searchKey = "\"" + key + "\": ";
                size_t pos = content.find(searchKey);
                if (pos == std::string::npos) return false;
                
                pos += searchKey.length();
                size_t endPos = pos;
                while (endPos < content.length() && std::isdigit(content[endPos])) {
                    ++endPos;
                }
                
                if (endPos <= pos) return false;
                try {
                    out = std::stoll(content.substr(pos, endPos - pos));
                    return true;
                } catch (...) {
                    return false;
                }
            };
            
            auto extractBool = [&content](const std::string& key, bool& out) -> bool {
                std::string searchKey = "\"" + key + "\": ";
                size_t pos = content.find(searchKey);
                if (pos == std::string::npos) return false;
                
                pos += searchKey.length();
                if (content.substr(pos, 4) == "true") {
                    out = true;
                    return true;
                } else if (content.substr(pos, 5) == "false") {
                    out = false;
                    return true;
                }
                return false;
            };
            
            auto extractArray = [&content](const std::string& key, std::vector<std::string>& out) -> bool {
                std::string searchKey = "\"" + key + "\": [";
                size_t pos = content.find(searchKey);
                if (pos == std::string::npos) return false;
                
                pos += searchKey.length();
                while (pos < content.length() && content[pos] != ']') {
                    if (content[pos] == '"') {
                        ++pos;
                        size_t endPos = pos;
                        while (endPos < content.length() && content[endPos] != '"') {
                            if (content[endPos] == '\\') ++endPos;
                            ++endPos;
                        }
                        if (endPos < content.length()) {
                            out.push_back(UnescapeJsonString(content.substr(pos, endPos - pos)));
                            pos = endPos + 1;
                        }
                    } else {
                        ++pos;
                    }
                }
                return true;
            };
            
            // Extract fields
            std::string nameStr;
            if (extractString("name", nameStr)) name = nameStr;
            
            std::string descStr;
            if (extractString("description", descStr)) description = descStr;
            
            std::string engineVerStr;
            if (extractString("engineVersion", engineVerStr)) engineVersion = engineVerStr;
            
            long long createdTime = 0;
            if (extractNumber("created", createdTime)) {
                created = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(createdTime));
            }
            
            long long lastModTime = 0;
            if (extractNumber("lastModified", lastModTime)) {
                lastModified = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(lastModTime));
            }
            
            extractBool("isFavorite", isFavorite);
            
            std::string screenshotStr;
            if (extractString("screenshot", screenshotStr)) screenshotPath = screenshotStr;
            
            extractArray("tags", tags);
            
            return true;
        } catch (...) {
            return false;
        }
    }
    
    // Fall back to legacy metadata file format
    if (!std::filesystem::exists(legacyMetadataPath)) {
        return false;
    }
    
    try {
        std::ifstream file(legacyMetadataPath);
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
    LoadCustomPaths();
}

void ProjectManager::Shutdown() {
    // Cleanup icon textures and ImGui descriptors
    for (auto& [key, icon] : m_IconResources) {
        (void)key;
        if (icon.descriptor != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(icon.descriptor);
            icon.descriptor = VK_NULL_HANDLE;
            icon.handle = ImTextureID(0);
        }
        if (m_Device && icon.texture.image != VK_NULL_HANDLE) {
            icon.texture.Destroy(m_Device->allocator, m_Device->device);
        }
    }
    m_IconResources.clear();
    
    for (auto& [path, texture] : m_Thumbnails) {
        // Same as above
    }
    m_Thumbnails.clear();
}

void ProjectManager::SetProjectsRoot(const std::filesystem::path& root) {
    m_ProjectsRoot = root;
    RefreshProjects();
}

void ProjectManager::AddCustomProjectPath(const std::filesystem::path& projectPath) {
    // Avoid duplicates
    auto normalized = projectPath.lexically_normal();
    for (const auto& existing : m_CustomProjectPaths) {
        if (existing.lexically_normal() == normalized) {
            return;
        }
    }
    m_CustomProjectPaths.push_back(normalized);
    SaveCustomPaths();
}

void ProjectManager::RefreshProjects() {
    m_AllProjects.clear();
    m_FilteredProjects.clear();
    m_RecentProjects.clear();
    m_SelectedProject = nullptr;
    
    if (!std::filesystem::exists(m_ProjectsRoot)) {
        std::filesystem::create_directories(m_ProjectsRoot);
    }
    
    auto isValidHeProject = [](const std::filesystem::path& projectPath) -> bool {
        // A valid Hephaestus project must contain a project.heproject file
        return std::filesystem::exists(projectPath / "project.heproject");
    };
    
    auto loadProjectInfo = [](const std::filesystem::path& projectPath) -> ProjectInfo {
        ProjectInfo info;
        info.path = projectPath;
        
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
        
        return info;
    };
    
    // Scan for projects in the default root
    try {
        if (std::filesystem::exists(m_ProjectsRoot)) {
            for (const auto& entry : std::filesystem::directory_iterator(m_ProjectsRoot)) {
                if (!entry.is_directory()) continue;
                
                // Try to migrate legacy projects automatically
                if (IsLegacyHeProject(entry.path())) {
                    MigrateProjectToHeproject(entry.path());
                }
                
                // Only load valid Hephaestus projects
                if (isValidHeProject(entry.path())) {
                    m_AllProjects.push_back(loadProjectInfo(entry.path()));
                }
            }
        }
    } catch (const std::exception& e) {
        m_ErrorMessage = std::string("Error scanning projects: ") + e.what();
    }
    
    // Scan custom project paths
    for (const auto& customPath : m_CustomProjectPaths) {
        try {
            if (std::filesystem::exists(customPath) && std::filesystem::is_directory(customPath)) {
                // Try to migrate legacy projects automatically
                if (IsLegacyHeProject(customPath)) {
                    MigrateProjectToHeproject(customPath);
                }
                
                // Only load valid Hephaestus projects
                if (isValidHeProject(customPath)) {
                    m_AllProjects.push_back(loadProjectInfo(customPath));
                }
            }
        } catch (const std::exception& e) {
            // Skip invalid custom paths
        }
    }
    
    ApplyFilterAndSort();
}

std::string ProjectManager::BuildProjectManagerPayloadJson() const {
    std::ostringstream json;
    json << "\"root\":\"" << EscapeJsonString(m_ProjectsRoot.generic_string()) << "\",";

    json << "\"projects\":[";
    for (size_t i = 0; i < m_AllProjects.size(); ++i) {
        const ProjectInfo& project = m_AllProjects[i];
        const ProjectMetadata& meta = project.metadata;
        const std::string name = meta.name.empty() ? project.path.filename().string() : meta.name;
        const std::string description = meta.description.empty() ? "No description" : meta.description;

        auto createdTime = std::chrono::system_clock::to_time_t(meta.created);
        auto modifiedTime = std::chrono::system_clock::to_time_t(meta.lastModified);

        json << "{";
        json << "\"name\":\"" << EscapeJsonString(name) << "\",";
        json << "\"path\":\"" << EscapeJsonString(project.path.generic_string()) << "\",";
        json << "\"description\":\"" << EscapeJsonString(description) << "\",";
        json << "\"engineVersion\":\"" << EscapeJsonString(meta.engineVersion) << "\",";
        json << "\"created\":" << static_cast<long long>(createdTime) << ",";
        json << "\"lastModified\":" << static_cast<long long>(modifiedTime) << ",";
        json << "\"favorite\":" << (meta.isFavorite ? "true" : "false") << ",";
        json << "\"valid\":" << (project.isValid ? "true" : "false") << ",";
        json << "\"error\":\"" << EscapeJsonString(project.errorMessage) << "\",";
        json << "\"screenshot\":\"" << EscapeJsonString(meta.screenshotPath) << "\",";
        json << "\"tags\":[";
        for (size_t t = 0; t < meta.tags.size(); ++t) {
            json << "\"" << EscapeJsonString(meta.tags[t]) << "\"";
            if (t + 1 < meta.tags.size()) json << ",";
        }
        json << "]";
        json << "}";
        if (i + 1 < m_AllProjects.size()) json << ",";
    }
    json << "],";

    json << "\"templates\":[";
    for (size_t i = 0; i < m_Templates.size(); ++i) {
        const ProjectTemplate& tpl = m_Templates[i];
        json << "{";
        json << "\"index\":" << static_cast<int>(i) << ",";
        json << "\"name\":\"" << EscapeJsonString(tpl.name) << "\",";
        json << "\"description\":\"" << EscapeJsonString(tpl.description) << "\"";
        json << "}";
        if (i + 1 < m_Templates.size()) json << ",";
    }
    json << "]";
    return json.str();
}

bool ProjectManager::CreateProjectFromTemplate(const std::string& name, const std::string& description,
                                              int templateIndex, const std::string& customLocation) {
    std::fill(std::begin(m_CustomProjectLocationBuffer), std::end(m_CustomProjectLocationBuffer), 0);
    if (!customLocation.empty()) {
        std::strncpy(m_CustomProjectLocationBuffer, customLocation.c_str(),
                     sizeof(m_CustomProjectLocationBuffer) - 1);
    }

    const ProjectTemplate* tpl = nullptr;
    if (templateIndex >= 0 && templateIndex < static_cast<int>(m_Templates.size())) {
        tpl = &m_Templates[static_cast<size_t>(templateIndex)];
    }

    bool ok = CreateProject(name, description, tpl);
    if (ok) {
        RefreshProjects();
    }
    return ok;
}

bool ProjectManager::SetProjectFavorite(const std::filesystem::path& projectPath, bool isFavorite) {
    for (auto& project : m_AllProjects) {
        if (project.path.lexically_normal() == projectPath.lexically_normal()) {
            project.metadata.isFavorite = isFavorite;
            project.metadata.SaveToFile(project.path);
            ApplyFilterAndSort();
            return true;
        }
    }
    return false;
}

std::optional<std::filesystem::path> ProjectManager::PickProjectFolder(const std::filesystem::path& initialPath) const {
#ifdef _WIN32
    return PickFolderDialog(initialPath);
#else
    (void)initialPath;
    return std::nullopt;
#endif
}

void ProjectManager::OpenProjectLocationExternal(const std::filesystem::path& projectPath) {
    OpenProjectLocation(projectPath);
}

bool ProjectManager::IsLegacyHeProject(const std::filesystem::path& projectPath) const {
    namespace fs = std::filesystem;
    
    // A legacy project doesn't have project.heproject but has signs of being a project
    if (fs::exists(projectPath / "project.heproject")) {
        return false;  // Already migrated
    }
    
    // Check for legacy metadata or characteristic folder structure
    bool hasLegacyMetadata = fs::exists(projectPath / ".hephaestus" / "project.meta");
    bool hasAssetsFolder = fs::exists(projectPath / "assets");
    bool hasHephaestusFolder = fs::exists(projectPath / ".hephaestus");
    
    // It's a legacy project if:
    // - Has .hephaestus folder with metadata, OR
    // - Has standard Hephaestus structure (.hephaestus + assets folders)
    return hasLegacyMetadata || (hasHephaestusFolder && hasAssetsFolder);
}

bool ProjectManager::MigrateProjectToHeproject(const std::filesystem::path& projectPath) {
    try {
        // Don't migrate if already has .heproject
        if (std::filesystem::exists(projectPath / "project.heproject")) {
            return true;
        }
        
        // Don't migrate if not a legacy project
        if (!IsLegacyHeProject(projectPath)) {
            return false;
        }
        
        // Try to load existing metadata
        ProjectMetadata metadata;
        
        // Try legacy metadata file first
        std::filesystem::path legacyMetadataPath = projectPath / ".hephaestus" / "project.meta";
        if (std::filesystem::exists(legacyMetadataPath)) {
            metadata.LoadFromFile(projectPath);
        }
        
        // If metadata is still empty, create default
        if (metadata.name.empty()) {
            metadata.name = projectPath.filename().string();
            metadata.description = "Migrated from legacy project format";
        }
        
        // Ensure version is set
        if (metadata.engineVersion.empty()) {
            metadata.engineVersion = "1.0.0";
        }
        
        // Set timestamps if not already set
        if (metadata.created.time_since_epoch().count() == 0) {
            auto ftime = std::filesystem::last_write_time(projectPath);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            metadata.created = sctp;
        }
        
        if (metadata.lastModified.time_since_epoch().count() == 0) {
            auto ftime = std::filesystem::last_write_time(projectPath);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            metadata.lastModified = sctp;
        }
        
        // Save as new .heproject format (this also maintains legacy format)
        return metadata.SaveToFile(projectPath);
    } catch (const std::exception& e) {
        m_ErrorMessage = std::string("Failed to migrate project: ") + e.what();
        return false;
    }
}

std::vector<std::filesystem::path> ProjectManager::FindLegacyProjects(const std::filesystem::path& searchRoot) const {
    namespace fs = std::filesystem;
    std::vector<fs::path> legacyProjects;
    
    try {
        if (!fs::exists(searchRoot) || !fs::is_directory(searchRoot)) {
            return legacyProjects;
        }
        
        for (const auto& entry : fs::directory_iterator(searchRoot)) {
            if (!entry.is_directory()) continue;
            
            if (IsLegacyHeProject(entry.path())) {
                legacyProjects.push_back(entry.path());
            }
        }
    } catch (const std::exception& e) {
        // Silent catch - just skip invalid directories
    }
    
    return legacyProjects;
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
    
    if (m_ShowMigrationDialog) {
        DrawMigrationDialog();
    }

    if (m_ShowNewScriptDialog) {
        DrawNewScriptDialog();
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
    
    ImGui::BeginChild("##Header", ImVec2(0, 80), true);
    
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
    
    // Title
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Assume first font is larger
    ImGui::TextColored(m_ColorAccent, "HEPHAESTUS ENGINE");
    ImGui::PopFont();
    
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
    ImGui::TextDisabled("Project Manager");
    
    ImGui::SameLine(ImGui::GetWindowWidth() - 160);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8);
    
    // New Project button
    ImGui::PushStyleColor(ImGuiCol_Button, m_ColorAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_ColorAccentHover);
    if (ImGui::Button("+ New Project", ImVec2(150, 40))) {
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
    
    if (ImGui::Selectable("Migrate Projects...")) {
        // Open migration dialog (user can specify custom path)
        m_ShowMigrationDialog = true;
        m_LegacyProjectsFound.clear();
        m_LegacyProjectsMigrated.clear();
        memset(m_MigrationSearchPathBuffer, 0, sizeof(m_MigrationSearchPathBuffer));
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
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 12));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12);
    
    // Search box
    ImGui::SetNextItemWidth(320);
    if (ImGui::InputTextWithHint("##Search", "Search projects...", m_SearchBuffer, sizeof(m_SearchBuffer))) {
        ApplyFilterAndSort();
    }
    
    ImGui::SameLine();
    
    // Sort dropdown
    ImGui::SetNextItemWidth(170);
    const char* sortModes[] = { "Last Modified", "Name", "Created", "Favorites" };
    int currentSort = static_cast<int>(m_SortMode);
    if (ImGui::Combo("##Sort", &currentSort, sortModes, IM_ARRAYSIZE(sortModes))) {
        m_SortMode = static_cast<ProjectSortMode>(currentSort);
        ApplyFilterAndSort();
    }
    
    ImGui::SameLine();
    
    // View mode toggle
    ImGui::PushStyleColor(ImGuiCol_Button, m_ViewMode == ProjectViewMode::Grid ? m_ColorAccent : ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Grid", ImVec2(60, 0))) {
        m_ViewMode = ProjectViewMode::Grid;
    }
    ImGui::PopStyleColor();
    
    ImGui::SameLine(0, 4);
    
    ImGui::PushStyleColor(ImGuiCol_Button, m_ViewMode == ProjectViewMode::List ? m_ColorAccent : ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("List", ImVec2(60, 0))) {
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
    
    ImVec2 cardSize(m_ThumbnailSize, m_ThumbnailSize + 110);
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
    ImVec2 thumbnailSize(m_ThumbnailSize - 16, m_ThumbnailSize - 16 - 90);
    
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
        
        ImTextureID iconHandle = GetIcon(kDefaultProjectIconKey);
        if (iconHandle) {
            float iconEdge = std::min(thumbnailSize.x, thumbnailSize.y);
            iconEdge = std::min(iconEdge, 96.0f);
            ImVec2 iconMin(
                thumbnailPos.x + (thumbnailSize.x - iconEdge) * 0.5f,
                thumbnailPos.y + (thumbnailSize.y - iconEdge) * 0.5f);
            ImVec2 iconMax(iconMin.x + iconEdge, iconMin.y + iconEdge);
            ImGui::GetWindowDrawList()->AddImage(iconHandle, iconMin, iconMax);
        } else {
            ImVec2 textPos = ImVec2(
                thumbnailPos.x + thumbnailSize.x * 0.5f - 24.0f,
                thumbnailPos.y + thumbnailSize.y * 0.5f - ImGui::GetTextLineHeight() * 0.5f);
            ImGui::GetWindowDrawList()->AddText(
                ImGui::GetFont(),
                20.0f,
                textPos,
                IM_COL32(160, 160, 170, 255),
                "Project");
        }
    }
    
    // Favorite icon
    if (project.metadata.isFavorite) {
        ImVec2 favPos = ImVec2(cursorPos.x + cardSize.x - 32, cursorPos.y + 10);
        ImGui::GetWindowDrawList()->AddText(
            ImGui::GetFont(),
            24.0f,
            favPos,
            IM_COL32(255, 200, 0, 255),
            ""
        );
    }
    
    // Project name with larger font
    ImVec2 textPos = ImVec2(cursorPos.x + 10, cursorPos.y + m_ThumbnailSize - 85);
    ImGui::SetCursorScreenPos(textPos);
    
    std::string displayName = project.metadata.name;
    if (displayName.length() > 22) {
        displayName = displayName.substr(0, 19) + "...";
    }
    
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::TextWrapped("%s", displayName.c_str());
    ImGui::PopFont();
    
    // Last modified with better formatting
    ImGui::SetCursorScreenPos(ImVec2(textPos.x, textPos.y + 35));
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
                         ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 48))) {
        m_SelectedProject = const_cast<ProjectInfo*>(&project);
    }
    
    if (ImGui::BeginPopupContextItem(("##CtxList" + project.path.string()).c_str())) {
        m_ContextMenuProject = const_cast<ProjectInfo*>(&project);
        DrawProjectContextMenu(project);
        ImGui::EndPopup();
    }
    
    ImVec2 pos = ImGui::GetItemRectMin();
    pos.x += 12;
    pos.y += 10;

    ImTextureID listIcon = GetIcon(kDefaultProjectIconKey);
    if (listIcon) {
        ImVec2 iconMin = pos;
        ImVec2 iconMax(pos.x + 28.0f, pos.y + 28.0f);
        ImGui::GetWindowDrawList()->AddImage(listIcon, iconMin, iconMax);
        pos.x = iconMax.x + 8.0f;
    }
    
    // Favorite star
    if (project.metadata.isFavorite) {
        ImGui::SetCursorScreenPos(pos);
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "");
        pos.x += 28;
    }
    
    // Name
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::Text("%s", project.metadata.name.c_str());
    ImGui::PopFont();
    
    // Last modified (right-aligned)
    auto time = std::chrono::system_clock::to_time_t(project.metadata.lastModified);
    std::tm tm = *std::localtime(&time);
    char timeStr[64];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M", &tm);
    
    ImVec2 timeSize = ImGui::CalcTextSize(timeStr);
    ImGui::SameLine(ImGui::GetWindowWidth() - timeSize.x - 20);
    ImGui::TextDisabled("%s", timeStr);
}

void ProjectManager::DrawProjectContextMenu(const ProjectInfo& project) {
    if (ImGui::MenuItem("Open")) {
        m_SelectedProject = const_cast<ProjectInfo*>(&project);
        m_RequestProjectOpen = true;
    }
    
    ImGui::Separator();
    
    if (ImGui::MenuItem("New C++ Script...")) {
        m_ScriptDialogProject = const_cast<ProjectInfo*>(&project);
        m_ShowNewScriptDialog = true;
        memset(m_NewScriptNameBuffer, 0, sizeof(m_NewScriptNameBuffer));
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
        
        ImGui::Text("Project Location:");
        ImGui::SetNextItemWidth(-140.0f);
        ImGui::InputTextWithHint("##CustomLocation", "Leave blank to use default location", m_CustomProjectLocationBuffer, sizeof(m_CustomProjectLocationBuffer));
#ifdef _WIN32
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(120, 0))) {
            std::string current = m_CustomProjectLocationBuffer;
            std::filesystem::path baseLocation = current.empty() ? m_ProjectsRoot : std::filesystem::path(current);
            if (auto picked = PickFolderDialog(baseLocation)) {
                std::string normalized = picked->lexically_normal().generic_string();
                std::fill(std::begin(m_CustomProjectLocationBuffer), std::end(m_CustomProjectLocationBuffer), 0);
                std::strncpy(m_CustomProjectLocationBuffer, normalized.c_str(), sizeof(m_CustomProjectLocationBuffer) - 1);
            }
        }
#endif
        ImGui::TextDisabled("Default: %s", m_ProjectsRoot.generic_string().c_str());
        
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

void ProjectManager::DrawMigrationDialog() {
    ImGui::SetNextWindowSize(ImVec2(700, 600), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    
    if (ImGui::Begin("Migrate Legacy Projects", &m_ShowMigrationDialog, flags)) {
        ImGui::TextWrapped("Find and migrate projects from previous versions to the new .heproject format.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Search path input
        ImGui::Text("Search Location:");
        ImGui::SetNextItemWidth(-140.0f);
        if (ImGui::InputTextWithHint("##MigrationSearchPath", "Enter path or leave blank for default", 
                                     m_MigrationSearchPathBuffer, sizeof(m_MigrationSearchPathBuffer))) {
            // Path changed, clear previous results
            if (m_MigrationSearchPathBuffer[0] != '\0') {
                m_LegacyProjectsFound.clear();
                m_LegacyProjectsMigrated.clear();
            }
        }
        
#ifdef _WIN32
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(130, 0))) {
            std::string current = m_MigrationSearchPathBuffer;
            std::filesystem::path baseLocation = current.empty() ? m_ProjectsRoot : std::filesystem::path(current);
            if (auto picked = PickFolderDialog(baseLocation)) {
                std::string normalized = picked->lexically_normal().generic_string();
                std::fill(std::begin(m_MigrationSearchPathBuffer), std::end(m_MigrationSearchPathBuffer), 0);
                std::strncpy(m_MigrationSearchPathBuffer, normalized.c_str(), sizeof(m_MigrationSearchPathBuffer) - 1);
                m_LegacyProjectsFound.clear();
                m_LegacyProjectsMigrated.clear();
            }
        }
#endif
        
        ImGui::Spacing();
        
        // Search button
        ImGui::PushStyleColor(ImGuiCol_Button, m_ColorAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_ColorAccentHover);
        if (ImGui::Button("Search", ImVec2(150, 30))) {
            std::string searchPath = m_MigrationSearchPathBuffer;
            std::filesystem::path searchLocation = searchPath.empty() ? m_ProjectsRoot : std::filesystem::path(searchPath);
            
            m_LegacyProjectsFound = FindLegacyProjects(searchLocation);
            
            if (m_LegacyProjectsFound.empty()) {
                m_StatusMessage = "No legacy projects found in: " + searchLocation.string();
                m_MessageTimer = 3.0f;
            } else {
                m_StatusMessage = "Found " + std::to_string(m_LegacyProjectsFound.size()) + " legacy project(s)";
                m_MessageTimer = 3.0f;
            }
            
            m_LegacyProjectsMigrated.clear();
        }
        ImGui::PopStyleColor(2);
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (m_LegacyProjectsFound.empty()) {
            ImGui::TextDisabled("No projects found yet. Enter a search path and click Search.");
        } else {
            ImGui::Text("Legacy projects found: %zu", m_LegacyProjectsFound.size());
            ImGui::Spacing();
            
            ImGui::BeginChild("##LegacyProjectsList", ImVec2(0, 220), true);
            for (size_t i = 0; i < m_LegacyProjectsFound.size(); ++i) {
                const auto& projectPath = m_LegacyProjectsFound[i];
                bool migrated = i < m_LegacyProjectsMigrated.size() && m_LegacyProjectsMigrated[i];
                
                if (migrated) {
                    ImGui::TextColored(m_ColorSuccess, "✓ %s", projectPath.filename().string().c_str());
                } else {
                    ImGui::TextDisabled("• %s", projectPath.filename().string().c_str());
                }
                ImGui::TextDisabled("  %s", projectPath.string().c_str());
            }
            ImGui::EndChild();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (m_MigrationInProgress) {
            ImGui::TextColored(m_ColorAccent, "Migrating projects...");
        } else if (m_LegacyProjectsFound.empty()) {
            if (ImGui::Button("Close", ImVec2(150, 30))) {
                m_ShowMigrationDialog = false;
                m_LegacyProjectsFound.clear();
                m_LegacyProjectsMigrated.clear();
                memset(m_MigrationSearchPathBuffer, 0, sizeof(m_MigrationSearchPathBuffer));
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, m_ColorAccent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_ColorAccentHover);
            if (ImGui::Button("Migrate All", ImVec2(150, 30))) {
                m_MigrationInProgress = true;
                m_LegacyProjectsMigrated.clear();
                m_LegacyProjectsMigrated.resize(m_LegacyProjectsFound.size(), false);
                
                for (size_t i = 0; i < m_LegacyProjectsFound.size(); ++i) {
                    bool success = MigrateProjectToHeproject(m_LegacyProjectsFound[i]);
                    m_LegacyProjectsMigrated[i] = success;
                    if (!success) {
                        m_ErrorMessage = "Failed to migrate: " + m_LegacyProjectsFound[i].filename().string();
                    }
                }
                
                m_MigrationInProgress = false;
                
                // Refresh projects to show the migrated ones
                RefreshProjects();
            }
            ImGui::PopStyleColor(2);
            
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 30))) {
                m_ShowMigrationDialog = false;
                m_LegacyProjectsFound.clear();
                m_LegacyProjectsMigrated.clear();
                memset(m_MigrationSearchPathBuffer, 0, sizeof(m_MigrationSearchPathBuffer));
            }
        }
        
        if (!m_ErrorMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(m_ColorError, "%s", m_ErrorMessage.c_str());
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
        
        std::string customLocation = m_CustomProjectLocationBuffer;
        std::filesystem::path baseLocation = customLocation.empty() ? m_ProjectsRoot : std::filesystem::path(customLocation);
        baseLocation = baseLocation.lexically_normal();
        std::filesystem::path projectPath = baseLocation / sanitizedName;
        
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
            "assets/blueprints",
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
        
        // If project was created outside the default root, track it
        auto normalizedProject = projectPath.lexically_normal();
        auto normalizedRoot = m_ProjectsRoot.lexically_normal();
        if (normalizedProject.parent_path() != normalizedRoot) {
            AddCustomProjectPath(projectPath);
        }
        
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
    if (!m_Device) {
        return;
    }

    struct IconDef {
        const char* key;
        const char* file;
    };

    static const IconDef kIconDefs[] = {
        {kDefaultProjectIconKey, "folder.png"}
    };

    namespace fs = std::filesystem;
    fs::path iconsRoot = FindEditorIconsRoot();
    std::error_code rootErr;
    if (!fs::exists(iconsRoot, rootErr) || rootErr) {
        std::cerr << "[ProjectManager] Icon directory not found: " << iconsRoot << std::endl;
        return;
    }

    for (const auto& def : kIconDefs) {
        if (m_IconResources.find(def.key) != m_IconResources.end()) {
            continue;
        }

        fs::path iconPath = iconsRoot / def.file;
        std::error_code existsErr;
        if (!fs::exists(iconPath, existsErr) || existsErr) {
            std::cerr << "[ProjectManager] Icon missing: " << iconPath << std::endl;
            continue;
        }

        IconResource resource;
        bool loaded = resource.texture.LoadFromFile(
            m_Device->allocator,
            m_Device->device,
            m_Device->physicalDevice,
            m_Device->commandPool,
            m_Device->graphicsQueue,
            iconPath.string(),
            false);
        if (!loaded) {
            std::cerr << "[ProjectManager] Failed to load icon texture: " << iconPath << std::endl;
            continue;
        }

        m_IconResources.emplace(def.key, std::move(resource));
    }
}

ImTextureID ProjectManager::GetIcon(const std::string& iconKey) {
    auto it = m_IconResources.find(iconKey);
    if (it == m_IconResources.end()) {
        return ImTextureID(0);
    }

    IconResource& icon = it->second;
    if (icon.descriptor == VK_NULL_HANDLE) {
        if (!icon.texture.sampler || !icon.texture.imageView) {
            return ImTextureID(0);
        }
        VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
            icon.texture.sampler,
            icon.texture.imageView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (descriptor == VK_NULL_HANDLE) {
            std::cerr << "[ProjectManager] Failed to register icon with ImGui." << std::endl;
            return ImTextureID(0);
        }
        icon.descriptor = descriptor;
        icon.handle = static_cast<ImTextureID>(reinterpret_cast<uint64_t>(descriptor));
    }

    return icon.handle;
}

void ProjectManager::SaveCustomPaths() {
    namespace fs = std::filesystem;
    
#ifdef _WIN32
    // Save to AppData/Local on Windows for better persistence
    wchar_t* localAppData = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData) == S_OK) {
        fs::path configDir = fs::path(localAppData) / "HephaestusEngine";
        CoTaskMemFree(localAppData);
        
        std::error_code ec;
        fs::create_directories(configDir, ec);
        if (!ec) {
            fs::path configPath = configDir / "custom_projects.txt";
            
            try {
                std::ofstream file(configPath);
                if (file.is_open()) {
                    for (const auto& path : m_CustomProjectPaths) {
                        file << path.generic_string() << "\n";
                    }
                    return;
                }
            } catch (...) {}
        }
    }
#endif
    
    // Fallback: save next to executable
    fs::path exeDir = fs::current_path();
    fs::path configPath = exeDir / "custom_projects.txt";
    
    try {
        std::ofstream file(configPath);
        if (!file.is_open()) {
            return;
        }
        
        for (const auto& path : m_CustomProjectPaths) {
            file << path.generic_string() << "\n";
        }
    } catch (...) {
        // Silently fail
    }
}

void ProjectManager::LoadCustomPaths() {
    namespace fs = std::filesystem;
    
    m_CustomProjectPaths.clear();
    
    fs::path configPath;
    
#ifdef _WIN32
    // Try loading from AppData/Local on Windows
    wchar_t* localAppData = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData) == S_OK) {
        fs::path configDir = fs::path(localAppData) / "HephaestusEngine";
        CoTaskMemFree(localAppData);
        configPath = configDir / "custom_projects.txt";
        
        if (!fs::exists(configPath)) {
            configPath.clear();
        }
    }
#endif
    
    // Fallback: try loading from executable directory
    if (configPath.empty()) {
        configPath = fs::current_path() / "custom_projects.txt";
        if (!fs::exists(configPath)) {
            return;
        }
    }
    
    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            return;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            fs::path path(line);
            if (fs::exists(path) && fs::is_directory(path)) {
                m_CustomProjectPaths.push_back(path.lexically_normal());
            }
        }
    } catch (...) {
        // Silently fail
    }
}

void ProjectManager::DrawNewScriptDialog() {
    ImGui::SetNextWindowSize(ImVec2(500, 250), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
    
    if (ImGui::Begin("Create New C++ Script", &m_ShowNewScriptDialog, flags)) {
        if (!m_ScriptDialogProject) {
            ImGui::Text("Error: No project selected");
            if (ImGui::Button("Close", ImVec2(100, 30))) {
                m_ShowNewScriptDialog = false;
            }
            ImGui::End();
            return;
        }
        
        ImGui::Text("Project: %s", m_ScriptDialogProject->metadata.name.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::Text("Script Name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ScriptName", m_NewScriptNameBuffer, sizeof(m_NewScriptNameBuffer));
        
        ImGui::Text("(Will be created in project root with .h and .cpp files)");
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Buttons
        ImGui::PushStyleColor(ImGuiCol_Button, m_ColorSuccess);
        if (ImGui::Button("Create", ImVec2(100, 30))) {
            std::string scriptName(m_NewScriptNameBuffer);
            if (!scriptName.empty()) {
                if (CreateCppScript(m_ScriptDialogProject->path, scriptName)) {
                    m_StatusMessage = "Script '" + scriptName + "' created successfully!";
                    m_MessageTimer = 3.0f;
                    m_ShowNewScriptDialog = false;
                    memset(m_NewScriptNameBuffer, 0, sizeof(m_NewScriptNameBuffer));
                    m_ScriptDialogProject = nullptr;
                } else {
                    m_ErrorMessage = "Failed to create script '" + scriptName + "'";
                    m_MessageTimer = 3.0f;
                }
            } else {
                m_ErrorMessage = "Please enter a script name";
                m_MessageTimer = 2.0f;
            }
        }
        ImGui::PopStyleColor();
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 30))) {
            m_ShowNewScriptDialog = false;
            memset(m_NewScriptNameBuffer, 0, sizeof(m_NewScriptNameBuffer));
            m_ScriptDialogProject = nullptr;
        }
    }
    ImGui::End();
}

bool ProjectManager::CreateCppScript(const std::filesystem::path& projectPath, const std::string& scriptName) {
    try {
        // Create scripts directory if it doesn't exist
        std::filesystem::path scriptsDir = projectPath / "Scripts";
        std::filesystem::create_directories(scriptsDir);
        
        // Header file content
        std::string headerContent = R"(#pragma once
#include "engine/Scripting/ScriptBase.h"

class )" + scriptName + R"( : public ScriptBase {
public:
    )" + scriptName + R"((Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    virtual ~)" + scriptName + R"(() = default;

    void OnAwake() override {
        // Initialize the script
    }

    void OnUpdate(float deltaTime) override {
        // Main game logic - called every frame
        // deltaTime is time elapsed since last frame in seconds
    }

    void OnLateUpdate(float deltaTime) override {
        // Called after OnUpdate - useful for camera or post-processing logic
    }

    void OnEnable() override {
        // Called when the script is enabled
    }

    void OnDisable() override {
        // Called when the script is disabled
    }

    void OnDestroy() override {
        // Called when the script is about to be destroyed - cleanup here
    }

private:
    // Add your script members here
};
)";
        
        // Implementation file content
        std::string implContent = R"(#include ")" + scriptName + R"(.h"

// Add any implementation details here if needed
// You can use this file for out-of-line method definitions if your header gets complex
)";
        
        // Write header file
        std::filesystem::path headerPath = scriptsDir / (scriptName + ".h");
        std::ofstream headerFile(headerPath);
        if (!headerFile.is_open()) {
            return false;
        }
        headerFile << headerContent;
        headerFile.close();
        
        // Write implementation file
        std::filesystem::path implPath = scriptsDir / (scriptName + ".cpp");
        std::ofstream implFile(implPath);
        if (!implFile.is_open()) {
            return false;
        }
        implFile << implContent;
        implFile.close();
        
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace HephaestusEngine

#else

#include "ProjectManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <ctime>
#include <iomanip>

namespace {
    std::string EscapeJsonString(const std::string& str) {
        std::ostringstream escaped;
        for (char ch : str) {
            switch (ch) {
                case '"': escaped << "\\\""; break;
                case '\\': escaped << "\\\\"; break;
                case '\b': escaped << "\\b"; break;
                case '\f': escaped << "\\f"; break;
                case '\n': escaped << "\\n"; break;
                case '\r': escaped << "\\r"; break;
                case '\t': escaped << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20) {
                        escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)ch;
                    } else {
                        escaped << ch;
                    }
            }
        }
        return escaped.str();
    }

    std::string UnescapeJsonString(const std::string& str) {
        std::ostringstream out;
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '\\' && i + 1 < str.size()) {
                char next = str[i + 1];
                switch (next) {
                    case '"': out << '"'; ++i; break;
                    case '\\': out << '\\'; ++i; break;
                    case 'b': out << '\b'; ++i; break;
                    case 'f': out << '\f'; ++i; break;
                    case 'n': out << '\n'; ++i; break;
                    case 'r': out << '\r'; ++i; break;
                    case 't': out << '\t'; ++i; break;
                    default: out << str[i];
                }
            } else {
                out << str[i];
            }
        }
        return out.str();
    }

    bool FindJsonStringValue(const std::string& content, const std::string& key, std::string& out) {
        std::string pattern = "\"" + key + "\"";
        size_t keyPos = content.find(pattern);
        if (keyPos == std::string::npos) return false;
        size_t colonPos = content.find(':', keyPos + pattern.size());
        if (colonPos == std::string::npos) return false;
        size_t quoteStart = content.find('"', colonPos + 1);
        if (quoteStart == std::string::npos) return false;
        size_t pos = quoteStart + 1;
        size_t endPos = pos;
        while (endPos < content.size()) {
            if (content[endPos] == '"' && content[endPos - 1] != '\\') break;
            ++endPos;
        }
        if (endPos >= content.size()) return false;
        out = UnescapeJsonString(content.substr(pos, endPos - pos));
        return true;
    }

    bool FindJsonBoolValue(const std::string& content, const std::string& key, bool& out) {
        std::string pattern = "\"" + key + "\"";
        size_t keyPos = content.find(pattern);
        if (keyPos == std::string::npos) return false;
        size_t colonPos = content.find(':', keyPos + pattern.size());
        if (colonPos == std::string::npos) return false;
        size_t valueStart = content.find_first_not_of(" \t\n\r", colonPos + 1);
        if (valueStart == std::string::npos) return false;
        if (content.substr(valueStart, 4) == "true") { out = true; return true; }
        if (content.substr(valueStart, 5) == "false") { out = false; return true; }
        return false;
    }
}

namespace HephaestusEngine {

void ProjectManager::Initialize(::VulkanDevice* device) {
    m_Device = device;
    // Initialize templates
    m_Templates.clear();
    m_Templates.push_back({ "Empty Project", "A blank project with no content" });
    m_Templates.push_back({ "First Person", "A first-person template with basic controls" });
    m_Templates.push_back({ "Third Person", "A third-person template with camera setup" });
    
    // Load custom paths
    std::filesystem::path configPath = std::filesystem::current_path() / "editor" / "settings.ini";
    if (std::filesystem::exists(configPath)) {
        std::ifstream file(configPath);
        std::string line;
        while (std::getline(file, line)) {
            if (line.rfind("custom_path=", 0) == 0) {
                std::filesystem::path customPath = line.substr(12);
                if (!customPath.empty()) {
                    m_CustomProjectPaths.push_back(customPath);
                }
            }
        }
    }
}

void ProjectManager::Shutdown() {}

std::optional<std::filesystem::path> ProjectManager::Draw() {
    return std::nullopt;
}

void ProjectManager::SetProjectsRoot(const std::filesystem::path& root) {
    m_ProjectsRoot = root;
    RefreshProjects();
}

void ProjectManager::RefreshProjects() {
    m_AllProjects.clear();
    m_FilteredProjects.clear();
    
    std::cerr << "[DEBUG] RefreshProjects: m_ProjectsRoot = " << m_ProjectsRoot.string() << std::endl;
    
    if (!std::filesystem::exists(m_ProjectsRoot)) {
        std::cerr << "[DEBUG] Projects root does not exist, creating..." << std::endl;
        try {
            std::filesystem::create_directories(m_ProjectsRoot);
        } catch (...) {}
    }
    
    auto isValidHeProject = [](const std::filesystem::path& projectPath) -> bool {
        return std::filesystem::exists(projectPath / "project.heproject");
    };
    
    auto loadProjectInfo = [](const std::filesystem::path& projectPath) -> ProjectInfo {
        ProjectInfo info;
        info.path = projectPath;
        info.isValid = true;
        
        // Try to load metadata from project.heproject
        std::filesystem::path metaPath = projectPath / "project.heproject";
        if (std::filesystem::exists(metaPath)) {
            std::ifstream file(metaPath);
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            
            FindJsonStringValue(content, "name", info.metadata.name);
            FindJsonStringValue(content, "description", info.metadata.description);
            FindJsonStringValue(content, "engineVersion", info.metadata.engineVersion);
            FindJsonStringValue(content, "screenshot", info.metadata.screenshotPath);
            FindJsonBoolValue(content, "favorite", info.metadata.isFavorite);
        }
        
        // Default values
        if (info.metadata.name.empty()) {
            info.metadata.name = projectPath.filename().string();
        }
        if (info.metadata.description.empty()) {
            info.metadata.description = "No description available";
        }
        
        // Get file timestamps
        try {
            auto ftime = std::filesystem::last_write_time(projectPath);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            info.metadata.lastModified = sctp;
            info.metadata.created = sctp; // Use same for created if not available
        } catch (...) {
            info.metadata.lastModified = std::chrono::system_clock::now();
            info.metadata.created = std::chrono::system_clock::now();
        }
        
        return info;
    };
    
    // Scan for projects in the default root
    try {
        if (std::filesystem::exists(m_ProjectsRoot)) {
            std::cerr << "[DEBUG] Scanning projects root: " << m_ProjectsRoot << std::endl;
            for (const auto& entry : std::filesystem::directory_iterator(m_ProjectsRoot)) {
                if (!entry.is_directory()) continue;
                std::cerr << "[DEBUG]   Found directory: " << entry.path().filename().string();
                if (isValidHeProject(entry.path())) {
                    std::cerr << " (valid project)" << std::endl;
                    m_AllProjects.push_back(loadProjectInfo(entry.path()));
                } else {
                    std::cerr << " (not a valid project)" << std::endl;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[DEBUG] Exception scanning projects: " << e.what() << std::endl;
    }
    
    std::cerr << "[DEBUG] RefreshProjects: Found " << m_AllProjects.size() << " projects" << std::endl;
    
    // Scan custom project paths
    for (const auto& customPath : m_CustomProjectPaths) {
        try {
            if (std::filesystem::exists(customPath) && std::filesystem::is_directory(customPath)) {
                if (isValidHeProject(customPath)) {
                    // Check if already added
                    bool found = false;
                    for (const auto& proj : m_AllProjects) {
                        if (proj.path == customPath) { found = true; break; }
                    }
                    if (!found) {
                        m_AllProjects.push_back(loadProjectInfo(customPath));
                    }
                }
            }
        } catch (...) {}
    }
    
    // Rebuild filtered pointers (no filtering for now)
    m_FilteredProjects.clear();
    for (auto& proj : m_AllProjects) {
        m_FilteredProjects.push_back(&proj);
    }
}

void ProjectManager::AddCustomProjectPath(const std::filesystem::path& projectPath) {
    m_CustomProjectPaths.push_back(projectPath);
}

std::vector<std::filesystem::path> ProjectManager::FindLegacyProjects(const std::filesystem::path& /*searchRoot*/) const {
    return {};
}

bool ProjectManager::MigrateProjectToHeproject(const std::filesystem::path& /*projectPath*/) {
    return false;
}

std::string ProjectManager::BuildProjectManagerPayloadJson() const {
    std::ostringstream json;
    json << "\"root\":\"" << EscapeJsonString(m_ProjectsRoot.generic_string()) << "\",";

    json << "\"projects\":[";
    for (size_t i = 0; i < m_AllProjects.size(); ++i) {
        const ProjectInfo& project = m_AllProjects[i];
        const ProjectMetadata& meta = project.metadata;
        const std::string name = meta.name.empty() ? project.path.filename().string() : meta.name;
        const std::string description = meta.description.empty() ? "No description" : meta.description;

        auto createdTime = std::chrono::system_clock::to_time_t(meta.created);
        auto modifiedTime = std::chrono::system_clock::to_time_t(meta.lastModified);

        json << "{";
        json << "\"name\":\"" << EscapeJsonString(name) << "\",";
        json << "\"path\":\"" << EscapeJsonString(project.path.generic_string()) << "\",";
        json << "\"description\":\"" << EscapeJsonString(description) << "\",";
        json << "\"engineVersion\":\"" << EscapeJsonString(meta.engineVersion) << "\",";
        json << "\"created\":" << static_cast<long long>(createdTime) << ",";
        json << "\"lastModified\":" << static_cast<long long>(modifiedTime) << ",";
        json << "\"favorite\":" << (meta.isFavorite ? "true" : "false") << ",";
        json << "\"valid\":" << (project.isValid ? "true" : "false") << ",";
        json << "\"error\":\"" << EscapeJsonString(project.errorMessage) << "\",";
        json << "\"screenshot\":\"" << EscapeJsonString(meta.screenshotPath) << "\",";
        json << "\"tags\":[";
        for (size_t t = 0; t < meta.tags.size(); ++t) {
            json << "\"" << EscapeJsonString(meta.tags[t]) << "\"";
            if (t + 1 < meta.tags.size()) json << ",";
        }
        json << "]";
        json << "}";
        if (i + 1 < m_AllProjects.size()) json << ",";
    }
    json << "],";

    json << "\"templates\":[";
    for (size_t i = 0; i < m_Templates.size(); ++i) {
        const ProjectTemplate& tpl = m_Templates[i];
        json << "{";
        json << "\"index\":" << static_cast<int>(i) << ",";
        json << "\"name\":\"" << EscapeJsonString(tpl.name) << "\",";
        json << "\"description\":\"" << EscapeJsonString(tpl.description) << "\"";
        json << "}";
        if (i + 1 < m_Templates.size()) json << ",";
    }
    json << "]";
    return json.str();
}

bool ProjectManager::CreateProjectFromTemplate(const std::string& name, const std::string& description,
                                               int /*templateIndex*/, const std::string& customLocation) {
    std::filesystem::path projectDir;
    if (customLocation.empty()) {
        projectDir = m_ProjectsRoot / name;
    } else {
        projectDir = std::filesystem::path(customLocation) / name;
    }
    
    try {
        if (std::filesystem::exists(projectDir)) {
            return false; // Project already exists
        }
        std::filesystem::create_directories(projectDir);
        
        // Create project.heproject
        std::filesystem::path projectFile = projectDir / "project.heproject";
        std::ofstream file(projectFile);
        file << "{\n";
        file << "  \"name\": \"" << EscapeJsonString(name) << "\",\n";
        file << "  \"description\": \"" << EscapeJsonString(description) << "\",\n";
        file << "  \"engineVersion\": \"1.0.0\",\n";
        file << "  \"favorite\": false,\n";
        file << "  \"screenshot\": \"\",\n";
        file << "  \"tags\": []\n";
        file << "}\n";
        file.close();
        
        // Create basic folder structure
        std::filesystem::create_directories(projectDir / "assets");
        std::filesystem::create_directories(projectDir / "assets" / "meshes");
        std::filesystem::create_directories(projectDir / "assets" / "textures");
        std::filesystem::create_directories(projectDir / "assets" / "blueprints");
        std::filesystem::create_directories(projectDir / "scripts");
        
        RefreshProjects();
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjectManager::SetProjectFavorite(const std::filesystem::path& projectPath, bool isFavorite) {
    std::filesystem::path projectFile = projectPath / "project.heproject";
    if (!std::filesystem::exists(projectFile)) {
        return false;
    }
    
    try {
        // Read existing content
        std::ifstream inFile(projectFile);
        std::stringstream buffer;
        buffer << inFile.rdbuf();
        std::string content = buffer.str();
        inFile.close();
        
        // Simple replacement of favorite value
        size_t pos = content.find("\"favorite\"");
        if (pos != std::string::npos) {
            size_t colonPos = content.find(':', pos);
            if (colonPos != std::string::npos) {
                size_t valueStart = content.find_first_not_of(" \t", colonPos + 1);
                size_t valueEnd = content.find_first_of(",}\n", valueStart);
                std::string newContent = content.substr(0, valueStart) + 
                    (isFavorite ? "true" : "false") + content.substr(valueEnd);
                
                std::ofstream outFile(projectFile);
                outFile << newContent;
                outFile.close();
            }
        }
        
        // Update in-memory state
        for (auto& proj : m_AllProjects) {
            if (proj.path == projectPath) {
                proj.metadata.isFavorite = isFavorite;
                break;
            }
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<std::filesystem::path> ProjectManager::PickProjectFolder(const std::filesystem::path& /*initialPath*/) const {
    return std::nullopt;
}

void ProjectManager::OpenProjectLocationExternal(const std::filesystem::path& /*projectPath*/) {}

} // namespace HephaestusEngine

#endif

