#include "LuaScriptEngine.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"
#include "../Scene/Components.h"
#include "../Core/Math.h"
#include <iostream>
#include <fstream>
#include <sstream>

#ifdef LUA_VERSION
    #define LUA_AVAILABLE 1
#else
    #define LUA_AVAILABLE 0
#endif

LuaScriptEngine::LuaScriptEngine() {
#if LUA_AVAILABLE
    m_State = luaL_newstate();
    if (!m_State) {
        std::cerr << "Failed to create Lua state\n";
    }
#else
    m_State = nullptr;
    std::cerr << "Lua not available - scripting disabled. Install Lua to enable.\n";
#endif
}

LuaScriptEngine::~LuaScriptEngine() {
    Shutdown();
}

void LuaScriptEngine::Initialize(Scene* scene) {
#if LUA_AVAILABLE
    if (!m_State) {
        std::cerr << "Lua state not initialized\n";
        return;
    }

    m_Scene = scene;

    // Open standard libraries
    luaL_openlibs(m_State);

    // Register all C++ bindings
    RegisterBindings();

    std::cout << "Lua scripting engine initialized\n";
#else
    std::cerr << "Lua not available - skipping initialization\n";
#endif
}

bool LuaScriptEngine::LoadScript(const std::string& filepath) {
#if LUA_AVAILABLE
    if (!m_State) {
        std::cerr << "Lua state not initialized\n";
        return false;
    }

    // Read file
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open Lua script: " << filepath << "\n";
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    return ExecuteString(buffer.str());
#else
    std::cerr << "Lua not available\n";
    return false;
#endif
}

bool LuaScriptEngine::ExecuteString(const std::string& luaCode) {
#if LUA_AVAILABLE
    if (!m_State) {
        std::cerr << "Lua state not initialized\n";
        return false;
    }

    int result = luaL_dostring(m_State, luaCode.c_str());
    if (result != LUA_OK) {
        std::cerr << "Lua error: " << lua_tostring(m_State, -1) << "\n";
        lua_pop(m_State, 1);
        return false;
    }

    return true;
#else
    return false;
#endif
}

bool LuaScriptEngine::CallFunction(const std::string& functionName) {
#if LUA_AVAILABLE
    if (!m_State) {
        std::cerr << "Lua state not initialized\n";
        return false;
    }

    lua_getglobal(m_State, functionName.c_str());
    if (!lua_isfunction(m_State, -1)) {
        lua_pop(m_State, 1);
        return false;
    }

    int result = lua_pcall(m_State, 0, 0, 0);
    if (result != LUA_OK) {
        std::cerr << "Lua error in " << functionName << ": " << lua_tostring(m_State, -1) << "\n";
        lua_pop(m_State, 1);
        return false;
    }

    return true;
#else
    return false;
#endif
}

bool LuaScriptEngine::CallEntityFunction(const std::string& functionName, Entity entity) {
#if LUA_AVAILABLE
    if (!m_State) {
        std::cerr << "Lua state not initialized\n";
        return false;
    }

    lua_getglobal(m_State, functionName.c_str());
    if (!lua_isfunction(m_State, -1)) {
        lua_pop(m_State, 1);
        return false;
    }

    // Push entity as userdata
    // TODO: Implement entity userdata pushing
    // For now, just call with no arguments
    int result = lua_pcall(m_State, 0, 0, 0);
    if (result != LUA_OK) {
        std::cerr << "Lua error in " << functionName << ": " << lua_tostring(m_State, -1) << "\n";
        lua_pop(m_State, 1);
        return false;
    }

    return true;
#else
    return false;
#endif
}

void LuaScriptEngine::Shutdown() {
#if LUA_AVAILABLE
    if (m_State) {
        lua_close(m_State);
        m_State = nullptr;
    }
    std::cout << "Lua scripting engine shut down\n";
#endif
}

void LuaScriptEngine::RegisterBindings() {
#if LUA_AVAILABLE
    std::cout << "Registering Lua bindings...\n";
    
    RegisterEntityBindings();
    RegisterTransformBindings();
    RegisterInputBindings();
    RegisterUtilityBindings();

    std::cout << "Lua bindings registered\n";
#endif
}

void LuaScriptEngine::RegisterEntityBindings() {
#if LUA_AVAILABLE
    // Create Entity metatable
    luaL_newmetatable(m_State, "Entity");
    
    // Entity methods
    lua_newtable(m_State);
    
    // entity:getPosition() -> {x, y, z}
    lua_pushcfunction(m_State, [](lua_State* L) -> int {
        // This is a placeholder - proper implementation requires userdata
        lua_newtable(L);
        lua_pushstring(L, "x");
        lua_pushnumber(L, 0.0f);
        lua_settable(L, -3);
        lua_pushstring(L, "y");
        lua_pushnumber(L, 0.0f);
        lua_settable(L, -3);
        lua_pushstring(L, "z");
        lua_pushnumber(L, 0.0f);
        lua_settable(L, -3);
        return 1;
    });
    lua_setfield(m_State, -2, "getPosition");

    lua_setfield(m_State, -2, "__index");
    lua_pop(m_State, 1);

    std::cout << "  - Entity bindings registered\n";
#endif
}

void LuaScriptEngine::RegisterTransformBindings() {
#if LUA_AVAILABLE
    // Create Transform functions in a global table
    lua_newtable(m_State);

    // Transform.setPosition(x, y, z)
    lua_pushcfunction(m_State, [](lua_State* L) -> int {
        // Placeholder
        return 0;
    });
    lua_setfield(m_State, -2, "setPosition");

    // Transform.getPosition()
    lua_pushcfunction(m_State, [](lua_State* L) -> int {
        lua_newtable(L);
        lua_pushstring(L, "x");
        lua_pushnumber(L, 0.0f);
        lua_settable(L, -3);
        lua_pushstring(L, "y");
        lua_pushnumber(L, 0.0f);
        lua_settable(L, -3);
        lua_pushstring(L, "z");
        lua_pushnumber(L, 0.0f);
        lua_settable(L, -3);
        return 1;
    });
    lua_setfield(m_State, -2, "getPosition");

    lua_setglobal(m_State, "Transform");

    std::cout << "  - Transform bindings registered\n";
#endif
}

void LuaScriptEngine::RegisterInputBindings() {
#if LUA_AVAILABLE
    // Create Input functions
    lua_newtable(m_State);

    // Input.isKeyPressed(key)
    lua_pushcfunction(m_State, [](lua_State* L) -> int {
        // Placeholder
        lua_pushboolean(L, false);
        return 1;
    });
    lua_setfield(m_State, -2, "isKeyPressed");

    lua_setglobal(m_State, "Input");

    std::cout << "  - Input bindings registered\n";
#endif
}

void LuaScriptEngine::RegisterUtilityBindings() {
#if LUA_AVAILABLE
    // Register print function redirect to engine log
    lua_register(m_State, "engine_log", [](lua_State* L) -> int {
        const char* msg = luaL_checkstring(L, 1);
        std::cout << "[Lua] " << msg << "\n";
        return 0;
    });

    std::cout << "  - Utility bindings registered\n";
#endif
}
