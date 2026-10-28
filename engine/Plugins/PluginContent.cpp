#include "PluginContent.h"

#include "PluginManifest.h"
#include "../Assets/AssetMountRegistry.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace Hephaestus::Plugins {
namespace {

void DiscoverManifests(const std::filesystem::path& root,
                       std::vector<PluginManifest>& out)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        return;
    }
    std::vector<std::filesystem::path> paths;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".hpplugin") {
            paths.push_back(it->path());
        }
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
        const ManifestParseResult parsed = ParsePluginManifest(path);
        if (parsed.success) {
            out.push_back(parsed.manifest);
        }
    }
}

std::unordered_map<std::string, bool> LoadEnabledOverrides(const std::filesystem::path& projectRoot)
{
    std::unordered_map<std::string, bool> overrides;
    if (projectRoot.empty()) {
        return overrides;
    }
    std::ifstream input(projectRoot / ".hephaestus" / "plugins.json");
    if (!input) {
        return overrides;
    }
    try {
        const auto json = nlohmann::json::parse(input);
        const auto enabled = json.value("enabled", nlohmann::json::object());
        for (auto it = enabled.begin(); it != enabled.end(); ++it) {
            if (it.value().is_boolean()) {
                overrides[it.key()] = it.value().get<bool>();
            }
        }
    } catch (...) {
    }
    return overrides;
}

} // namespace

std::vector<EnabledPluginContent> CollectEnabledPluginContent(
    const std::filesystem::path& engineRoot,
    const std::filesystem::path& projectRoot)
{
    std::vector<PluginManifest> manifests;
    DiscoverManifests(engineRoot / "Plugins", manifests);
    if (!projectRoot.empty()) {
        DiscoverManifests(projectRoot / "Plugins", manifests);
    }

    std::unordered_map<std::string, std::vector<std::size_t>> ids;
    for (std::size_t i = 0; i < manifests.size(); ++i) {
        if (!manifests[i].id.empty()) {
            ids[manifests[i].id].push_back(i);
        }
    }
    std::unordered_set<std::string> duplicates;
    for (const auto& [id, indices] : ids) {
        if (indices.size() > 1) {
            duplicates.insert(id);
        }
    }

    const auto overrides = LoadEnabledOverrides(projectRoot);
    std::vector<EnabledPluginContent> result;
    std::unordered_set<std::string> emitted;
    for (const auto& manifest : manifests) {
        if (manifest.id.empty() || duplicates.contains(manifest.id) || emitted.contains(manifest.id)) {
            continue;
        }
        bool enabled = manifest.defaultEnabled;
        const auto override = overrides.find(manifest.id);
        if (override != overrides.end()) {
            enabled = override->second;
        }
        if (!enabled || manifest.content.empty()) {
            continue;
        }
        const std::filesystem::path source = manifest.root / manifest.content;
        std::error_code ec;
        if (!std::filesystem::is_directory(source, ec)) {
            continue;
        }
        emitted.insert(manifest.id);
        result.push_back(EnabledPluginContent{manifest.id, source});
    }
    return result;
}

std::vector<std::string> FindMissingEnabledPlugins(
    const std::filesystem::path& engineRoot,
    const std::filesystem::path& projectRoot)
{
    std::vector<PluginManifest> manifests;
    DiscoverManifests(engineRoot / "Plugins", manifests);
    if (!projectRoot.empty()) {
        DiscoverManifests(projectRoot / "Plugins", manifests);
    }
    std::vector<std::string> missing;
    for (const auto& [id, enabled] : LoadEnabledOverrides(projectRoot)) {
        const bool found = std::any_of(manifests.begin(), manifests.end(),
            [&](const PluginManifest& manifest) { return manifest.id == id; });
        if (enabled && !found) {
            missing.push_back(id);
        }
    }
    std::sort(missing.begin(), missing.end());
    return missing;
}

bool WriteStagedPluginMounts(const std::filesystem::path& packageRoot,
                             const std::vector<StagedPluginMount>& mounts,
                             std::string& error)
{
    try {
        nlohmann::json plugins = nlohmann::json::array();
        for (const auto& mount : mounts) {
            plugins.push_back({
                {"id", mount.id},
                {"root", mount.relativeRoot.generic_string()}
            });
        }
        const nlohmann::json json = {
            {"version", 1},
            {"plugins", plugins}
        };
        const auto path = packageRoot / kStagedPluginMountsRelative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::trunc);
        output << json.dump(2) << '\n';
        if (!output) {
            error = "could not write " + std::string(kStagedPluginMountsRelative);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

int MountStagedPluginContent(const std::filesystem::path& runtimeRoot)
{
    const auto path = runtimeRoot / kStagedPluginMountsRelative;
    std::ifstream input(path);
    if (!input) {
        return 0;
    }
    int mounted = 0;
    try {
        const auto json = nlohmann::json::parse(input);
        const auto plugins = json.value("plugins", nlohmann::json::array());
        for (const auto& plugin : plugins) {
            const std::string id = plugin.value("id", "");
            const std::string relative = plugin.value("root", "");
            if (id.empty() || relative.empty()) {
                continue;
            }
            std::filesystem::path root = relative;
            if (root.is_relative()) {
                root = runtimeRoot / root;
            }
            AssetMountRegistry::Instance().Mount("plugin://" + id, root, true);
            AssetMountRegistry::Instance().Mount("/plugin/" + id, root, true);
            ++mounted;
        }
    } catch (...) {
        return mounted;
    }
    return mounted;
}

} // namespace Hephaestus::Plugins
