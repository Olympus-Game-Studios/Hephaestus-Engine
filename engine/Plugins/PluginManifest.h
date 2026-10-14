#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace Hephaestus::Plugins {

enum class PluginType { Editor, Runtime, Hybrid };

struct PluginManifest {
    std::uint32_t version = 0;
    std::uint32_t apiVersion = 1;
    std::string id;
    std::string name;
    std::string description;
    std::string author;
    std::string pluginVersion;
    std::string engineVersion;
    PluginType type = PluginType::Editor;
    bool defaultEnabled = true;
    std::filesystem::path entry;
    std::filesystem::path content;
    std::filesystem::path icon;
    std::filesystem::path manifestPath;
    std::filesystem::path root;
};

struct ManifestParseResult {
    bool success = false;
    PluginManifest manifest;
    std::string error;
};

ManifestParseResult ParsePluginManifest(const std::filesystem::path& path);
bool IsEngineVersionCompatible(const std::string& constraint,
                               const std::string& engineVersion);

} // namespace Hephaestus::Plugins
