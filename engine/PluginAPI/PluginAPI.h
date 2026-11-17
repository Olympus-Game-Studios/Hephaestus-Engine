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

/// An icon in the editor's title bar, left of the notifications bell, that runs a command when clicked. Users can
/// hide it from its right-click menu and show it again from the plugin's right-click menu in Edit > Plugins.
struct HeaderButtonDescriptor {
    std::uint32_t structSize = sizeof(HeaderButtonDescriptor);
    const char* commandId = nullptr; // a command this plugin registered
    const char* iconPath = nullptr;  // .svg or .png, relative to the plugin folder or absolute
    const char* toolTip = nullptr;
};

/// A button for one of this plugin's commands in the editor's tool row (next to the transform and construction
/// tools). The icon is tinted like the editor's own icons when it is an SVG drawn with currentColor.
struct ToolbarButtonDescriptor {
    std::uint32_t structSize = sizeof(ToolbarButtonDescriptor);
    const char* commandId = nullptr; // a command this plugin registered
    const char* iconPath = nullptr;  // .svg or .png, relative to the plugin folder or absolute
    const char* toolTip = nullptr;   // shown under the command's name
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

/// A downloaded asset pack to import into the open project (IAssetService::ImportAssetPack).
/// Credit fields are optional; when title, author or license is set the asset is added to the project's
/// generated credits (assets/scripts/ImportedCredits.lua).
struct AssetPackImportRequest {
    std::uint32_t structSize = sizeof(AssetPackImportRequest);
    const char* sourcePath = nullptr;   // .zip, folder, or one .fbx/.glb/.gltf/.obj file
    const char* targetFolder = nullptr; // portable folder under assets/, e.g. "assets/Imported/Sketchfab/Crate"
    const char* title = nullptr;
    const char* source = nullptr;       // e.g. "Sketchfab"
    const char* sourceUrl = nullptr;
    const char* author = nullptr;
    const char* authorUrl = nullptr;
    const char* license = nullptr;
    const char* licenseUrl = nullptr;
};

struct AssetPackImportResult {
    std::uint32_t structSize = sizeof(AssetPackImportResult);
    std::uint32_t prefabCount = 0;
    std::uint32_t materialCount = 0;
    std::uint32_t warningCount = 0;
    char folder[512] = {};   // portable folder the pack was imported into
    char message[2048] = {}; // the error on failure, otherwise the warnings, one per line
};

/// Called on the editor's main thread with the tool's arguments as a JSON object. The plugin answers with
/// IAutomationToolService::CompleteCall(callId, ...), before returning or later (after network work, for
/// example). MCP clients give up on a call after about 30 seconds, so longer work should start a job and
/// return at once, with another tool to ask how it is going.
using AutomationToolCallback = void (*)(const char* argumentsJson, std::uint64_t callId, void* userData);

struct AutomationToolDescriptor {
    const char* name = nullptr;            // [A-Za-z0-9_.-], e.g. "sketchfab.search"
    const char* description = nullptr;     // what the tool does, for AI clients
    const char* inputSchemaJson = nullptr; // JSON Schema of the arguments object
    bool mutatesProject = false;           // true: only offered while the editor is not playing
    AutomationToolCallback call = nullptr;
    void* userData = nullptr;
};

/// The number types material node pins carry.
enum class MaterialValueType : std::uint32_t { Float = 1, Float2 = 2, Float3 = 3, Float4 = 4 };

/// One pin of a material node. For an output, `glsl` is the expression that computes it; for an input, the value
/// used when nothing is connected (null means zero). Both may use {0}..{9} for the node's inputs (converted to
/// their declared types) and {uv}, {worldPos}, {normal}, {viewDir}, {cameraPos}, {time} (seconds), {objectPos} and
/// {screenPos} (gl_FragCoord).
struct MaterialNodePin {
    const char* name = nullptr;
    MaterialValueType type = MaterialValueType::Float;
    const char* glsl = nullptr;
};

/// A node for the Material Editor's graph. It is compiled into each material's own shader, so packaged games need
/// nothing from the plugin; materials keep the node by typeName while the plugin is off.
struct MaterialNodeDescriptor {
    std::uint32_t structSize = sizeof(MaterialNodeDescriptor);
    const char* typeName = nullptr;    // saved in materials, [A-Za-z0-9_.], unique across core and plugin nodes
    const char* displayName = nullptr;
    const char* category = nullptr;    // the library group; "Plugins" when null
    const char* tooltip = nullptr;
    const MaterialNodePin* inputs = nullptr;
    std::uint32_t inputCount = 0;      // up to 10
    const MaterialNodePin* outputs = nullptr;
    std::uint32_t outputCount = 0;     // 1 to 8
    const char* glslFunctions = nullptr; // optional GLSL functions the expressions call, added once per material
};

enum class SettingScope : std::uint32_t { Editor, Project };
struct SettingDescriptor {
    const char* id = nullptr;
    const char* displayName = nullptr;
    const char* description = nullptr;
    const char* defaultValue = nullptr;
    SettingScope scope = SettingScope::Project;
};

enum class PrimitiveKind : std::uint32_t { Empty, Cube, Sphere, Plane, PointLight, Cylinder };

/// Subtract only applies to Cube: it makes a cutter that carves the construction blocks and rooms it overlaps.
enum class PrimitiveOperation : std::uint32_t { Add, Subtract };

struct PrimitiveRequest {
    std::uint32_t structSize = sizeof(PrimitiveRequest);
    PrimitiveKind kind = PrimitiveKind::Cube;
    const char* name = nullptr;
    float size[3] = {1.0f, 1.0f, 1.0f}; // metres; sphere uses size[0] as diameter, cylinder size[0] and size[1]
    PrimitiveOperation operation = PrimitiveOperation::Add;
    bool collision = true;               // a static collider players and the navmesh use
    bool placeInView = true;             // on the surface in front of the editor camera, else at the origin
};

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
    virtual bool RegisterHeaderButton(const HeaderButtonDescriptor&) { return false; }
    /// Appended to API 1; an older editor's vtable has no such entry, so plugins that call it need a matching
    /// "engineVersion" in their manifest.
    virtual bool RegisterToolbarButton(const ToolbarButtonDescriptor&) { return false; }
};

class IAssetService {
public:
    virtual ~IAssetService() = default;
    virtual bool RegisterAssetType(const AssetTypeDescriptor& descriptor) = 0;
    virtual bool RegisterImporter(const ImporterDescriptor& descriptor) = 0;
    /// Imports models, materials (applied through one prefab per model), textures and the licence record.
    /// Appended to API 1 after its first release; an older editor's vtable has no such entry, so a plugin that
    /// calls it requires a matching "engineVersion" in its manifest.
    virtual bool ImportAssetPack(const AssetPackImportRequest& request, AssetPackImportResult& result) = 0;
};

class ISceneService {
public:
    virtual ~ISceneService() = default;
    virtual bool HasScene() const = 0;
    virtual std::uint64_t CreatePrimitive(PrimitiveKind kind, const char* name) = 0;
    virtual bool DeleteEntity(std::uint64_t entityId) = 0;
    virtual bool SelectEntity(std::uint64_t entityId) = 0;
    /// Creates, selects and returns a primitive, or 0. Appended to API 1 (see RegisterToolbarButton).
    virtual std::uint64_t CreatePrimitiveEx(const PrimitiveRequest&) { return 0; }
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

/// Tools a plugin adds to the editor's automation surface, which the Hephaestus MCP plugin offers to AI clients.
/// A plugin's tools disappear when it unloads.
class IAutomationToolService {
public:
    virtual ~IAutomationToolService() = default;
    virtual bool RegisterTool(const AutomationToolDescriptor& descriptor) = 0;
    /// success: resultJson is the JSON object returned to the caller; otherwise it is the error message.
    virtual bool CompleteCall(std::uint64_t callId, bool success, const char* resultJson) = 0;
};

/// Nodes a plugin adds to material graphs. They disappear from the editor when the plugin unloads.
class IMaterialNodeService {
public:
    virtual ~IMaterialNodeService() = default;
    virtual bool RegisterNode(const MaterialNodeDescriptor& descriptor) = 0;
};

/// What the level's characters can do, from their Character Controllers (defaults when there are none).
struct LevelMetrics {
    std::uint32_t structSize = sizeof(LevelMetrics);
    float height = 1.8f;
    float radius = 0.3f;
    float stepHeight = 0.3f;
    float maxSlopeDegrees = 45.0f;
    float jumpHeight = 0.0f;
    float walkSpeed = 5.0f;
    bool fromCharacters = false;
};

enum class LevelProblemSeverity : std::uint32_t { Error, Warning, Info };

struct LevelProblemDescriptor {
    std::uint32_t structSize = sizeof(LevelProblemDescriptor);
    const char* check = nullptr;     // a short id such as "spawn-exposed"; the check's own id when null
    LevelProblemSeverity severity = LevelProblemSeverity::Warning;
    const char* message = nullptr;   // one sentence for the Check Level list
    float position[3] = {0.0f, 0.0f, 0.0f};  // where the viewport frames the problem
    std::uint64_t entityId = 0;      // the entity at fault (as ISceneService ids), or 0
};

struct LevelEntityInfo {
    std::uint32_t structSize = sizeof(LevelEntityInfo);
    std::uint64_t id = 0;             // as ISceneService ids
    const char* name = nullptr;       // valid until the check returns
    float position[3] = {0.0f, 0.0f, 0.0f};  // world space
};

/// One run of a plugin's level check, valid only during its callback.
class ILevelCheckRun {
public:
    virtual ~ILevelCheckRun() = default;
    virtual const LevelMetrics& Metrics() const = 0;
    /// The nearest point where the segment meets the level's collision (what characters stand on and walk into).
    virtual bool Raycast(const float from[3], const float to[3], float hit[3]) const = 0;
    /// The walking distance between two points along the navmesh; false when `to` can't be walked to.
    virtual bool WalkDistance(const float from[3], const float to[3], float* metres) const = 0;
    /// The scene's named entities, such as spawn points, with their world positions.
    virtual std::uint32_t EntityCount() const = 0;
    virtual bool GetEntity(std::uint32_t index, LevelEntityInfo& out) const = 0;
    virtual void Report(const LevelProblemDescriptor& problem) = 0;
};

using LevelCheckCallback = void (*)(ILevelCheckRun& run, void* userData);

/// A check that runs with Build > Check Level (and MCP level.check), after the engine's own checks.
struct LevelCheckDescriptor {
    std::uint32_t structSize = sizeof(LevelCheckDescriptor);
    const char* id = nullptr;           // unique, e.g. "fps.spawn-exposure"
    const char* displayName = nullptr;
    LevelCheckCallback run = nullptr;
    void* userData = nullptr;
};

/// Level checks a plugin adds. They are removed when the plugin unloads.
class ILevelAnalysisService {
public:
    virtual ~ILevelAnalysisService() = default;
    virtual bool RegisterCheck(const LevelCheckDescriptor& descriptor) = 0;
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
    /// Appended to API 1 after its first release (see IAssetService::ImportAssetPack); null where the host has
    /// no automation.
    virtual IAutomationToolService* AutomationTools() { return nullptr; }
    /// Appended to API 1 after AutomationTools; null where the host has no Material Editor.
    virtual IMaterialNodeService* MaterialNodes() { return nullptr; }
    /// Appended to API 1 after MaterialNodes; null where the host has no level checks.
    virtual ILevelAnalysisService* LevelAnalysis() { return nullptr; }
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
