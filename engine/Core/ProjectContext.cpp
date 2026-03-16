/**
 * @file ProjectContext.cpp
 * @brief Implementation of ProjectContext - shared project state.
 */

#include "ProjectContext.h"
#include "../Scene/Json.h"
#include "../Assets/AssetManager.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace Hephaestus {

ProjectContext::ProjectContext()
{
}

ProjectContext::~ProjectContext()
{
}

bool ProjectContext::LoadProject(const std::string& projectFilePath, std::string& outError)
{
    namespace fs = std::filesystem;

    // Validate file exists
    if (!fs::exists(projectFilePath)) {
        outError = "Project file not found: " + projectFilePath;
        return false;
    }

    // Read file content
    std::ifstream file(projectFilePath);
    if (!file.is_open()) {
        outError = "Failed to open project file: " + projectFilePath;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    // Parse JSON
    if (!parseProjectFile(content, outError)) {
        return false;
    }

    // Set paths
    m_projectFilePath = projectFilePath;
    fs::path projPath(projectFilePath);
    m_projectRootDir = projPath.parent_path().string();

    // Normalize paths (use forward slashes)
    std::replace(m_projectRootDir.begin(), m_projectRootDir.end(), '\\', '/');

    // Set asset root (convention: <projectRoot>/assets)
    fs::path assetPath = fs::path(m_projectRootDir) / "assets";
    m_assetRootDir = assetPath.string();
    std::replace(m_assetRootDir.begin(), m_assetRootDir.end(), '\\', '/');

    m_hasProject = true;

    // Add to recent projects
    AddToRecentProjects(projectFilePath);

    std::cout << "[ProjectContext] Loaded project: " << m_projectData.name 
              << " from " << m_projectFilePath << "\n";
    std::cout << "[ProjectContext] Project root: " << m_projectRootDir << "\n";
    std::cout << "[ProjectContext] Asset root: " << m_assetRootDir << "\n";

    // Update AssetManager with project roots
    auto& assetMgr = Hephaestus::AssetManager::GetInstance();
    assetMgr.SetProjectRoot(m_projectRootDir);
    assetMgr.SetAssetRoot(m_assetRootDir);
    assetMgr.Refresh();

    // Synchronise built-in engine assets into <projectRoot>/.engine/
    SyncEngineAssets();

    // Notify listeners
    if (m_onProjectChanged) {
        m_onProjectChanged();
    }

    return true;
}

void ProjectContext::CloseProject()
{
    if (!m_hasProject) {
        return;
    }

    std::cout << "[ProjectContext] Closing project: " << m_projectData.name << "\n";

    m_hasProject = false;
    m_projectFilePath.clear();
    m_projectRootDir.clear();
    m_assetRootDir.clear();
    m_projectData = ProjectData();

    // Clear AssetManager
    Hephaestus::AssetManager::GetInstance().Clear();

    // Notify listeners
    if (m_onProjectChanged) {
        m_onProjectChanged();
    }
}

bool ProjectContext::parseProjectFile(const std::string& content, std::string& outError)
{
    JsonParser parser(content);
    JsonValue root;

    if (!parser.Parse(root, outError)) {
        outError = "Failed to parse project JSON: " + outError;
        return false;
    }

    if (!root.IsObject()) {
        outError = "Project file root must be a JSON object";
        return false;
    }

    // Validate formatVersion
    const JsonValue* formatVersionVal = root.Find("formatVersion");
    if (!formatVersionVal || !formatVersionVal->IsNumber()) {
        outError = "Project file missing or invalid 'formatVersion'";
        return false;
    }
    m_projectData.formatVersion = static_cast<int>(formatVersionVal->AsNumber());

    // Only support formatVersion 1 for now
    if (m_projectData.formatVersion != 1) {
        outError = "Unsupported project format version: " + std::to_string(m_projectData.formatVersion);
        return false;
    }

    // Validate name
    const JsonValue* nameVal = root.Find("name");
    if (!nameVal || !nameVal->IsString() || nameVal->AsString().empty()) {
        outError = "Project file missing or empty 'name'";
        return false;
    }
    m_projectData.name = nameVal->AsString();

    // Optional fields
    const JsonValue* descVal = root.Find("description");
    if (descVal && descVal->IsString()) {
        m_projectData.description = descVal->AsString();
    }

    const JsonValue* engineVerVal = root.Find("engineVersion");
    if (engineVerVal && engineVerVal->IsString()) {
        m_projectData.engineVersion = engineVerVal->AsString();
    }

    const JsonValue* createdVal = root.Find("created");
    if (createdVal && createdVal->IsNumber()) {
        m_projectData.created = std::chrono::system_clock::from_time_t(
            static_cast<std::time_t>(createdVal->AsNumber()));
    }

    const JsonValue* lastModVal = root.Find("lastModified");
    if (lastModVal && lastModVal->IsNumber()) {
        m_projectData.lastModified = std::chrono::system_clock::from_time_t(
            static_cast<std::time_t>(lastModVal->AsNumber()));
    }

    const JsonValue* favVal = root.Find("isFavorite");
    if (favVal && favVal->IsBool()) {
        m_projectData.isFavorite = favVal->AsBool();
    }

    const JsonValue* screenshotVal = root.Find("screenshot");
    if (screenshotVal && screenshotVal->IsString()) {
        m_projectData.screenshotPath = screenshotVal->AsString();
    }

    const JsonValue* tagsVal = root.Find("tags");
    if (tagsVal && tagsVal->IsArray()) {
        m_projectData.tags.clear();
        for (const auto& tag : tagsVal->AsArray()) {
            if (tag.IsString()) {
                m_projectData.tags.push_back(tag.AsString());
            }
        }
    }

    // Optional: lastScene
    const JsonValue* lastSceneVal = root.Find("lastScene");
    if (lastSceneVal && lastSceneVal->IsString()) {
        m_projectData.lastScene = lastSceneVal->AsString();
    }

    return true;
}

void ProjectContext::AddToRecentProjects(const std::string& projectFilePath)
{
    namespace fs = std::filesystem;

    // Normalize path
    std::string normalizedPath = fs::absolute(projectFilePath).string();
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');

    // Remove if already in list
    auto it = std::find(m_recentProjects.begin(), m_recentProjects.end(), normalizedPath);
    if (it != m_recentProjects.end()) {
        m_recentProjects.erase(it);
    }

    // Add to front
    m_recentProjects.insert(m_recentProjects.begin(), normalizedPath);

    // Trim to max size
    if (m_recentProjects.size() > kMaxRecentProjects) {
        m_recentProjects.resize(kMaxRecentProjects);
    }

    // Auto-save
    SaveRecentProjectsToFile();
}

void ProjectContext::ClearRecentProjects()
{
    m_recentProjects.clear();
    SaveRecentProjectsToFile();
}

std::string ProjectContext::getDefaultSettingsPath() const
{
    namespace fs = std::filesystem;
    std::string basePath;

#ifdef _WIN32
    // Use %APPDATA%/Hephaestus
    wchar_t* appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        std::wstring wpath(appData);
        CoTaskMemFree(appData);
        basePath = fs::path(wpath).string();
    } else {
        // Fallback to environment variable
        const char* appDataEnv = std::getenv("APPDATA");
        if (appDataEnv) {
            basePath = appDataEnv;
        } else {
            basePath = ".";
        }
    }
#else
    // Use ~/.config/Hephaestus on Linux/Mac
    const char* home = std::getenv("HOME");
    if (home) {
        basePath = std::string(home) + "/.config";
    } else {
        basePath = ".";
    }
#endif

    fs::path settingsDir = fs::path(basePath) / "Hephaestus";
    
    // Create directory if it doesn't exist
    std::error_code ec;
    fs::create_directories(settingsDir, ec);

    return (settingsDir / "EditorSettings.json").string();
}

void ProjectContext::LoadRecentProjectsFromFile(const std::string& filePath)
{
    std::string path = filePath.empty() ? getDefaultSettingsPath() : filePath;

    std::ifstream file(path);
    if (!file.is_open()) {
        return; // No settings file yet, that's OK
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    JsonParser parser(content);
    JsonValue root;
    std::string error;

    if (!parser.Parse(root, error)) {
        std::cerr << "[ProjectContext] Failed to parse settings file: " << error << "\n";
        return;
    }

    if (!root.IsObject()) {
        return;
    }

    const JsonValue* recentVal = root.Find("recentProjects");
    if (recentVal && recentVal->IsArray()) {
        m_recentProjects.clear();
        for (const auto& item : recentVal->AsArray()) {
            if (item.IsString() && !item.AsString().empty()) {
                m_recentProjects.push_back(item.AsString());
            }
        }
    }

    std::cout << "[ProjectContext] Loaded " << m_recentProjects.size() << " recent projects\n";
}

void ProjectContext::SaveRecentProjectsToFile(const std::string& filePath) const
{
    std::string path = filePath.empty() ? getDefaultSettingsPath() : filePath;

    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "[ProjectContext] Failed to save settings to: " << path << "\n";
        return;
    }

    // Simple JSON output
    file << "{\n";
    file << "  \"recentProjects\": [\n";
    for (size_t i = 0; i < m_recentProjects.size(); ++i) {
        // Escape backslashes for JSON
        std::string escaped = m_recentProjects[i];
        std::string result;
        for (char c : escaped) {
            if (c == '\\') {
                result += "\\\\";
            } else if (c == '"') {
                result += "\\\"";
            } else {
                result += c;
            }
        }
        file << "    \"" << result << "\"";
        if (i < m_recentProjects.size() - 1) {
            file << ",";
        }
        file << "\n";
    }
    file << "  ]\n";
    file << "}\n";

    file.close();
}

// ─────────────────────────────────────────────────────────────────────────────
// Engine asset synchronisation
// ─────────────────────────────────────────────────────────────────────────────

static std::filesystem::path getExecutableDir()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) return {};

    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    return std::filesystem::path(buf.data()).parent_path();
#else
    // Linux: resolve executable path from /proc/self/exe
    char buf[4096]{};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return {};
    buf[len] = '\0';
    return std::filesystem::path(buf).parent_path();
#endif
}

std::string ProjectContext::GetEngineFolderDir() const
{
    if (!m_hasProject || m_projectRootDir.empty()) return {};
    namespace fs = std::filesystem;
    fs::path dir = fs::path(m_projectRootDir) / ".engine";
    std::string s = dir.string();
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

void ProjectContext::SyncEngineAssets()
{
    if (!m_hasProject || m_projectRootDir.empty()) return;

    namespace fs = std::filesystem;
    fs::path exeDir = getExecutableDir();
    fs::path srcMeshDir = exeDir / "assets" / "meshes";
    fs::path dstEngineDir = fs::path(m_projectRootDir) / ".engine";
    fs::path dstMeshDir = dstEngineDir / "meshes";

    // Create .engine/meshes/ if needed
    std::error_code ec;
    fs::create_directories(dstMeshDir, ec);
    if (ec) {
        std::cerr << "[ProjectContext] Failed to create .engine/meshes: " << ec.message() << "\n";
        return;
    }

    if (!fs::is_directory(srcMeshDir)) {
        std::cerr << "[ProjectContext] Engine mesh source not found: " << srcMeshDir << "\n";
        return;
    }

    int copied = 0, skipped = 0;
    for (const auto& entry : fs::directory_iterator(srcMeshDir, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".glb" && ext != ".gltf" && ext != ".obj" && ext != ".fbx") continue;

        fs::path dst = dstMeshDir / entry.path().filename();

        // Skip if destination already exists and is same size (fast check)
        if (fs::exists(dst) && fs::file_size(dst) == entry.file_size()) {
            ++skipped;
            continue;
        }

        fs::copy_file(entry.path(), dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "[ProjectContext] Failed to copy " << entry.path().filename()
                      << ": " << ec.message() << "\n";
        } else {
            ++copied;
        }
    }

    std::cout << "[ProjectContext] SyncEngineAssets: copied=" << copied
              << " skipped=" << skipped << " to " << dstMeshDir << "\n";
}

} // namespace Hephaestus
