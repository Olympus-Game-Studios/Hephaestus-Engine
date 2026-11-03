#pragma once

#include "Build/BuildManifest.h"
#include "Build/RuntimeLocator.h"

namespace Hephaestus::Build {

struct PackageStageResult {
    bool success = false;
    bool cancelled = false;
    std::filesystem::path outputPath;
    std::filesystem::path executablePath;
    std::vector<EditorProblem> problems;
    std::uintmax_t packageBytes = 0;
    std::vector<FileSizeEntry> largestFiles;
};

class PackageStager {
public:
    PackageStageResult stage(const BuildRequest& request,
                             const RuntimeArtifacts& runtime) const;
    PackageStageResult clean(const BuildRequest& request) const;

    static std::string sanitizeProductName(const std::string& productName);
    // Where game content goes in a package: <product>.app/Contents/Resources on macOS.
    static std::filesystem::path contentDirectory(const std::filesystem::path& package,
                                                  const std::string& product);
    // What players open: <product>.app on macOS, otherwise the executable.
    static std::filesystem::path launchPath(const std::filesystem::path& package,
                                            const std::string& product);
};

} // namespace Hephaestus::Build
