#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <limits>
#include <cstdint>
#include <functional>

#ifndef LUA_AVAILABLE
    #define LUA_AVAILABLE 0
#endif

#include "../Core/Math.h"
#include "../Core/PropertyValue.h"
#include "../Physics/PhysicsContactEvents.h"

#if LUA_AVAILABLE
    #include <lua.hpp>
#else
    // Lua not available - use stubs
    typedef void lua_State;
#endif

// Forward declarations
class Entity;
class PluginScriptHost;
class Scene;
namespace Hephaestus {
class InputSystem;
class EngineFrameRenderer;
}
struct GLFWwindow;
struct LuaScriptComponent;
struct TransformComponent;
struct TagComponent;
struct CharacterComponent;
struct MeshRendererComponent;
class AudioEngine;

namespace Hephaestus::HephaestusUI {
    class UIElement;
    class UIScreen;
    class UISystem;
    class UIContext;
    class UIRenderer;
    struct UIStyle;
}
#include "../UI/HephaestusUI/UIStyle.h"

namespace entt {
    enum class entity : uint32_t;
}

struct CameraBinding {
    std::function<void(bool)> setActive;
    std::function<bool()> isActive;
    std::function<void(const Vec3&)> setPosition;
    std::function<Vec3()> getPosition;
    std::function<void(const Vec3&)> setRotation;
    std::function<Vec3()> getRotation;
    std::function<void(const Vec3&)> translate;
};

struct PhysicsRaycastHit {
    bool hit = false;
    float distance = 0.0f;
    Vec3 point{0.0f, 0.0f, 0.0f};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    uint32_t entityId = std::numeric_limits<uint32_t>::max(); // max = no entity mapping
    bool sensor = false;
};

enum class LuaScriptSlot : uint8_t {
    Gameplay = 0,
    CharacterController = 1
};

struct LuaScriptInstanceKey {
    uint32_t entityId = 0;
    LuaScriptSlot slot = LuaScriptSlot::Gameplay;

    bool operator==(const LuaScriptInstanceKey& other) const noexcept {
        return entityId == other.entityId && slot == other.slot;
    }
};

struct LuaScriptInstanceKeyHash {
    std::size_t operator()(const LuaScriptInstanceKey& key) const noexcept {
        return (static_cast<std::size_t>(key.entityId) << 1) ^ static_cast<std::size_t>(key.slot);
    }
};

struct CharacterBinding {
    std::function<bool(uint32_t entityId, float worldX, float worldZ)> move;
    std::function<bool(uint32_t entityId)> jump;
    std::function<bool(uint32_t entityId, bool crouched)> setCrouched;
    std::function<bool(uint32_t entityId, bool prone)> setProne;
    std::function<bool(uint32_t entityId)> isProne;
    std::function<bool(uint32_t entityId)> isGrounded;
    std::function<bool(uint32_t entityId, float multiplier)> setSpeedMultiplier;
    std::function<bool(uint32_t entityId, float& outMultiplier)> getSpeedMultiplier;
    std::function<bool(uint32_t entityId)> isCrouched;
    std::function<bool(uint32_t entityId)> isStandBlocked;
    std::function<bool(uint32_t entityId, Vec3& out)> getVelocity;
    std::function<bool(uint32_t entityId, Vec3& out)> getGroundNormal;
    std::function<bool(uint32_t entityId, Vec3& out)> getGroundVelocity;
    std::function<bool(uint32_t entityId, std::string& outState)> getGroundState;
    std::function<bool(uint32_t entityId, float x, float y, float z)> teleport;
    std::function<bool(uint32_t entityId, float radians)> setFacingYaw;
};

struct PhysicsBinding {
    std::function<bool(uint32_t entityId)> hasBody;
    std::function<bool(uint32_t entityId, Vec3& outVelocity)> getLinearVelocity;
    std::function<bool(uint32_t entityId, const Vec3& velocity, bool activate)> setLinearVelocity;
    std::function<bool(uint32_t entityId, const Vec3& velocity, bool activate)> setAngularVelocity;
    std::function<bool(uint32_t entityId, const Vec3& rotationEuler, bool activate)> setRotation;
    std::function<bool(const std::vector<uint32_t>& ignoreEntityIds, const Vec3& origin, const Vec3& direction, float maxDistance, PhysicsRaycastHit& outHit, const char* layerName, std::string& outError, bool ignoreSensors)> raycast;
    std::function<bool(uint32_t entityId)> isGrounded;
    std::function<bool(uint32_t entityId, float wishX, float wishZ, float speed)> move;
    std::function<bool(uint32_t entityId, float impulse)> jump;
};

/**
 * Manages Lua scripting for the engine
 * Provides C++ bindings to engine systems
 * Handles script loading, execution, and lifecycle
 */
class LuaScriptEngine {
public:
    LuaScriptEngine();
    ~LuaScriptEngine();

    /**
     * Initialize the Lua engine with bindings
     */
    void Initialize(Scene* scene, GLFWwindow* window);

    /**
     * Load and execute a Lua script file
     * Returns true if successful
     */
    bool LoadScript(const std::string& filepath);

    /**
     * Call a global Lua function with no arguments
     */
    bool CallFunction(const std::string& functionName);

    /**
     * Call a global Lua function for an entity
     * Pushes entity as userdata argument
     */
    bool CallEntityFunction(const std::string& functionName, Entity entity);

    /**
     * Call a function on the entity's gameplay Lua script with arguments.
     * Returns false when the entity has no Lua script defining it.
     */
    bool CallEntityFunction(const std::string& functionName, Entity entity,
                            const std::vector<PropertyValue>& args);

    /// Resolve an authored script path against the project and plugin search paths.
    std::filesystem::path ResolveScriptPath(const std::string& scriptPath) const;

    /// Plugin-language scripts that entity:Call falls back to when the target
    /// entity's script is not Lua.
    void SetPluginScriptHost(PluginScriptHost* host) { m_PluginScripts = host; }

    /**
     * Execute Lua code directly
     */
    bool ExecuteString(const std::string& luaCode);

    /// Runs console input in the global environment: an expression's values,
    /// or a statement. Returns the values or the error; ok says which.
    std::string EvaluateConsole(const std::string& code, bool& ok);

    struct ApiEntry {
        std::string name; // "print", "Input", "Input.isActionDown"
        std::string kind; // Lua type name of the value
    };

    /// Globals and the members of global tables that scripts can use.
    static std::vector<ApiEntry> DescribeApi();

    /**
     * Get the Lua state (for advanced usage)
     */
    lua_State* GetState() { return m_State; }

    /**
     * Returns true if Lua is available and initialized
     */
    bool IsLuaAvailable() const;

    /**
     * Update active Lua scripts (onUpdate)
     */
    void Update(Scene& scene, float deltaTime);

    /**
     * Force every live script instance to reload on the next Update().
     *
     * This reuses the existing mtime/embedded-source reload path rather than
     * building a parallel loader. Used by the editor automation host to
     * implement a semantic script reload request.
     */
    void RequestReloadAll();

    /**
     * Late update active Lua scripts (onLateUpdate)
     */
    void LateUpdate(Scene& scene, float deltaTime);

    /**
     * Dispatch onAnimationFinished after CharacterAnimationSystem::Tick.
     */
    void DispatchAnimationFinished(Scene& scene);

    /**
     * Dispatch onTrigger* and onCollision* after the fixed physics step.
     * Exit events pass no contact. A destroyed other entity is nil.
     */
    void DispatchContactEvents(Scene& scene, const std::vector<Physics::PhysicsContactEvent>& events);

    /**
     * Notify engine that play mode just started
     */
    void OnSimulationStarted();

    /**
     * Notify engine that play mode ended (stop button)
     */
    void OnSimulationStopped();

    /**
     * Handle entity destruction so onDestroy can be invoked immediately
     */
    void OnEntityDestroyed(Entity entity);

    /**
     * Update window pointer for input bindings
     */
    void SetWindow(GLFWwindow* window);

    /**
     * Configure where Lua should search for script files
     */
    void SetProjectRoot(const std::filesystem::path& rootPath);

    /**
     * Renderer used to upload Character visuals when Scene.instantiate spawns a prefab.
     */
    void SetFrameRenderer(Hephaestus::EngineFrameRenderer* renderer);

    /**
     * Shutdown and cleanup
     */
    void Shutdown();

    /**
     * Provide camera control callbacks for Lua bindings
     */
    void SetCameraBinding(CameraBinding binding);

    /**
     * Provide physics callbacks (Jolt) for Lua bindings
     */
    void SetPhysicsBinding(PhysicsBinding binding);
    void SetCharacterBinding(CharacterBinding binding);

    /// Loads and uploads the mesh of a renderer spawned by Scene.instantiate when no entity has it loaded yet.
    using SpawnedMeshLoader = std::function<void(MeshRendererComponent&)>;
    void SetSpawnedMeshLoader(SpawnedMeshLoader loader);

    /// Provide the shared input system populated by the current host.
    void SetInputSystem(Hephaestus::InputSystem* inputSystem);
    Hephaestus::InputSystem* GetInputSystem() const { return m_InputSystem; }

    using QuitRequestCallback = std::function<void()>;
    void SetQuitRequestCallback(QuitRequestCallback cb) { m_QuitRequestCallback = std::move(cb); }

    using SceneReloadCallback = std::function<bool()>;
    void SetSceneReloadCallback(SceneReloadCallback cb) { m_SceneReloadCallback = std::move(cb); }

    /**
     * Provide audio backend for Lua Audio.* bindings.
     */
    void SetAudioEngine(AudioEngine* audioEngine);

    /**
     * Provide runtime UI bridge for Lua UI.* bindings.
     */
    void SetUIBindings(Hephaestus::HephaestusUI::UISystem* uiSystem,
                       Hephaestus::HephaestusUI::UIContext* uiContext,
                       Hephaestus::HephaestusUI::UIRenderer* uiRenderer);

private:
    friend struct LuaEntityReference;

    lua_State* m_State = nullptr;
    PluginScriptHost* m_PluginScripts = nullptr;
    Scene* m_Scene = nullptr;
    GLFWwindow* m_Window = nullptr;
    std::filesystem::path m_ProjectRoot;
    Hephaestus::EngineFrameRenderer* m_FrameRenderer = nullptr;
    CameraBinding m_CameraBinding{};
    PhysicsBinding m_PhysicsBinding{};
    CharacterBinding m_CharacterBinding{};
    SpawnedMeshLoader m_SpawnedMeshLoader;
    Hephaestus::InputSystem* m_InputSystem = nullptr;
    std::unordered_set<std::string> m_MissingInputNames;
    QuitRequestCallback m_QuitRequestCallback;
    SceneReloadCallback m_SceneReloadCallback;
    AudioEngine* m_AudioEngine = nullptr;
    Hephaestus::HephaestusUI::UISystem* m_UISystem = nullptr;
    Hephaestus::HephaestusUI::UIContext* m_UIContext = nullptr;
    Hephaestus::HephaestusUI::UIRenderer* m_UIRenderer = nullptr;
    int m_CurrentCursorMode = 0; // 0=normal, 1=disabled

    struct ScriptInstance {
        uint32_t entityId = 0;
        LuaScriptSlot slot = LuaScriptSlot::Gameplay;
        std::string userPath;
        std::filesystem::path resolvedPath;
        std::filesystem::file_time_type lastWriteTime{};
        size_t embeddedSourceHash = 0;
        bool forceReload = false;
        int environmentRef = -1;
        int onAwakeRef = -1;
        int onEnableRef = -1;
        int onUpdateRef = -1;
        int onLateUpdateRef = -1;
        int onDisableRef = -1;
        int onDestroyRef = -1;
        int onAnimationFinishedRef = -1;
        int onTriggerEnterRef = -1;
        int onTriggerStayRef = -1;
        int onTriggerExitRef = -1;
        int onCollisionEnterRef = -1;
        int onCollisionStayRef = -1;
        int onCollisionExitRef = -1;
        bool awakeCalled = false;
        bool valid = false;
        bool errorLogged = false;
        bool functionErrorLogged = false;
        std::string lastBindingError;
    };

    std::unordered_map<LuaScriptInstanceKey, ScriptInstance, LuaScriptInstanceKeyHash> m_ScriptInstances;
    std::vector<LuaScriptInstanceKey> m_ActiveUpdateOrder;
    std::vector<std::filesystem::path> m_SearchPaths;

    struct PendingSpawn {
        std::string prefabPath;
        bool hasPosition = false;
        Vec3 position{0.0f, 0.0f, 0.0f};
        bool hasRotation = false;
        Vec3 rotationDegrees{0.0f, 0.0f, 0.0f};
        std::unordered_map<std::string, PropertyValue> vars;
    };

    struct PendingDestroy {
        uint32_t entityId = 0;
    };

    std::vector<PendingSpawn> m_PendingSpawns;
    std::vector<PendingDestroy> m_PendingDestroys;
    bool m_UpdatingScripts = false;
    bool m_SceneReloadRequested = false;

    uint32_t m_CurrentEntityId = std::numeric_limits<uint32_t>::max();
    float m_CurrentDeltaTime = 0.0f;

    static LuaScriptEngine* s_Instance;
    static LuaScriptEngine* GetInstance();

    /**
     * Register all C++ bindings with Lua
     */
    void RegisterBindings();

    /**
     * Register Entity bindings
     */
    void RegisterEntityBindings();

    /**
     * Register Transform bindings
     */
    void RegisterTransformBindings();
    void RegisterMeshBindings();
    void RegisterParticleBindings();

    /**
     * Register Input bindings
     */
    void RegisterInputBindings();

    /**
     * Register utility functions
     */
    void RegisterUtilityBindings();

    /**
     * Register script parameter declarations
     */
    void RegisterExposeBindings();

    /**
     * Register camera bindings
     */
    void RegisterCameraBindings();

    /**
     * Register physics bindings
     */
    void RegisterPhysicsBindings();
    void RegisterCharacterBindings();

    /**
     * Register audio bindings
     */
    void RegisterAudioBindings();
    void RegisterUIBindings();

    void RebuildSearchPaths();
    void AddSearchPath(const std::filesystem::path& path);

    static LuaScriptInstanceKey MakeInstanceKey(uint32_t entityId, LuaScriptSlot slot);
    ScriptInstance* FindInstance(uint32_t entityId, LuaScriptSlot slot);
    ScriptInstance* EnsureInstance(Scene& scene,
                                   entt::entity entityHandle,
                                   LuaScriptComponent& component,
                                   LuaScriptSlot slot);
    void DestroyInstance(LuaScriptInstanceKey key, bool callOnDestroy);
    void DestroyAllSlotsForEntity(uint32_t entityId, bool callOnDestroy);
    void AwakeInstanceIfNeeded(Scene& scene, entt::entity entityHandle, ScriptInstance& instance);
    void UpdateInstance(Scene& scene, entt::entity entityHandle, ScriptInstance& instance, float deltaTime);
    bool PushSocketTable(lua_State* L, uint32_t entityId, const char* socketName, bool sceneWorld);
    void LogBindingError(ScriptInstance* instance, const std::string& message);
    void LogBindingErrorForEntity(uint32_t entityId, const std::string& message);
    CharacterComponent* CharacterFromLuaSelf(lua_State* L, const char* meta);
    void ReleaseInstanceResources(ScriptInstance& instance);
    bool LoadScriptForInstance(ScriptInstance& instance, const LuaScriptComponent& component, Entity entity);
    void ApplyVariablesToEnvironment(ScriptInstance& instance, const LuaScriptComponent& component);
    void PushPropertyValue(const PropertyValue& value);
    int FetchFunctionRef(int environmentRef, const std::string& functionName);
    bool CallInstanceFunction(ScriptInstance& instance, int functionRef, Entity entity, float deltaTime, bool includeDelta);
    bool CallInstanceFunctionByName(ScriptInstance& instance, const std::string& functionName, Entity entity, float deltaTime, bool includeDelta);
    Entity MakeEntity(uint32_t entityId) const;
    TransformComponent* GetTransform(uint32_t entityId) const;
    TagComponent* GetTag(uint32_t entityId) const;
    bool CurrentEntityValid() const;

    void LogLuaError(const std::string& context);

    // Setup the "this" global for OOP-style script access
    void SetupThisForEntity(uint32_t entityId);

    void PushEntityUserdata(lua_State* L, uint32_t entityId);
    std::filesystem::path ResolvePrefabPath(const std::string& prefabPath) const;
    Entity ExecuteSpawn(const PendingSpawn& spawn);
    void ExecuteDestroy(uint32_t entityId);
    void FlushDeferredSceneOps();
    void ClearDeferredSceneOps();
    PropertyValue ParsePropertyValueFromLua(lua_State* L, int index);

    // Lua callback helpers
    static int Lua_Transform_getPosition(lua_State* L);
    static int Lua_Transform_setPosition(lua_State* L);
    static int Lua_Transform_translate(lua_State* L);
    static int Lua_Transform_getRotation(lua_State* L);
    static int Lua_Transform_setRotation(lua_State* L);
    static int Lua_Transform_rotate(lua_State* L);
    static int Lua_Transform_getScale(lua_State* L);
    static int Lua_Transform_setScale(lua_State* L);
    static int Lua_Mesh_setVisible(lua_State* L);
    static int Lua_Mesh_isVisible(lua_State* L);
    static int Lua_Particles_play(lua_State* L);
    static int Lua_Particles_stop(lua_State* L);
    static int Lua_Particles_burst(lua_State* L);
    static int Lua_Particles_isPlaying(lua_State* L);
    static int Lua_Particles_count(lua_State* L);

    static int Lua_Entity_isValid(lua_State* L);
    static int Lua_Entity_getName(lua_State* L);
    static int Lua_Entity_setName(lua_State* L);

    static int Lua_Input_isKeyPressed(lua_State* L);
    static int Lua_Input_isMouseButtonPressed(lua_State* L);
    static int Lua_Input_getMousePosition(lua_State* L);
    static int Lua_Input_getMouseDelta(lua_State* L);
    static int Lua_Input_getScrollDelta(lua_State* L);
    static int Lua_Input_isActionDown(lua_State* L);
    static int Lua_Input_isActionPressed(lua_State* L);
    static int Lua_Input_isActionReleased(lua_State* L);
    static int Lua_Input_getAxis(lua_State* L);
    static int Lua_Input_isGamepadConnected(lua_State* L);
    static int Lua_Input_setCursorMode(lua_State* L);

    static int Lua_Time_deltaTime(lua_State* L);

    static int Lua_Application_quit(lua_State* L);

    static int Lua_Camera_setActive(lua_State* L);
    static int Lua_Camera_isActive(lua_State* L);
    static int Lua_Camera_setPosition(lua_State* L);
    static int Lua_Camera_getPosition(lua_State* L);
    static int Lua_Camera_setRotation(lua_State* L);
    static int Lua_Camera_getRotation(lua_State* L);
    static int Lua_Camera_translate(lua_State* L);

    static int Lua_Physics_isAvailable(lua_State* L);
    static int Lua_Physics_hasBody(lua_State* L);
    static int Lua_Physics_getLinearVelocity(lua_State* L);
    static int Lua_Physics_setLinearVelocity(lua_State* L);
    static int Lua_Physics_setAngularVelocity(lua_State* L);
    static int Lua_Physics_setRotation(lua_State* L);
    static int Lua_Physics_raycast(lua_State* L);
    static int Lua_Physics_isGrounded(lua_State* L);
    static int Lua_Physics_move(lua_State* L);
    static int Lua_Physics_jump(lua_State* L);
    static int Lua_Character_move(lua_State* L);
    static int Lua_Character_jump(lua_State* L);
    static int Lua_Character_setCrouched(lua_State* L);
    static int Lua_Character_isGrounded(lua_State* L);
    static int Lua_Character_setSpeedMultiplier(lua_State* L);
    static int Lua_Character_getSpeedMultiplier(lua_State* L);
    static int Lua_Character_isCrouched(lua_State* L);
    static int Lua_Character_isStandBlocked(lua_State* L);
    static int Lua_Character_setProne(lua_State* L);
    static int Lua_Character_isProne(lua_State* L);
    static int Lua_Character_getVelocity(lua_State* L);
    static int Lua_Character_getGroundNormal(lua_State* L);
    static int Lua_Character_getGroundVelocity(lua_State* L);
    static int Lua_Character_getGroundState(lua_State* L);
    static int Lua_Character_teleport(lua_State* L);
    static int Lua_Character_setFacingYaw(lua_State* L);

    static int Lua_Audio_isAvailable(lua_State* L);
    static int Lua_Audio_play(lua_State* L);
    static int Lua_Audio_stop(lua_State* L);
    static int Lua_Audio_pause(lua_State* L);
    static int Lua_Audio_resume(lua_State* L);
    static int Lua_Audio_setVolume(lua_State* L);
    static int Lua_Audio_isPlaying(lua_State* L);
    static int Lua_Audio_stopAll(lua_State* L);
    static int Lua_Audio_setPitch(lua_State* L);
    static int Lua_Audio_setPosition(lua_State* L);
    static int Lua_Audio_setBusVolume(lua_State* L);
    static int Lua_Audio_getBusVolume(lua_State* L);
    static int Lua_Audio_getVoiceCount(lua_State* L);

    static int Lua_UI_isAvailable(lua_State* L);
    static int Lua_UI_createSimpleMenu(lua_State* L);
    static int Lua_UI_createScreen(lua_State* L);
    static int Lua_UI_getRoot(lua_State* L);
    static int Lua_UI_createPanel(lua_State* L);
    static int Lua_UI_createText(lua_State* L);
    static int Lua_UI_createButton(lua_State* L);
    static int Lua_UI_createImage(lua_State* L);
    static int Lua_UI_createVerticalBox(lua_State* L);
    static int Lua_UI_createHorizontalBox(lua_State* L);
    static int Lua_UI_clearScreens(lua_State* L);
    static int Lua_UI_getScreenCount(lua_State* L);
    static int Lua_UI_setScreenVisible(lua_State* L);
    static int Lua_UI_setScreenActive(lua_State* L);
    static int Lua_UI_setViewportSize(lua_State* L);
    static int Lua_UI_getDrawCommandCount(lua_State* L);
    static int Lua_UI_setPosition(lua_State* L);
    static int Lua_UI_setSize(lua_State* L);
    static int Lua_UI_setVisible(lua_State* L);
    static int Lua_UI_setOpacity(lua_State* L);
    static int Lua_UI_setZOrder(lua_State* L);
    static int Lua_UI_setText(lua_State* L);
    static int Lua_UI_setImage(lua_State* L);
    static int Lua_UI_setSpacing(lua_State* L);
    static int Lua_UI_setCursorMode(lua_State* L);
    static int Lua_UI_getCursorMode(lua_State* L);
    static int Lua_UI_setOnClick(lua_State* L);
    static int Lua_UI_setOnHover(lua_State* L);
    static int Lua_UI_createGrid(lua_State* L);
    static int Lua_UI_setGridColumns(lua_State* L);
    static int Lua_UI_setGridSpacing(lua_State* L);
    static int Lua_UI_setEnabled(lua_State* L);
    static int Lua_UI_setPadding(lua_State* L);
    static int Lua_UI_setMargin(lua_State* L);
    static int Lua_UI_setMinSize(lua_State* L);
    static int Lua_UI_setMaxSize(lua_State* L);
    static int Lua_UI_setAnchor(lua_State* L);
    static int Lua_UI_setAlignment(lua_State* L);
    static int Lua_UI_setBackgroundColor(lua_State* L);
    static int Lua_UI_setBorderColor(lua_State* L);
    static int Lua_UI_setBorderWidth(lua_State* L);
    static int Lua_UI_setCornerRadius(lua_State* L);
    static int Lua_UI_setTextColor(lua_State* L);
    static int Lua_UI_setFontSize(lua_State* L);
    static int Lua_UI_setFontBold(lua_State* L);
    static int Lua_UI_setFontItalic(lua_State* L);
    static int Lua_UI_setTextAlignment(lua_State* L);
    static int Lua_UI_setTextShadow(lua_State* L);
    static int Lua_UI_setWordWrap(lua_State* L);
    static int Lua_UI_setStyle(lua_State* L);
    static int Lua_UI_setButtonStyle(lua_State* L);
    static int Lua_UI_setFocused(lua_State* L);
    static int Lua_UI_registerThemeStyle(lua_State* L);
    static int Lua_UI_applyThemeStyle(lua_State* L);
    static int Lua_UI_setChildAlignment(lua_State* L);
    static int Lua_UI_setImageScaleMode(lua_State* L);
    static int Lua_UI_setImageTint(lua_State* L);

    static int Lua_UI_loadHUI(lua_State* L);
    static int Lua_UI_getWidget(lua_State* L);

    // display / destroy global Lua API
    static int Lua_Display_ui(lua_State* L);
    static int Lua_Destroy_ui(lua_State* L);
    void RegisterDisplayDestroyBindings();

    static Hephaestus::HephaestusUI::UIStyle ParseStyleTable(lua_State* L, int tableIndex);
    static void ApplyStyleToPanel(Hephaestus::HephaestusUI::UIElement* element,
                                  const Hephaestus::HephaestusUI::UIStyle& style);

    uint32_t RegisterUIScreenHandle(const std::shared_ptr<Hephaestus::HephaestusUI::UIScreen>& screen);
    uint32_t RegisterUIElementHandle(Hephaestus::HephaestusUI::UIElement* element);
    std::shared_ptr<Hephaestus::HephaestusUI::UIScreen> ResolveUIScreen(uint32_t handle) const;
    Hephaestus::HephaestusUI::UIElement* ResolveUIElement(uint32_t handle) const;
    void ResetUIHandleMaps();

    uint32_t m_NextUIScreenHandle = 1;
    uint32_t m_NextUIElementHandle = 1;
    std::unordered_map<uint32_t, std::shared_ptr<Hephaestus::HephaestusUI::UIScreen>> m_UIScreenHandles;
    std::unordered_map<uint32_t, Hephaestus::HephaestusUI::UIElement*> m_UIElementHandles;
    std::unordered_map<const Hephaestus::HephaestusUI::UIElement*, uint32_t> m_UIElementReverseHandles;
    std::unordered_map<uint32_t, int> m_UIButtonClickCallbacks;
    std::unordered_map<uint32_t, int> m_UIHoverCallbacks;

    // HUI asset widget maps: screen handle -> (widget id -> element handle)
    std::unordered_map<uint32_t, std::unordered_map<std::string, uint32_t>> m_HUIWidgetMaps;

    // Managed HUI instance lifecycle tracking
    struct HUIInstance {
        uint32_t screenHandle = 0;
        std::string sourcePath;
        bool alive = false;
    };
    std::unordered_map<uint32_t, HUIInstance> m_ActiveHUIInstances;
    std::unordered_map<std::string, uint32_t> m_HUIPathToHandle;
    std::vector<uint32_t> m_HUIStack; // most-recent-last; back() = current

    uint32_t displayHUI(const std::string& filePath, std::string& outError);
    bool destroyHUI(uint32_t screenHandle);
    void destroyAllHUIInstances();
    std::string normalizeHUIPath(const std::string& rawPath) const;

    // 'this' entity binding methods (OOP-style API)
    static int Lua_This_GetTransform(lua_State* L);
    static int Lua_This_GetName(lua_State* L);
    static int Lua_This_SetName(lua_State* L);
    static int Lua_This_IsValid(lua_State* L);
    static int Lua_This_GetId(lua_State* L);
    static int Lua_This_GetCamera(lua_State* L);
    
    // Transform object methods (returned by this:GetTransform())
    static int Lua_TransformObj_GetPosition(lua_State* L);
    static int Lua_TransformObj_GetWorldPosition(lua_State* L);
    static int Lua_TransformObj_SetPosition(lua_State* L);
    static int Lua_TransformObj_GetRotation(lua_State* L);
    static int Lua_TransformObj_SetRotation(lua_State* L);
    static int Lua_TransformObj_GetScale(lua_State* L);
    static int Lua_TransformObj_SetScale(lua_State* L);
    static int Lua_TransformObj_Translate(lua_State* L);
    static int Lua_TransformObj_Rotate(lua_State* L);

    // Camera object methods (returned by this:GetCamera())
    static int Lua_CameraObj_GetFov(lua_State* L);
    static int Lua_CameraObj_SetFov(lua_State* L);
    static int Lua_CameraObj_GetNear(lua_State* L);
    static int Lua_CameraObj_SetNear(lua_State* L);
    static int Lua_CameraObj_GetFar(lua_State* L);
    static int Lua_CameraObj_SetFar(lua_State* L);
    static int Lua_CameraObj_IsActive(lua_State* L);
    static int Lua_CameraObj_SetActive(lua_State* L);

    // Scene camera queries + spawn/destroy
    static int Lua_Scene_FindFirstCamera(lua_State* L);
    static int Lua_Scene_FindCameras(lua_State* L);
    static int Lua_Scene_SetActiveCamera(lua_State* L);
    static int Lua_Scene_GetActiveCamera(lua_State* L);
    static int Lua_Scene_Instantiate(lua_State* L);
    static int Lua_Scene_Destroy(lua_State* L);
    static int Lua_Scene_FindAllWithPrefix(lua_State* L);
    static int Lua_Scene_Reload(lua_State* L);

    static int Lua_This_Call(lua_State* L);
    static int Lua_This_CallController(lua_State* L);
    static int CallNamedOnSlot(lua_State* L, LuaScriptSlot slot);
    static int Lua_This_GetAnimator(lua_State* L);
    static int Lua_This_GetCharacter(lua_State* L);

    static int Lua_Animator_Play(lua_State* L);
    static int Lua_Animator_CrossFade(lua_State* L);
    static int Lua_Animator_Pause(lua_State* L);
    static int Lua_Animator_Resume(lua_State* L);
    static int Lua_Animator_Stop(lua_State* L);
    static int Lua_Animator_SetSpeed(lua_State* L);
    static int Lua_Animator_IsPlaying(lua_State* L);
    static int Lua_Animator_GetCurrentAnimation(lua_State* L);
    static int Lua_Animator_GetTime(lua_State* L);
    static int Lua_Animator_GetDuration(lua_State* L);

    static int Lua_Character_GetSocket(lua_State* L);
    static int Lua_Character_GetSocketLocal(lua_State* L);

    void RegisterApplicationBindings();
    void RegisterThisBindings();
    void RegisterSceneBindings();
};
