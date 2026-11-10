#include "Build/BuildService.h"

#include <algorithm>
#include <cstdlib>

namespace Hephaestus::Build {
namespace {

bool HasErrors(const std::vector<EditorProblem>& problems) {
    return std::any_of(problems.begin(), problems.end(), [](const EditorProblem& problem) {
        return problem.severity == ProblemSeverity::Error;
    });
}

bool HasDevelopmentBlockingErrors(const std::vector<EditorProblem>& problems) {
    return std::any_of(problems.begin(), problems.end(), [](const EditorProblem& problem) {
        return problem.severity == ProblemSeverity::Error &&
               (problem.code.rfind("STARTUP_SCENE_", 0) == 0 ||
                problem.code == "PROJECT_ROOT_MISSING");
    });
}

bool Cancelled(const BuildRequest& request) {
    return request.isCancelled && request.isCancelled();
}

} // namespace

BuildService::BuildService(std::shared_ptr<RuntimeBuilder> runtimeBuilder)
    : m_runtimeBuilder(std::move(runtimeBuilder)) {}

BuildResult BuildService::execute(const BuildRequest& originalRequest) const {
    BuildResult result;
    BuildRequest request = originalRequest;
    const auto log = [&](const std::string& message) {
        result.logSummary.push_back(message);
        if (request.onLog) {
            request.onLog(message);
        }
    };
    const auto progress = [&](float value, const std::string& message) {
        if (request.onProgress) {
            request.onProgress(value, message);
        }
        log(message);
    };

    if (request.projectRoot.empty() ||
        (request.action != BuildAction::Validate && request.outputRoot.empty())) {
        result.problems.push_back({ProblemSeverity::Error, "BUILD_REQUEST_INVALID",
                                   request.projectRoot.empty()
                                       ? "projectRoot is required"
                                       : "outputRoot is required for packaging"});
        return result;
    }
    if (request.productName.empty()) {
        request.productName = request.projectRoot.filename().string();
    }
    if (request.enableRuntimeAutomation &&
        request.configuration == BuildConfiguration::Release) {
        result.problems.push_back({ProblemSeverity::Error, "BUILD_RUNTIME_AUTOMATION_NOT_ALLOWED",
                                   "Runtime automation cannot be enabled in a Release package",
                                   "Release builds have MCP automation compiled out at build time. "
                                   "Use a Development package to enable runtime automation."});
        return result;
    }
    if (request.action == BuildAction::Clean) {
        progress(0.0F, "Cleaning package");
        PackageStager stager;
        auto cleaned = stager.clean(request);
        result.success = cleaned.success;
        result.outputPath = cleaned.outputPath;
        result.problems = std::move(cleaned.problems);
        progress(1.0F, result.success ? "Package cleaned" : "Package clean failed");
        return result;
    }
    if (request.startupScene.empty()) {
        request.startupScene = ProjectValidator::determineStartupScene(request.projectRoot);
    }
    if (Cancelled(request)) {
        result.cancelled = true;
        return result;
    }

    ProjectValidator validator;
    if (request.validateFirst || request.action == BuildAction::Validate) {
        progress(0.1F, "Validating project");
        ProjectValidationOptions validationOptions;
        validationOptions.startupScene = request.startupScene;
        validationOptions.includeUnusedAssetAdvisories = request.reportUnusedAssets;
        result.problems = validator.validate(request.projectRoot, validationOptions);
        if (request.action == BuildAction::Validate) {
            result.success = !HasErrors(result.problems);
            progress(1.0F, result.success ? "Validation completed" : "Validation failed");
            return result;
        }
        if (request.configuration == BuildConfiguration::Release && HasErrors(result.problems)) {
            log("Release packaging stopped because validation reported errors");
            return result;
        }
        if (request.configuration == BuildConfiguration::Development &&
            HasDevelopmentBlockingErrors(result.problems)) {
            log("Development packaging stopped because the project cannot start");
            return result;
        }
    }

    if (Cancelled(request)) {
        result.cancelled = true;
        return result;
    }
    progress(0.3F, "Locating runtime artifacts");
    RuntimeLocator locator;
    auto located = locator.locate(request.engineRoot, request.runtimeSearchRoot,
                                  request.configuration, request.enableRuntimeAutomation);
    const bool iconRequiresRebuild = !request.applicationIcon.empty();
    const bool requiresRuntimeBuild = iconRequiresRebuild || request.enableRuntimeAutomation;
    if ((!located.success() || requiresRuntimeBuild) && m_runtimeBuilder) {
        log(iconRequiresRebuild
                ? "Application icon requested; rebuilding the game runtime"
                : (request.enableRuntimeAutomation
                       ? "Runtime automation requested; rebuilding the game runtime"
                       : "Runtime artifacts are incomplete; attempting runtime build"));
        const LogCallback builderLog = [&](const std::string& message) { log(message); };
        if (m_runtimeBuilder->build(request.engineRoot, request.configuration,
                                    request.applicationIcon,
                                    builderLog, request.isCancelled,
                                    request.enableRuntimeAutomation)) {
            located = locator.locate(request.engineRoot, request.runtimeSearchRoot,
                                     request.configuration, request.enableRuntimeAutomation);
        } else if (Cancelled(request)) {
            result.cancelled = true;
            return result;
        } else if (iconRequiresRebuild) {
            const CancellationCallback neverCancel = []() { return false; };
            m_runtimeBuilder->build(request.engineRoot, request.configuration, {},
                                    log, neverCancel, false);
            result.problems.push_back(
                {ProblemSeverity::Error, "BUILD_ICON_REBUILD_FAILED",
                 "The application icon requires rebuilding HephaestusGame",
                 "Use an existing .ico file and ensure CMake/MSVC are available."});
            return result;
        }
    }
    if (!located.success()) {
        if (iconRequiresRebuild && m_runtimeBuilder) {
            const CancellationCallback neverCancel = []() { return false; };
            m_runtimeBuilder->build(request.engineRoot, request.configuration, {},
                                    log, neverCancel, false);
        }
        result.problems.insert(result.problems.end(), located.problems.begin(),
                               located.problems.end());
        log("Packaging stopped because runtime prerequisites are missing");
        return result;
    }
    for (const auto& problem : located.problems) {
        if (problem.severity != ProblemSeverity::Error) {
            result.problems.push_back(problem);
        }
    }

    progress(0.55F, "Staging package");
    PackageStager stager;
    auto staged = stager.stage(request, located.artifacts);
    result.cancelled = staged.cancelled;
    result.outputPath = staged.outputPath;
    result.executablePath = staged.executablePath;
    result.packageBytes = staged.packageBytes;
    result.largestFiles = std::move(staged.largestFiles);
    result.problems.insert(result.problems.end(), staged.problems.begin(), staged.problems.end());
    if (iconRequiresRebuild && m_runtimeBuilder) {
        log("Restoring the shared game runtime after icon-specific packaging");
        const CancellationCallback neverCancel = []() { return false; };
        if (!m_runtimeBuilder->build(request.engineRoot, request.configuration, {},
                                     log, neverCancel, false)) {
            result.problems.push_back(
                {ProblemSeverity::Warning, "BUILD_ICON_RUNTIME_RESTORE_FAILED",
                 "The package was created, but the shared runtime icon could not be restored",
                 "Rebuild HephaestusGame before packaging a different project."});
        }
    }
    if (!staged.success) {
        log(staged.cancelled ? "Packaging cancelled" : "Packaging failed");
        return result;
    }

    if (request.action == BuildAction::PackageAndRun) {
        progress(0.95F, "Launching packaged game");
#ifdef _WIN32
        const std::string command = "start \"\" \"" + result.executablePath.string() + "\"";
        const bool launched = std::system(command.c_str()) == 0;
#elif defined(__APPLE__)
        const bool launched = RunQuietly({"open", result.executablePath.string()}) == 0;
#else
        const std::string command = "\"" + result.executablePath.string() + "\" &";
        const bool launched = std::system(command.c_str()) == 0;
#endif
        if (!launched) {
            result.problems.push_back({ProblemSeverity::Warning, "BUILD_LAUNCH_FAILED",
                                       "The package succeeded but the game could not be launched",
                                       result.executablePath.generic_string()});
        }
    }

    result.success = true;
    progress(1.0F, "Package completed");
    return result;
}

} // namespace Hephaestus::Build
