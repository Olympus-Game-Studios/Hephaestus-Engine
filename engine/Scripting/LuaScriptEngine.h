#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <limits>
#include <cstdint>
#include <functional>

#ifndef LUA_AVAILABLE
    #define LUA_AVAILABLE 0
#endif

#include "../Core/Math.h"

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

private:
    lua_State* m_State = nullptr;
    Scene* m_Scene = nullptr;
    GLFWwindow* m_Window = nullptr;
    std::filesystem::path m_ProjectRoot;
    CameraBinding m_CameraBinding{};

    struct ScriptInstance {
        uint32_t entityId = 0;
        std::string userPath;
        std::filesystem::path resolvedPath;
        std::filesystem::file_time_type lastWriteTime{};
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

    void RebuildSearchPaths();
    void AddSearchPath(const std::filesystem::path& path);

    ScriptInstance* EnsureInstance(Scene& scene, entt::entity entityHandle, LuaScriptComponent& component);
    void DestroyInstance(uint32_t entityId, bool callOnDestroy);
    void ReleaseInstanceResources(ScriptInstance& instance);
    bool LoadScriptForInstance(ScriptInstance& instance, const LuaScriptComponent& component, Entity entity);
    int FetchFunctionRef(int environmentRef, const std::string& functionName);
    bool CallInstanceFunction(ScriptInstance& instance, int functionRef, Entity entity, float deltaTime, bool includeDelta);
    bool CallInstanceFunctionByName(ScriptInstance& instance, const std::string& functionName, Entity entity, float deltaTime, bool includeDelta);
    std::filesystem::path ResolveScriptPath(const std::string& scriptPath) const;
    Entity MakeEntity(uint32_t entityId) const;
    TransformComponent* GetTransform(uint32_t entityId) const;
    TagComponent* GetTag(uint32_t entityId) const;
    bool CurrentEntityValid() const;

    void LogLuaError(const std::string& context);

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

    static int Lua_Time_deltaTime(lua_State* L);

    static int Lua_Camera_setActive(lua_State* L);
    static int Lua_Camera_isActive(lua_State* L);
    static int Lua_Camera_setPosition(lua_State* L);
    static int Lua_Camera_getPosition(lua_State* L);
    static int Lua_Camera_setRotation(lua_State* L);
    static int Lua_Camera_getRotation(lua_State* L);
    static int Lua_Camera_translate(lua_State* L);

    static LuaScriptEngine* GetInstance() { return s_Instance; }
};
