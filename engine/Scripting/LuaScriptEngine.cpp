#include "LuaScriptEngine.h"
#include "LuaScriptComponent.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"
#include "../Scene/Components.h"
#include "../Core/Math.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <cctype>
#include <limits>
#include <utility>

#include <GLFW/glfw3.h>

#ifndef LUA_AVAILABLE
    #define LUA_AVAILABLE 0
#endif

namespace {
    constexpr int kInvalidRef = -1;
    constexpr uint32_t kInvalidEntityId = std::numeric_limits<uint32_t>::max();

#if LUA_AVAILABLE
    std::string ToUpper(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return value;
    }

    int TranslateKey(const std::string& input) {
        if (input.empty()) {
            return GLFW_KEY_UNKNOWN;
        }

        if (input.size() == 1) {
            unsigned char ch = static_cast<unsigned char>(input[0]);
            if (std::isalpha(ch)) {
                return GLFW_KEY_A + (static_cast<int>(std::toupper(ch)) - 'A');
            }
            if (std::isdigit(ch)) {
                return GLFW_KEY_0 + (static_cast<int>(ch) - '0');
            }
        }

        static const std::unordered_map<std::string, int> kKeyTable = {
            {"SPACE", GLFW_KEY_SPACE},
            {"ENTER", GLFW_KEY_ENTER},
            {"RETURN", GLFW_KEY_ENTER},
            {"ESC", GLFW_KEY_ESCAPE},
            {"ESCAPE", GLFW_KEY_ESCAPE},
            {"TAB", GLFW_KEY_TAB},
            {"BACKSPACE", GLFW_KEY_BACKSPACE},
            {"LEFT", GLFW_KEY_LEFT},
            {"RIGHT", GLFW_KEY_RIGHT},
            {"UP", GLFW_KEY_UP},
            {"DOWN", GLFW_KEY_DOWN},
            {"SHIFT", GLFW_KEY_LEFT_SHIFT},
            {"LEFT_SHIFT", GLFW_KEY_LEFT_SHIFT},
            {"RIGHT_SHIFT", GLFW_KEY_RIGHT_SHIFT},
            {"CTRL", GLFW_KEY_LEFT_CONTROL},
            {"CONTROL", GLFW_KEY_LEFT_CONTROL},
            {"LEFT_CTRL", GLFW_KEY_LEFT_CONTROL},
            {"RIGHT_CTRL", GLFW_KEY_RIGHT_CONTROL},
            {"ALT", GLFW_KEY_LEFT_ALT},
            {"LEFT_ALT", GLFW_KEY_LEFT_ALT},
            {"RIGHT_ALT", GLFW_KEY_RIGHT_ALT},
            {"SUPER", GLFW_KEY_LEFT_SUPER},
            {"LMB", GLFW_MOUSE_BUTTON_LEFT},
            {"RMB", GLFW_MOUSE_BUTTON_RIGHT},
            {"MMB", GLFW_MOUSE_BUTTON_MIDDLE},
            {"F1", GLFW_KEY_F1},
            {"F2", GLFW_KEY_F2},
            {"F3", GLFW_KEY_F3},
            {"F4", GLFW_KEY_F4},
            {"F5", GLFW_KEY_F5},
            {"F6", GLFW_KEY_F6},
            {"F7", GLFW_KEY_F7},
            {"F8", GLFW_KEY_F8},
            {"F9", GLFW_KEY_F9},
            {"F10", GLFW_KEY_F10},
            {"F11", GLFW_KEY_F11},
            {"F12", GLFW_KEY_F12}
        };

        std::string upper = ToUpper(input);
        auto it = kKeyTable.find(upper);
        if (it != kKeyTable.end()) {
            return it->second;
        }
        return GLFW_KEY_UNKNOWN;
    }

    int TranslateMouseButton(lua_State* L, int index) {
        if (lua_isnumber(L, index)) {
            return static_cast<int>(lua_tointeger(L, index));
        }
        const char* label = luaL_checkstring(L, index);
        std::string upper = ToUpper(label);
        if (upper == "LEFT" || upper == "LMB") return GLFW_MOUSE_BUTTON_LEFT;
        if (upper == "RIGHT" || upper == "RMB") return GLFW_MOUSE_BUTTON_RIGHT;
        if (upper == "MIDDLE" || upper == "MMB") return GLFW_MOUSE_BUTTON_MIDDLE;
        return GLFW_MOUSE_BUTTON_LAST + 1; // invalid sentinel
    }
#endif
}

LuaScriptEngine* LuaScriptEngine::s_Instance = nullptr;

LuaScriptEngine::LuaScriptEngine() {
    s_Instance = this;
#if LUA_AVAILABLE
    m_State = luaL_newstate();
    if (!m_State) {
        std::cerr << "[Lua] Failed to create Lua state" << std::endl;
    }
#else
    m_State = nullptr;
    std::cerr << "[Lua] Lua runtime not available. Install Lua to enable scripting." << std::endl;
#endif
    RebuildSearchPaths();
}

LuaScriptEngine::~LuaScriptEngine() {
    Shutdown();
    if (s_Instance == this) {
        s_Instance = nullptr;
    }
}

void LuaScriptEngine::Initialize(Scene* scene, GLFWwindow* window) {
    m_Scene = scene;
    m_Window = window;

#if LUA_AVAILABLE
    if (!m_State) {
        std::cerr << "[Lua] Lua state not initialized" << std::endl;
        return;
    }

    luaL_openlibs(m_State);
    RegisterBindings();
    std::cout << "[Lua] Scripting engine initialized" << std::endl;
#else
    (void)scene;
    (void)window;
#endif
}

bool LuaScriptEngine::IsLuaAvailable() const {
#if LUA_AVAILABLE
    return m_State != nullptr;
#else
    return false;
#endif
}

void LuaScriptEngine::SetWindow(GLFWwindow* window) {
    m_Window = window;
}

void LuaScriptEngine::SetProjectRoot(const std::filesystem::path& rootPath) {
    m_ProjectRoot = rootPath.lexically_normal();
    RebuildSearchPaths();
}

bool LuaScriptEngine::LoadScript(const std::string& filepath) {
#if LUA_AVAILABLE
    if (!m_State) {
        std::cerr << "[Lua] Lua state not initialized" << std::endl;
        return false;
    }

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Lua] Failed to open script: " << filepath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return ExecuteString(buffer.str());
#else
    (void)filepath;
    return false;
#endif
}

bool LuaScriptEngine::ExecuteString(const std::string& luaCode) {
#if LUA_AVAILABLE
    if (!m_State) {
        std::cerr << "[Lua] Lua state not initialized" << std::endl;
        return false;
    }

    if (luaL_dostring(m_State, luaCode.c_str()) != LUA_OK) {
        LogLuaError("executing code");
        return false;
    }
    return true;
#else
    (void)luaCode;
    return false;
#endif
}

bool LuaScriptEngine::CallFunction(const std::string& functionName) {
#if LUA_AVAILABLE
    if (!m_State) {
        return false;
    }
    lua_getglobal(m_State, functionName.c_str());
    if (!lua_isfunction(m_State, -1)) {
        lua_pop(m_State, 1);
        return false;
    }
    if (lua_pcall(m_State, 0, 0, 0) != LUA_OK) {
        LogLuaError(functionName);
        return false;
    }
    return true;
#else
    (void)functionName;
    return false;
#endif
}

bool LuaScriptEngine::CallEntityFunction(const std::string& functionName, Entity entity) {
#if LUA_AVAILABLE
    if (!entity) {
        return false;
    }
    uint32_t entityId = static_cast<uint32_t>(entity);
    auto it = m_ScriptInstances.find(entityId);
    if (it == m_ScriptInstances.end()) {
        return false;
    }
    return CallInstanceFunctionByName(it->second, functionName, entity, 0.0f, false);
#else
    (void)functionName;
    (void)entity;
    return false;
#endif
}

void LuaScriptEngine::Update(Scene& scene, float deltaTime) {
#if LUA_AVAILABLE
    if (!m_State) {
        std::cerr << "[Lua] Update called but m_State is null!" << std::endl;
        return;
    }

    std::cout << "[Lua] Update called with " << scene.GetRegistry().view<LuaScriptComponent>().size() << " entities with LuaScriptComponent" << std::endl;

    m_Scene = &scene;
    m_ActiveUpdateOrder.clear();

    auto& registry = scene.GetRegistry();
    std::unordered_set<uint32_t> activeIds;

    auto view = registry.view<LuaScriptComponent>();
    for (auto entityHandle : view) {
        auto& component = view.get<LuaScriptComponent>(entityHandle);
        uint32_t entityId = static_cast<uint32_t>(entityHandle);

        if (!component.enabled || component.scriptPath.empty()) {
            DestroyInstance(entityId, true);
            continue;
        }

        ScriptInstance* instance = EnsureInstance(scene, entityHandle, component);
        if (!instance || !instance->valid) {
            continue;
        }

        Entity entity(entityHandle, &scene);
        if (!instance->awakeCalled) {
            if (CallInstanceFunction(*instance, instance->onAwakeRef, entity, 0.0f, false)) {
                instance->awakeCalled = true;
                std::cout << "[Lua] onAwake called successfully for entity " << entityId << std::endl;
            } else {
                std::cerr << "[Lua] onAwake failed or not found for entity " << entityId << std::endl;
            }
            CallInstanceFunction(*instance, instance->onEnableRef, entity, 0.0f, false);
        }

        activeIds.insert(entityId);
        m_ActiveUpdateOrder.push_back(entityId);
        if (!CallInstanceFunction(*instance, instance->onUpdateRef, entity, deltaTime, true)) {
            std::cerr << "[Lua] onUpdate failed or not found for entity " << entityId << std::endl;
        }
    }

    std::vector<uint32_t> toRemove;
    toRemove.reserve(m_ScriptInstances.size());
    for (const auto& [entityId, instance] : m_ScriptInstances) {
        entt::entity handle = static_cast<entt::entity>(entityId);
        bool stillValid = registry.valid(handle) && registry.all_of<LuaScriptComponent>(handle);
        bool stillActive = activeIds.find(entityId) != activeIds.end();
        if (!stillValid || !stillActive) {
            toRemove.push_back(entityId);
        }
    }

    for (uint32_t entityId : toRemove) {
        DestroyInstance(entityId, true);
    }
#else
    (void)scene;
    (void)deltaTime;
#endif
}

void LuaScriptEngine::LateUpdate(Scene& scene, float deltaTime) {
#if LUA_AVAILABLE
    if (!m_State) {
        return;
    }

    m_Scene = &scene;
    for (uint32_t entityId : m_ActiveUpdateOrder) {
        auto it = m_ScriptInstances.find(entityId);
        if (it == m_ScriptInstances.end()) {
            continue;
        }
        ScriptInstance& instance = it->second;
        if (!instance.valid) {
            continue;
        }
        Entity entity(static_cast<entt::entity>(entityId), &scene);
        CallInstanceFunction(instance, instance.onLateUpdateRef, entity, deltaTime, true);
    }
    m_ActiveUpdateOrder.clear();
#else
    (void)scene;
    (void)deltaTime;
#endif
}

void LuaScriptEngine::OnSimulationStarted() {
#if LUA_AVAILABLE
    for (auto& [_, instance] : m_ScriptInstances) {
        instance.awakeCalled = false;
    }
    if (m_CameraBinding.setActive) {
        m_CameraBinding.setActive(false);
    }
#endif
}

void LuaScriptEngine::OnSimulationStopped() {
#if LUA_AVAILABLE
    if (!m_State) {
        return;
    }

    if (m_CameraBinding.setActive) {
        m_CameraBinding.setActive(false);
    }

    for (auto& [entityId, instance] : m_ScriptInstances) {
        if (!instance.valid || instance.environmentRef == kInvalidRef) {
            continue;
        }
        Entity entity(static_cast<entt::entity>(entityId), m_Scene);
        if (!entity) {
            continue;
        }
        if (instance.awakeCalled) {
            CallInstanceFunction(instance, instance.onDisableRef, entity, 0.0f, false);
            CallInstanceFunction(instance, instance.onDestroyRef, entity, 0.0f, false);
        }
        ReleaseInstanceResources(instance);
        instance.awakeCalled = false;
        instance.valid = false;
        instance.errorLogged = false;
    }
    m_ActiveUpdateOrder.clear();
#endif
}

void LuaScriptEngine::OnEntityDestroyed(Entity entity) {
#if LUA_AVAILABLE
    if (!entity) {
        return;
    }
    DestroyInstance(static_cast<uint32_t>(entity), true);
#else
    (void)entity;
#endif
}

void LuaScriptEngine::Shutdown() {
#if LUA_AVAILABLE
    if (m_State) {
        for (auto& [entityId, instance] : m_ScriptInstances) {
            if (instance.valid && instance.environmentRef != kInvalidRef && m_Scene) {
                Entity entity(static_cast<entt::entity>(entityId), m_Scene);
                if (entity) {
                    CallInstanceFunction(instance, instance.onDisableRef, entity, 0.0f, false);
                    CallInstanceFunction(instance, instance.onDestroyRef, entity, 0.0f, false);
                }
            }
            ReleaseInstanceResources(instance);
        }
        m_ScriptInstances.clear();
        lua_close(m_State);
        m_State = nullptr;
    }
    std::cout << "[Lua] Scripting engine shut down" << std::endl;
#endif
    m_Scene = nullptr;
    m_Window = nullptr;
    m_ActiveUpdateOrder.clear();
}

void LuaScriptEngine::SetCameraBinding(CameraBinding binding) {
    m_CameraBinding = std::move(binding);
}

void LuaScriptEngine::SetPhysicsBinding(PhysicsBinding binding) {
    m_PhysicsBinding = std::move(binding);
}

void LuaScriptEngine::RegisterBindings() {
#if LUA_AVAILABLE
    RegisterEntityBindings();
    RegisterTransformBindings();
    RegisterInputBindings();
    RegisterUtilityBindings();
    RegisterCameraBindings();
    RegisterPhysicsBindings();
    RegisterThisBindings();
#endif
}

void LuaScriptEngine::RegisterEntityBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);

    lua_pushcfunction(m_State, Lua_Entity_isValid);
    lua_setfield(m_State, -2, "isValid");

    lua_pushcfunction(m_State, Lua_Entity_getName);
    lua_setfield(m_State, -2, "getName");

    lua_pushcfunction(m_State, Lua_Entity_setName);
    lua_setfield(m_State, -2, "setName");

    lua_setglobal(m_State, "Entity");
#endif
}

void LuaScriptEngine::RegisterTransformBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);

    lua_pushcfunction(m_State, Lua_Transform_getPosition);
    lua_setfield(m_State, -2, "getPosition");

    lua_pushcfunction(m_State, Lua_Transform_setPosition);
    lua_setfield(m_State, -2, "setPosition");

    lua_pushcfunction(m_State, Lua_Transform_translate);
    lua_setfield(m_State, -2, "translate");

    lua_pushcfunction(m_State, Lua_Transform_getRotation);
    lua_setfield(m_State, -2, "getRotation");

    lua_pushcfunction(m_State, Lua_Transform_setRotation);
    lua_setfield(m_State, -2, "setRotation");

    lua_pushcfunction(m_State, Lua_Transform_rotate);
    lua_setfield(m_State, -2, "rotate");

    lua_pushcfunction(m_State, Lua_Transform_getScale);
    lua_setfield(m_State, -2, "getScale");

    lua_pushcfunction(m_State, Lua_Transform_setScale);
    lua_setfield(m_State, -2, "setScale");

    lua_setglobal(m_State, "Transform");
#endif
}

void LuaScriptEngine::RegisterInputBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);

    lua_pushcfunction(m_State, Lua_Input_isKeyPressed);
    lua_setfield(m_State, -2, "isKeyPressed");

    lua_pushcfunction(m_State, Lua_Input_isMouseButtonPressed);
    lua_setfield(m_State, -2, "isMouseButtonPressed");

    lua_pushcfunction(m_State, Lua_Input_getMousePosition);
    lua_setfield(m_State, -2, "getMousePosition");

    lua_setglobal(m_State, "Input");
#endif
}

void LuaScriptEngine::RegisterUtilityBindings() {
#if LUA_AVAILABLE
    lua_register(m_State, "engine_log", [](lua_State* L) -> int {
        const char* msg = luaL_checkstring(L, 1);
        std::cout << "[Lua] " << msg << std::endl;
        return 0;
    });

    lua_newtable(m_State);
    lua_pushcfunction(m_State, Lua_Time_deltaTime);
    lua_setfield(m_State, -2, "deltaTime");
    lua_setglobal(m_State, "Time");
#endif
}

void LuaScriptEngine::RegisterCameraBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);

    lua_pushcfunction(m_State, Lua_Camera_setActive);
    lua_setfield(m_State, -2, "setActive");

    lua_pushcfunction(m_State, Lua_Camera_isActive);
    lua_setfield(m_State, -2, "isActive");

    lua_pushcfunction(m_State, Lua_Camera_setPosition);
    lua_setfield(m_State, -2, "setPosition");

    lua_pushcfunction(m_State, Lua_Camera_getPosition);
    lua_setfield(m_State, -2, "getPosition");

    lua_pushcfunction(m_State, Lua_Camera_setRotation);
    lua_setfield(m_State, -2, "setRotation");

    lua_pushcfunction(m_State, Lua_Camera_getRotation);
    lua_setfield(m_State, -2, "getRotation");

    lua_pushcfunction(m_State, Lua_Camera_translate);
    lua_setfield(m_State, -2, "translate");

    lua_setglobal(m_State, "Camera");
#endif
}

void LuaScriptEngine::RegisterPhysicsBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);

    lua_pushcfunction(m_State, Lua_Physics_isAvailable);
    lua_setfield(m_State, -2, "isAvailable");

    lua_pushcfunction(m_State, Lua_Physics_hasBody);
    lua_setfield(m_State, -2, "hasBody");

    lua_pushcfunction(m_State, Lua_Physics_getLinearVelocity);
    lua_setfield(m_State, -2, "getLinearVelocity");

    lua_pushcfunction(m_State, Lua_Physics_setLinearVelocity);
    lua_setfield(m_State, -2, "setLinearVelocity");

    lua_pushcfunction(m_State, Lua_Physics_setAngularVelocity);
    lua_setfield(m_State, -2, "setAngularVelocity");

    lua_pushcfunction(m_State, Lua_Physics_setRotation);
    lua_setfield(m_State, -2, "setRotation");

    lua_pushcfunction(m_State, Lua_Physics_raycast);
    lua_setfield(m_State, -2, "raycast");

    lua_setglobal(m_State, "Physics");
#endif
}

int LuaScriptEngine::Lua_Physics_isAvailable(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    bool available = engine && engine->m_PhysicsBinding.hasBody;
    lua_pushboolean(L, available);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Physics_hasBody(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_PhysicsBinding.hasBody) {
        lua_pushboolean(L, false);
        return 1;
    }
    lua_pushboolean(L, engine->m_PhysicsBinding.hasBody(engine->m_CurrentEntityId));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Physics_getLinearVelocity(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_PhysicsBinding.getLinearVelocity) {
        lua_pushnil(L);
        return 1;
    }

    Vec3 velocity{};
    if (!engine->m_PhysicsBinding.getLinearVelocity(engine->m_CurrentEntityId, velocity)) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushnumber(L, velocity.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, velocity.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, velocity.z);
    lua_setfield(L, -2, "z");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Physics_setLinearVelocity(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_PhysicsBinding.setLinearVelocity) {
        lua_pushboolean(L, false);
        return 1;
    }

    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    bool activate = true;
    if (lua_gettop(L) >= 4) {
        activate = lua_toboolean(L, 4) != 0;
    }

    bool ok = engine->m_PhysicsBinding.setLinearVelocity(engine->m_CurrentEntityId, Vec3(x, y, z), activate);
    lua_pushboolean(L, ok);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Physics_setAngularVelocity(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_PhysicsBinding.setAngularVelocity) {
        lua_pushboolean(L, false);
        return 1;
    }

    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    bool activate = true;
    if (lua_gettop(L) >= 4) {
        activate = lua_toboolean(L, 4) != 0;
    }

    bool ok = engine->m_PhysicsBinding.setAngularVelocity(engine->m_CurrentEntityId, Vec3(x, y, z), activate);
    lua_pushboolean(L, ok);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Physics_setRotation(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_PhysicsBinding.setRotation) {
        lua_pushboolean(L, false);
        return 1;
    }

    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    bool activate = true;
    if (lua_gettop(L) >= 4) {
        activate = lua_toboolean(L, 4) != 0;
    }

    bool ok = engine->m_PhysicsBinding.setRotation(engine->m_CurrentEntityId, Vec3(x, y, z), activate);
    lua_pushboolean(L, ok);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Physics_raycast(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_PhysicsBinding.raycast) {
        lua_pushnil(L);
        return 1;
    }

    float ox = static_cast<float>(luaL_checknumber(L, 1));
    float oy = static_cast<float>(luaL_checknumber(L, 2));
    float oz = static_cast<float>(luaL_checknumber(L, 3));
    float dx = static_cast<float>(luaL_checknumber(L, 4));
    float dy = static_cast<float>(luaL_checknumber(L, 5));
    float dz = static_cast<float>(luaL_checknumber(L, 6));
    float maxDistance = static_cast<float>(luaL_checknumber(L, 7));

    PhysicsRaycastHit hit{};
    bool ok = engine->m_PhysicsBinding.raycast(
        engine->m_CurrentEntityId,
        Vec3(ox, oy, oz),
        Vec3(dx, dy, dz),
        maxDistance,
        hit
    );

    if (!ok || !hit.hit) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushboolean(L, true);
    lua_setfield(L, -2, "hit");
    lua_pushnumber(L, hit.distance);
    lua_setfield(L, -2, "distance");

    lua_newtable(L);
    lua_pushnumber(L, hit.point.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, hit.point.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, hit.point.z);
    lua_setfield(L, -2, "z");
    lua_setfield(L, -2, "point");

    lua_newtable(L);
    lua_pushnumber(L, hit.normal.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, hit.normal.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, hit.normal.z);
    lua_setfield(L, -2, "z");
    lua_setfield(L, -2, "normal");

    return 1;
#else
    (void)L;
    return 0;
#endif
}

void LuaScriptEngine::RebuildSearchPaths() {
    m_SearchPaths.clear();
    auto add = [this](const std::filesystem::path& path) {
        AddSearchPath(path);
    };

    const std::filesystem::path current = std::filesystem::current_path();
    add(current);
    add(current / "engine");
    add(current / "engine" / "Game");
    add(current / "engine" / "Game" / "scripts");

    if (!m_ProjectRoot.empty()) {
        add(m_ProjectRoot);
        add(m_ProjectRoot / "assets");
        add(m_ProjectRoot / "assets" / "scripts");
        add(m_ProjectRoot / "scripts");
    }
}

void LuaScriptEngine::AddSearchPath(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    std::filesystem::path normalized = path.lexically_normal();
    if (std::find(m_SearchPaths.begin(), m_SearchPaths.end(), normalized) == m_SearchPaths.end()) {
        m_SearchPaths.push_back(normalized);
    }
}

LuaScriptEngine::ScriptInstance* LuaScriptEngine::EnsureInstance(Scene& scene, entt::entity entityHandle, LuaScriptComponent& component) {
#if LUA_AVAILABLE
    uint32_t entityId = static_cast<uint32_t>(entityHandle);
    ScriptInstance& instance = m_ScriptInstances[entityId];
    instance.entityId = entityId;

    std::filesystem::path resolved = ResolveScriptPath(component.scriptPath);
    bool pathChanged = instance.userPath != component.scriptPath;
    instance.userPath = component.scriptPath;
    instance.resolvedPath = resolved;

    if (pathChanged && instance.valid) {
        Entity entity(entityHandle, &scene);
        CallInstanceFunction(instance, instance.onDisableRef, entity, 0.0f, false);
        CallInstanceFunction(instance, instance.onDestroyRef, entity, 0.0f, false);
        ReleaseInstanceResources(instance);
        instance.awakeCalled = false;
        instance.valid = false;
        instance.errorLogged = false;
    }

    std::error_code fileError;
    auto timestamp = std::filesystem::exists(resolved, fileError)
        ? std::filesystem::last_write_time(resolved, fileError)
        : std::filesystem::file_time_type{};

    if (fileError) {
        if (!instance.errorLogged) {
            std::cerr << "[Lua] Unable to read script '" << resolved << "': " << fileError.message() << std::endl;
            instance.errorLogged = true;
        }
        return nullptr;
    }

    bool needsReload = !instance.valid || (instance.valid && timestamp != instance.lastWriteTime);

    if (needsReload) {
        Entity entity(entityHandle, &scene);
        if (instance.valid) {
            CallInstanceFunction(instance, instance.onDisableRef, entity, 0.0f, false);
            CallInstanceFunction(instance, instance.onDestroyRef, entity, 0.0f, false);
        }
        ReleaseInstanceResources(instance);
        instance.awakeCalled = false;
        instance.valid = false;

        if (!std::filesystem::exists(resolved)) {
            if (!instance.errorLogged) {
                std::cerr << "[Lua] Script not found: " << resolved << std::endl;
                instance.errorLogged = true;
            }
            return nullptr;
        }

        if (!LoadScriptForInstance(instance, component, entity)) {
            instance.errorLogged = true;
            return nullptr;
        }

        instance.errorLogged = false;
        instance.lastWriteTime = timestamp;
    }

    return &instance;
#else
    (void)scene;
    (void)entityHandle;
    (void)component;
    return nullptr;
#endif
}

void LuaScriptEngine::DestroyInstance(uint32_t entityId, bool callOnDestroy) {
#if LUA_AVAILABLE
    auto it = m_ScriptInstances.find(entityId);
    if (it == m_ScriptInstances.end()) {
        return;
    }

    ScriptInstance& instance = it->second;
    if (callOnDestroy && instance.valid && instance.environmentRef != kInvalidRef && m_Scene) {
        Entity entity(static_cast<entt::entity>(entityId), m_Scene);
        if (entity) {
            if (instance.awakeCalled) {
                CallInstanceFunction(instance, instance.onDisableRef, entity, 0.0f, false);
                CallInstanceFunction(instance, instance.onDestroyRef, entity, 0.0f, false);
            }
        }
    }

    ReleaseInstanceResources(instance);
    m_ScriptInstances.erase(it);
#else
    (void)entityId;
    (void)callOnDestroy;
#endif
}

void LuaScriptEngine::ReleaseInstanceResources(ScriptInstance& instance) {
#if LUA_AVAILABLE
    if (!m_State) {
        return;
    }

    auto unref = [this](int& ref) {
        if (ref != kInvalidRef && ref != LUA_NOREF && ref != LUA_REFNIL) {
            luaL_unref(m_State, LUA_REGISTRYINDEX, ref);
        }
        ref = kInvalidRef;
    };

    unref(instance.onAwakeRef);
    unref(instance.onEnableRef);
    unref(instance.onUpdateRef);
    unref(instance.onLateUpdateRef);
    unref(instance.onDisableRef);
    unref(instance.onDestroyRef);
    unref(instance.environmentRef);

    instance.valid = false;
    instance.awakeCalled = false;
#else
    (void)instance;
#endif
}

bool LuaScriptEngine::LoadScriptForInstance(ScriptInstance& instance, const LuaScriptComponent& component, Entity entity) {
#if LUA_AVAILABLE
    if (!m_State) {
        return false;
    }

    std::ifstream file(instance.resolvedPath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Lua] Failed to open script: " << instance.resolvedPath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string code = buffer.str();

    lua_newtable(m_State);
    int envIndex = lua_gettop(m_State);

    lua_newtable(m_State);
    lua_pushglobaltable(m_State);
    lua_setfield(m_State, -2, "__index");
    lua_setmetatable(m_State, envIndex);

    if (luaL_loadbuffer(m_State, code.c_str(), code.size(), instance.resolvedPath.string().c_str()) != LUA_OK) {
        LogLuaError("loading " + instance.resolvedPath.string());
        lua_pop(m_State, 1);
        return false;
    }

#if LUA_VERSION_NUM >= 502
    lua_pushvalue(m_State, envIndex);
    lua_setupvalue(m_State, -2, 1);
#else
    lua_pushvalue(m_State, envIndex);
    lua_setfenv(m_State, -2);
#endif

    if (lua_pcall(m_State, 0, 0, 0) != LUA_OK) {
        LogLuaError("executing " + instance.resolvedPath.string());
        lua_pop(m_State, 1);
        return false;
    }

    instance.environmentRef = luaL_ref(m_State, LUA_REGISTRYINDEX);
    if (instance.environmentRef == LUA_REFNIL || instance.environmentRef == LUA_NOREF) {
        instance.environmentRef = kInvalidRef;
        return false;
    }

    const std::string awakeName = component.functionOnAwake.empty() ? "onAwake" : component.functionOnAwake;
    const std::string updateName = component.functionOnUpdate.empty() ? "onUpdate" : component.functionOnUpdate;
    const std::string destroyName = component.functionOnDestroy.empty() ? "onDestroy" : component.functionOnDestroy;

    instance.onAwakeRef = FetchFunctionRef(instance.environmentRef, awakeName);
    instance.onEnableRef = FetchFunctionRef(instance.environmentRef, "onEnable");
    instance.onUpdateRef = FetchFunctionRef(instance.environmentRef, updateName);
    instance.onLateUpdateRef = FetchFunctionRef(instance.environmentRef, "onLateUpdate");
    instance.onDisableRef = FetchFunctionRef(instance.environmentRef, "onDisable");
    instance.onDestroyRef = FetchFunctionRef(instance.environmentRef, destroyName);

    instance.valid = instance.environmentRef != kInvalidRef;
    instance.awakeCalled = false;
    instance.errorLogged = false;

    (void)entity;
    return instance.valid;
#else
    (void)instance;
    (void)component;
    (void)entity;
    return false;
#endif
}

int LuaScriptEngine::FetchFunctionRef(int environmentRef, const std::string& functionName) {
#if LUA_AVAILABLE
    if (!m_State || environmentRef == kInvalidRef || functionName.empty()) {
        return kInvalidRef;
    }

    lua_rawgeti(m_State, LUA_REGISTRYINDEX, environmentRef);
    if (!lua_istable(m_State, -1)) {
        lua_pop(m_State, 1);
        return kInvalidRef;
    }

    lua_getfield(m_State, -1, functionName.c_str());
    if (!lua_isfunction(m_State, -1)) {
        lua_pop(m_State, 2);
        return kInvalidRef;
    }

    int ref = luaL_ref(m_State, LUA_REGISTRYINDEX);
    lua_pop(m_State, 1);
    return ref;
#else
    (void)environmentRef;
    (void)functionName;
    return kInvalidRef;
#endif
}

bool LuaScriptEngine::CallInstanceFunction(ScriptInstance& instance, int functionRef, Entity entity, float deltaTime, bool includeDelta) {
#if LUA_AVAILABLE
    if (!m_State || functionRef == kInvalidRef || functionRef == LUA_NOREF || functionRef == LUA_REFNIL) {
        return false;
    }

    (void)entity;

    lua_rawgeti(m_State, LUA_REGISTRYINDEX, functionRef);
    if (!lua_isfunction(m_State, -1)) {
        lua_pop(m_State, 1);
        return false;
    }

    uint32_t previousEntity = m_CurrentEntityId;
    float previousDelta = m_CurrentDeltaTime;
    m_CurrentEntityId = instance.entityId;
    m_CurrentDeltaTime = deltaTime;

    // Set up "this" global for OOP-style script access
    SetupThisForEntity(instance.entityId);

    int argumentCount = 0;
    if (includeDelta) {
        lua_pushnumber(m_State, deltaTime);
        argumentCount = 1;
    }

    if (lua_pcall(m_State, argumentCount, 0, 0) != LUA_OK) {
        LogLuaError("calling script function");
        m_CurrentEntityId = previousEntity;
        m_CurrentDeltaTime = previousDelta;
        return false;
    }

    m_CurrentEntityId = previousEntity;
    m_CurrentDeltaTime = previousDelta;
    return true;
#else
    (void)instance;
    (void)functionRef;
    (void)entity;
    (void)deltaTime;
    (void)includeDelta;
    return false;
#endif
}

bool LuaScriptEngine::CallInstanceFunctionByName(ScriptInstance& instance, const std::string& functionName, Entity entity, float deltaTime, bool includeDelta) {
#if LUA_AVAILABLE
    if (!m_State || instance.environmentRef == kInvalidRef) {
        return false;
    }

    (void)entity;

    lua_rawgeti(m_State, LUA_REGISTRYINDEX, instance.environmentRef);
    if (!lua_istable(m_State, -1)) {
        lua_pop(m_State, 1);
        return false;
    }

    lua_getfield(m_State, -1, functionName.c_str());
    if (!lua_isfunction(m_State, -1)) {
        lua_pop(m_State, 2);
        return false;
    }

    lua_remove(m_State, -2);

    uint32_t previousEntity = m_CurrentEntityId;
    float previousDelta = m_CurrentDeltaTime;
    m_CurrentEntityId = instance.entityId;
    m_CurrentDeltaTime = deltaTime;

    // Set up "this" global for OOP-style script access
    SetupThisForEntity(instance.entityId);

    int argumentCount = 0;
    if (includeDelta) {
        lua_pushnumber(m_State, deltaTime);
        argumentCount = 1;
    }

    if (lua_pcall(m_State, argumentCount, 0, 0) != LUA_OK) {
        LogLuaError(functionName);
        m_CurrentEntityId = previousEntity;
        m_CurrentDeltaTime = previousDelta;
        return false;
    }

    m_CurrentEntityId = previousEntity;
    m_CurrentDeltaTime = previousDelta;
    return true;
#else
    (void)instance;
    (void)functionName;
    (void)entity;
    (void)deltaTime;
    (void)includeDelta;
    return false;
#endif
}

std::filesystem::path LuaScriptEngine::ResolveScriptPath(const std::string& scriptPath) const {
    if (scriptPath.empty()) {
        return {};
    }

    std::filesystem::path candidate(scriptPath);
    if (candidate.is_absolute()) {
        return candidate.lexically_normal();
    }

    for (const auto& base : m_SearchPaths) {
        std::filesystem::path resolved = (base / candidate).lexically_normal();
        if (std::filesystem::exists(resolved)) {
            return resolved;
        }
    }

    return (std::filesystem::current_path() / candidate).lexically_normal();
}

Entity LuaScriptEngine::MakeEntity(uint32_t entityId) const {
    if (!m_Scene) {
        return {};
    }
    entt::entity handle = static_cast<entt::entity>(entityId);
    if (!m_Scene->GetRegistry().valid(handle)) {
        return {};
    }
    return Entity(handle, m_Scene);
}

TransformComponent* LuaScriptEngine::GetTransform(uint32_t entityId) const {
    if (!m_Scene) {
        return nullptr;
    }
    entt::entity handle = static_cast<entt::entity>(entityId);
    auto& registry = m_Scene->GetRegistry();
    if (!registry.valid(handle) || !registry.all_of<TransformComponent>(handle)) {
        return nullptr;
    }
    return &registry.get<TransformComponent>(handle);
}

TagComponent* LuaScriptEngine::GetTag(uint32_t entityId) const {
    if (!m_Scene) {
        return nullptr;
    }
    entt::entity handle = static_cast<entt::entity>(entityId);
    auto& registry = m_Scene->GetRegistry();
    if (!registry.valid(handle)) {
        return nullptr;
    }
    if (!registry.all_of<TagComponent>(handle)) {
        return nullptr;
    }
    return &registry.get<TagComponent>(handle);
}

bool LuaScriptEngine::CurrentEntityValid() const {
    return m_CurrentEntityId != kInvalidEntityId && m_Scene && m_Scene->GetRegistry().valid(static_cast<entt::entity>(m_CurrentEntityId));
}

void LuaScriptEngine::LogLuaError(const std::string& context) {
#if LUA_AVAILABLE
    if (!m_State) {
        return;
    }
    const char* message = lua_tostring(m_State, -1);
    std::cerr << "[Lua] Error while " << context << ": " << (message ? message : "<unknown>") << std::endl;
    lua_pop(m_State, 1);
#else
    (void)context;
#endif
}

int LuaScriptEngine::Lua_Transform_getPosition(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        lua_pushnil(L);
        return 1;
    }

    TransformComponent* transform = engine->GetTransform(engine->m_CurrentEntityId);
    if (!transform) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushnumber(L, transform->position.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, transform->position.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, transform->position.z);
    lua_setfield(L, -2, "z");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Transform_setPosition(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        return 0;
    }

    TransformComponent* transform = engine->GetTransform(engine->m_CurrentEntityId);
    if (!transform) {
        return 0;
    }

    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    transform->position = Vec3(x, y, z);
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Transform_translate(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        return 0;
    }

    TransformComponent* transform = engine->GetTransform(engine->m_CurrentEntityId);
    if (!transform) {
        return 0;
    }

    float dx = static_cast<float>(luaL_optnumber(L, 1, 0.0));
    float dy = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    float dz = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    transform->position.x += dx;
    transform->position.y += dy;
    transform->position.z += dz;
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Transform_getRotation(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        lua_pushnil(L);
        return 1;
    }

    TransformComponent* transform = engine->GetTransform(engine->m_CurrentEntityId);
    if (!transform) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushnumber(L, transform->rotation.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, transform->rotation.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, transform->rotation.z);
    lua_setfield(L, -2, "z");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Transform_setRotation(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        return 0;
    }

    TransformComponent* transform = engine->GetTransform(engine->m_CurrentEntityId);
    if (!transform) {
        return 0;
    }

    float rx = static_cast<float>(luaL_checknumber(L, 1));
    float ry = static_cast<float>(luaL_checknumber(L, 2));
    float rz = static_cast<float>(luaL_checknumber(L, 3));
    transform->rotation = Vec3(rx, ry, rz);
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Transform_rotate(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        return 0;
    }

    TransformComponent* transform = engine->GetTransform(engine->m_CurrentEntityId);
    if (!transform) {
        return 0;
    }

    float rx = static_cast<float>(luaL_optnumber(L, 1, 0.0));
    float ry = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    float rz = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    transform->rotation.x += rx;
    transform->rotation.y += ry;
    transform->rotation.z += rz;
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Transform_getScale(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        lua_pushnil(L);
        return 1;
    }

    TransformComponent* transform = engine->GetTransform(engine->m_CurrentEntityId);
    if (!transform) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushnumber(L, transform->scale.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, transform->scale.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, transform->scale.z);
    lua_setfield(L, -2, "z");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Transform_setScale(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        return 0;
    }

    TransformComponent* transform = engine->GetTransform(engine->m_CurrentEntityId);
    if (!transform) {
        return 0;
    }

    float sx = static_cast<float>(luaL_checknumber(L, 1));
    float sy = static_cast<float>(luaL_checknumber(L, 2));
    float sz = static_cast<float>(luaL_checknumber(L, 3));
    transform->scale = Vec3(sx, sy, sz);
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Entity_isValid(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    bool valid = engine && engine->CurrentEntityValid();
    lua_pushboolean(L, valid);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Entity_getName(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        lua_pushnil(L);
        return 1;
    }
    TagComponent* tag = engine->GetTag(engine->m_CurrentEntityId);
    if (!tag) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, tag->tag.c_str());
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Entity_setName(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        return 0;
    }
    const char* name = luaL_checkstring(L, 1);
    if (!name) {
        return 0;
    }
    Entity entity = engine->MakeEntity(engine->m_CurrentEntityId);
    if (!entity) {
        return 0;
    }
    if (!entity.HasComponent<TagComponent>()) {
        entity.AddComponent<TagComponent>(name);
    } else {
        entity.GetComponent<TagComponent>().tag = name;
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_isKeyPressed(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_Window) {
        lua_pushboolean(L, false);
        return 1;
    }

    int key = GLFW_KEY_UNKNOWN;
    if (lua_isnumber(L, 1)) {
        key = static_cast<int>(lua_tointeger(L, 1));
    } else {
        const char* keyName = luaL_checkstring(L, 1);
        key = TranslateKey(keyName ? keyName : "");
    }

    if (key == GLFW_KEY_UNKNOWN) {
        lua_pushboolean(L, false);
        return 1;
    }

    int state = glfwGetKey(engine->m_Window, key);
    lua_pushboolean(L, state == GLFW_PRESS || state == GLFW_REPEAT);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_isMouseButtonPressed(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_Window) {
        lua_pushboolean(L, false);
        return 1;
    }

    int button = TranslateMouseButton(L, 1);
    if (button < GLFW_MOUSE_BUTTON_1 || button > GLFW_MOUSE_BUTTON_LAST) {
        lua_pushboolean(L, false);
        return 1;
    }

    int state = glfwGetMouseButton(engine->m_Window, button);
    lua_pushboolean(L, state == GLFW_PRESS);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_getMousePosition(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_Window) {
        lua_pushnil(L);
        return 1;
    }

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(engine->m_Window, &x, &y);

    lua_newtable(L);
    lua_pushnumber(L, x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, y);
    lua_setfield(L, -2, "y");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Time_deltaTime(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    double dt = engine ? static_cast<double>(engine->m_CurrentDeltaTime) : 0.0;
    lua_pushnumber(L, dt);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Camera_setActive(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        return 0;
    }
    bool active = lua_toboolean(L, 1) != 0;
    if (engine->m_CameraBinding.setActive) {
        engine->m_CameraBinding.setActive(active);
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Camera_isActive(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    bool active = false;
    if (engine && engine->m_CameraBinding.isActive) {
        active = engine->m_CameraBinding.isActive();
    }
    lua_pushboolean(L, active);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Camera_setPosition(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_CameraBinding.setPosition) {
        return 0;
    }
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    engine->m_CameraBinding.setPosition(Vec3(x, y, z));
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Camera_getPosition(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_CameraBinding.getPosition) {
        lua_pushnil(L);
        return 1;
    }
    Vec3 pos = engine->m_CameraBinding.getPosition();
    lua_newtable(L);
    lua_pushnumber(L, pos.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, pos.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, pos.z);
    lua_setfield(L, -2, "z");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Camera_setRotation(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_CameraBinding.setRotation) {
        return 0;
    }
    float rx = static_cast<float>(luaL_checknumber(L, 1));
    float ry = static_cast<float>(luaL_checknumber(L, 2));
    float rz = static_cast<float>(luaL_checknumber(L, 3));
    engine->m_CameraBinding.setRotation(Vec3(rx, ry, rz));
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Camera_getRotation(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_CameraBinding.getRotation) {
        lua_pushnil(L);
        return 1;
    }
    Vec3 rot = engine->m_CameraBinding.getRotation();
    lua_newtable(L);
    lua_pushnumber(L, rot.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, rot.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, rot.z);
    lua_setfield(L, -2, "z");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Camera_translate(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        return 0;
    }

    float dx = static_cast<float>(luaL_optnumber(L, 1, 0.0));
    float dy = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    float dz = static_cast<float>(luaL_optnumber(L, 3, 0.0));

    if (engine->m_CameraBinding.translate) {
        engine->m_CameraBinding.translate(Vec3(dx, dy, dz));
        return 0;
    }

    if (engine->m_CameraBinding.getPosition && engine->m_CameraBinding.setPosition) {
        Vec3 current = engine->m_CameraBinding.getPosition();
        Vec3 updated = vadd(current, Vec3(dx, dy, dz));
        engine->m_CameraBinding.setPosition(updated);
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

void LuaScriptEngine::RegisterThisBindings() {
#if LUA_AVAILABLE
    // Create the "this" metatable for entity objects
    luaL_newmetatable(m_State, "HephaestusEntity");
    
    lua_pushcfunction(m_State, Lua_This_GetTransform);
    lua_setfield(m_State, -2, "GetTransform");
    
    lua_pushcfunction(m_State, Lua_This_GetName);
    lua_setfield(m_State, -2, "GetName");
    
    lua_pushcfunction(m_State, Lua_This_SetName);
    lua_setfield(m_State, -2, "SetName");
    
    lua_pushcfunction(m_State, Lua_This_IsValid);
    lua_setfield(m_State, -2, "IsValid");
    
    // Set __index to self for method lookup
    lua_pushvalue(m_State, -1);
    lua_setfield(m_State, -2, "__index");
    
    lua_pop(m_State, 1);
    
    // Create the "Transform" metatable for transform objects
    luaL_newmetatable(m_State, "HephaestusTransform");
    
    lua_pushcfunction(m_State, Lua_TransformObj_GetPosition);
    lua_setfield(m_State, -2, "GetPosition");
    
    lua_pushcfunction(m_State, Lua_TransformObj_SetPosition);
    lua_setfield(m_State, -2, "SetPosition");
    
    lua_pushcfunction(m_State, Lua_TransformObj_GetRotation);
    lua_setfield(m_State, -2, "GetRotation");
    
    lua_pushcfunction(m_State, Lua_TransformObj_SetRotation);
    lua_setfield(m_State, -2, "SetRotation");
    
    lua_pushcfunction(m_State, Lua_TransformObj_GetScale);
    lua_setfield(m_State, -2, "GetScale");
    
    lua_pushcfunction(m_State, Lua_TransformObj_SetScale);
    lua_setfield(m_State, -2, "SetScale");
    
    lua_pushcfunction(m_State, Lua_TransformObj_Translate);
    lua_setfield(m_State, -2, "Translate");
    
    lua_pushcfunction(m_State, Lua_TransformObj_Rotate);
    lua_setfield(m_State, -2, "Rotate");
    
    // Set __index to self for method lookup
    lua_pushvalue(m_State, -1);
    lua_setfield(m_State, -2, "__index");
    
    lua_pop(m_State, 1);
#endif
}

void LuaScriptEngine::SetupThisForEntity(uint32_t entityId) {
#if LUA_AVAILABLE
    if (!m_State) {
        return;
    }
    
    // Create a userdata to hold the entity ID
    uint32_t* pEntityId = static_cast<uint32_t*>(lua_newuserdata(m_State, sizeof(uint32_t)));
    *pEntityId = entityId;
    
    // Set its metatable to "HephaestusEntity"
    luaL_getmetatable(m_State, "HephaestusEntity");
    lua_setmetatable(m_State, -2);
    
    // Set as global "this"
    lua_setglobal(m_State, "this");
#else
    (void)entityId;
#endif
}

int LuaScriptEngine::Lua_This_GetTransform(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        lua_pushnil(L);
        return 1;
    }
    
    // Create transform userdata containing the entity ID
    uint32_t* pEntityId = static_cast<uint32_t*>(lua_newuserdata(L, sizeof(uint32_t)));
    *pEntityId = engine->m_CurrentEntityId;
    
    // Set its metatable to "HephaestusTransform"
    luaL_getmetatable(L, "HephaestusTransform");
    lua_setmetatable(L, -2);
    
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_This_GetName(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        lua_pushnil(L);
        return 1;
    }
    TagComponent* tag = engine->GetTag(engine->m_CurrentEntityId);
    if (!tag) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, tag->tag.c_str());
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_This_SetName(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        return 0;
    }
    const char* name = luaL_checkstring(L, 2); // Index 2 because self is at index 1
    if (!name) {
        return 0;
    }
    Entity entity = engine->MakeEntity(engine->m_CurrentEntityId);
    if (!entity) {
        return 0;
    }
    if (!entity.HasComponent<TagComponent>()) {
        entity.AddComponent<TagComponent>(name);
    } else {
        entity.GetComponent<TagComponent>().tag = name;
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_This_IsValid(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    bool valid = engine && engine->CurrentEntityValid();
    lua_pushboolean(L, valid);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_TransformObj_GetPosition(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }
    
    // Get entity ID from userdata (self at index 1)
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusTransform"));
    if (!pEntityId) {
        lua_pushnil(L);
        return 1;
    }
    
    TransformComponent* transform = engine->GetTransform(*pEntityId);
    if (!transform) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushnumber(L, transform->position.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, transform->position.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, transform->position.z);
    lua_setfield(L, -2, "z");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_TransformObj_SetPosition(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        return 0;
    }
    
    // Get entity ID from userdata (self at index 1)
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusTransform"));
    if (!pEntityId) {
        return 0;
    }
    
    TransformComponent* transform = engine->GetTransform(*pEntityId);
    if (!transform) {
        return 0;
    }

    // Check if second argument is a table (position table)
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "x");
        float x = static_cast<float>(luaL_optnumber(L, -1, transform->position.x));
        lua_pop(L, 1);
        
        lua_getfield(L, 2, "y");
        float y = static_cast<float>(luaL_optnumber(L, -1, transform->position.y));
        lua_pop(L, 1);
        
        lua_getfield(L, 2, "z");
        float z = static_cast<float>(luaL_optnumber(L, -1, transform->position.z));
        lua_pop(L, 1);
        
        transform->position = Vec3(x, y, z);
    } else {
        // Three separate number arguments
        float x = static_cast<float>(luaL_checknumber(L, 2));
        float y = static_cast<float>(luaL_checknumber(L, 3));
        float z = static_cast<float>(luaL_checknumber(L, 4));
        transform->position = Vec3(x, y, z);
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_TransformObj_GetRotation(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusTransform"));
    if (!pEntityId) {
        lua_pushnil(L);
        return 1;
    }
    
    TransformComponent* transform = engine->GetTransform(*pEntityId);
    if (!transform) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushnumber(L, transform->rotation.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, transform->rotation.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, transform->rotation.z);
    lua_setfield(L, -2, "z");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_TransformObj_SetRotation(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        return 0;
    }
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusTransform"));
    if (!pEntityId) {
        return 0;
    }
    
    TransformComponent* transform = engine->GetTransform(*pEntityId);
    if (!transform) {
        return 0;
    }

    // Check if second argument is a table (rotation table)
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "x");
        float x = static_cast<float>(luaL_optnumber(L, -1, transform->rotation.x));
        lua_pop(L, 1);
        
        lua_getfield(L, 2, "y");
        float y = static_cast<float>(luaL_optnumber(L, -1, transform->rotation.y));
        lua_pop(L, 1);
        
        lua_getfield(L, 2, "z");
        float z = static_cast<float>(luaL_optnumber(L, -1, transform->rotation.z));
        lua_pop(L, 1);
        
        transform->rotation = Vec3(x, y, z);
    } else {
        float rx = static_cast<float>(luaL_checknumber(L, 2));
        float ry = static_cast<float>(luaL_checknumber(L, 3));
        float rz = static_cast<float>(luaL_checknumber(L, 4));
        transform->rotation = Vec3(rx, ry, rz);
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_TransformObj_GetScale(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusTransform"));
    if (!pEntityId) {
        lua_pushnil(L);
        return 1;
    }
    
    TransformComponent* transform = engine->GetTransform(*pEntityId);
    if (!transform) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushnumber(L, transform->scale.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, transform->scale.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, transform->scale.z);
    lua_setfield(L, -2, "z");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_TransformObj_SetScale(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        return 0;
    }
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusTransform"));
    if (!pEntityId) {
        return 0;
    }
    
    TransformComponent* transform = engine->GetTransform(*pEntityId);
    if (!transform) {
        return 0;
    }

    // Check if second argument is a table
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "x");
        float x = static_cast<float>(luaL_optnumber(L, -1, transform->scale.x));
        lua_pop(L, 1);
        
        lua_getfield(L, 2, "y");
        float y = static_cast<float>(luaL_optnumber(L, -1, transform->scale.y));
        lua_pop(L, 1);
        
        lua_getfield(L, 2, "z");
        float z = static_cast<float>(luaL_optnumber(L, -1, transform->scale.z));
        lua_pop(L, 1);
        
        transform->scale = Vec3(x, y, z);
    } else {
        float sx = static_cast<float>(luaL_checknumber(L, 2));
        float sy = static_cast<float>(luaL_checknumber(L, 3));
        float sz = static_cast<float>(luaL_checknumber(L, 4));
        transform->scale = Vec3(sx, sy, sz);
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_TransformObj_Translate(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        return 0;
    }
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusTransform"));
    if (!pEntityId) {
        return 0;
    }
    
    TransformComponent* transform = engine->GetTransform(*pEntityId);
    if (!transform) {
        return 0;
    }

    float dx = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    float dy = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    float dz = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    transform->position.x += dx;
    transform->position.y += dy;
    transform->position.z += dz;
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_TransformObj_Rotate(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        return 0;
    }
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusTransform"));
    if (!pEntityId) {
        return 0;
    }
    
    TransformComponent* transform = engine->GetTransform(*pEntityId);
    if (!transform) {
        return 0;
    }

    float rx = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    float ry = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    float rz = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    transform->rotation.x += rx;
    transform->rotation.y += ry;
    transform->rotation.z += rz;
    return 0;
#else
    (void)L;
    return 0;
#endif
}
