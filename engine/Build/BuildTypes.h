#pragma once

#include "Build/EditorProblem.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Hephaestus::Build {

enum class BuildConfiguration {
    Development,
    Release
};

enum class BuildAction {
    Validate,
    Package,
    PackageAndRun,
    Clean
};

struct WindowSettings {
    std::string title;
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool resizable = true;
    bool vsync = true;
};

struct FileSizeEntry {
    std::string path;
    std::uintmax_t bytes = 0;
};

using LogCallback = std::function<void(const std::string&)>;
using ProgressCallback = std::function<void(float, const std::string&)>;
using CancellationCallback = std::function<bool()>;

struct BuildRequest {
    std::filesystem::path projectRoot;
    std::filesystem::path engineRoot;
    std::filesystem::path runtimeSearchRoot;
    std::filesystem::path outputRoot;
    std::string productName;
    std::string version = "0.1.0";
    std::string company;
    std::string engineVersion = "unknown";
    std::string startupScene;
    WindowSettings window;
    BuildConfiguration configuration = BuildConfiguration::Development;
    BuildAction action = BuildAction::Package;
    bool validateFirst = true;
    bool createZip = false;
    bool reportUnusedAssets = false;
    bool incremental = true;
    bool clean = false;
    std::filesystem::path applicationIcon;
    LogCallback onLog;
    ProgressCallback onProgress;
    CancellationCallback isCancelled;
};

struct BuildResult {
    bool success = false;
    bool cancelled = false;
    std::filesystem::path outputPath;
    std::filesystem::path executablePath;
    std::vector<EditorProblem> problems;
    std::vector<std::string> logSummary;
    std::uintmax_t packageBytes = 0;
    std::vector<FileSizeEntry> largestFiles;
};

inline const char* ToString(BuildConfiguration configuration) {
    return configuration == BuildConfiguration::Release ? "Release" : "Development";
}

} // namespace Hephaestus::Build
