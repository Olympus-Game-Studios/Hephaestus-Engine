#include "SceneExporter.h"
#include "Scene.h"
#include "Components.h"
#include "SceneSerializer.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// Helper to resolve asset paths relative to project root
fs::path ResolveAssetPath(const std::string& assetPath, const fs::path& projectRoot)
{
    if (assetPath.empty()) {
        return {};
    }

    // Skip built-in resources
    if (assetPath.find("builtin://") == 0) {
        return {};
    }

    // Try to resolve as absolute path first
    fs::path path(assetPath);
    if (path.is_absolute() && fs::exists(path)) {
        std::cout << "[SceneExporter] Resolved (absolute): " << path << std::endl;
        return path;
    }

    // Try relative to project root
    fs::path relative = projectRoot / assetPath;
    if (fs::exists(relative)) {
        std::cout << "[SceneExporter] Resolved (project root): " << relative << std::endl;
        return relative;
    }

    // Try relative to assets directory
    fs::path assetsRelative = projectRoot / "assets" / assetPath;
    if (fs::exists(assetsRelative)) {
        std::cout << "[SceneExporter] Resolved (assets/): " << assetsRelative << std::endl;
        return assetsRelative;
    }

    // Try relative to scripts directory
    fs::path scriptsRelative = projectRoot / "scripts" / assetPath;
    if (fs::exists(scriptsRelative)) {
        std::cout << "[SceneExporter] Resolved (scripts/): " << scriptsRelative << std::endl;
        return scriptsRelative;
    }

    // Try just the filename in scripts directory
    if (!path.empty() && path.has_filename()) {
        fs::path scriptsByName = projectRoot / "scripts" / path.filename();
        if (fs::exists(scriptsByName)) {
            std::cout << "[SceneExporter] Resolved (scripts/ by filename): " << scriptsByName << std::endl;
            return scriptsByName;
        }
    }

    std::cerr << "[SceneExporter] Failed to resolve: " << assetPath << std::endl;
    return {};
}

// Get current timestamp as string
std::string GetTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

} // anonymous namespace

std::vector<fs::path> SceneExporter::CollectAssetPaths(const Scene& scene)
{
    std::set<fs::path> uniquePaths; // Use set to avoid duplicates
    
    const entt::registry& registry = scene.GetRegistry();

    // Collect mesh asset paths
    auto meshView = registry.view<MeshRendererComponent>();
    for (auto entity : meshView) {
        const auto& mesh = meshView.get<MeshRendererComponent>(entity);
        if (!mesh.meshAssetPath.empty()) {
            uniquePaths.insert(mesh.meshAssetPath);
        }
    }

    // Collect texture and material paths
    auto materialView = registry.view<MaterialComponent>();
    for (auto entity : materialView) {
        const auto& material = materialView.get<MaterialComponent>(entity);
        
        if (!material.albedoTexturePath.empty()) {
            uniquePaths.insert(material.albedoTexturePath);
        }
        if (!material.normalTexturePath.empty()) {
            uniquePaths.insert(material.normalTexturePath);
        }
        if (!material.roughnessTexturePath.empty()) {
            uniquePaths.insert(material.roughnessTexturePath);
        }
        if (!material.aoTexturePath.empty()) {
            uniquePaths.insert(material.aoTexturePath);
        }
    }

    // Collect script paths
    auto scriptView = registry.view<LuaScriptComponent>();
    for (auto entity : scriptView) {
        const auto& script = scriptView.get<LuaScriptComponent>(entity);
        if (!script.scriptPath.empty()) {
            std::cout << "[SceneExporter] Found Lua script: " << script.scriptPath << std::endl;
            uniquePaths.insert(script.scriptPath);
        }
    }

    // Convert set to vector
    std::vector<fs::path> result;
    result.reserve(uniquePaths.size());
    for (const auto& path : uniquePaths) {
        result.push_back(path);
    }

    return result;
}

bool SceneExporter::CopyAsset(const fs::path& sourcePath,
                              const fs::path& outputDir,
                              const fs::path& projectRoot,
                              std::string& outError)
{
    if (!fs::exists(sourcePath)) {
        outError = "Source file does not exist: " + sourcePath.string();
        return false;
    }

    // Calculate relative path from project root
    fs::path relativePath;
    std::error_code ec;
    
    if (sourcePath.string().find(projectRoot.string()) == 0) {
        relativePath = fs::relative(sourcePath, projectRoot, ec);
        if (ec) {
            // Fallback to just the filename
            relativePath = sourcePath.filename();
        }
    } else {
        // If not under project root, just use filename
        relativePath = sourcePath.filename();
    }

    // Construct destination path
    fs::path destPath = outputDir / relativePath;

    // Create destination directories
    fs::create_directories(destPath.parent_path(), ec);
    if (ec) {
        outError = "Failed to create directory: " + ec.message();
        return false;
    }

    // Copy the file
    fs::copy_file(sourcePath, destPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        outError = "Failed to copy file: " + ec.message();
        return false;
    }

    return true;
}

bool SceneExporter::CreateManifest(const fs::path& manifestPath,
                                   const std::string& projectName,
                                   const std::vector<fs::path>& assets,
                                   std::string& outError)
{
    std::ofstream out(manifestPath);
    if (!out) {
        outError = "Failed to create manifest file: " + manifestPath.string();
        return false;
    }

    // Write manifest header
    out << "# Hephaestus Engine Export Manifest\n";
    out << "# Generated: " << GetTimestamp() << "\n";
    out << "\n";
    out << "Project: " << projectName << "\n";
    out << "Engine Version: 1.0.0-dev\n";
    out << "\n";
    out << "# Exported Assets (" << assets.size() << " files)\n";
    out << "# ----------------------------------------\n";

    // List all exported assets
    for (const auto& asset : assets) {
        out << asset.generic_string() << "\n";
    }

    out << "\n";
    out << "# Runtime Requirements\n";
    out << "# ----------------------------------------\n";
    out << "# - HephaestusEngine.exe (or runtime executable)\n";
    out << "# - shaders/ directory with compiled shaders\n";
    out << "# - Vulkan runtime libraries\n";
    out << "# - All assets listed above\n";

    out.close();
    return true;
}

SceneExporter::ExportResult SceneExporter::Export(const Scene& scene, const ExportOptions& options)
{
    ExportResult result;
    std::error_code ec;

    // Validate output directory
    if (options.outputDirectory.empty()) {
        result.errorMessage = "Output directory not specified";
        return result;
    }

    // Create output directory
    fs::create_directories(options.outputDirectory, ec);
    if (ec) {
        result.errorMessage = "Failed to create output directory: " + ec.message();
        return result;
    }

    std::cout << "[SceneExporter] Exporting to: " << options.outputDirectory << std::endl;

    // Determine project root (current working directory or specified)
    fs::path projectRoot = fs::current_path();
    if (!options.projectName.empty()) {
        // Try to find project root
        fs::path potential = projectRoot / options.projectName;
        if (fs::exists(potential)) {
            projectRoot = potential;
        }
    }

    // Step 1: Serialize and save the scene
    std::string sceneName = scene.GetName();
    if (sceneName.empty()) {
        sceneName = "ExportedScene";
    }
    
    fs::path scenePath = options.outputDirectory / "assets" / "scenes" / (sceneName + ".hpscene");
    fs::create_directories(scenePath.parent_path(), ec);
    if (ec) {
        result.errorMessage = "Failed to create scenes directory: " + ec.message();
        return result;
    }

    SceneSerializer serializer(scene);
    std::string serializeError;
    if (!serializer.Serialize(scenePath, serializeError)) {
        result.errorMessage = "Failed to serialize scene: " + serializeError;
        return result;
    }

    result.exportedAssets.push_back(scenePath);
    std::cout << "[SceneExporter] Exported scene: " << scenePath << std::endl;

    fs::path relativeScenePath = scenePath.lexically_relative(options.outputDirectory);

    // Step 2: Collect all asset references
    std::vector<fs::path> assetPaths = CollectAssetPaths(scene);
    std::cout << "[SceneExporter] Found " << assetPaths.size() << " asset references" << std::endl;

    // Step 3: Copy entire assets directory to output
    fs::path sourceAssetsDir = projectRoot / "assets";
    fs::path destAssetsDir = options.outputDirectory / "assets";
    
    if (fs::exists(sourceAssetsDir)) {
        std::cout << "[SceneExporter] Copying entire assets directory: " << sourceAssetsDir << std::endl;
        
        // Copy all assets recursively
        for (const auto& entry : fs::recursive_directory_iterator(sourceAssetsDir)) {
            if (fs::is_regular_file(entry)) {
                fs::path relative = fs::relative(entry, sourceAssetsDir);
                fs::path dest = destAssetsDir / relative;
                fs::create_directories(dest.parent_path(), ec);
                if (!ec) {
                    fs::copy_file(entry, dest, fs::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        result.exportedAssets.push_back(dest);
                    } else {
                        std::cerr << "[SceneExporter] Warning: Failed to copy " << entry << ": " << ec.message() << std::endl;
                    }
                } else {
                    std::cerr << "[SceneExporter] Warning: Failed to create directory " << dest.parent_path() << ": " << ec.message() << std::endl;
                }
            }
        }
        std::cout << "[SceneExporter] Completed copying assets directory" << std::endl;
    } else {
        std::cerr << "[SceneExporter] Warning: assets directory not found at " << sourceAssetsDir << std::endl;
    }
    
    // Also collect and report on explicitly-referenced assets (for logging)
    std::cout << "[SceneExporter] Scene references " << assetPaths.size() << " explicit assets" << std::endl;

    // Step 4: Copy runtime executable if found
    std::vector<fs::path> exeSearchPaths = {
        projectRoot / "build" / "Release" / "HephaestusEngine.exe",
        projectRoot / "build" / "Debug" / "HephaestusEngine.exe",
        projectRoot / "HephaestusEngine.exe"
    };

    bool exeCopied = false;
    for (const auto& exePath : exeSearchPaths) {
        if (fs::exists(exePath)) {
            fs::path destExe = options.outputDirectory / exePath.filename();
            fs::copy_file(exePath, destExe, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                result.exportedAssets.push_back(destExe);
                std::cout << "[SceneExporter] Copied runtime executable: " << exePath.filename() << std::endl;
                exeCopied = true;
                
                // Create runtime launcher batch script
                fs::path launcherPath = options.outputDirectory / "RunGame.bat";
                std::ofstream launcher(launcherPath);
                if (launcher) {
                    launcher << "@echo off\n";
                    launcher << "echo Starting " << options.projectName << "...\n";
                    launcher << "setlocal\n";
                    launcher << "cd /d %~dp0\n";
                    launcher << "HephaestusEngine.exe --runtime";
                    if (!relativeScenePath.empty()) {
                        launcher << " --scene \"" << relativeScenePath.generic_string() << "\"";
                    }
                    launcher << "\n";
                    launcher << "if %ERRORLEVEL% NEQ 0 (\n";
                    launcher << "    echo.\n";
                    launcher << "    echo Game exited with error code %ERRORLEVEL%\n";
                    launcher << "    pause\n";
                    launcher << ")\n";
                    launcher.close();
                    result.exportedAssets.push_back(launcherPath);
                    std::cout << "[SceneExporter] Created launcher script: RunGame.bat" << std::endl;
                }
                
                break;
            }
        }
    }
    
    if (!exeCopied) {
        std::cerr << "[SceneExporter] Warning: Runtime executable not found in expected locations" << std::endl;
    }

    // Step 4b: Copy runtime dependencies (DLLs)
    std::vector<std::pair<std::string, std::vector<fs::path>>> dependencies = {
        {"lua54.dll", {
            projectRoot / "build" / "Release" / "lua54.dll",
            projectRoot / "build" / "Debug" / "lua54.dll",
            projectRoot / "lua54.dll"
        }},
        {"lua53.dll", {
            projectRoot / "build" / "Release" / "lua53.dll",
            projectRoot / "build" / "Debug" / "lua53.dll",
            projectRoot / "lua53.dll"
        }}
    };

    for (const auto& [dllName, searchPaths] : dependencies) {
        bool found = false;
        for (const auto& dllPath : searchPaths) {
            if (fs::exists(dllPath)) {
                fs::path destDll = options.outputDirectory / dllPath.filename();
                fs::copy_file(dllPath, destDll, fs::copy_options::overwrite_existing, ec);
                if (!ec) {
                    result.exportedAssets.push_back(destDll);
                    std::cout << "[SceneExporter] Copied dependency: " << dllPath.filename() << std::endl;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            std::cerr << "[SceneExporter] Warning: " << dllName << " not found in expected locations" << std::endl;
        }
    }

    // Step 5: Copy shaders if requested
    if (options.includeShaders) {
        fs::path shadersSource = projectRoot / "shaders";
        fs::path shadersDest = options.outputDirectory / "shaders";

        if (fs::exists(shadersSource)) {
            fs::create_directories(shadersDest, ec);
            if (!ec) {
                int shaderCount = 0;
                // Copy all shader files
                for (const auto& entry : fs::recursive_directory_iterator(shadersSource, ec)) {
                    if (entry.is_regular_file()) {
                        fs::path relativePath = fs::relative(entry.path(), shadersSource, ec);
                        fs::path destPath = shadersDest / relativePath;
                        
                        fs::create_directories(destPath.parent_path(), ec);
                        fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing, ec);
                        
                        if (!ec) {
                            result.exportedAssets.push_back(destPath);
                            shaderCount++;
                        }
                    }
                }
                std::cout << "[SceneExporter] Copied " << shaderCount << " shader files" << std::endl;
            }
        }
    }

    // Step 6: Create manifest if requested
    if (options.createManifest) {
        fs::path manifestPath = options.outputDirectory / "export_manifest.txt";
        std::string manifestError;
        
        if (CreateManifest(manifestPath, options.projectName, result.exportedAssets, manifestError)) {
            result.manifestPath = manifestPath;
            std::cout << "[SceneExporter] Created manifest: " << manifestPath << std::endl;
        } else {
            std::cerr << "[SceneExporter] Warning: " << manifestError << std::endl;
        }
    }

    result.success = true;
    std::cout << "[SceneExporter] Export completed successfully!" << std::endl;
    std::cout << "[SceneExporter] Total files exported: " << result.exportedAssets.size() << std::endl;

    return result;
}
