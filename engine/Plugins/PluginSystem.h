#pragma once

#include "PluginManifest.h"
#include "../PluginAPI/PluginAPI.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hephaestus::Plugins {

enum class PluginLocation { Engine, Project };
enum class PluginStatus { Discovered, Disabled, Incompatible, Invalid, Duplicate, Loaded, Failed };

struct PluginRecord {
    PluginManifest manifest;
    PluginLocation location = PluginLocation::Engine;
    PluginStatus status = PluginStatus::Discovered;
    bool enabled = true;
    bool restartPending = false;
    std::string error;
};

class PluginSystem {
public:
    using ContextFactory = std::function<std::unique_ptr<IPluginContext>(
        const PluginManifest& manifest)>;

    PluginSystem();
    ~PluginSystem();
    PluginSystem(const PluginSystem&) = delete;
    PluginSystem& operator=(const PluginSystem&) = delete;

    void SetEngineVersion(std::string version);
    void SetContextFactory(ContextFactory factory);
    void Discover(const std::filesystem::path& engineRoot,
                  const std::filesystem::path& projectRoot);
    void LoadEnabled();
    void UnloadAll();
    bool SetEnabled(const std::string& pluginId, bool enabled, std::string* error = nullptr);
    const std::vector<PluginRecord>& Records() const { return records_; }
    std::vector<PluginRecord>& Records() { return records_; }
    std::filesystem::path ResolvePluginAsset(const std::string& portablePath) const;
    const PluginManifest* FindManifest(const std::string& pluginId) const;
    static const char* StatusText(PluginStatus status);

private:
    struct LoadedPlugin;
    void DiscoverRoot(const std::filesystem::path& root, PluginLocation location);
    void LoadState();
    bool SaveState(std::string* error) const;
    std::string engineVersion_ = "0.0.0";
    std::filesystem::path projectRoot_;
    ContextFactory contextFactory_;
    std::vector<PluginRecord> records_;
    std::vector<std::unique_ptr<LoadedPlugin>> loaded_;
};

} // namespace Hephaestus::Plugins
