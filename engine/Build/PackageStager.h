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

    static std::string sanitizeProductName(const std::string& productName);
};

} // namespace Hephaestus::Build
