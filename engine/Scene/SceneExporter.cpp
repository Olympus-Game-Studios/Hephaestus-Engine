#include "SceneExporter.h"
#include "Scene.h"
#include "Components.h"
#include "SceneSerializer.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <cstdlib>
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

    auto normalizePath = [](const fs::path& path) {
        if (path.empty()) {
            return path;
        }
        std::error_code normEc;
        fs::path normalized = fs::weakly_canonical(path, normEc);
        if (normEc) {
            return path.lexically_normal();
        }
        return normalized;
    };

    fs::path projectRoot = options.projectRootOverride.empty()
        ? fs::current_path()
        : options.projectRootOverride;
    projectRoot = normalizePath(projectRoot);

    if (options.projectRootOverride.empty() && !options.projectName.empty()) {
        fs::path potential = normalizePath(projectRoot / options.projectName);
        if (fs::exists(potential)) {
            projectRoot = potential;
        }
    }

    fs::path engineRoot = options.engineRootOverride.empty()
        ? fs::current_path()
        : options.engineRootOverride;
    engineRoot = normalizePath(engineRoot);

    std::cout << "[SceneExporter] Project root: " << projectRoot << std::endl;
    std::cout << "[SceneExporter] Engine root: " << engineRoot << std::endl;

    // Reserve final scene path (will serialize after asset copy to ensure freshest data)
    std::string sceneName = scene.GetName();
    if (sceneName.empty()) {
        sceneName = "ExportedScene";
    }
    fs::path scenePath = options.outputDirectory / "assets" / "scenes" / (sceneName + ".hpscene");

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
    
    // Step 3b: Copy scripts directory (Lua/runtime scripts) if present
    fs::path sourceScriptsDir = projectRoot / "scripts";
    fs::path destScriptsDir = options.outputDirectory / "scripts";
    if (fs::exists(sourceScriptsDir)) {
        std::cout << "[SceneExporter] Copying scripts directory: " << sourceScriptsDir << std::endl;
        for (const auto& entry : fs::recursive_directory_iterator(sourceScriptsDir)) {
            if (fs::is_regular_file(entry)) {
                fs::path relative = fs::relative(entry, sourceScriptsDir);
                fs::path dest = destScriptsDir / relative;
                fs::create_directories(dest.parent_path(), ec);
                if (!ec) {
                    fs::copy_file(entry, dest, fs::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        result.exportedAssets.push_back(dest);
                    } else {
                        std::cerr << "[SceneExporter] Warning: Failed to copy script "
                                  << entry.path() << ": " << ec.message() << std::endl;
                    }
                } else {
                    std::cerr << "[SceneExporter] Warning: Failed to create script directory "
                              << dest.parent_path() << ": " << ec.message() << std::endl;
                }
            }
        }
    }

    // After copying stock assets/scripts, serialize the up-to-date scene into the package
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

    // Also collect and report on explicitly-referenced assets (for logging)
    std::cout << "[SceneExporter] Scene references " << assetPaths.size() << " explicit assets" << std::endl;

    auto copyRequiredAssetIfMissing = [&](const fs::path& relativeAssetPath) {
        fs::path destPath = destAssetsDir / relativeAssetPath;
        if (fs::exists(destPath)) {
            return;
        }
        fs::path sourcePath = engineRoot / "assets" / relativeAssetPath;
        if (!fs::exists(sourcePath)) {
            std::cerr << "[SceneExporter] Warning: Fallback asset missing: " << sourcePath << std::endl;
            return;
        }
        std::error_code copyEc;
        fs::create_directories(destPath.parent_path(), copyEc);
        if (copyEc) {
            std::cerr << "[SceneExporter] Warning: Failed to create directory for fallback asset "
                      << destPath.parent_path() << ": " << copyEc.message() << std::endl;
            return;
        }
        fs::copy_file(sourcePath, destPath, fs::copy_options::overwrite_existing, copyEc);
        if (copyEc) {
            std::cerr << "[SceneExporter] Warning: Failed to copy fallback asset " << sourcePath
                      << ": " << copyEc.message() << std::endl;
            return;
        }
        result.exportedAssets.push_back(destPath);
        std::cout << "[SceneExporter] Added fallback asset: " << destPath << std::endl;
    };

    const std::vector<fs::path> requiredRuntimeAssets = {
        fs::path("textures") / "light_bulb.png",
        fs::path("textures") / "spotlight.png",
        fs::path("textures") / "sun.png"
    };
    for (const auto& rel : requiredRuntimeAssets) {
        copyRequiredAssetIfMissing(rel);
    }

    auto copyDirectoryRecursive = [&](const fs::path& sourceDir, const fs::path& destDir, const char* label) {
        if (!fs::exists(sourceDir)) {
            return;
        }
        std::error_code dirEc;
        for (const auto& entry : fs::recursive_directory_iterator(sourceDir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            fs::path relative = fs::relative(entry.path(), sourceDir, dirEc);
            if (dirEc) {
                std::cerr << "[SceneExporter] Warning: Failed to relativize path in " << label
                          << ": " << dirEc.message() << std::endl;
                dirEc.clear();
                continue;
            }
            fs::path destPath = destDir / relative;
            fs::create_directories(destPath.parent_path(), dirEc);
            if (dirEc) {
                std::cerr << "[SceneExporter] Warning: Failed to create directory for " << label
                          << " asset " << destPath.parent_path() << ": " << dirEc.message() << std::endl;
                dirEc.clear();
                continue;
            }
            fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing, dirEc);
            if (dirEc) {
                std::cerr << "[SceneExporter] Warning: Failed to copy " << label << " asset "
                          << entry.path() << ": " << dirEc.message() << std::endl;
                dirEc.clear();
                continue;
            }
            result.exportedAssets.push_back(destPath);
        }
        std::cout << "[SceneExporter] Copied " << label << " resources from " << sourceDir << std::endl;
    };

    copyDirectoryRecursive(engineRoot / "editor" / "Icons",
                           options.outputDirectory / "editor" / "Icons",
                           "editor icon");

    // Step 4: Copy runtime executable if found
    // Prefer HephaestusGame.exe (standalone game runtime) over HephaestusEngine.exe
    std::vector<fs::path> exeSearchPaths = {
        engineRoot / "build" / "Release" / "HephaestusGame.exe",
        engineRoot / "build" / "Debug" / "HephaestusGame.exe",
        engineRoot / "HephaestusGame.exe",
        engineRoot / "build" / "Release" / "HephaestusEngine.exe",
        engineRoot / "build" / "Debug" / "HephaestusEngine.exe",
        engineRoot / "HephaestusEngine.exe"
    };

    bool exeCopied = false;
    std::string copiedExeName;
    for (const auto& exePath : exeSearchPaths) {
        if (fs::exists(exePath)) {
            // Rename to project name for cleaner distribution
            std::string targetName = options.projectName.empty() ? 
                exePath.filename().string() : 
                (options.projectName + ".exe");
            fs::path destExe = options.outputDirectory / targetName;
            fs::copy_file(exePath, destExe, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                result.exportedAssets.push_back(destExe);
                copiedExeName = targetName;
                std::cout << "[SceneExporter] Copied runtime executable: " << exePath.filename() 
                          << " as " << targetName << std::endl;
                exeCopied = true;
                
                // Create runtime launcher batch script
                fs::path launcherPath = options.outputDirectory / "RunGame.bat";
                std::ofstream launcher(launcherPath);
                if (launcher) {
                    launcher << "@echo off\n";
                    launcher << "title " << options.projectName << "\n";
                    launcher << "echo Starting " << options.projectName << "...\n";
                    launcher << "setlocal\n";
                    launcher << "cd /d \"%~dp0\"\n";
                    launcher << "\"" << copiedExeName << "\"";
                    if (!relativeScenePath.empty()) {
                        launcher << " --scene \"" << relativeScenePath.generic_string() << "\"";
                    }
                    launcher << "\n";
                    launcher << "if %ERRORLEVEL% NEQ 0 (\n";
                    launcher << "    echo.\n";
                    launcher << "    echo ================================================================\n";
                    launcher << "    echo  Game exited with error code %ERRORLEVEL%\n";
                    launcher << "    echo  Check logs/runtime.log for details.\n";
                    launcher << "    echo ================================================================\n";
                    launcher << "    pause\n";
                    launcher << ")\n";
                    launcher.close();
                    result.exportedAssets.push_back(launcherPath);
                    std::cout << "[SceneExporter] Created launcher script: RunGame.bat" << std::endl;
                }

                // Create game.ini configuration file
                fs::path gameIniPath = options.outputDirectory / "game.ini";
                std::ofstream gameIni(gameIniPath);
                if (gameIni) {
                    gameIni << "# Hephaestus Game Configuration\n";
                    gameIni << "# Auto-generated during export\n\n";
                    gameIni << "[Game]\n";
                    gameIni << "project_name = " << options.projectName << "\n";
                    gameIni << "window_title = " << options.projectName << "\n";
                    if (!relativeScenePath.empty()) {
                        gameIni << "scene = " << relativeScenePath.generic_string() << "\n";
                    }
                    gameIni << "\n[Display]\n";
                    gameIni << "window_width = 1280\n";
                    gameIni << "window_height = 720\n";
                    gameIni << "fullscreen = false\n";
                    gameIni << "vsync = true\n";
                    gameIni.close();
                    result.exportedAssets.push_back(gameIniPath);
                    std::cout << "[SceneExporter] Created game.ini configuration" << std::endl;
                }
                
                break;
            }
        }
    }
    
    if (!exeCopied) {
        std::cerr << "[SceneExporter] Warning: Runtime executable not found in expected locations" << std::endl;
    }

    // Step 4b: Copy runtime dependencies (DLLs)
    // Lua DLLs
    std::vector<std::pair<std::string, std::vector<fs::path>>> dependencies = {
        {"lua.dll", {
            engineRoot / "build" / "Release" / "lua.dll",
            engineRoot / "build" / "Debug" / "lua.dll",
            engineRoot / "lua.dll"
        }},
        {"lua54.dll", {
            engineRoot / "build" / "Release" / "lua54.dll",
            engineRoot / "build" / "Debug" / "lua54.dll",
            engineRoot / "lua54.dll"
        }},
        {"lua53.dll", {
            engineRoot / "build" / "Release" / "lua53.dll",
            engineRoot / "build" / "Debug" / "lua53.dll",
            engineRoot / "lua53.dll"
        }}
    };
    
    // Also check vcpkg paths
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        fs::path vcpkgBin = fs::path(userProfile) / "vcpkg" / "installed" / "x64-windows" / "bin";
        if (fs::exists(vcpkgBin / "lua.dll")) {
            dependencies.insert(dependencies.begin(), {"lua.dll", {vcpkgBin / "lua.dll"}});
        }
    }

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
        // Prefer compiled shaders from build output over source shaders
        std::vector<fs::path> shaderSearchPaths = {
            engineRoot / "build" / "Release" / "shaders",
            engineRoot / "build" / "Debug" / "shaders",
            projectRoot / "shaders",
            engineRoot / "shaders"
        };

        fs::path shadersSource;
        for (const auto& candidate : shaderSearchPaths) {
            if (fs::exists(candidate)) {
                shadersSource = candidate;
                std::cout << "[SceneExporter] Using shaders from: " << shadersSource << std::endl;
                break;
            }
        }

        fs::path shadersDest = options.outputDirectory / "shaders";

        if (!shadersSource.empty() && fs::exists(shadersSource)) {
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
        else {
            std::cerr << "[SceneExporter] Warning: No shaders directory found to export." << std::endl;
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
