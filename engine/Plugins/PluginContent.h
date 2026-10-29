#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus::Plugins {

struct EnabledPluginContent {
    std::string id;
    std::filesystem::path sourceRoot;   // Empty when the plugin has no content folder.
    std::filesystem::path pluginRoot;
    std::filesystem::path runtimeEntry; // Relative to pluginRoot; empty when none.
};

struct StagedPluginMount {
    std::string id;
    std::filesystem::path relativeRoot;  // Empty when the plugin has no content folder.
    std::filesystem::path runtimeEntry;  // Package-relative module path; empty when none.
};

std::vector<EnabledPluginContent> CollectEnabledPluginContent(
    const std::filesystem::path& engineRoot,
    const std::filesystem::path& projectRoot);

std::vector<std::string> FindMissingEnabledPlugins(
    const std::filesystem::path& engineRoot,
    const std::filesystem::path& projectRoot);

bool WriteStagedPluginMounts(const std::filesystem::path& packageRoot,
                             const std::vector<StagedPluginMount>& mounts,
                             std::string& error);

int MountStagedPluginContent(const std::filesystem::path& runtimeRoot);

/// Absolute paths of the runtime modules staged into a packaged game.
std::vector<std::filesystem::path> StagedRuntimeModules(const std::filesystem::path& runtimeRoot);

inline constexpr const char* kStagedPluginMountsRelative = "Plugins/content-mounts.json";

} // namespace Hephaestus::Plugins
