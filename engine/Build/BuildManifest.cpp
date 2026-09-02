#include "Build/BuildManifest.h"

#include <fstream>

#include <nlohmann/json.hpp>

namespace Hephaestus::Build {
namespace {

bool OpenFailed(std::string* error, const std::filesystem::path& path) {
    if (error) {
        *error = "Could not write " + path.generic_string();
    }
    return false;
}

} // namespace

bool BuildManifest::writeJson(const std::filesystem::path& path,
                              const BuildManifestData& data,
                              std::string* error) {
    nlohmann::json root = {
        {"schemaVersion", 1},
        {"productName", data.productName},
        {"version", data.version},
        {"company", data.company},
        {"engineVersion", data.engineVersion},
        {"configuration", data.configuration},
        {"platform", data.platform},
        {"timestampUtc", data.timestampUtc},
        {"startupScene", data.startupScene},
        {"executable", data.executable}
    };
    root["files"] = nlohmann::json::array();
    for (const auto& file : data.files) {
        root["files"].push_back({{"path", file.path}, {"bytes", file.bytes}});
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return OpenFailed(error, path);
    }
    stream << root.dump(2) << '\n';
    return static_cast<bool>(stream);
}

bool BuildManifest::writeExportList(const std::filesystem::path& path,
                                    const BuildManifestData& data,
                                    std::string* error) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return OpenFailed(error, path);
    }
    for (const auto& file : data.files) {
        stream << file.path << '\t' << file.bytes << '\n';
    }
    return static_cast<bool>(stream);
}

} // namespace Hephaestus::Build
