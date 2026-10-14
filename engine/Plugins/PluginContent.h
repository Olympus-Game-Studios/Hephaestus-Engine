#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus::Plugins {

struct EnabledPluginContent {
    std::string id;
    std::filesystem::path sourceRoot;
};

struct StagedPluginMount {
    std::string id;
    std::filesystem::path relativeRoot;
};

std::vector<EnabledPluginContent> CollectEnabledPluginContent(
    const std::filesystem::path& engineRoot,
    const std::filesystem::path& projectRoot);

bool WriteStagedPluginMounts(const std::filesystem::path& packageRoot,
                             const std::vector<StagedPluginMount>& mounts,
                             std::string& error);

int MountStagedPluginContent(const std::filesystem::path& runtimeRoot);

inline constexpr const char* kStagedPluginMountsRelative = "Plugins/content-mounts.json";

} // namespace Hephaestus::Plugins
