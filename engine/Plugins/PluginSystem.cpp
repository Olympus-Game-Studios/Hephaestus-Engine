#include "PluginSystem.h"

#include "../Assets/AssetMountRegistry.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <unordered_set>

#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace Hephaestus::Plugins {

struct PluginSystem::LoadedPlugin {
    std::size_t recordIndex = 0;
    IHephaestusPlugin* instance = nullptr;
    DestroyPluginFn destroy = nullptr;
    std::unique_ptr<IPluginContext> context;
#ifdef _WIN32
    HMODULE module = nullptr;
#else
    void* module = nullptr;
#endif
};

PluginSystem::PluginSystem() = default;
PluginSystem::~PluginSystem() { UnloadAll(); }
void PluginSystem::SetEngineVersion(std::string version) { engineVersion_ = std::move(version); }
void PluginSystem::SetContextFactory(ContextFactory factory) { contextFactory_ = std::move(factory); }
void PluginSystem::SetFirstPartyContextFactory(FirstPartyContextFactory factory) {
    firstPartyFactory_ = std::move(factory);
}

void PluginSystem::DiscoverRoot(const std::filesystem::path& root, PluginLocation location) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return;
    std::vector<std::filesystem::path> paths;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".hpplugin") paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
        PluginRecord record;
        record.location = location;
        const auto parsed = ParsePluginManifest(path);
        record.manifest = parsed.manifest;
        if (!parsed.success) {
            record.status = PluginStatus::Invalid;
            record.error = parsed.error;
        } else {
            record.enabled = parsed.manifest.defaultEnabled;
        }
        records_.push_back(std::move(record));
    }
}

void PluginSystem::Discover(const std::filesystem::path& engineRoot,
                            const std::filesystem::path& projectRoot) {
    UnloadAll();
    records_.clear();
    projectRoot_ = projectRoot;
    DiscoverRoot(engineRoot / "Plugins", PluginLocation::Engine);
    if (!projectRoot.empty()) DiscoverRoot(projectRoot / "Plugins", PluginLocation::Project);

    std::unordered_map<std::string, std::vector<std::size_t>> ids;
    for (std::size_t i = 0; i < records_.size(); ++i) {
        if (!records_[i].manifest.id.empty()) ids[records_[i].manifest.id].push_back(i);
    }
    for (const auto& [id, indices] : ids) if (indices.size() > 1) {
        for (const auto index : indices) {
            records_[index].status = PluginStatus::Duplicate;
            records_[index].error = "duplicate plugin id: " + id;
        }
    }
    LoadState();
    for (auto& record : records_) {
        if (record.status == PluginStatus::Invalid || record.status == PluginStatus::Duplicate) continue;
        if (record.manifest.apiVersion != HP_PLUGIN_API_VERSION ||
            !IsEngineVersionCompatible(record.manifest.engineVersion, engineVersion_)) {
            record.status = PluginStatus::Incompatible;
            record.error = record.manifest.apiVersion != HP_PLUGIN_API_VERSION
                ? "plugin API version is incompatible"
                : "engine version constraint is not satisfied";
        } else if (!record.enabled) record.status = PluginStatus::Disabled;
        if (record.enabled && record.status == PluginStatus::Discovered &&
            !record.manifest.content.empty()) {
            const auto content = record.manifest.root / record.manifest.content;
            AssetMountRegistry::Instance().Mount("plugin://" + record.manifest.id, content, true);
            AssetMountRegistry::Instance().Mount("/plugin/" + record.manifest.id, content, true);
        }
    }
}

void PluginSystem::LoadState() {
    if (projectRoot_.empty()) return;
    std::ifstream input(projectRoot_ / ".hephaestus" / "plugins.json");
    if (!input) return;
    try {
        const auto json = nlohmann::json::parse(input);
        const auto enabled = json.value("enabled", nlohmann::json::object());
        for (auto& record : records_) if (enabled.contains(record.manifest.id))
            record.enabled = enabled.at(record.manifest.id).get<bool>();
    } catch (const std::exception& e) {
        (void)e; // Corrupt state is non-fatal; defaults remain enabled.
    }
}

bool PluginSystem::SaveState(std::string* error) const {
    if (projectRoot_.empty()) {
        if (error) *error = "open a project before changing plugin state";
        return false;
    }
    try {
        const auto dir = projectRoot_ / ".hephaestus";
        std::filesystem::create_directories(dir);
        nlohmann::json enabled = nlohmann::json::object();
        for (const auto& record : records_) if (!record.manifest.id.empty())
            enabled[record.manifest.id] = record.enabled;
        std::ofstream output(dir / "plugins.json", std::ios::trunc);
        output << nlohmann::json{{"version", 1}, {"enabled", enabled}}.dump(2) << '\n';
        if (!output) throw std::runtime_error("could not write plugin state");
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

void PluginSystem::LoadEnabled() {
    for (std::size_t i = 0; i < records_.size(); ++i) {
        auto& record = records_[i];
        if (!record.enabled || record.status != PluginStatus::Discovered) continue;
        auto candidate = std::make_unique<LoadedPlugin>();
        candidate->recordIndex = i;
        auto cleanup = [&candidate]() {
            if (!candidate) return;
            try { if (candidate->instance) candidate->instance->OnUnload(); } catch (...) {}
            if (candidate->instance && candidate->destroy)
                candidate->destroy(candidate->instance);
            candidate->instance = nullptr;
            candidate->context.reset();
#ifdef _WIN32
            if (candidate->module) FreeLibrary(candidate->module);
#else
            if (candidate->module) dlclose(candidate->module);
#endif
            candidate->module = nullptr;
        };
        try {
            const auto libraryPath = record.manifest.root / record.manifest.entry;
            if (!std::filesystem::is_regular_file(libraryPath))
                throw std::runtime_error("plugin library is missing: " + libraryPath.string());
#ifdef _WIN32
            candidate->module = LoadLibraryW(libraryPath.wstring().c_str());
            if (!candidate->module) throw std::runtime_error("LoadLibrary failed (error " + std::to_string(GetLastError()) + ")");
            auto getExports = reinterpret_cast<GetPluginExportsFn>(GetProcAddress(candidate->module, "HP_GetPluginExports"));
#else
            candidate->module = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!candidate->module) throw std::runtime_error(dlerror());
            auto getExports = reinterpret_cast<GetPluginExportsFn>(dlsym(candidate->module, "HP_GetPluginExports"));
#endif
            if (!getExports) throw std::runtime_error("missing HP_GetPluginExports entry point");
            const PluginExports* exports = getExports(HP_PLUGIN_API_VERSION);
            if (!exports || exports->structSize < sizeof(PluginExports) ||
                exports->apiVersion != HP_PLUGIN_API_VERSION || !exports->create || !exports->destroy)
                throw std::runtime_error("plugin exports are incompatible or incomplete");
            if (!contextFactory_) throw std::runtime_error("editor plugin context is unavailable");
            candidate->destroy = exports->destroy;
            candidate->context = contextFactory_(record.manifest);
            candidate->instance = exports->create();
            if (!candidate->context || !candidate->instance)
                throw std::runtime_error("plugin instance could not be created");
            if (firstPartyFactory_) {
#ifdef _WIN32
                auto setContext = reinterpret_cast<SetFirstPartyContextFn>(
                    GetProcAddress(candidate->module, HP_FIRST_PARTY_SET_CONTEXT_EXPORT));
#else
                auto setContext = reinterpret_cast<SetFirstPartyContextFn>(
                    dlsym(candidate->module, HP_FIRST_PARTY_SET_CONTEXT_EXPORT));
#endif
                if (setContext) setContext(candidate->instance, firstPartyFactory_());
            }
            if (!candidate->instance->OnLoad(*candidate->context))
                throw std::runtime_error("plugin OnLoad failed");
            record.status = PluginStatus::Loaded;
            loaded_.push_back(std::move(candidate));
        } catch (const std::exception& e) {
            cleanup();
            AssetMountRegistry::Instance().Unmount("plugin://" + record.manifest.id);
            AssetMountRegistry::Instance().Unmount("/plugin/" + record.manifest.id);
            record.status = PluginStatus::Failed;
            record.error = e.what();
        } catch (...) {
            cleanup();
            AssetMountRegistry::Instance().Unmount("plugin://" + record.manifest.id);
            AssetMountRegistry::Instance().Unmount("/plugin/" + record.manifest.id);
            record.status = PluginStatus::Failed;
            record.error = "unknown initialization failure";
        }
    }
}

void PluginSystem::UnloadAll() {
    AssetMountRegistry::Instance().UnmountPrefix("plugin://");
    AssetMountRegistry::Instance().UnmountPrefix("/plugin/");
    for (auto it = loaded_.rbegin(); it != loaded_.rend(); ++it) {
        auto& loaded = **it;
        try { if (loaded.instance) loaded.instance->OnUnload(); } catch (...) {}
        if (loaded.instance && loaded.destroy) loaded.destroy(loaded.instance);
        loaded.instance = nullptr;
        loaded.context.reset();
#ifdef _WIN32
        if (loaded.module) FreeLibrary(loaded.module);
#else
        if (loaded.module) dlclose(loaded.module);
#endif
    }
    loaded_.clear();
}

bool PluginSystem::SetEnabled(const std::string& id, bool enabled, std::string* error) {
    for (auto& record : records_) if (record.manifest.id == id) {
        if (record.status == PluginStatus::Invalid || record.status == PluginStatus::Duplicate ||
            record.status == PluginStatus::Incompatible) {
            if (error) *error = "this plugin is not compatible and cannot be enabled";
            return false;
        }
        const bool old = record.enabled;
        record.enabled = enabled;
        if (!SaveState(error)) { record.enabled = old; return false; }
        record.restartPending = (record.status == PluginStatus::Loaded) != enabled;
        if (!enabled && record.status != PluginStatus::Loaded) record.status = PluginStatus::Disabled;
        return true;
    }
    if (error) *error = "plugin not found";
    return false;
}

const PluginManifest* PluginSystem::FindManifest(const std::string& id) const {
    for (const auto& r : records_) if (r.manifest.id == id) return &r.manifest;
    return nullptr;
}

std::filesystem::path PluginSystem::ResolvePluginAsset(const std::string& portable) const {
    std::string value = portable;
    constexpr const char* scheme = "plugin://";
    constexpr const char* rooted = "/plugin/";
    std::size_t start = 0;
    if (value.starts_with(scheme)) start = std::char_traits<char>::length(scheme);
    else if (value.starts_with(rooted)) start = std::char_traits<char>::length(rooted);
    else return {};
    const auto slash = value.find('/', start);
    if (slash == std::string::npos) return {};
    const auto id = value.substr(start, slash - start);
    const auto* manifest = FindManifest(id);
    if (!manifest || manifest->content.empty()) return {};
    const auto relative = std::filesystem::path(value.substr(slash + 1));
    for (const auto& part : relative) if (part == "..") return {};
    return manifest->root / manifest->content / relative;
}

const char* PluginSystem::StatusText(PluginStatus status) {
    switch (status) {
        case PluginStatus::Discovered: return "Ready";
        case PluginStatus::Disabled: return "Disabled";
        case PluginStatus::Incompatible: return "Incompatible";
        case PluginStatus::Invalid: return "Invalid";
        case PluginStatus::Duplicate: return "Duplicate ID";
        case PluginStatus::Loaded: return "Loaded";
        case PluginStatus::Failed: return "Load failed";
    }
    return "Unknown";
}

} // namespace Hephaestus::Plugins
