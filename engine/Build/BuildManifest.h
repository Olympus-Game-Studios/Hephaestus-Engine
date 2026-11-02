#pragma once

#include "Build/BuildTypes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus::Build {

struct BuildManifestData {
    std::string productName;
    std::string version;
    std::string company;
    std::string engineVersion;
    std::string configuration;
#if defined(__APPLE__) && defined(__aarch64__)
    std::string platform = "macos-arm64";
#elif defined(__APPLE__)
    std::string platform = "macos-x64";
#else
    std::string platform = "windows-x64";
#endif
    std::string timestampUtc;
    std::string startupScene;
    std::string executable;
    std::vector<FileSizeEntry> files;
};

class BuildManifest {
public:
    static bool writeJson(const std::filesystem::path& path,
                          const BuildManifestData& data,
                          std::string* error = nullptr);
    static bool writeExportList(const std::filesystem::path& path,
                                const BuildManifestData& data,
                                std::string* error = nullptr);
};

} // namespace Hephaestus::Build
