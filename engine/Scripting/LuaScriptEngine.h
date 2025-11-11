#pragma once
#include <string>
#include <memory>
#include <unordered_map>

#ifdef LUA_VERSION
    #include <lua.hpp>
#else
    // Lua not available - use stubs
    typedef void lua_State;
#endif

// Forward declarations
class Entity;
class Scene;

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
    void Initialize(Scene* scene);

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
     * Shutdown and cleanup
     */
    void Shutdown();

private:
    lua_State* m_State = nullptr;
    Scene* m_Scene = nullptr;

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
};
