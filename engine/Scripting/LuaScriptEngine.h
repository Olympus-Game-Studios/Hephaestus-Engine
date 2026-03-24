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

#if LUA_AVAILABLE
    #include <lua.hpp>
#else
    // Lua not available - use stubs
    typedef void lua_State;
#endif

// Forward declarations
class Entity;
class Scene;
struct GLFWwindow;
struct LuaScriptComponent;
struct TransformComponent;
struct TagComponent;
class AudioEngine;

namespace Hephaestus::HephaestusUI {
    class UIElement;
    class UIScreen;
    class UISystem;
    class UIContext;
    class UIRenderer;
}

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
};

struct PhysicsBinding {
    std::function<bool(uint32_t entityId)> hasBody;
    std::function<bool(uint32_t entityId, Vec3& outVelocity)> getLinearVelocity;
    std::function<bool(uint32_t entityId, const Vec3& velocity, bool activate)> setLinearVelocity;
    std::function<bool(uint32_t entityId, const Vec3& velocity, bool activate)> setAngularVelocity;
    std::function<bool(uint32_t entityId, const Vec3& rotationEuler, bool activate)> setRotation;
    std::function<bool(uint32_t entityId, const Vec3& origin, const Vec3& direction, float maxDistance, PhysicsRaycastHit& outHit)> raycast;
};

/**
 * Platform-independent input state that the host (Qt editor, etc.) can populate
 * each frame so Lua scripts receive correct Input.* values even without GLFW.
 */
struct RuntimeInputState {
    /// Bitfield indexed by GLFW key codes (0..GLFW_KEY_LAST).
    /// true = pressed this frame.
    static constexpr int kMaxKeys = 512;
    bool keys[kMaxKeys] = {};

    /// Mouse buttons indexed by GLFW mouse button codes (0..7).
    static constexpr int kMaxMouseButtons = 8;
    bool mouseButtons[kMaxMouseButtons] = {};

    /// Cursor position in viewport pixels.
    double mouseX = 0.0;
    double mouseY = 0.0;

    /// Mouse movement delta (pixels) since last frame.
    double mouseDeltaX = 0.0;
    double mouseDeltaY = 0.0;

    /// Cursor mode requested by Lua scripts.
    /// 0 = normal, 1 = disabled (captured + hidden + infinite movement)
    int cursorModeRequest = 0;

    /// Set to true each frame the host has populated these fields.
    bool valid = false;

    void Reset() {
        for (int i = 0; i < kMaxKeys; ++i) keys[i] = false;
        for (int i = 0; i < kMaxMouseButtons; ++i) mouseButtons[i] = false;
        mouseX = mouseY = 0.0;
        mouseDeltaX = mouseDeltaY = 0.0;
        cursorModeRequest = 0;
        valid = false;
    }
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
     * Execute Lua code directly
     */
    bool ExecuteString(const std::string& luaCode);

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
     * Late update active Lua scripts (onLateUpdate)
     */
    void LateUpdate(Scene& scene, float deltaTime);

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

    /**
     * Provide a platform-independent input state for environments without GLFW.
     * The host must call this each frame BEFORE Update() to keep keys fresh.
     */
    void SetRuntimeInputState(const RuntimeInputState& state);
    RuntimeInputState& GetRuntimeInputState() { return m_RuntimeInput; }

    using QuitRequestCallback = std::function<void()>;
    void SetQuitRequestCallback(QuitRequestCallback cb) { m_QuitRequestCallback = std::move(cb); }

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
    lua_State* m_State = nullptr;
    Scene* m_Scene = nullptr;
    GLFWwindow* m_Window = nullptr;
    std::filesystem::path m_ProjectRoot;
    CameraBinding m_CameraBinding{};
    PhysicsBinding m_PhysicsBinding{};
    RuntimeInputState m_RuntimeInput{};
    QuitRequestCallback m_QuitRequestCallback;
    AudioEngine* m_AudioEngine = nullptr;
    Hephaestus::HephaestusUI::UISystem* m_UISystem = nullptr;
    Hephaestus::HephaestusUI::UIContext* m_UIContext = nullptr;
    Hephaestus::HephaestusUI::UIRenderer* m_UIRenderer = nullptr;
    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;
    bool m_HasLastMousePos = false;
    int m_CurrentCursorMode = 0; // 0=normal, 1=disabled

    struct ScriptInstance {
        uint32_t entityId = 0;
        std::string userPath;
        std::filesystem::path resolvedPath;
        std::filesystem::file_time_type lastWriteTime{};
        size_t embeddedSourceHash = 0;
        int environmentRef = -1;
        int onAwakeRef = -1;
        int onEnableRef = -1;
        int onUpdateRef = -1;
        int onLateUpdateRef = -1;
        int onDisableRef = -1;
        int onDestroyRef = -1;
        bool awakeCalled = false;
        bool valid = false;
        bool errorLogged = false;
    };

    std::unordered_map<uint32_t, ScriptInstance> m_ScriptInstances;
    std::vector<uint32_t> m_ActiveUpdateOrder;
    std::vector<std::filesystem::path> m_SearchPaths;

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

    /**
     * Register Input bindings
     */
    void RegisterInputBindings();

    /**
     * Register utility functions
     */
    void RegisterUtilityBindings();

    /**
     * Register camera bindings
     */
    void RegisterCameraBindings();

    /**
     * Register physics bindings
     */
    void RegisterPhysicsBindings();

    /**
     * Register audio bindings
     */
    void RegisterAudioBindings();
    void RegisterUIBindings();

    void RebuildSearchPaths();
    void AddSearchPath(const std::filesystem::path& path);

    ScriptInstance* EnsureInstance(Scene& scene, entt::entity entityHandle, LuaScriptComponent& component);
    void DestroyInstance(uint32_t entityId, bool callOnDestroy);
    void ReleaseInstanceResources(ScriptInstance& instance);
    bool LoadScriptForInstance(ScriptInstance& instance, const LuaScriptComponent& component, Entity entity);
    void ApplyVariablesToEnvironment(ScriptInstance& instance, const LuaScriptComponent& component);
    void PushPropertyValue(const PropertyValue& value);
    int FetchFunctionRef(int environmentRef, const std::string& functionName);
    bool CallInstanceFunction(ScriptInstance& instance, int functionRef, Entity entity, float deltaTime, bool includeDelta);
    bool CallInstanceFunctionByName(ScriptInstance& instance, const std::string& functionName, Entity entity, float deltaTime, bool includeDelta);
    std::filesystem::path ResolveScriptPath(const std::string& scriptPath) const;
    Entity MakeEntity(uint32_t entityId) const;
    TransformComponent* GetTransform(uint32_t entityId) const;
    TagComponent* GetTag(uint32_t entityId) const;
    bool CurrentEntityValid() const;

    void LogLuaError(const std::string& context);
    
    // Setup the "this" global for OOP-style script access
    void SetupThisForEntity(uint32_t entityId);

    // Lua callback helpers
    static int Lua_Transform_getPosition(lua_State* L);
    static int Lua_Transform_setPosition(lua_State* L);
    static int Lua_Transform_translate(lua_State* L);
    static int Lua_Transform_getRotation(lua_State* L);
    static int Lua_Transform_setRotation(lua_State* L);
    static int Lua_Transform_rotate(lua_State* L);
    static int Lua_Transform_getScale(lua_State* L);
    static int Lua_Transform_setScale(lua_State* L);

    static int Lua_Entity_isValid(lua_State* L);
    static int Lua_Entity_getName(lua_State* L);
    static int Lua_Entity_setName(lua_State* L);

    static int Lua_Input_isKeyPressed(lua_State* L);
    static int Lua_Input_isMouseButtonPressed(lua_State* L);
    static int Lua_Input_getMousePosition(lua_State* L);
    static int Lua_Input_getMouseDelta(lua_State* L);
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

    static int Lua_Audio_isAvailable(lua_State* L);
    static int Lua_Audio_play(lua_State* L);
    static int Lua_Audio_stop(lua_State* L);
    static int Lua_Audio_pause(lua_State* L);
    static int Lua_Audio_resume(lua_State* L);
    static int Lua_Audio_setVolume(lua_State* L);
    static int Lua_Audio_isPlaying(lua_State* L);
    static int Lua_Audio_stopAll(lua_State* L);

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
    std::unordered_map<uint32_t, int> m_UIButtonClickCallbacks;  // elementHandle -> Lua registry ref

    // 'this' entity binding methods (OOP-style API)
    static int Lua_This_GetTransform(lua_State* L);
    static int Lua_This_GetName(lua_State* L);
    static int Lua_This_SetName(lua_State* L);
    static int Lua_This_IsValid(lua_State* L);
    static int Lua_This_GetCamera(lua_State* L);
    
    // Transform object methods (returned by this:GetTransform())
    static int Lua_TransformObj_GetPosition(lua_State* L);
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

    // Scene camera queries
    static int Lua_Scene_FindFirstCamera(lua_State* L);
    static int Lua_Scene_FindCameras(lua_State* L);
    static int Lua_Scene_SetActiveCamera(lua_State* L);
    static int Lua_Scene_GetActiveCamera(lua_State* L);

    void RegisterApplicationBindings();
    void RegisterThisBindings();
    void RegisterSceneBindings();
};