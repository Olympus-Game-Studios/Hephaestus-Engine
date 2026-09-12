#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#  define HP_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define HP_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

class QWidget;

namespace Hephaestus::Plugins {

inline constexpr std::uint32_t HP_PLUGIN_API_VERSION = 1;

using PluginCallback = void (*)(void* userData);
using PanelFactory = QWidget* (*)(QWidget* parent, void* userData);
using AssetPathCallback = bool (*)(const char* portablePath, void* userData);
using AssetRetargetCallback = void (*)(const char* oldPortablePath,
                                       const char* newPortablePath, void* userData);
using AssetCreateCallback = bool (*)(const char* targetDirectory, void* userData);
using ImportCallback = bool (*)(const char* sourcePath, const char* targetDirectory,
                                void* userData);

struct CommandDescriptor {
    const char* id = nullptr;
    const char* displayName = nullptr;
    const char* description = nullptr;
    const char* defaultShortcut = nullptr;
    PluginCallback execute = nullptr;
    void* userData = nullptr;
};

struct PanelDescriptor {
    const char* id = nullptr;
    const char* title = nullptr;
    const char* icon = nullptr;
    std::uint32_t defaultDockArea = 2; // Qt::RightDockWidgetArea without Qt in the ABI.
    PanelFactory factory = nullptr;
    void* userData = nullptr;
};

struct MenuActionDescriptor {
    const char* menuPath = nullptr; // e.g. "Tools/Primitive Tools"
    const char* commandId = nullptr;
    const char* group = nullptr;
};

struct ToolbarActionDescriptor {
    const char* toolbarId = nullptr;
    const char* commandId = nullptr;
};

struct ContextActionDescriptor {
    const char* location = nullptr; // assetBrowser, sceneHierarchy, viewport, inspector
    const char* commandId = nullptr;
};

struct AssetTypeDescriptor {
    const char* typeId = nullptr;
    const char* extension = nullptr;
    const char* displayName = nullptr;
    const char* icon = nullptr;
    bool readOnlyByDefault = false;
    AssetCreateCallback create = nullptr;
    AssetPathCallback openEditor = nullptr;
    void* userData = nullptr;
    AssetPathCallback isDirty = nullptr;
    AssetPathCallback saveEditor = nullptr;
    AssetPathCallback closeEditor = nullptr;
    AssetRetargetCallback retargetEditor = nullptr;
};

struct ImporterDescriptor {
    const char* id = nullptr;
    const char* sourceExtensions = nullptr; // semicolon-separated
    const char* resultAssetTypeId = nullptr;
    ImportCallback import = nullptr;
    void* userData = nullptr;
};

enum class SettingScope : std::uint32_t { Editor, Project };
struct SettingDescriptor {
    const char* id = nullptr;
    const char* displayName = nullptr;
    const char* description = nullptr;
    const char* defaultValue = nullptr;
    SettingScope scope = SettingScope::Project;
};

enum class PrimitiveKind : std::uint32_t { Empty, Cube, Sphere, Plane, PointLight };

class ICommandService {
public:
    virtual ~ICommandService() = default;
    virtual bool Register(const CommandDescriptor& descriptor) = 0;
    virtual bool Execute(const char* commandId) = 0;
};

class IUIService {
public:
    virtual ~IUIService() = default;
    virtual bool RegisterPanel(const PanelDescriptor& descriptor) = 0;
    virtual bool RegisterMenuAction(const MenuActionDescriptor& descriptor) = 0;
    virtual bool RegisterToolbarAction(const ToolbarActionDescriptor& descriptor) = 0;
    virtual bool RegisterContextAction(const ContextActionDescriptor& descriptor) = 0;
    virtual bool ShowPanel(const char* panelId) = 0;
};

class IAssetService {
public:
    virtual ~IAssetService() = default;
    virtual bool RegisterAssetType(const AssetTypeDescriptor& descriptor) = 0;
    virtual bool RegisterImporter(const ImporterDescriptor& descriptor) = 0;
};

class ISceneService {
public:
    virtual ~ISceneService() = default;
    virtual bool HasScene() const = 0;
    virtual std::uint64_t CreatePrimitive(PrimitiveKind kind, const char* name) = 0;
    virtual bool DeleteEntity(std::uint64_t entityId) = 0;
    virtual bool SelectEntity(std::uint64_t entityId) = 0;
};

class IUndoService {
public:
    virtual ~IUndoService() = default;
    virtual std::uint64_t Begin(const char* label) = 0;
    virtual bool Commit(std::uint64_t transaction) = 0;
    virtual bool Rollback(std::uint64_t transaction) = 0;
};

class IProjectService {
public:
    virtual ~IProjectService() = default;
    virtual const char* ProjectRoot() const = 0;
    virtual const char* PluginRoot() const = 0;
    virtual bool ResolveAssetPath(const char* portablePath, char* output,
                                  std::size_t outputSize) const = 0;
};

class ISettingsService {
public:
    virtual ~ISettingsService() = default;
    virtual bool Register(const SettingDescriptor& descriptor) = 0;
    virtual const char* Get(const char* settingId) const = 0;
    virtual bool Set(const char* settingId, const char* value) = 0;
};

class IPluginContext {
public:
    virtual ~IPluginContext() = default;
    virtual const char* PluginId() const = 0;
    virtual ICommandService& Commands() = 0;
    virtual IUIService& UI() = 0;
    virtual IAssetService& Assets() = 0;
    virtual ISceneService& Scene() = 0;
    virtual IUndoService& Undo() = 0;
    virtual IProjectService& Project() = 0;
    virtual ISettingsService& Settings() = 0;
};

class IHephaestusPlugin {
public:
    virtual ~IHephaestusPlugin() = default;
    virtual bool OnLoad(IPluginContext& context) = 0;
    virtual void OnUnload() {}
};

using CreatePluginFn = IHephaestusPlugin* (*)();
using DestroyPluginFn = void (*)(IHephaestusPlugin*);

struct PluginExports {
    std::uint32_t structSize = sizeof(PluginExports);
    std::uint32_t apiVersion = HP_PLUGIN_API_VERSION;
    CreatePluginFn create = nullptr;
    DestroyPluginFn destroy = nullptr;
};

using GetPluginExportsFn = const PluginExports* (*)(std::uint32_t hostApiVersion);

} // namespace Hephaestus::Plugins

#define HP_DECLARE_PLUGIN(PluginClass)                                                \
    extern "C" HP_PLUGIN_EXPORT const ::Hephaestus::Plugins::PluginExports*          \
    HP_GetPluginExports(std::uint32_t hostApiVersion) {                               \
        using namespace ::Hephaestus::Plugins;                                        \
        static const PluginExports exports{sizeof(PluginExports),                     \
            HP_PLUGIN_API_VERSION,                                                    \
            []() -> IHephaestusPlugin* { return new PluginClass(); },                 \
            [](IHephaestusPlugin* plugin) { delete plugin; }};                        \
        return hostApiVersion == HP_PLUGIN_API_VERSION ? &exports : nullptr;           \
    }
