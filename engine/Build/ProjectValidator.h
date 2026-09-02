#pragma once

#include "Build/BuildTypes.h"
#include "Build/RuntimeLocator.h"

#include <optional>
#include <vector>

namespace Hephaestus::Build {

struct ProjectValidationOptions {
    std::string startupScene;
    std::optional<RuntimeArtifacts> runtimeArtifacts;
    bool includeUnusedAssetAdvisories = false;
};

class ProjectValidator {
public:
    std::vector<EditorProblem> validate(
        const std::filesystem::path& projectRoot,
        const ProjectValidationOptions& options = {}) const;

    std::vector<EditorProblem> findUnusedAssets(
        const std::filesystem::path& projectRoot,
        const std::string& startupScene = {}) const;

    static std::string determineStartupScene(const std::filesystem::path& projectRoot,
                                             std::vector<EditorProblem>* problems = nullptr);
};

} // namespace Hephaestus::Build
