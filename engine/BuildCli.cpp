#include "Build/BuildService.h"
#include "AppConfig.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void Usage() {
    std::cout
        << "Usage: HephaestusBuild --project <path> [options]\n"
        << "  --config Development|Release\n"
        << "  --output <path>\n"
        << "  --engine-root <path>\n"
        << "  --icon <path-to-ico>\n"
        << "  --validate-only\n"
        << "  --clean              Rebuild package without incremental reuse\n"
        << "  --clean-output       Remove the current package and staging data\n"
        << "  --unused-report\n"
        << "  --zip\n";
}

std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (length > 0) {
        path.resize(length);
        return std::filesystem::path(path).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

void LoadProjectMetadata(Hephaestus::Build::BuildRequest& request) {
    std::ifstream stream(request.projectRoot / "project.heproject");
    if (!stream) {
        return;
    }
    try {
        nlohmann::json root;
        stream >> root;
        request.productName = root.value("name", request.projectRoot.filename().string());
        request.version = root.value("version", request.version);
        request.company = root.value("companyName", std::string{});
    } catch (const nlohmann::json::exception&) {
        // ProjectValidator reports the actionable parse error.
    }
}

const char* Severity(Hephaestus::Build::ProblemSeverity severity) {
    using Hephaestus::Build::ProblemSeverity;
    switch (severity) {
    case ProblemSeverity::Error: return "error";
    case ProblemSeverity::Warning: return "warning";
    case ProblemSeverity::Info: return "info";
    }
    return "info";
}

} // namespace

int main(int argc, char** argv) {
    using namespace Hephaestus::Build;
    BuildRequest request;
    request.engineVersion = HEPHAESTUS_VERSION_STRING;
    request.runtimeSearchRoot = ExecutableDirectory();
    request.engineRoot = RuntimeLocator::discoverEngineRoot(request.runtimeSearchRoot);
    request.action = BuildAction::Package;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto requireValue = [&](const char* option) -> std::string {
            if (index + 1 >= argc) {
                std::cerr << option << " requires a value\n";
                return {};
            }
            return argv[++index];
        };

        if (argument == "--project") {
            request.projectRoot = requireValue("--project");
        } else if (argument == "--config") {
            const std::string value = requireValue("--config");
            if (value == "Development") {
                request.configuration = BuildConfiguration::Development;
            } else if (value == "Release") {
                request.configuration = BuildConfiguration::Release;
            } else {
                std::cerr << "--config must be Development or Release\n";
                return 2;
            }
        } else if (argument == "--output") {
            request.outputRoot = requireValue("--output");
        } else if (argument == "--engine-root") {
            request.engineRoot = requireValue("--engine-root");
        } else if (argument == "--icon") {
            request.applicationIcon = requireValue("--icon");
        } else if (argument == "--validate-only") {
            request.action = BuildAction::Validate;
        } else if (argument == "--clean") {
            request.clean = true;
            request.incremental = false;
        } else if (argument == "--clean-output") {
            request.action = BuildAction::Clean;
        } else if (argument == "--unused-report") {
            request.reportUnusedAssets = true;
        } else if (argument == "--zip") {
            request.createZip = true;
        } else if (argument == "--help" || argument == "-h") {
            Usage();
            return 0;
        } else {
            std::cerr << "Unknown option: " << argument << '\n';
            Usage();
            return 2;
        }
    }

    if (request.projectRoot.empty()) {
        std::cerr << "--project is required\n";
        Usage();
        return 2;
    }
    request.projectRoot = std::filesystem::absolute(request.projectRoot);
    if (request.outputRoot.empty()) {
        request.outputRoot = request.projectRoot / "Builds";
    }
    LoadProjectMetadata(request);
    request.onLog = [](const std::string& message) { std::cout << message << '\n'; };

    BuildService service;
    const BuildResult result = service.execute(request);
    for (const auto& problem : result.problems) {
        std::cerr << Severity(problem.severity) << " [" << problem.code << "] "
                  << problem.message;
        if (!problem.assetPath.empty()) {
            std::cerr << " (" << problem.assetPath << ')';
        }
        if (!problem.detail.empty()) {
            std::cerr << ": " << problem.detail;
        }
        std::cerr << '\n';
    }
    if (result.success && !result.outputPath.empty()) {
        std::cout << "Output: " << result.outputPath << "\nPackage bytes: "
                  << result.packageBytes << '\n';
    }
    return result.success ? 0 : (result.cancelled ? 3 : 1);
}
