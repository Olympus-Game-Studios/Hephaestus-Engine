#include "LuaScriptEngine.h"
#include "LuaScriptComponent.h"
#include "PluginScriptHost.h"
#include "ScriptLanguageRegistry.h"
#include "LuaDebugger.h"
#include "../Audio/AudioEngine.h"
#include "../Scene/ParticleSimulation.h"
#include "../Navigation/NavMesh.h"
#include "../UI/HephaestusUI/UIContext.h"
#include "../UI/HephaestusUI/Rendering/HUIGeometryBuilder.h"
#include "../UI/HephaestusUI/UICanvas.h"
#include "../UI/HephaestusUI/UIElement.h"
#include "../UI/HephaestusUI/UIGridLayout.h"
#include "../UI/HephaestusUI/UIHorizontalBox.h"
#include "../UI/HephaestusUI/UIImage.h"
#include "../UI/HephaestusUI/UIPanel.h"
#include "../UI/HephaestusUI/UIRenderer.h"
#include "../UI/HephaestusUI/UIScreen.h"
#include "../UI/HephaestusUI/UISimpleMenuExample.h"
#include "../UI/HephaestusUI/UISystem.h"
#include "../UI/HephaestusUI/UIText.h"
#include "../UI/HephaestusUI/UITheme.h"
#include "../UI/HephaestusUI/UIVerticalBox.h"
#include "../UI/HephaestusUI/UIButton.h"
#include "../UI/HephaestusUI/HUIAsset.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"
#include "../Scene/Components.h"
#include "../Scene/ActiveCameraSync.h"
#include "../Scene/PrefabAsset.h"
#include "../Scene/PrefabInstantiator.h"
#include "../Scene/PrefabOverrideSystem.h"
#include "../Scene/PrefabComponentIO.h"
#include "../Assets/AssetMountRegistry.h"
#include "../Assets/AssetPathPolicy.h"
#include "../Assets/CharacterRuntime.h"
#include "../Gfx/EngineFrameRenderer.h"
#include "../Animation/CharacterPlayback.h"
#include "../Scene/CharacterComponent.h"
#include "../Core/EngineLog.h"
#include "../Core/IniFile.h"
#include "../Core/Math.h"
#include "../Input/InputSystem.h"
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <cctype>
#include <limits>
#include <string_view>
#include <utility>
#include <functional>
#include <cmath>
#include <exception>
#include <new>

#include <GLFW/glfw3.h>

#ifndef LUA_AVAILABLE
    #define LUA_AVAILABLE 0
#endif

struct LuaEntityReference {
    std::uint32_t entityId = static_cast<std::uint32_t>(entt::null);
    std::uint64_t sceneIdentity = 0;
    std::uint64_t sceneGeneration = 0;
    LuaScriptEngine* owner = nullptr;

    operator std::uint32_t() const noexcept
    {
        if (!owner || !owner->m_Scene ||
            owner->m_Scene->GetIdentity() != sceneIdentity ||
            owner->m_Scene->GetRuntimeGeneration() != sceneGeneration) {
            return static_cast<std::uint32_t>(entt::null);
        }
        return entityId;
    }

    LuaEntityReference& operator=(std::uint32_t value) noexcept
    {
        entityId = value;
        return *this;
    }

#if LUA_AVAILABLE
    static LuaEntityReference* Create(lua_State* state,
                                      LuaScriptEngine* engine,
                                      std::uint32_t entity)
    {
        auto* reference = new (lua_newuserdata(state, sizeof(LuaEntityReference))) LuaEntityReference{};
        reference->entityId = entity;
        reference->owner = engine;
        reference->sceneIdentity = engine && engine->m_Scene
            ? engine->m_Scene->GetIdentity()
            : 0;
        reference->sceneGeneration = engine && engine->m_Scene
            ? engine->m_Scene->GetRuntimeGeneration()
            : 0;
        return reference;
    }
#endif
};

namespace {
    constexpr int kInvalidRef = -1;
    constexpr uint32_t kInvalidEntityId = std::numeric_limits<uint32_t>::max();

    bool ContainsUIElement(const Hephaestus::HephaestusUI::UIElement* root,
                           const Hephaestus::HephaestusUI::UIElement* target) {
        if (!root || !target) {
            return false;
        }
        if (root == target) {
            return true;
        }

        for (const std::unique_ptr<Hephaestus::HephaestusUI::UIElement>& child : root->GetChildren()) {
            if (ContainsUIElement(child.get(), target)) {
                return true;
            }
        }

        return false;
    }

#if LUA_AVAILABLE
    std::string ToUpper(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return value;
    }

    int TranslateKey(const std::string& input) {
        const int key = Hephaestus::ResolveInputControl(
            Hephaestus::InputBindingType::Keyboard, input);
        return key >= 0 ? key : GLFW_KEY_UNKNOWN;
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

    enum class RuntimeExposeType {
        Bool,
        Integer,
        Number,
        String,
        Vec2,
        Vec3,
        Color
    };

    int PushCallingEnvironment(lua_State* state) {
        lua_Debug caller{};
        if (!lua_getstack(state, 1, &caller) || !lua_getinfo(state, "f", &caller)) {
            return luaL_error(state, "Expose.* must be called from a Lua script");
        }

        for (int index = 1;; ++index) {
            const char* name = lua_getupvalue(state, -1, index);
            if (!name) {
                break;
            }
            if (std::string_view(name) == "_ENV" && lua_istable(state, -1)) {
                lua_remove(state, -2);
                return lua_gettop(state);
            }
            lua_pop(state, 1);
        }

        lua_pop(state, 1);
        return luaL_error(state, "Expose.* could not resolve the script environment");
    }

    void CheckExposeArguments(lua_State* state, RuntimeExposeType type) {
        const int expected = type == RuntimeExposeType::Vec2 ? 3 :
            type == RuntimeExposeType::Vec3 ? 4 :
            type == RuntimeExposeType::Color ? 5 : 2;
        if (lua_gettop(state) != expected) {
            luaL_error(state, "Expose declaration has the wrong number of arguments");
        }
        if (lua_type(state, 1) != LUA_TSTRING || lua_rawlen(state, 1) == 0) {
            luaL_error(state, "Expose declaration name must be a non-empty string");
        }

        if (type == RuntimeExposeType::Bool && lua_type(state, 2) != LUA_TBOOLEAN) {
            luaL_error(state, "Expose.bool default must be a boolean");
        }
        if (type == RuntimeExposeType::String && lua_type(state, 2) != LUA_TSTRING) {
            luaL_error(state, "Expose.string default must be a string");
        }
        if (type == RuntimeExposeType::Integer && !lua_isinteger(state, 2)) {
            luaL_error(state, "Expose.integer default must be an integer");
        }
        if (type == RuntimeExposeType::Number && lua_type(state, 2) != LUA_TNUMBER) {
            luaL_error(state, "Expose.number default must be a number");
        }
        if (type == RuntimeExposeType::Vec2 ||
            type == RuntimeExposeType::Vec3 ||
            type == RuntimeExposeType::Color) {
            for (int index = 2; index <= expected; ++index) {
                if (lua_type(state, index) != LUA_TNUMBER) {
                    luaL_error(state, "Expose vector and color defaults must be numbers");
                }
            }
        }
    }

    void PushExposeDefault(lua_State* state, RuntimeExposeType type) {
        if (type == RuntimeExposeType::Bool ||
            type == RuntimeExposeType::Integer ||
            type == RuntimeExposeType::Number ||
            type == RuntimeExposeType::String) {
            lua_pushvalue(state, 2);
            return;
        }

        lua_newtable(state);
        static constexpr const char* VECTOR_FIELDS[] = {"x", "y", "z", "w"};
        const int count = type == RuntimeExposeType::Vec2 ? 2 :
            type == RuntimeExposeType::Vec3 ? 3 : 4;
        for (int index = 0; index < count; ++index) {
            lua_pushvalue(state, index + 2);
            lua_setfield(state, -2, VECTOR_FIELDS[index]);
        }
    }

    int Lua_ExposeDeclare(lua_State* state) {
        const auto type = static_cast<RuntimeExposeType>(lua_tointeger(state, lua_upvalueindex(1)));
        CheckExposeArguments(state, type);

        const int environmentIndex = PushCallingEnvironment(state);
        lua_pushvalue(state, 1);
        lua_rawget(state, environmentIndex);
        const bool alreadySeeded = !lua_isnil(state, -1);
        lua_pop(state, 1);
        if (!alreadySeeded) {
            lua_pushvalue(state, 1);
            PushExposeDefault(state, type);
            lua_rawset(state, environmentIndex);
        }
        lua_pop(state, 1);
        return 0;
    }

    void SetExposeFunction(lua_State* state, const char* name, RuntimeExposeType type) {
        lua_pushinteger(state, static_cast<lua_Integer>(type));
        lua_pushcclosure(state, Lua_ExposeDeclare, 1);
        lua_setfield(state, -2, name);
    }
#endif
}

LuaScriptEngine* LuaScriptEngine::s_Instance = nullptr;

LuaScriptEngine* LuaScriptEngine::GetInstance() {
    return s_Instance;
}

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
    RebuildSearchPaths();
    LuaDebugger::Instance().Attach(m_State);
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

void LuaScriptEngine::SetFrameRenderer(Hephaestus::EngineFrameRenderer* renderer) {
    m_FrameRenderer = renderer;
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

std::string LuaScriptEngine::EvaluateConsole(const std::string& code, bool& ok) {
    ok = false;
#if LUA_AVAILABLE
    if (!m_State) {
        return "Lua is not running";
    }
    const int top = lua_gettop(m_State);
    std::string chunk = "return " + code;
    if (luaL_loadbuffer(m_State, chunk.c_str(), chunk.size(), "=console") != LUA_OK) {
        lua_pop(m_State, 1);
        if (luaL_loadbuffer(m_State, code.c_str(), code.size(), "=console") != LUA_OK) {
            std::string error = lua_tostring(m_State, -1) ? lua_tostring(m_State, -1) : "syntax error";
            lua_settop(m_State, top);
            return error;
        }
    }
    if (lua_pcall(m_State, 0, LUA_MULTRET, 0) != LUA_OK) {
        std::string error = lua_tostring(m_State, -1) ? lua_tostring(m_State, -1) : "error";
        lua_settop(m_State, top);
        return error;
    }
    ok = true;
    std::string result;
    for (int i = top + 1; i <= lua_gettop(m_State); ++i) {
        result += (i > top + 1 ? ", " : "") + LuaDebugger::Describe(m_State, i, 2);
    }
    lua_settop(m_State, top);
    return result;
#else
    (void)code;
    return "Lua is not available in this build";
#endif
}

std::vector<LuaScriptEngine::ApiEntry> LuaScriptEngine::DescribeApi() {
    std::vector<ApiEntry> entries;
#if LUA_AVAILABLE
    // A throwaway engine registers the same bindings as gameplay, so the list cannot drift.
    LuaScriptEngine* previous = s_Instance;
    {
        LuaScriptEngine probe;
        if (probe.m_State) {
            luaL_openlibs(probe.m_State);
            probe.RegisterBindings();
            lua_State* L = probe.m_State;
            lua_pushglobaltable(L);
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (lua_type(L, -2) == LUA_TSTRING) {
                    const std::string name = lua_tostring(L, -2);
                    entries.push_back({ name, lua_typename(L, lua_type(L, -1)) });
                    if (lua_type(L, -1) == LUA_TTABLE && name != "_G" && name != "package") {
                        lua_pushnil(L);
                        while (lua_next(L, -2)) {
                            if (lua_type(L, -2) == LUA_TSTRING) {
                                entries.push_back({ name + "." + lua_tostring(L, -2), lua_typename(L, lua_type(L, -1)) });
                            }
                            lua_pop(L, 1);
                        }
                    }
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
    }
    s_Instance = previous;
#endif
    return entries;
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

LuaScriptInstanceKey LuaScriptEngine::MakeInstanceKey(uint32_t entityId, LuaScriptSlot slot) {
    LuaScriptInstanceKey key;
    key.entityId = entityId;
    key.slot = slot;
    return key;
}

LuaScriptEngine::ScriptInstance* LuaScriptEngine::FindInstance(uint32_t entityId, LuaScriptSlot slot) {
    auto it = m_ScriptInstances.find(MakeInstanceKey(entityId, slot));
    if (it == m_ScriptInstances.end()) {
        return nullptr;
    }
    return &it->second;
}

bool LuaScriptEngine::CallEntityFunction(const std::string& functionName, Entity entity) {
#if LUA_AVAILABLE
    if (!entity) {
        return false;
    }
    uint32_t entityId = static_cast<uint32_t>(entity);
    ScriptInstance* instance = FindInstance(entityId, LuaScriptSlot::Gameplay);
    if (!instance) {
        return false;
    }
    return CallInstanceFunctionByName(*instance, functionName, entity, 0.0f, false);
#else
    (void)functionName;
    (void)entity;
    return false;
#endif
}

bool LuaScriptEngine::CallEntityFunction(const std::string& functionName, Entity entity,
                                         const std::vector<PropertyValue>& args) {
#if LUA_AVAILABLE
    if (!m_State || !entity) {
        return false;
    }
    ScriptInstance* instance = FindInstance(static_cast<uint32_t>(entity), LuaScriptSlot::Gameplay);
    if (!instance || !instance->valid || instance->environmentRef == kInvalidRef) {
        return false;
    }
    lua_rawgeti(m_State, LUA_REGISTRYINDEX, instance->environmentRef);
    if (!lua_istable(m_State, -1)) {
        lua_pop(m_State, 1);
        return false;
    }
    lua_getfield(m_State, -1, functionName.c_str());
    lua_remove(m_State, -2);
    if (!lua_isfunction(m_State, -1)) {
        lua_pop(m_State, 1);
        return false;
    }
    for (const PropertyValue& arg : args) {
        PushPropertyValue(arg);
    }
    const uint32_t previousEntity = m_CurrentEntityId;
    const float previousDelta = m_CurrentDeltaTime;
    m_CurrentEntityId = instance->entityId;
    SetupThisForEntity(instance->entityId);
    const bool ok = lua_pcall(m_State, static_cast<int>(args.size()), 0, 0) == LUA_OK;
    if (!ok) {
        LogLuaError(functionName);
    }
    m_CurrentEntityId = previousEntity;
    m_CurrentDeltaTime = previousDelta;
    if (previousEntity != kInvalidEntityId) {
        SetupThisForEntity(previousEntity);
    }
    return ok;
#else
    (void)functionName;
    (void)entity;
    (void)args;
    return false;
#endif
}

void LuaScriptEngine::AwakeInstanceIfNeeded(Scene& scene, entt::entity entityHandle, ScriptInstance& instance) {
#if LUA_AVAILABLE
    if (instance.awakeCalled) {
        return;
    }
    Entity entity(entityHandle, &scene);
    if (instance.onAwakeRef != kInvalidRef) {
        CallInstanceFunction(instance, instance.onAwakeRef, entity, 0.0f, false);
    }
    instance.awakeCalled = true;
    CallInstanceFunction(instance, instance.onEnableRef, entity, 0.0f, false);
#else
    (void)scene;
    (void)entityHandle;
    (void)instance;
#endif
}

void LuaScriptEngine::UpdateInstance(Scene& scene, entt::entity entityHandle, ScriptInstance& instance, float deltaTime) {
#if LUA_AVAILABLE
    Entity entity(entityHandle, &scene);
    m_ActiveUpdateOrder.push_back(MakeInstanceKey(instance.entityId, instance.slot));
    if (!CallInstanceFunction(instance, instance.onUpdateRef, entity, deltaTime, true)) {
        if (!instance.functionErrorLogged && instance.onUpdateRef != kInvalidRef) {
            instance.functionErrorLogged = true;
        }
    }
#else
    (void)scene;
    (void)entityHandle;
    (void)instance;
    (void)deltaTime;
#endif
}

void LuaScriptEngine::Update(Scene& scene, float deltaTime) {
#if LUA_AVAILABLE
    if (!m_State) {
        std::cerr << "[Lua] Update called but m_State is null!" << std::endl;
        return;
    }

    m_Scene = &scene;
    m_SceneReloadRequested = false;
    m_ActiveUpdateOrder.clear();
    m_UpdatingScripts = true;

    auto& registry = scene.GetRegistry();
    std::unordered_set<LuaScriptInstanceKey, LuaScriptInstanceKeyHash> activeKeys;
    std::vector<entt::entity> gameplayReady;
    std::vector<entt::entity> controllerReady;

    std::vector<entt::entity> scriptEntities;
    {
        auto view = registry.view<LuaScriptComponent>();
        scriptEntities.reserve(view.size());
        for (auto entityHandle : view) {
            scriptEntities.push_back(entityHandle);
        }
    }

    for (auto entityHandle : scriptEntities) {
        if (!registry.valid(entityHandle) || !registry.all_of<LuaScriptComponent>(entityHandle)) {
            continue;
        }
        auto& component = registry.get<LuaScriptComponent>(entityHandle);
        const LuaScriptInstanceKey key = MakeInstanceKey(static_cast<uint32_t>(entityHandle),
                                                         LuaScriptSlot::Gameplay);

        if (!component.enabled || (component.scriptPath.empty() && component.scriptSource.empty()) ||
            ScriptLanguageRegistry::Instance().FindForPath(component.scriptPath)) {
            // A plugin language runs scripts whose extension it claims.
            DestroyInstance(key, true);
            continue;
        }

        ScriptInstance* instance = EnsureInstance(scene, entityHandle, component, LuaScriptSlot::Gameplay);
        if (!instance || !instance->valid) {
            continue;
        }
        gameplayReady.push_back(entityHandle);
        activeKeys.insert(key);
    }

    std::vector<entt::entity> characterEntities;
    {
        auto view = registry.view<CharacterComponent>();
        characterEntities.reserve(view.size());
        for (auto entityHandle : view) {
            characterEntities.push_back(entityHandle);
        }
    }

    for (auto entityHandle : characterEntities) {
        if (!registry.valid(entityHandle) || !registry.all_of<CharacterComponent>(entityHandle)) {
            continue;
        }
        auto& character = registry.get<CharacterComponent>(entityHandle);
        const LuaScriptInstanceKey key = MakeInstanceKey(static_cast<uint32_t>(entityHandle),
                                                         LuaScriptSlot::CharacterController);

        if (!character.runController || character.controllerScriptPath.empty()) {
            DestroyInstance(key, true);
            continue;
        }

        LuaScriptComponent view;
        view.scriptPath = character.controllerScriptPath;
        view.variables = character.controllerVariables;
        view.enabled = character.runController;

        ScriptInstance* instance = EnsureInstance(scene, entityHandle, view, LuaScriptSlot::CharacterController);
        if (!instance || !instance->valid) {
            continue;
        }
        controllerReady.push_back(entityHandle);
        activeKeys.insert(key);
    }

    for (auto entityHandle : gameplayReady) {
        if (m_SceneReloadRequested) break;
        if (!registry.valid(entityHandle)) {
            continue;
        }
        ScriptInstance* instance = FindInstance(static_cast<uint32_t>(entityHandle), LuaScriptSlot::Gameplay);
        if (instance) {
            AwakeInstanceIfNeeded(scene, entityHandle, *instance);
        }
    }
    for (auto entityHandle : controllerReady) {
        if (m_SceneReloadRequested) break;
        if (!registry.valid(entityHandle)) {
            continue;
        }
        ScriptInstance* instance = FindInstance(static_cast<uint32_t>(entityHandle), LuaScriptSlot::CharacterController);
        if (instance) {
            AwakeInstanceIfNeeded(scene, entityHandle, *instance);
        }
    }
    for (auto entityHandle : gameplayReady) {
        if (m_SceneReloadRequested) break;
        if (!registry.valid(entityHandle)) {
            continue;
        }
        ScriptInstance* instance = FindInstance(static_cast<uint32_t>(entityHandle), LuaScriptSlot::Gameplay);
        if (instance) {
            UpdateInstance(scene, entityHandle, *instance, deltaTime);
        }
    }
    for (auto entityHandle : controllerReady) {
        if (m_SceneReloadRequested) break;
        if (!registry.valid(entityHandle)) {
            continue;
        }
        ScriptInstance* instance = FindInstance(static_cast<uint32_t>(entityHandle), LuaScriptSlot::CharacterController);
        if (instance) {
            UpdateInstance(scene, entityHandle, *instance, deltaTime);
        }
    }

    std::vector<LuaScriptInstanceKey> toRemove;
    toRemove.reserve(m_ScriptInstances.size());
    for (const auto& [key, instance] : m_ScriptInstances) {
        entt::entity handle = static_cast<entt::entity>(key.entityId);
        if (!registry.valid(handle)) {
            toRemove.push_back(key);
            continue;
        }
        if (activeKeys.find(key) != activeKeys.end()) {
            continue;
        }
        if (key.slot == LuaScriptSlot::Gameplay) {
            const bool stillGameplay = registry.all_of<LuaScriptComponent>(handle);
            if (!stillGameplay) {
                toRemove.push_back(key);
            }
        } else {
            const bool stillCharacter = registry.all_of<CharacterComponent>(handle);
            if (!stillCharacter) {
                toRemove.push_back(key);
            }
        }
    }

    for (const LuaScriptInstanceKey& key : toRemove) {
        DestroyInstance(key, true);
    }

    m_UpdatingScripts = false;
    if (m_SceneReloadRequested) {
        ClearDeferredSceneOps();
    } else {
        FlushDeferredSceneOps();
    }

    // Bind characters spawned this tick after Lua returns. Doing FBX load / GPU
    // upload inside Scene.instantiate (Director onUpdate) can abort the spawn.
    if (!m_ProjectRoot.empty()) {
        try {
            Hephaestus::CharacterRuntime::BindScene(scene, m_ProjectRoot, m_FrameRenderer);
        } catch (const std::exception& ex) {
            std::cerr << "[Lua] Character bind after spawn failed: " << ex.what() << "\n";
        } catch (...) {
            std::cerr << "[Lua] Character bind after spawn failed\n";
        }
    }
#else
    (void)scene;
    (void)deltaTime;
#endif
}

void LuaScriptEngine::LateUpdate(Scene& scene, float deltaTime) {
#if LUA_AVAILABLE
    if (!m_State || m_SceneReloadRequested) {
        return;
    }

    m_Scene = &scene;
    for (const LuaScriptInstanceKey& key : m_ActiveUpdateOrder) {
        if (m_SceneReloadRequested) break;
        auto it = m_ScriptInstances.find(key);
        if (it == m_ScriptInstances.end()) {
            continue;
        }
        ScriptInstance& instance = it->second;
        if (!instance.valid) {
            continue;
        }
        Entity entity(static_cast<entt::entity>(key.entityId), &scene);
        CallInstanceFunction(instance, instance.onLateUpdateRef, entity, deltaTime, true);
    }
    m_ActiveUpdateOrder.clear();
#else
    (void)scene;
    (void)deltaTime;
#endif
}

void LuaScriptEngine::DispatchAnimationFinished(Scene& scene) {
#if LUA_AVAILABLE
    if (!m_State || m_SceneReloadRequested) {
        return;
    }
    m_Scene = &scene;
    auto& registry = scene.GetRegistry();
    auto view = registry.view<CharacterComponent>();
    for (auto entityHandle : view) {
        if (m_SceneReloadRequested) break;
        CharacterComponent& character = view.get<CharacterComponent>(entityHandle);
        if (!character.player.finishedThisFrame || character.player.finishedClip.empty()) {
            continue;
        }
        const uint32_t entityId = static_cast<uint32_t>(entityHandle);
        Entity entity(entityHandle, &scene);
        const LuaScriptSlot slots[] = {LuaScriptSlot::CharacterController, LuaScriptSlot::Gameplay};
        for (LuaScriptSlot slot : slots) {
            ScriptInstance* instance = FindInstance(entityId, slot);
            if (!instance || !instance->valid) {
                continue;
            }
            if (instance->onAnimationFinishedRef == kInvalidRef) {
                continue;
            }
            lua_rawgeti(m_State, LUA_REGISTRYINDEX, instance->onAnimationFinishedRef);
            if (!lua_isfunction(m_State, -1)) {
                lua_pop(m_State, 1);
                continue;
            }
            uint32_t previousEntity = m_CurrentEntityId;
            float previousDelta = m_CurrentDeltaTime;
            m_CurrentEntityId = entityId;
            SetupThisForEntity(entityId);
            lua_pushstring(m_State, character.player.finishedClip.c_str());
            if (lua_pcall(m_State, 1, 0, 0) != LUA_OK) {
                LogLuaError("onAnimationFinished");
            }
            m_CurrentEntityId = previousEntity;
            m_CurrentDeltaTime = previousDelta;
            if (previousEntity != kInvalidEntityId) {
                SetupThisForEntity(previousEntity);
            }
            (void)entity;
        }
        character.player.finishedThisFrame = false;
    }
#else
    (void)scene;
#endif
}

void LuaScriptEngine::DispatchContactEvents(Scene& scene, const std::vector<Physics::PhysicsContactEvent>& events) {
#if LUA_AVAILABLE
    if (!m_State || m_SceneReloadRequested || events.empty()) {
        return;
    }
    m_Scene = &scene;
    for (const Physics::PhysicsContactEvent& event : events) {
        if (m_SceneReloadRequested) break;
        if (event.receiver == Physics::kNullPhysicsEntity) {
            continue;
        }
        int ScriptInstance::* refMember = nullptr;
        const char* callbackName = nullptr;
        switch (event.kind) {
            case Physics::PhysicsEventKind::TriggerEnter:
                refMember = &ScriptInstance::onTriggerEnterRef;
                callbackName = "onTriggerEnter";
                break;
            case Physics::PhysicsEventKind::TriggerStay:
                refMember = &ScriptInstance::onTriggerStayRef;
                callbackName = "onTriggerStay";
                break;
            case Physics::PhysicsEventKind::TriggerExit:
                refMember = &ScriptInstance::onTriggerExitRef;
                callbackName = "onTriggerExit";
                break;
            case Physics::PhysicsEventKind::CollisionEnter:
                refMember = &ScriptInstance::onCollisionEnterRef;
                callbackName = "onCollisionEnter";
                break;
            case Physics::PhysicsEventKind::CollisionStay:
                refMember = &ScriptInstance::onCollisionStayRef;
                callbackName = "onCollisionStay";
                break;
            case Physics::PhysicsEventKind::CollisionExit:
                refMember = &ScriptInstance::onCollisionExitRef;
                callbackName = "onCollisionExit";
                break;
        }
        if (!refMember || !callbackName) {
            continue;
        }
        const LuaScriptSlot slots[] = {LuaScriptSlot::CharacterController, LuaScriptSlot::Gameplay};
        for (LuaScriptSlot slot : slots) {
            ScriptInstance* instance = FindInstance(event.receiver, slot);
            if (!instance || !instance->valid) {
                continue;
            }
            const int functionRef = instance->*refMember;
            if (functionRef == kInvalidRef) {
                continue;
            }
            lua_rawgeti(m_State, LUA_REGISTRYINDEX, functionRef);
            if (!lua_isfunction(m_State, -1)) {
                lua_pop(m_State, 1);
                continue;
            }
            const uint32_t previousEntity = m_CurrentEntityId;
            const float previousDelta = m_CurrentDeltaTime;
            m_CurrentEntityId = event.receiver;
            SetupThisForEntity(event.receiver);
            if (event.otherAlive) {
                PushEntityUserdata(m_State, event.other);
            } else {
                lua_pushnil(m_State);
            }
            int argumentCount = 1;
            if (event.hasContact) {
                auto pushVec = [this](float x, float y, float z) {
                    lua_createtable(m_State, 0, 3);
                    lua_pushnumber(m_State, x);
                    lua_setfield(m_State, -2, "x");
                    lua_pushnumber(m_State, y);
                    lua_setfield(m_State, -2, "y");
                    lua_pushnumber(m_State, z);
                    lua_setfield(m_State, -2, "z");
                };
                lua_createtable(m_State, 0, 4);
                pushVec(event.pointX, event.pointY, event.pointZ);
                lua_setfield(m_State, -2, "point");
                pushVec(event.normalX, event.normalY, event.normalZ);
                lua_setfield(m_State, -2, "normal");
                pushVec(event.relativeVelocityX, event.relativeVelocityY, event.relativeVelocityZ);
                lua_setfield(m_State, -2, "relativeVelocity");
                lua_pushnumber(m_State, event.penetration);
                lua_setfield(m_State, -2, "penetration");
                argumentCount = 2;
            }
            if (lua_pcall(m_State, argumentCount, 0, 0) != LUA_OK) {
                LogLuaError(callbackName);
            }
            m_CurrentEntityId = previousEntity;
            m_CurrentDeltaTime = previousDelta;
            if (previousEntity != kInvalidEntityId) {
                SetupThisForEntity(previousEntity);
            }
        }
    }
#else
    (void)scene;
    (void)events;
#endif
}

void LuaScriptEngine::OnSimulationStarted() {
    m_NavMesh.reset();
    m_NavBuildAttempted = false;
    m_TimeScale = 1.0f;
#if LUA_AVAILABLE
    m_SceneReloadRequested = false;
    for (auto& [_, instance] : m_ScriptInstances) {
        instance.awakeCalled = false;
    }
    if (m_CameraBinding.setActive) {
        m_CameraBinding.setActive(false);
    }
#endif
}

void LuaScriptEngine::OnSimulationStopped() {
    m_NavMesh.reset();
    if (m_FrameRenderer) {
        m_FrameRenderer->SetScopeLens({});
    }
    m_NavBuildAttempted = false;
    m_TimeScale = 1.0f;
#if LUA_AVAILABLE
    m_SceneReloadRequested = false;
    if (!m_State) {
        return;
    }

    // Restore cursor mode to normal
    if (m_CurrentCursorMode != 0) {
        if (m_InputSystem) {
            m_InputSystem->SetCursorModeRequest(0);
        }
        if (m_Window) {
            glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
        m_CurrentCursorMode = 0;
    }

    if (m_CameraBinding.setActive) {
        m_CameraBinding.setActive(false);
    }

    for (auto& [key, instance] : m_ScriptInstances) {
        if (!instance.valid || instance.environmentRef == kInvalidRef) {
            continue;
        }
        Entity entity(static_cast<entt::entity>(key.entityId), m_Scene);
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
        instance.functionErrorLogged = false;
    }
    // Drop all cached instances so the next Play start always rebuilds from scene data.
    m_ScriptInstances.clear();
    m_ActiveUpdateOrder.clear();

    // Drop deferred spawn/destroy — Stop restores the scene snapshot.
    ClearDeferredSceneOps();

    // Destroy all managed HUI instances so nothing leaks between sessions
    destroyAllHUIInstances();
    ResetUIHandleMaps();
#endif
}

void LuaScriptEngine::OnEntityDestroyed(Entity entity) {
#if LUA_AVAILABLE
    if (!entity) {
        return;
    }
    DestroyAllSlotsForEntity(static_cast<uint32_t>(entity), true);
#else
    (void)entity;
#endif
}

void LuaScriptEngine::Shutdown() {
#if LUA_AVAILABLE
    if (m_State) {
        for (auto& [key, instance] : m_ScriptInstances) {
            if (instance.valid && instance.environmentRef != kInvalidRef && m_Scene) {
                Entity entity(static_cast<entt::entity>(key.entityId), m_Scene);
                if (entity) {
                    CallInstanceFunction(instance, instance.onDisableRef, entity, 0.0f, false);
                    CallInstanceFunction(instance, instance.onDestroyRef, entity, 0.0f, false);
                }
            }
            ReleaseInstanceResources(instance);
        }
        m_ScriptInstances.clear();
        LuaDebugger::Instance().Detach(m_State);
        lua_close(m_State);
        m_State = nullptr;
    }
    std::cout << "[Lua] Scripting engine shut down" << std::endl;
#endif
    m_Scene = nullptr;
    m_Window = nullptr;
    m_InputSystem = nullptr;
    m_MissingInputNames.clear();
    m_ActiveUpdateOrder.clear();
}

void LuaScriptEngine::SetCameraBinding(CameraBinding binding) {
    m_CameraBinding = std::move(binding);
}

void LuaScriptEngine::SetPhysicsBinding(PhysicsBinding binding) {
    m_PhysicsBinding = std::move(binding);
}

void LuaScriptEngine::SetCharacterBinding(CharacterBinding binding) {
    m_CharacterBinding = std::move(binding);
}

void LuaScriptEngine::SetSpawnedMeshLoader(SpawnedMeshLoader loader) {
    m_SpawnedMeshLoader = std::move(loader);
}

void LuaScriptEngine::SetInputSystem(Hephaestus::InputSystem* inputSystem) {
    m_InputSystem = inputSystem;
    m_MissingInputNames.clear();
}

void LuaScriptEngine::SetAudioEngine(AudioEngine* audioEngine) {
    m_AudioEngine = audioEngine;
}

void LuaScriptEngine::SetUIBindings(Hephaestus::HephaestusUI::UISystem* uiSystem,
                                    Hephaestus::HephaestusUI::UIContext* uiContext,
                                    Hephaestus::HephaestusUI::UIRenderer* uiRenderer) {
    m_UISystem = uiSystem;
    m_UIContext = uiContext;
    m_UIRenderer = uiRenderer;
    if (!m_UISystem || !m_UIContext || !m_UIRenderer) {
        ResetUIHandleMaps();
    }
}

uint32_t LuaScriptEngine::RegisterUIScreenHandle(const std::shared_ptr<Hephaestus::HephaestusUI::UIScreen>& screen) {
    if (!screen) {
        return 0;
    }

    for (const auto& [handle, existing] : m_UIScreenHandles) {
        if (existing == screen) {
            return handle;
        }
    }

    const uint32_t handle = m_NextUIScreenHandle++;
    m_UIScreenHandles[handle] = screen;
    RegisterUIElementHandle(screen->GetRootCanvas());
    return handle;
}

uint32_t LuaScriptEngine::RegisterUIElementHandle(Hephaestus::HephaestusUI::UIElement* element) {
    if (!element) {
        return 0;
    }

    const auto existing = m_UIElementReverseHandles.find(element);
    if (existing != m_UIElementReverseHandles.end()) {
        return existing->second;
    }

    const uint32_t handle = m_NextUIElementHandle++;
    m_UIElementHandles[handle] = element;
    m_UIElementReverseHandles[element] = handle;
    return handle;
}

std::shared_ptr<Hephaestus::HephaestusUI::UIScreen> LuaScriptEngine::ResolveUIScreen(uint32_t handle) const {
    const auto it = m_UIScreenHandles.find(handle);
    if (it == m_UIScreenHandles.end()) {
        return nullptr;
    }
    return it->second;
}

Hephaestus::HephaestusUI::UIElement* LuaScriptEngine::ResolveUIElement(uint32_t handle) const {
    const auto it = m_UIElementHandles.find(handle);
    if (it == m_UIElementHandles.end()) {
        return nullptr;
    }

    Hephaestus::HephaestusUI::UIElement* element = it->second;
    if (!m_UISystem || !element) {
        return nullptr;
    }

    for (const std::shared_ptr<Hephaestus::HephaestusUI::UIScreen>& screen : m_UISystem->GetScreens()) {
        if (!screen || !screen->GetRootCanvas()) {
            continue;
        }
        if (ContainsUIElement(screen->GetRootCanvas(), element)) {
            return element;
        }
    }

    return nullptr;
}

void LuaScriptEngine::ResetUIHandleMaps() {
#if LUA_AVAILABLE
    if (m_State) {
        for (const auto& [handle, ref] : m_UIButtonClickCallbacks) {
            luaL_unref(m_State, LUA_REGISTRYINDEX, ref);
        }
        for (const auto& [handle, ref] : m_UIHoverCallbacks) {
            luaL_unref(m_State, LUA_REGISTRYINDEX, ref);
        }
    }
#endif
    m_UIButtonClickCallbacks.clear();
    m_UIHoverCallbacks.clear();
    m_UIScreenHandles.clear();
    m_UIElementHandles.clear();
    m_UIElementReverseHandles.clear();
    m_HUIWidgetMaps.clear();
    m_ActiveHUIInstances.clear();
    m_HUIPathToHandle.clear();
    m_HUIStack.clear();
}

void LuaScriptEngine::RegisterBindings() {
#if LUA_AVAILABLE
    RegisterEntityBindings();
    RegisterTransformBindings();
    RegisterMeshBindings();
    RegisterParticleBindings();
    RegisterNavBindings();
    RegisterInputBindings();
    RegisterUtilityBindings();
    RegisterExposeBindings();
    RegisterApplicationBindings();
    RegisterCameraBindings();
    RegisterGraphicsBindings();
    RegisterPhysicsBindings();
    RegisterCharacterBindings();
    RegisterAudioBindings();
    RegisterUIBindings();
    RegisterDisplayDestroyBindings();
    RegisterThisBindings();
    RegisterSceneBindings();
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

void LuaScriptEngine::RegisterMeshBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);
    lua_pushcfunction(m_State, Lua_Mesh_setVisible);
    lua_setfield(m_State, -2, "setVisible");
    lua_pushcfunction(m_State, Lua_Mesh_isVisible);
    lua_setfield(m_State, -2, "isVisible");
    lua_setglobal(m_State, "Mesh");
#endif
}

void LuaScriptEngine::RegisterParticleBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);
    lua_pushcfunction(m_State, Lua_Particles_play);
    lua_setfield(m_State, -2, "play");
    lua_pushcfunction(m_State, Lua_Particles_stop);
    lua_setfield(m_State, -2, "stop");
    lua_pushcfunction(m_State, Lua_Particles_burst);
    lua_setfield(m_State, -2, "burst");
    lua_pushcfunction(m_State, Lua_Particles_isPlaying);
    lua_setfield(m_State, -2, "isPlaying");
    lua_pushcfunction(m_State, Lua_Particles_count);
    lua_setfield(m_State, -2, "count");
    lua_setglobal(m_State, "Particles");
#endif
}

int LuaScriptEngine::Lua_Particles_play(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_Scene) {
        lua_pushnil(L);
        return 1;
    }
    const entt::entity handle = static_cast<entt::entity>(engine->m_CurrentEntityId);
    auto& registry = engine->m_Scene->GetRegistry();
    if (!registry.all_of<ParticleEmitterComponent>(handle)) {
        lua_pushnil(L);
        return 1;
    }
    auto& emitter = registry.get<ParticleEmitterComponent>(handle);
    const Mat4 world = engine->m_Scene->GetWorldTransform(Entity(handle, engine->m_Scene));
    Hephaestus::Particles::Play(emitter, world);
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Particles_stop(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_Scene) {
        lua_pushnil(L);
        return 1;
    }
    const entt::entity handle = static_cast<entt::entity>(engine->m_CurrentEntityId);
    auto& registry = engine->m_Scene->GetRegistry();
    if (!registry.all_of<ParticleEmitterComponent>(handle)) {
        lua_pushnil(L);
        return 1;
    }
    auto& emitter = registry.get<ParticleEmitterComponent>(handle);
    Hephaestus::Particles::Stop(emitter);
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Particles_burst(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_Scene) {
        lua_pushnil(L);
        return 1;
    }
    const entt::entity handle = static_cast<entt::entity>(engine->m_CurrentEntityId);
    auto& registry = engine->m_Scene->GetRegistry();
    if (!registry.all_of<ParticleEmitterComponent>(handle)) {
        lua_pushnil(L);
        return 1;
    }
    auto& emitter = registry.get<ParticleEmitterComponent>(handle);
    const Mat4 world = engine->m_Scene->GetWorldTransform(Entity(handle, engine->m_Scene));
    Hephaestus::Particles::Burst(emitter, world, static_cast<int>(luaL_optinteger(L, 1, emitter.burst > 0 ? emitter.burst : 1)));
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Particles_isPlaying(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_Scene) {
        lua_pushnil(L);
        return 1;
    }
    const entt::entity handle = static_cast<entt::entity>(engine->m_CurrentEntityId);
    auto& registry = engine->m_Scene->GetRegistry();
    if (!registry.all_of<ParticleEmitterComponent>(handle)) {
        lua_pushnil(L);
        return 1;
    }
    auto& emitter = registry.get<ParticleEmitterComponent>(handle);
    lua_pushboolean(L, emitter.playing);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Particles_count(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_Scene) {
        lua_pushnil(L);
        return 1;
    }
    const entt::entity handle = static_cast<entt::entity>(engine->m_CurrentEntityId);
    auto& registry = engine->m_Scene->GetRegistry();
    if (!registry.all_of<ParticleEmitterComponent>(handle)) {
        lua_pushnil(L);
        return 1;
    }
    auto& emitter = registry.get<ParticleEmitterComponent>(handle);
    lua_pushinteger(L, static_cast<lua_Integer>(emitter.particles.size()));
    return 1;
#else
    (void)L;
    return 0;
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

    lua_pushcfunction(m_State, Lua_Input_getMouseDelta);
    lua_setfield(m_State, -2, "getMouseDelta");

    lua_pushcfunction(m_State, Lua_Input_getScrollDelta);
    lua_setfield(m_State, -2, "getScrollDelta");

    lua_pushcfunction(m_State, Lua_Input_isActionDown);
    lua_setfield(m_State, -2, "isActionDown");

    lua_pushcfunction(m_State, Lua_Input_isActionPressed);
    lua_setfield(m_State, -2, "isActionPressed");

    lua_pushcfunction(m_State, Lua_Input_isActionReleased);
    lua_setfield(m_State, -2, "isActionReleased");

    lua_pushcfunction(m_State, Lua_Input_getAxis);
    lua_setfield(m_State, -2, "getAxis");

    lua_pushcfunction(m_State, Lua_Input_isGamepadConnected);
    lua_setfield(m_State, -2, "isGamepadConnected");

    lua_pushcfunction(m_State, Lua_Input_setCursorMode);
    lua_setfield(m_State, -2, "setCursorMode");

    lua_pushcfunction(m_State, Lua_Input_getBindings);
    lua_setfield(m_State, -2, "getBindings");

    lua_pushcfunction(m_State, Lua_Input_setBindings);
    lua_setfield(m_State, -2, "setBindings");

    lua_pushcfunction(m_State, Lua_Input_getPressedControl);
    lua_setfield(m_State, -2, "getPressedControl");

    lua_setglobal(m_State, "Input");
#endif
}

void LuaScriptEngine::RegisterUtilityBindings() {
#if LUA_AVAILABLE
    lua_register(m_State, "engine_log", [](lua_State* L) -> int {
        const char* msg = luaL_checkstring(L, 1);
        Hephaestus::EngineLog::RecordOnly(Hephaestus::EngineLogLevel::Info, msg, "Lua");
        std::cout << "[Lua] " << msg << std::endl;
        return 0;
    });

    lua_newtable(m_State);
    lua_pushcfunction(m_State, Lua_Time_deltaTime);
    lua_setfield(m_State, -2, "deltaTime");
    lua_pushcfunction(m_State, Lua_Time_unscaledDeltaTime);
    lua_setfield(m_State, -2, "unscaledDeltaTime");
    lua_pushcfunction(m_State, Lua_Time_setScale);
    lua_setfield(m_State, -2, "setScale");
    lua_pushcfunction(m_State, Lua_Time_getScale);
    lua_setfield(m_State, -2, "getScale");
    lua_setglobal(m_State, "Time");

    lua_newtable(m_State);
    lua_pushcfunction(m_State, Lua_Storage_save);
    lua_setfield(m_State, -2, "save");
    lua_pushcfunction(m_State, Lua_Storage_load);
    lua_setfield(m_State, -2, "load");
    lua_setglobal(m_State, "Storage");

    lua_newtable(m_State);
    lua_pushcfunction(m_State, Lua_Config_load);
    lua_setfield(m_State, -2, "load");
    lua_pushcfunction(m_State, Lua_Config_save);
    lua_setfield(m_State, -2, "save");
    lua_pushcfunction(m_State, Lua_Config_path);
    lua_setfield(m_State, -2, "path");
    lua_setglobal(m_State, "Config");
#endif
}

void LuaScriptEngine::RegisterExposeBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);
    SetExposeFunction(m_State, "bool", RuntimeExposeType::Bool);
    SetExposeFunction(m_State, "integer", RuntimeExposeType::Integer);
    SetExposeFunction(m_State, "number", RuntimeExposeType::Number);
    SetExposeFunction(m_State, "string", RuntimeExposeType::String);
    SetExposeFunction(m_State, "vec2", RuntimeExposeType::Vec2);
    SetExposeFunction(m_State, "vec3", RuntimeExposeType::Vec3);
    SetExposeFunction(m_State, "color", RuntimeExposeType::Color);
    lua_setglobal(m_State, "Expose");
#endif
}

void LuaScriptEngine::RegisterApplicationBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);

    lua_pushcfunction(m_State, Lua_Application_quit);
    lua_setfield(m_State, -2, "quit");

    lua_pushcfunction(m_State, Lua_Application_isDevelopment);
    lua_setfield(m_State, -2, "isDevelopment");

    lua_setglobal(m_State, "Application");
#endif
}

int LuaScriptEngine::Lua_Application_isDevelopment(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    lua_pushboolean(L, engine && engine->m_Development);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Application_quit(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (engine) {
        if (engine->m_Window) {
            glfwSetWindowShouldClose(engine->m_Window, GLFW_TRUE);
        } else if (engine->m_QuitRequestCallback) {
            engine->m_QuitRequestCallback();
        }
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

void LuaScriptEngine::RegisterGraphicsBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);
    lua_pushcfunction(m_State, Lua_Graphics_setMaxTextureSize);
    lua_setfield(m_State, -2, "setMaxTextureSize");
    lua_pushcfunction(m_State, Lua_Graphics_getMaxTextureSize);
    lua_setfield(m_State, -2, "getMaxTextureSize");
    lua_pushcfunction(m_State, Lua_Graphics_setScopeLens);
    lua_setfield(m_State, -2, "setScopeLens");
    lua_setglobal(m_State, "Graphics");
#endif
}

int LuaScriptEngine::Lua_Graphics_setMaxTextureSize(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const lua_Integer pixels = luaL_checkinteger(L, 1);
    if (pixels < 0) {
        return luaL_error(L, "Graphics.setMaxTextureSize: size must be 0 (no limit) or more");
    }
    if (engine && engine->m_FrameRenderer) {
        engine->m_FrameRenderer->SetMaxMaterialTextureSize(static_cast<uint32_t>(pixels));
    }
#else
    (void)L;
#endif
    return 0;
}

int LuaScriptEngine::Lua_Graphics_getMaxTextureSize(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    lua_pushinteger(L, engine && engine->m_FrameRenderer ? engine->m_FrameRenderer->GetMaxMaterialTextureSize() : 0);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Graphics_setScopeLens(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    Hephaestus::EngineFrameRenderer::ScopeLens lens;
    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        auto number = [L](const char* key, float& out) {
            lua_getfield(L, 1, key);
            if (lua_isnumber(L, -1)) {
                out = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);
        };
        lens.radius = 0.45f;
        number("radius", lens.radius);
        number("distortion", lens.distortion);
        number("aberration", lens.aberration);
        number("edgeDarkening", lens.edgeDarkening);
        number("ringWidth", lens.ringWidth);
        number("opacity", lens.opacity);
        lua_getfield(L, 1, "reticle");
        if (!lua_isnil(L, -1)) {
            lens.reticle = lua_toboolean(L, -1) != 0;
        }
        lua_pop(L, 1);
    }
    if (engine && engine->m_FrameRenderer) {
        engine->m_FrameRenderer->SetScopeLens(lens);
    }
#else
    (void)L;
#endif
    return 0;
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

    lua_pushcfunction(m_State, Lua_Camera_worldToScreen);
    lua_setfield(m_State, -2, "worldToScreen");

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

    lua_pushcfunction(m_State, Lua_Physics_isGrounded);
    lua_setfield(m_State, -2, "isGrounded");

    lua_pushcfunction(m_State, Lua_Physics_move);
    lua_setfield(m_State, -2, "move");

    lua_pushcfunction(m_State, Lua_Physics_jump);
    lua_setfield(m_State, -2, "jump");

    lua_setglobal(m_State, "Physics");
#endif
}

void LuaScriptEngine::RegisterCharacterBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);
    lua_pushcfunction(m_State, Lua_Character_move);
    lua_setfield(m_State, -2, "move");
    lua_pushcfunction(m_State, Lua_Character_jump);
    lua_setfield(m_State, -2, "jump");
    lua_pushcfunction(m_State, Lua_Character_setCrouched);
    lua_setfield(m_State, -2, "setCrouched");
    lua_pushcfunction(m_State, Lua_Character_isGrounded);
    lua_setfield(m_State, -2, "isGrounded");
    lua_pushcfunction(m_State, Lua_Character_setSpeedMultiplier);
    lua_setfield(m_State, -2, "setSpeedMultiplier");
    lua_pushcfunction(m_State, Lua_Character_getSpeedMultiplier);
    lua_setfield(m_State, -2, "getSpeedMultiplier");
    lua_pushcfunction(m_State, Lua_Character_isCrouched);
    lua_setfield(m_State, -2, "isCrouched");
    lua_pushcfunction(m_State, Lua_Character_isStandBlocked);
    lua_setfield(m_State, -2, "isStandBlocked");

    lua_pushcfunction(m_State, Lua_Character_setProne);
    lua_setfield(m_State, -2, "setProne");

    lua_pushcfunction(m_State, Lua_Character_isProne);
    lua_setfield(m_State, -2, "isProne");
    lua_pushcfunction(m_State, Lua_Character_ragdoll);
    lua_setfield(m_State, -2, "ragdoll");
    lua_pushcfunction(m_State, Lua_Character_isRagdoll);
    lua_setfield(m_State, -2, "isRagdoll");
    lua_pushcfunction(m_State, Lua_Character_getVelocity);
    lua_setfield(m_State, -2, "getVelocity");
    lua_pushcfunction(m_State, Lua_Character_getGroundNormal);
    lua_setfield(m_State, -2, "getGroundNormal");
    lua_pushcfunction(m_State, Lua_Character_getGroundVelocity);
    lua_setfield(m_State, -2, "getGroundVelocity");
    lua_pushcfunction(m_State, Lua_Character_getGroundState);
    lua_setfield(m_State, -2, "getGroundState");
    lua_pushcfunction(m_State, Lua_Character_teleport);
    lua_setfield(m_State, -2, "teleport");
    lua_pushcfunction(m_State, Lua_Character_setFacingYaw);
    lua_setfield(m_State, -2, "setFacingYaw");
    lua_setglobal(m_State, "Character");
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
    if (ok) {
        if (TransformComponent* transform = engine->GetTransform(engine->m_CurrentEntityId)) {
            transform->rotation = Vec3(x, y, z);
        }
    }
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
    const char* layerName = nullptr;
    if (lua_gettop(L) >= 8 && !lua_isnoneornil(L, 8)) {
        layerName = luaL_checkstring(L, 8);
    }
    bool ignoreSensors = false;
    if (lua_gettop(L) >= 9 && !lua_isnoneornil(L, 9)) {
        ignoreSensors = lua_toboolean(L, 9) != 0;
    }
    std::vector<uint32_t> ignoreEntityIds;
    if (lua_gettop(L) >= 10 && !lua_isnoneornil(L, 10)) {
        if (lua_istable(L, 10)) {
            const lua_Integer count = luaL_len(L, 10);
            for (lua_Integer i = 1; i <= count; ++i) {
                lua_rawgeti(L, 10, i);
                LuaEntityReference* listed = static_cast<LuaEntityReference*>(luaL_testudata(L, -1, "HephaestusEntity"));
                if (!listed) {
                    return luaL_error(L, "Physics.raycast argument 10 must hold only entities (item %d)",
                                      static_cast<int>(i));
                }
                ignoreEntityIds.push_back(*listed);
                lua_pop(L, 1);
            }
        } else {
            LuaEntityReference* explicitId = static_cast<LuaEntityReference*>(luaL_testudata(L, 10, "HephaestusEntity"));
            if (!explicitId) {
                return luaL_error(L, "Physics.raycast expected an entity or a list of entities as argument 10");
            }
            ignoreEntityIds.push_back(*explicitId);
        }
    } else {
        ignoreEntityIds.push_back(engine->m_CurrentEntityId);
    }

    PhysicsRaycastHit hit{};
    std::string error;
    bool ok = engine->m_PhysicsBinding.raycast(
        ignoreEntityIds,
        Vec3(ox, oy, oz),
        Vec3(dx, dy, dz),
        maxDistance,
        hit,
        layerName,
        error,
        ignoreSensors
    );
    if (!error.empty()) {
        return luaL_error(L, "%s", error.c_str());
    }

    if (!ok || !hit.hit) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushboolean(L, true);
    lua_setfield(L, -2, "hit");
    lua_pushnumber(L, hit.distance);
    lua_setfield(L, -2, "distance");
    lua_pushboolean(L, hit.sensor);
    lua_setfield(L, -2, "sensor");

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

    if (hit.entityId != std::numeric_limits<uint32_t>::max()) {
        engine->PushEntityUserdata(L, hit.entityId);
        lua_setfield(L, -2, "entity");
    } else {
        lua_pushnil(L);
        lua_setfield(L, -2, "entity");
    }

    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Physics_isGrounded(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_PhysicsBinding.isGrounded) {
        lua_pushboolean(L, false);
        return 1;
    }
    lua_pushboolean(L, engine->m_PhysicsBinding.isGrounded(engine->m_CurrentEntityId));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Physics_move(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_PhysicsBinding.move) {
        lua_pushboolean(L, false);
        return 1;
    }
    float wishX = static_cast<float>(luaL_checknumber(L, 1));
    float wishZ = static_cast<float>(luaL_checknumber(L, 2));
    float speed = static_cast<float>(luaL_checknumber(L, 3));
    lua_pushboolean(L, engine->m_PhysicsBinding.move(engine->m_CurrentEntityId, wishX, wishZ, speed));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

namespace {

void PushVec3(lua_State* L, const Vec3& value)
{
    lua_newtable(L);
    lua_pushnumber(L, value.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, value.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, value.z);
    lua_setfield(L, -2, "z");
}

} // namespace

int LuaScriptEngine::Lua_Character_move(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.move) {
        lua_pushboolean(L, false);
        return 1;
    }
    const int argc = lua_gettop(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    float z = 0.0f;
    if (argc >= 3) {
        z = static_cast<float>(luaL_checknumber(L, 3));
    } else {
        z = static_cast<float>(luaL_checknumber(L, 2));
    }
    lua_pushboolean(L, engine->m_CharacterBinding.move(engine->m_CurrentEntityId, x, z));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_jump(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.jump) {
        lua_pushboolean(L, false);
        return 1;
    }
    lua_pushboolean(L, engine->m_CharacterBinding.jump(engine->m_CurrentEntityId));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_setCrouched(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.setCrouched) {
        lua_pushboolean(L, false);
        return 1;
    }
    const bool crouched = lua_toboolean(L, 1) != 0;
    lua_pushboolean(L, engine->m_CharacterBinding.setCrouched(engine->m_CurrentEntityId, crouched));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_isGrounded(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.isGrounded) {
        lua_pushboolean(L, false);
        return 1;
    }
    lua_pushboolean(L, engine->m_CharacterBinding.isGrounded(engine->m_CurrentEntityId));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_setSpeedMultiplier(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.setSpeedMultiplier) {
        lua_pushboolean(L, false);
        return 1;
    }
    const float multiplier = static_cast<float>(luaL_checknumber(L, 1));
    lua_pushboolean(L, engine->m_CharacterBinding.setSpeedMultiplier(engine->m_CurrentEntityId, multiplier));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_getSpeedMultiplier(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    float multiplier = 1.0f;
    if (engine && engine->CurrentEntityValid() && engine->m_CharacterBinding.getSpeedMultiplier) {
        engine->m_CharacterBinding.getSpeedMultiplier(engine->m_CurrentEntityId, multiplier);
    }
    lua_pushnumber(L, multiplier);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_isCrouched(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.isCrouched) {
        lua_pushboolean(L, false);
        return 1;
    }
    lua_pushboolean(L, engine->m_CharacterBinding.isCrouched(engine->m_CurrentEntityId));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_isStandBlocked(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.isStandBlocked) {
        lua_pushboolean(L, false);
        return 1;
    }
    lua_pushboolean(L, engine->m_CharacterBinding.isStandBlocked(engine->m_CurrentEntityId));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_setProne(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.setProne) {
        lua_pushboolean(L, false);
        return 1;
    }
    const bool prone = lua_toboolean(L, 1) != 0;
    lua_pushboolean(L, engine->m_CharacterBinding.setProne(engine->m_CurrentEntityId, prone));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_isProne(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.isProne) {
        lua_pushboolean(L, false);
        return 1;
    }
    lua_pushboolean(L, engine->m_CharacterBinding.isProne(engine->m_CurrentEntityId));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_ragdoll(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.ragdoll) {
        lua_pushboolean(L, false);
        return 1;
    }
    const Vec3 impulse(static_cast<float>(luaL_optnumber(L, 1, 0.0)), static_cast<float>(luaL_optnumber(L, 2, 0.0)),
                       static_cast<float>(luaL_optnumber(L, 3, 0.0)));
    Vec3 point;
    const bool hasPoint = lua_isnumber(L, 4) && lua_isnumber(L, 5) && lua_isnumber(L, 6);
    if (hasPoint) {
        point = Vec3(static_cast<float>(lua_tonumber(L, 4)), static_cast<float>(lua_tonumber(L, 5)),
                     static_cast<float>(lua_tonumber(L, 6)));
    }
    std::string error;
    const bool ok = engine->m_CharacterBinding.ragdoll(engine->m_CurrentEntityId, impulse, hasPoint ? &point : nullptr,
                                                       error);
    if (!ok && !error.empty()) {
        engine->LogBindingErrorForEntity(engine->m_CurrentEntityId, "Character.ragdoll: " + error);
    }
    lua_pushboolean(L, ok);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_isRagdoll(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.isRagdoll) {
        lua_pushboolean(L, false);
        return 1;
    }
    lua_pushboolean(L, engine->m_CharacterBinding.isRagdoll(engine->m_CurrentEntityId));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_getVelocity(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    Vec3 velocity;
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.getVelocity
        || !engine->m_CharacterBinding.getVelocity(engine->m_CurrentEntityId, velocity)) {
        lua_pushnil(L);
        return 1;
    }
    PushVec3(L, velocity);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_getGroundNormal(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    Vec3 normal;
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.getGroundNormal
        || !engine->m_CharacterBinding.getGroundNormal(engine->m_CurrentEntityId, normal)) {
        lua_pushnil(L);
        return 1;
    }
    PushVec3(L, normal);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_getGroundVelocity(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    Vec3 velocity;
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.getGroundVelocity
        || !engine->m_CharacterBinding.getGroundVelocity(engine->m_CurrentEntityId, velocity)) {
        lua_pushnil(L);
        return 1;
    }
    PushVec3(L, velocity);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_getGroundState(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    std::string state;
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.getGroundState
        || !engine->m_CharacterBinding.getGroundState(engine->m_CurrentEntityId, state)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, state.c_str());
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_teleport(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.teleport) {
        lua_pushboolean(L, false);
        return 1;
    }
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const float z = static_cast<float>(luaL_checknumber(L, 3));
    lua_pushboolean(L, engine->m_CharacterBinding.teleport(engine->m_CurrentEntityId, x, y, z));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_setFacingYaw(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_CharacterBinding.setFacingYaw) {
        lua_pushboolean(L, false);
        return 1;
    }
    const float yaw = static_cast<float>(luaL_checknumber(L, 1));
    lua_pushboolean(L, engine->m_CharacterBinding.setFacingYaw(engine->m_CurrentEntityId, yaw));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Physics_jump(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_PhysicsBinding.jump) {
        lua_pushboolean(L, false);
        return 1;
    }
    float impulse = static_cast<float>(luaL_checknumber(L, 1));
    lua_pushboolean(L, engine->m_PhysicsBinding.jump(engine->m_CurrentEntityId, impulse));
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

    // Opened project first: CWD is often the build output dir, which may contain a
    // stale copy of assets/; the user's edited .hui lives under the project root.
    if (!m_ProjectRoot.empty()) {
        add(m_ProjectRoot);
        add(m_ProjectRoot / "assets");
        add(m_ProjectRoot / "assets" / "scripts");
        add(m_ProjectRoot / "scripts");
    }
    for (const auto& mount : Hephaestus::AssetMountRegistry::Instance().Mounts()) {
        if (mount.scheme.rfind("plugin://", 0) == 0) {
            add(mount.root / "Scripts");
        }
    }

    const std::filesystem::path current = std::filesystem::current_path();
    add(current);
    // Deployed game paths (scripts next to executable)
    add(current / "scripts");
    add(current / "assets" / "scripts");
    add(current / "assets");

    // Source-tree fallbacks are development-only. A packaged game is rooted by
    // game.ini and must never resolve scripts from a parent engine checkout.
    if (!std::filesystem::exists(current / "game.ini")) {
        add(current / "engine" / "Game" / "scripts");
    }

#if LUA_AVAILABLE
    if (m_State) {
        auto luaPattern = [](const std::filesystem::path& dir) {
            return (dir / "?.lua").generic_string() + ";" +
                   (dir / "?" / "init.lua").generic_string() + ";";
        };
        std::string prefix;
        if (!m_ProjectRoot.empty()) {
            prefix += luaPattern(m_ProjectRoot / "assets" / "scripts");
        }
        for (const auto& mount : Hephaestus::AssetMountRegistry::Instance().Mounts()) {
            if (mount.scheme.rfind("plugin://", 0) == 0) {
                prefix += luaPattern(mount.root / "Scripts");
            }
        }
        if (!prefix.empty()) {
            lua_getglobal(m_State, "package");
            if (lua_istable(m_State, -1)) {
                lua_getfield(m_State, -1, "path");
                const char* currentPath = lua_tostring(m_State, -1);
                const std::string combined = prefix + (currentPath ? currentPath : "");
                lua_pop(m_State, 1);
                lua_pushstring(m_State, combined.c_str());
                lua_setfield(m_State, -2, "path");
            }
            lua_pop(m_State, 1);
        }
    }
#endif
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

void LuaScriptEngine::RequestReloadAll() {
    for (auto& [key, instance] : m_ScriptInstances) {
        (void)key;
        instance.forceReload = true;
    }
}

LuaScriptEngine::ScriptInstance* LuaScriptEngine::EnsureInstance(Scene& scene,
                                                                 entt::entity entityHandle,
                                                                 LuaScriptComponent& component,
                                                                 LuaScriptSlot slot) {
#if LUA_AVAILABLE
    uint32_t entityId = static_cast<uint32_t>(entityHandle);
    const LuaScriptInstanceKey key = MakeInstanceKey(entityId, slot);
    ScriptInstance& instance = m_ScriptInstances[key];
    instance.entityId = entityId;
    instance.slot = slot;

    if (component.scriptPath.empty() && component.scriptSource.empty()) {
        return nullptr;
    }

    // Script files are checked for edits twice a second, not on every frame for every instance.
    const auto now = std::chrono::steady_clock::now();
    if (component.scriptSource.empty() && instance.valid && !instance.forceReload &&
        instance.userPath == component.scriptPath && now - instance.lastFileCheck < kScriptRecheckInterval) {
        return &instance;
    }

    bool hasEmbeddedSource = !component.scriptSource.empty();
    size_t sourceHash = hasEmbeddedSource ? std::hash<std::string>{}(component.scriptSource) : 0;

    std::filesystem::path resolved = component.scriptPath.empty()
        ? std::filesystem::path("<embedded>")
        : ResolveScriptPath(component.scriptPath);
    bool pathChanged = instance.userPath != component.scriptPath;
    instance.userPath = component.scriptPath;
    instance.resolvedPath = resolved;

    bool sourceChanged = hasEmbeddedSource && instance.embeddedSourceHash != sourceHash;

    if ((pathChanged || sourceChanged) && instance.valid) {
        Entity entity(entityHandle, &scene);
        CallInstanceFunction(instance, instance.onDisableRef, entity, 0.0f, false);
        CallInstanceFunction(instance, instance.onDestroyRef, entity, 0.0f, false);
        ReleaseInstanceResources(instance);
        instance.awakeCalled = false;
        instance.valid = false;
        instance.errorLogged = false;
        instance.functionErrorLogged = false;
        instance.lastBindingError.clear();
    }

    if (hasEmbeddedSource) {
        bool needsReload = !instance.valid || sourceChanged || instance.forceReload;
        if (needsReload) {
            Entity entity(entityHandle, &scene);
            if (instance.valid) {
                CallInstanceFunction(instance, instance.onDisableRef, entity, 0.0f, false);
                CallInstanceFunction(instance, instance.onDestroyRef, entity, 0.0f, false);
            }
            ReleaseInstanceResources(instance);
            instance.awakeCalled = false;
            instance.valid = false;

            if (!LoadScriptForInstance(instance, component, entity)) {
                instance.errorLogged = true;
                return nullptr;
            }

            instance.embeddedSourceHash = sourceHash;
            instance.errorLogged = false;
            instance.forceReload = false;
        }
        return &instance;
    }

    std::error_code fileError;
    auto timestamp = std::filesystem::exists(resolved, fileError)
        ? std::filesystem::last_write_time(resolved, fileError)
        : std::filesystem::file_time_type{};
    instance.lastFileCheck = now;

    if (fileError) {
        if (!instance.errorLogged) {
            std::cerr << "[Lua] Unable to read script '" << resolved << "': " << fileError.message() << std::endl;
            instance.errorLogged = true;
        }
        return nullptr;
    }

    bool needsReload = !instance.valid || instance.forceReload ||
        (instance.valid && timestamp != instance.lastWriteTime);

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
        instance.forceReload = false;
    }

    return &instance;
#else
    (void)scene;
    (void)entityHandle;
    (void)component;
    return nullptr;
#endif
}

void LuaScriptEngine::DestroyInstance(LuaScriptInstanceKey key, bool callOnDestroy) {
#if LUA_AVAILABLE
    auto it = m_ScriptInstances.find(key);
    if (it == m_ScriptInstances.end()) {
        return;
    }

    ScriptInstance& instance = it->second;
    if (callOnDestroy && instance.valid && instance.environmentRef != kInvalidRef && m_Scene) {
        Entity entity(static_cast<entt::entity>(key.entityId), m_Scene);
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
    (void)key;
    (void)callOnDestroy;
#endif
}

void LuaScriptEngine::DestroyAllSlotsForEntity(uint32_t entityId, bool callOnDestroy) {
    DestroyInstance(MakeInstanceKey(entityId, LuaScriptSlot::Gameplay), callOnDestroy);
    DestroyInstance(MakeInstanceKey(entityId, LuaScriptSlot::CharacterController), callOnDestroy);
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
    unref(instance.onAnimationFinishedRef);
    unref(instance.onTriggerEnterRef);
    unref(instance.onTriggerStayRef);
    unref(instance.onTriggerExitRef);
    unref(instance.onCollisionEnterRef);
    unref(instance.onCollisionStayRef);
    unref(instance.onCollisionExitRef);
    unref(instance.environmentRef);

    instance.valid = false;
    instance.awakeCalled = false;
    instance.functionErrorLogged = false;
    instance.lastBindingError.clear();
#else
    (void)instance;
#endif
}

bool LuaScriptEngine::LoadScriptForInstance(ScriptInstance& instance, const LuaScriptComponent& component, Entity entity) {
#if LUA_AVAILABLE
    if (!m_State) {
        return false;
    }

    std::string code;
    std::string debugName;
    if (!component.scriptSource.empty()) {
        code = component.scriptSource;
        debugName = component.scriptPath.empty() ? std::string("<embedded>") : component.scriptPath;
    } else {
        std::ifstream file(instance.resolvedPath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[Lua] Failed to open script: " << instance.resolvedPath << std::endl;
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        code = buffer.str();
        debugName = instance.resolvedPath.string();
    }

    lua_newtable(m_State);
    int envIndex = lua_gettop(m_State);

    lua_newtable(m_State);
    lua_pushglobaltable(m_State);
    lua_setfield(m_State, -2, "__index");
    lua_setmetatable(m_State, envIndex);

    // Seed serialized/spawn variables before the chunk executes so top-level
    // declarations such as `local speed = speed or 3.5` see their overrides.
    for (const auto& [key, value] : component.variables) {
        PushPropertyValue(value);
        lua_setfield(m_State, envIndex, key.c_str());
    }

    // "@" makes Lua report errors as "path:line:" rather than [string "..."].
    const std::string chunkName = "@" + debugName;
    if (luaL_loadbuffer(m_State, code.c_str(), code.size(), chunkName.c_str()) != LUA_OK) {
        LogLuaError("loading " + debugName);
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
        LogLuaError("executing " + debugName);
        lua_pop(m_State, 1);
        return false;
    }

    instance.environmentRef = luaL_ref(m_State, LUA_REGISTRYINDEX);
    if (instance.environmentRef == LUA_REFNIL || instance.environmentRef == LUA_NOREF) {
        instance.environmentRef = kInvalidRef;
        return false;
    }

    ApplyVariablesToEnvironment(instance, component);

    const std::string awakeName = component.functionOnAwake.empty() ? "onAwake" : component.functionOnAwake;
    const std::string updateName = component.functionOnUpdate.empty() ? "onUpdate" : component.functionOnUpdate;
    const std::string destroyName = component.functionOnDestroy.empty() ? "onDestroy" : component.functionOnDestroy;

    instance.onAwakeRef = FetchFunctionRef(instance.environmentRef, awakeName);
    instance.onEnableRef = FetchFunctionRef(instance.environmentRef, "onEnable");
    instance.onUpdateRef = FetchFunctionRef(instance.environmentRef, updateName);
    instance.onLateUpdateRef = FetchFunctionRef(instance.environmentRef, "onLateUpdate");
    instance.onDisableRef = FetchFunctionRef(instance.environmentRef, "onDisable");
    instance.onDestroyRef = FetchFunctionRef(instance.environmentRef, destroyName);
    instance.onAnimationFinishedRef = FetchFunctionRef(instance.environmentRef, "onAnimationFinished");
    instance.onTriggerEnterRef = FetchFunctionRef(instance.environmentRef, "onTriggerEnter");
    instance.onTriggerStayRef = FetchFunctionRef(instance.environmentRef, "onTriggerStay");
    instance.onTriggerExitRef = FetchFunctionRef(instance.environmentRef, "onTriggerExit");
    instance.onCollisionEnterRef = FetchFunctionRef(instance.environmentRef, "onCollisionEnter");
    instance.onCollisionStayRef = FetchFunctionRef(instance.environmentRef, "onCollisionStay");
    instance.onCollisionExitRef = FetchFunctionRef(instance.environmentRef, "onCollisionExit");

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

void LuaScriptEngine::ApplyVariablesToEnvironment(ScriptInstance& instance, const LuaScriptComponent& component) {
#if LUA_AVAILABLE
    if (!m_State || instance.environmentRef == kInvalidRef) {
        return;
    }

    if (component.variables.empty()) {
        return;
    }

    lua_rawgeti(m_State, LUA_REGISTRYINDEX, instance.environmentRef);
    if (!lua_istable(m_State, -1)) {
        lua_pop(m_State, 1);
        return;
    }

    for (const auto& [key, value] : component.variables) {
        PushPropertyValue(value);
        lua_setfield(m_State, -2, key.c_str());
    }

    lua_pop(m_State, 1);
#else
    (void)instance;
    (void)component;
#endif
}

void LuaScriptEngine::PushPropertyValue(const PropertyValue& value) {
#if LUA_AVAILABLE
    if (value.IsNull()) {
        lua_pushnil(m_State);
        return;
    }
    if (value.IsBool()) {
        lua_pushboolean(m_State, *value.AsBool());
        return;
    }
    if (value.IsNumber()) {
        lua_pushnumber(m_State, *value.AsNumber());
        return;
    }
    if (value.IsString()) {
        lua_pushstring(m_State, value.AsString()->c_str());
        return;
    }
    if (value.IsVec2()) {
        const Vec2& v = *value.AsVec2();
        lua_newtable(m_State);
        lua_pushnumber(m_State, v.x);
        lua_setfield(m_State, -2, "x");
        lua_pushnumber(m_State, v.y);
        lua_setfield(m_State, -2, "y");
        return;
    }
    if (value.IsVec3()) {
        const Vec3& v = *value.AsVec3();
        lua_newtable(m_State);
        lua_pushnumber(m_State, v.x);
        lua_setfield(m_State, -2, "x");
        lua_pushnumber(m_State, v.y);
        lua_setfield(m_State, -2, "y");
        lua_pushnumber(m_State, v.z);
        lua_setfield(m_State, -2, "z");
        return;
    }
    if (value.IsVec4()) {
        const Vec4& v = *value.AsVec4();
        lua_newtable(m_State);
        lua_pushnumber(m_State, v.x);
        lua_setfield(m_State, -2, "x");
        lua_pushnumber(m_State, v.y);
        lua_setfield(m_State, -2, "y");
        lua_pushnumber(m_State, v.z);
        lua_setfield(m_State, -2, "z");
        lua_pushnumber(m_State, v.w);
        lua_setfield(m_State, -2, "w");
        return;
    }
    if (value.IsArray()) {
        lua_newtable(m_State);
        const auto& arr = *value.AsArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            PushPropertyValue(arr[i]);
            lua_seti(m_State, -2, static_cast<lua_Integer>(i + 1));
        }
        return;
    }
    if (value.IsObject()) {
        lua_newtable(m_State);
        const auto& obj = *value.AsObject();
        for (const auto& [key, entry] : obj) {
            PushPropertyValue(entry);
            lua_setfield(m_State, -2, key.c_str());
        }
        return;
    }
    lua_pushnil(m_State);
#else
    (void)value;
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

    if (Hephaestus::AssetPathPolicy::IsPluginContentKey(scriptPath)) {
        const std::filesystem::path mounted =
            Hephaestus::AssetPathPolicy::ResolvePortableKey(m_ProjectRoot, scriptPath);
        if (mounted.empty()) {
            Hephaestus::EngineLog::Write(
                Hephaestus::EngineLogLevel::Error,
                Hephaestus::AssetPathPolicy::UnresolvedPluginContentMessage(scriptPath) +
                    " (" + scriptPath + ")",
                "Script");
        }
        return mounted;
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
    Hephaestus::EngineLog::Write(Hephaestus::EngineLogLevel::Error,
                                 "Error while " + context + ": " + (message ? message : "<unknown>"), "Script");
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

int LuaScriptEngine::Lua_Mesh_setVisible(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_Scene) {
        lua_pushboolean(L, false);
        return 1;
    }
    const entt::entity handle = static_cast<entt::entity>(engine->m_CurrentEntityId);
    auto& registry = engine->m_Scene->GetRegistry();
    if (!registry.all_of<MeshRendererComponent>(handle)) {
        lua_pushboolean(L, false);
        return 1;
    }
    registry.get<MeshRendererComponent>(handle).visible = lua_toboolean(L, 1) != 0;
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Mesh_isVisible(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid() || !engine->m_Scene) {
        lua_pushboolean(L, false);
        return 1;
    }
    const entt::entity handle = static_cast<entt::entity>(engine->m_CurrentEntityId);
    auto& registry = engine->m_Scene->GetRegistry();
    if (!registry.all_of<MeshRendererComponent>(handle)) {
        lua_pushboolean(L, false);
        return 1;
    }
    lua_pushboolean(L, registry.get<MeshRendererComponent>(handle).visible);
    return 1;
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
    if (!engine || !engine->m_InputSystem) {
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

    // Compatibility API: despite its name, isKeyPressed retains held semantics.
    lua_pushboolean(L, engine->m_InputSystem->IsKeyDown(key));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_isMouseButtonPressed(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_InputSystem) {
        lua_pushboolean(L, false);
        return 1;
    }

    int button = TranslateMouseButton(L, 1);
    if (button < GLFW_MOUSE_BUTTON_1 || button > GLFW_MOUSE_BUTTON_LAST) {
        lua_pushboolean(L, false);
        return 1;
    }

    lua_pushboolean(L, engine->m_InputSystem->IsMouseButtonDown(button));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_getMousePosition(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_InputSystem) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushnumber(L, engine->m_InputSystem->GetMouseX());
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, engine->m_InputSystem->GetMouseY());
    lua_setfield(L, -2, "y");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_getMouseDelta(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    lua_newtable(L);
    lua_pushnumber(L, engine && engine->m_InputSystem
        ? engine->m_InputSystem->GetMouseDeltaX() : 0.0);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, engine && engine->m_InputSystem
        ? engine->m_InputSystem->GetMouseDeltaY() : 0.0);
    lua_setfield(L, -2, "y");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_getScrollDelta(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    lua_newtable(L);
    lua_pushnumber(L, engine && engine->m_InputSystem
        ? engine->m_InputSystem->GetScrollDeltaX() : 0.0);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, engine && engine->m_InputSystem
        ? engine->m_InputSystem->GetScrollDeltaY() : 0.0);
    lua_setfield(L, -2, "y");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_isActionDown(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const char* name = luaL_checkstring(L, 1);
    const bool known = engine && engine->m_InputSystem &&
        engine->m_InputSystem->HasAction(name ? name : "");
    if (engine && !known && engine->m_MissingInputNames.insert(name ? name : "").second) {
        std::cerr << "[Lua] Unknown input action: " << (name ? name : "") << '\n';
    }
    lua_pushboolean(L, known && engine->m_InputSystem->IsActionDown(name));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_isActionPressed(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const char* name = luaL_checkstring(L, 1);
    const bool known = engine && engine->m_InputSystem &&
        engine->m_InputSystem->HasAction(name ? name : "");
    if (engine && !known && engine->m_MissingInputNames.insert(name ? name : "").second) {
        std::cerr << "[Lua] Unknown input action: " << (name ? name : "") << '\n';
    }
    lua_pushboolean(L, known && engine->m_InputSystem->IsActionPressed(name));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_isActionReleased(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const char* name = luaL_checkstring(L, 1);
    const bool known = engine && engine->m_InputSystem &&
        engine->m_InputSystem->HasAction(name ? name : "");
    if (engine && !known && engine->m_MissingInputNames.insert(name ? name : "").second) {
        std::cerr << "[Lua] Unknown input action: " << (name ? name : "") << '\n';
    }
    lua_pushboolean(L, known && engine->m_InputSystem->IsActionReleased(name));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_getAxis(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const char* name = luaL_checkstring(L, 1);
    const bool known = engine && engine->m_InputSystem &&
        engine->m_InputSystem->HasAxis(name ? name : "");
    if (engine && !known && engine->m_MissingInputNames.insert(name ? name : "").second) {
        std::cerr << "[Lua] Unknown input axis: " << (name ? name : "") << '\n';
    }
    lua_pushnumber(L, known ? engine->m_InputSystem->GetAxis(name) : 0.0f);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_isGamepadConnected(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    lua_pushboolean(L, engine && engine->m_InputSystem &&
        engine->m_InputSystem->IsGamepadConnected());
    return 1;
#else
    (void)L;
    return 0;
#endif
}

#if LUA_AVAILABLE
namespace {

void PushInputBinding(lua_State* L, const Hephaestus::InputBinding& binding)
{
    lua_newtable(L);
    lua_pushstring(L, Hephaestus::ToString(binding.type));
    lua_setfield(L, -2, "type");
    lua_pushstring(L, binding.control.c_str());
    lua_setfield(L, -2, "control");
    lua_pushnumber(L, binding.scale);
    lua_setfield(L, -2, "scale");
    lua_pushnumber(L, binding.deadzone);
    lua_setfield(L, -2, "deadzone");
}

} // namespace
#endif

int LuaScriptEngine::Lua_Input_getBindings(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const char* name = luaL_checkstring(L, 1);
    const bool defaults = lua_toboolean(L, 2) != 0;
    const std::vector<Hephaestus::InputBinding>* bindings = nullptr;
    if (engine && engine->m_InputSystem) {
        bindings = defaults ? engine->m_InputSystem->GetDefaultBindings(name)
                            : engine->m_InputSystem->GetBindings(name);
    }
    if (!bindings) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, static_cast<int>(bindings->size()), 0);
    for (std::size_t i = 0; i < bindings->size(); ++i) {
        PushInputBinding(L, (*bindings)[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_setBindings(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    std::vector<Hephaestus::InputBinding> bindings;
    std::string error;
    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, 2));
    for (lua_Integer i = 1; i <= count && error.empty(); ++i) {
        lua_rawgeti(L, 2, i);
        if (!lua_istable(L, -1)) {
            error = "Each binding must be a table";
        } else {
            Hephaestus::InputBinding binding;
            lua_getfield(L, -1, "type");
            const char* type = lua_tostring(L, -1);
            if (!type || !Hephaestus::InputBindingTypeFromString(type, binding.type)) {
                error = std::string("Unknown input binding type: ") + (type ? type : "");
            }
            lua_pop(L, 1);
            lua_getfield(L, -1, "control");
            binding.control = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
            lua_pop(L, 1);
            lua_getfield(L, -1, "scale");
            if (lua_isnumber(L, -1)) binding.scale = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
            lua_getfield(L, -1, "deadzone");
            if (lua_isnumber(L, -1)) binding.deadzone = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
            bindings.push_back(std::move(binding));
        }
        lua_pop(L, 1);
    }
    if (error.empty() && !(engine && engine->m_InputSystem)) {
        error = "Input is not available";
    }
    if (error.empty() && engine->m_InputSystem->SetBindings(name, std::move(bindings), error)) {
        lua_pushboolean(L, true);
        return 1;
    }
    lua_pushboolean(L, false);
    lua_pushstring(L, error.c_str());
    return 2;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_getPressedControl(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    Hephaestus::InputBindingType type = Hephaestus::InputBindingType::Keyboard;
    int code = -1;
    const std::string control = engine && engine->m_InputSystem &&
                                        engine->m_InputSystem->GetPressedControl(type, code)
        ? Hephaestus::InputControlName(type, code) : std::string();
    if (control.empty()) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushstring(L, Hephaestus::ToString(type));
    lua_setfield(L, -2, "type");
    lua_pushstring(L, control.c_str());
    lua_setfield(L, -2, "control");
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_setCursorMode(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) return 0;

    const char* modeStr = luaL_checkstring(L, 1);
    std::string mode = modeStr ? modeStr : "";

    // Normalize to lowercase
    for (auto& c : mode) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    int newMode = 0;
    if (mode == "disabled" || mode == "captured" || mode == "locked") {
        newMode = 1;
    } else if (mode == "normal" || mode == "default") {
        newMode = 0;
    } else {
        luaL_error(L, "Input.setCursorMode: unknown mode '%s' (use 'disabled' or 'normal')", modeStr);
        return 0;
    }

    // Always store the request so the host picks it up on the next tick
    // (covers calls made during onAwake before the first host input tick).
    if (engine->m_InputSystem) {
        engine->m_InputSystem->SetCursorModeRequest(newMode);
    }

    // GLFW runtime: apply immediately since there is no host tick to consume the request
    if (engine->m_Window) {
        int glfwMode = (newMode == 1) ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
        glfwSetInputMode(engine->m_Window, GLFW_CURSOR, glfwMode);

        if (newMode == 1 && glfwRawMouseMotionSupported()) {
            glfwSetInputMode(engine->m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        } else if (newMode == 0) {
            glfwSetInputMode(engine->m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }

    }

    engine->m_CurrentCursorMode = newMode;
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Time_unscaledDeltaTime(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    lua_pushnumber(L, engine ? static_cast<double>(engine->m_UnscaledDeltaTime) : 0.0);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Time_setScale(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const double scale = luaL_checknumber(L, 1);
    if (engine && std::isfinite(scale)) {
        engine->m_TimeScale = static_cast<float>(std::clamp(scale, 0.0, 10.0));
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Time_getScale(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    lua_pushnumber(L, engine ? static_cast<double>(engine->m_TimeScale) : 1.0);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

#if LUA_AVAILABLE
namespace {

// Storage names become file names, so only plain names are accepted.
bool IsValidStorageName(const std::string& name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

bool LuaToJson(lua_State* L, int index, int depth, nlohmann::json& out, std::string& outError) {
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
    case LUA_TBOOLEAN:
        out = lua_toboolean(L, index) != 0;
        return true;
    case LUA_TNUMBER:
        if (lua_isinteger(L, index)) {
            out = static_cast<std::int64_t>(lua_tointeger(L, index));
        } else {
            const double value = lua_tonumber(L, index);
            if (!std::isfinite(value)) {
                outError = "numbers must be finite";
                return false;
            }
            out = value;
        }
        return true;
    case LUA_TSTRING: {
        std::size_t length = 0;
        const char* text = lua_tolstring(L, index, &length);
        out = std::string(text, length);
        return true;
    }
    case LUA_TTABLE: {
        if (depth > 32) {
            outError = "tables are nested too deeply";
            return false;
        }
        const lua_Integer length = static_cast<lua_Integer>(lua_rawlen(L, index));
        bool isArray = length > 0;
        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            const bool arrayKey = lua_isinteger(L, -2) && lua_tointeger(L, -2) >= 1 && lua_tointeger(L, -2) <= length;
            if (!arrayKey) {
                isArray = false;
            }
            lua_pop(L, 1);
        }
        if (isArray) {
            out = nlohmann::json::array();
            for (lua_Integer i = 1; i <= length; ++i) {
                lua_rawgeti(L, index, i);
                nlohmann::json element;
                const bool ok = LuaToJson(L, -1, depth + 1, element, outError);
                lua_pop(L, 1);
                if (!ok) {
                    return false;
                }
                out.push_back(std::move(element));
            }
            return true;
        }
        out = nlohmann::json::object();
        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            if (lua_type(L, -2) != LUA_TSTRING) {
                lua_pop(L, 2);
                outError = "table keys must be strings, or 1..n for a list";
                return false;
            }
            const std::string key = lua_tostring(L, -2);
            nlohmann::json element;
            if (!LuaToJson(L, -1, depth + 1, element, outError)) {
                lua_pop(L, 2);
                return false;
            }
            out[key] = std::move(element);
            lua_pop(L, 1);
        }
        return true;
    }
    default:
        outError = std::string("cannot save a ") + luaL_typename(L, index);
        return false;
    }
}

void PushJson(lua_State* L, const nlohmann::json& value) {
    if (value.is_boolean()) {
        lua_pushboolean(L, value.get<bool>() ? 1 : 0);
    } else if (value.is_number_integer()) {
        lua_pushinteger(L, static_cast<lua_Integer>(value.get<std::int64_t>()));
    } else if (value.is_number()) {
        lua_pushnumber(L, value.get<double>());
    } else if (value.is_string()) {
        const std::string& text = value.get_ref<const std::string&>();
        lua_pushlstring(L, text.data(), text.size());
    } else if (value.is_array()) {
        lua_createtable(L, static_cast<int>(value.size()), 0);
        lua_Integer i = 1;
        for (const auto& element : value) {
            PushJson(L, element);
            lua_rawseti(L, -2, i++);
        }
    } else if (value.is_object()) {
        lua_createtable(L, 0, static_cast<int>(value.size()));
        for (const auto& [key, element] : value.items()) {
            PushJson(L, element);
            lua_setfield(L, -2, key.c_str());
        }
    } else {
        lua_pushnil(L);
    }
}

} // namespace
#endif

int LuaScriptEngine::Lua_Storage_save(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const std::string name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    std::string error;
    nlohmann::json data;
    if (!engine || engine->m_StorageRoot.empty()) {
        error = "storage is not available here";
    } else if (!IsValidStorageName(name)) {
        error = "names may only use letters, digits, '_' and '-'";
    } else if (LuaToJson(L, 2, 0, data, error)) {
        std::error_code ec;
        std::filesystem::create_directories(engine->m_StorageRoot, ec);
        const auto target = engine->m_StorageRoot / (name + ".json");
        const auto temp = engine->m_StorageRoot / (name + ".json.tmp");
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << data.dump(2);
            if (!out.good()) {
                error = "could not write " + temp.string();
            }
        }
        if (error.empty()) {
            std::filesystem::rename(temp, target, ec);
            if (ec) {
                error = "could not replace " + target.string() + ": " + ec.message();
            }
        }
    }
    if (!error.empty()) {
        engine->LogBindingError(nullptr, "Storage.save('" + name + "') failed: " + error);
        lua_pushboolean(L, 0);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

#if LUA_AVAILABLE
namespace {

std::filesystem::path ConfigFile(const std::filesystem::path& root, const std::string& prefix, const std::string& name)
{
    return root / (prefix + name + ".ini");
}

std::string LuaValueText(lua_State* L, int index)
{
    if (lua_type(L, index) == LUA_TBOOLEAN) {
        return lua_toboolean(L, index) ? "true" : "false";
    }
    if (lua_type(L, index) == LUA_TNUMBER) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.6g", lua_tonumber(L, index));
        return buffer;
    }
    const char* text = lua_tostring(L, index);
    return text ? text : "";
}

void LogConfigProblem(const std::string& message)
{
    Hephaestus::EngineLog::RecordOnly(Hephaestus::EngineLogLevel::Warning, message, "Lua");
    std::cerr << "[Lua] " << message << std::endl;
}

std::string LuaFieldText(lua_State* L, int table, const char* field)
{
    lua_getfield(L, table, field);
    std::string text = lua_isnil(L, -1) ? std::string() : LuaValueText(L, -1);
    lua_pop(L, 1);
    return text;
}

} // namespace
#endif

int LuaScriptEngine::Lua_Config_path(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const std::string name = luaL_checkstring(L, 1);
    if (!engine || engine->m_ConfigRoot.empty() || !IsValidStorageName(name)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, ConfigFile(engine->m_ConfigRoot, engine->m_ConfigPrefix, name).string().c_str());
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Config_load(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const std::string name = luaL_checkstring(L, 1);
    if (!engine || engine->m_ConfigRoot.empty() || !IsValidStorageName(name)) {
        lua_pushnil(L);
        return 1;
    }
    const auto path = ConfigFile(engine->m_ConfigRoot, engine->m_ConfigPrefix, name);
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        lua_pushnil(L);
        return 1;
    }
    std::ostringstream text;
    text << in.rdbuf();
    std::vector<std::string> warnings;
    const Hephaestus::IniDocument document = Hephaestus::ParseIni(text.str(), warnings);
    for (const auto& warning : warnings) {
        LogConfigProblem(path.filename().string() + " " + warning + " (ignored)");
    }
    lua_newtable(L);
    for (const auto& section : document.sections) {
        lua_newtable(L);
        for (const auto& entry : section.entries) {
            lua_pushstring(L, entry.value.c_str());
            lua_setfield(L, -2, entry.key.c_str());
        }
        lua_setfield(L, -2, section.name.c_str());
    }
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Config_save(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const std::string name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    const char* header = luaL_optstring(L, 3, "");
    std::string error;
    if (!engine || engine->m_ConfigRoot.empty()) {
        error = "config files are not available here";
    } else if (!IsValidStorageName(name)) {
        error = "names may only use letters, digits, '_' and '-'";
    }
    Hephaestus::IniDocument document;
    document.header = header ? header : "";
    const lua_Integer sectionCount = static_cast<lua_Integer>(lua_rawlen(L, 2));
    for (lua_Integer s = 1; s <= sectionCount && error.empty(); ++s) {
        lua_rawgeti(L, 2, s);
        const int section = lua_gettop(L);
        if (!lua_istable(L, section)) {
            error = "each section must be a table";
        } else {
            Hephaestus::IniSection out{LuaFieldText(L, section, "name"), LuaFieldText(L, section, "comment"), {}};
            lua_getfield(L, section, "keys");
            const int keys = lua_gettop(L);
            const lua_Integer keyCount = lua_istable(L, keys) ? static_cast<lua_Integer>(lua_rawlen(L, keys)) : 0;
            for (lua_Integer k = 1; k <= keyCount; ++k) {
                lua_rawgeti(L, keys, k);
                if (lua_istable(L, -1)) {
                    const int entry = lua_gettop(L);
                    out.entries.push_back({LuaFieldText(L, entry, "key"), LuaFieldText(L, entry, "value"),
                                           LuaFieldText(L, entry, "comment")});
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            document.sections.push_back(std::move(out));
        }
        lua_pop(L, 1);
    }
    if (error.empty()) {
        const auto target = ConfigFile(engine->m_ConfigRoot, engine->m_ConfigPrefix, name);
        const auto temp = target.string() + ".tmp";
        std::error_code ec;
        std::filesystem::create_directories(engine->m_ConfigRoot, ec);
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << Hephaestus::WriteIni(document);
            if (!out.good()) {
                error = "could not write " + temp;
            }
        }
        if (error.empty()) {
            std::filesystem::rename(temp, target, ec);
            if (ec) {
                error = "could not replace " + target.string() + ": " + ec.message();
            }
        }
    }
    if (!error.empty()) {
        LogConfigProblem("Config.save('" + name + "') failed: " + error);
        lua_pushboolean(L, 0);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Storage_load(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const std::string name = luaL_checkstring(L, 1);
    if (!engine || engine->m_StorageRoot.empty() || !IsValidStorageName(name)) {
        lua_pushnil(L);
        return 1;
    }
    std::ifstream in(engine->m_StorageRoot / (name + ".json"), std::ios::binary);
    if (!in.is_open()) {
        lua_pushnil(L);
        return 1;
    }
    const nlohmann::json data = nlohmann::json::parse(in, nullptr, false);
    if (data.is_discarded()) {
        engine->LogBindingError(nullptr, "Storage.load('" + name + "'): the saved file is not valid JSON");
        lua_pushnil(L);
        return 1;
    }
    PushJson(L, data);
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

int LuaScriptEngine::Lua_Camera_worldToScreen(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    Vec3 point;
    if (lua_istable(L, 1)) {
        float* parts[3] = {&point.x, &point.y, &point.z};
        const char* names[3] = {"x", "y", "z"};
        for (int i = 0; i < 3; ++i) {
            lua_getfield(L, 1, names[i]);
            *parts[i] = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
    } else {
        point = Vec3(static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)),
                     static_cast<float>(luaL_checknumber(L, 3)));
    }
    const auto view = engine && engine->m_Scene ? Hephaestus::TryReadActiveCameraView(*engine->m_Scene)
                                                : std::optional<Hephaestus::ActiveCameraView>{};
    const Hephaestus::HephaestusUI::UIVec2 layout =
        engine && engine->m_UIContext ? engine->m_UIContext->GetLayoutSize() : Hephaestus::HephaestusUI::UIVec2{};
    if (!view || layout.x <= 0.0f || layout.y <= 0.0f) {
        lua_pushnil(L);
        return 1;
    }
    const Mat4 frame = Hephaestus::MakeCameraViewFrame(view->position, view->yawRadians, view->pitchRadians,
                                                       view->rollRadians);
    const Vec3 forward(frame.m[0], frame.m[1], frame.m[2]);
    const Vec3 up(frame.m[4], frame.m[5], frame.m[6]);
    const Vec3 right(frame.m[8], frame.m[9], frame.m[10]);
    const Vec3 offset(point.x - view->position.x, point.y - view->position.y, point.z - view->position.z);
    const auto dot = [](const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    const float depth = dot(offset, forward);
    const float tanHalf = std::tan(std::max(view->fovDegrees, 1.0f) * 0.5f * 3.14159265f / 180.0f);
    const float aspect = layout.x / layout.y;
    const float safeDepth = std::max(std::abs(depth), 1e-4f);
    const float ndcX = dot(offset, right) / (safeDepth * tanHalf * aspect);
    const float ndcY = dot(offset, up) / (safeDepth * tanHalf);
    const bool onScreen = depth > view->nearPlane && std::abs(ndcX) <= 1.0f && std::abs(ndcY) <= 1.0f;
    lua_pushnumber(L, (ndcX + 1.0f) * 0.5f * layout.x);
    lua_pushnumber(L, (1.0f - ndcY) * 0.5f * layout.y);
    lua_pushboolean(L, onScreen);
    lua_pushnumber(L, depth);
    return 4;
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

    lua_pushcfunction(m_State, Lua_This_GetParent);
    lua_setfield(m_State, -2, "GetParent");

    lua_pushcfunction(m_State, Lua_This_GetId);
    lua_setfield(m_State, -2, "GetId");

    lua_pushcfunction(m_State, Lua_This_GetCamera);
    lua_setfield(m_State, -2, "GetCamera");

    lua_pushcfunction(m_State, Lua_This_GetAnimator);
    lua_setfield(m_State, -2, "GetAnimator");

    lua_pushcfunction(m_State, Lua_This_GetCharacter);
    lua_setfield(m_State, -2, "GetCharacter");

    lua_pushcfunction(m_State, Lua_This_Call);
    lua_setfield(m_State, -2, "Call");

    lua_pushcfunction(m_State, Lua_This_CallController);
    lua_setfield(m_State, -2, "CallController");
    
    // Set __index to self for method lookup
    lua_pushvalue(m_State, -1);
    lua_setfield(m_State, -2, "__index");
    
    lua_pop(m_State, 1);
    
    // Create the "Transform" metatable for transform objects
    luaL_newmetatable(m_State, "HephaestusTransform");
    
    lua_pushcfunction(m_State, Lua_TransformObj_GetPosition);
    lua_setfield(m_State, -2, "GetPosition");

    lua_pushcfunction(m_State, Lua_TransformObj_GetWorldPosition);
    lua_setfield(m_State, -2, "GetWorldPosition");
    
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

    luaL_newmetatable(m_State, "HephaestusAnimator");
    lua_pushcfunction(m_State, Lua_Animator_Play);
    lua_setfield(m_State, -2, "Play");
    lua_pushcfunction(m_State, Lua_Animator_CrossFade);
    lua_setfield(m_State, -2, "CrossFade");
    lua_pushcfunction(m_State, Lua_Animator_Pause);
    lua_setfield(m_State, -2, "Pause");
    lua_pushcfunction(m_State, Lua_Animator_Resume);
    lua_setfield(m_State, -2, "Resume");
    lua_pushcfunction(m_State, Lua_Animator_Stop);
    lua_setfield(m_State, -2, "Stop");
    lua_pushcfunction(m_State, Lua_Animator_SetSpeed);
    lua_setfield(m_State, -2, "SetSpeed");
    lua_pushcfunction(m_State, Lua_Animator_IsPlaying);
    lua_setfield(m_State, -2, "IsPlaying");
    lua_pushcfunction(m_State, Lua_Animator_GetCurrentAnimation);
    lua_setfield(m_State, -2, "GetCurrentAnimation");
    lua_pushcfunction(m_State, Lua_Animator_GetTime);
    lua_setfield(m_State, -2, "GetTime");
    lua_pushcfunction(m_State, Lua_Animator_SetTime);
    lua_setfield(m_State, -2, "SetTime");
    lua_pushcfunction(m_State, Lua_Animator_SetLayer);
    lua_setfield(m_State, -2, "SetLayer");
    lua_pushcfunction(m_State, Lua_Animator_ClearLayer);
    lua_setfield(m_State, -2, "ClearLayer");
    lua_pushcfunction(m_State, Lua_Animator_GetDuration);
    lua_setfield(m_State, -2, "GetDuration");
    lua_pushvalue(m_State, -1);
    lua_setfield(m_State, -2, "__index");
    lua_pop(m_State, 1);

    luaL_newmetatable(m_State, "HephaestusCharacter");
    lua_pushcfunction(m_State, Lua_Character_GetSocket);
    lua_setfield(m_State, -2, "GetSocket");
    lua_pushcfunction(m_State, Lua_Character_GetSocketLocal);
    lua_setfield(m_State, -2, "GetSocketLocal");
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
    LuaEntityReference::Create(m_State, this, entityId);
    
    // Set its metatable to "HephaestusEntity"
    luaL_getmetatable(m_State, "HephaestusEntity");
    lua_setmetatable(m_State, -2);
    
    // Set as global "this"
    lua_setglobal(m_State, "this");
#else
    (void)entityId;
#endif
}

void LuaScriptEngine::PushEntityUserdata(lua_State* L, uint32_t entityId) {
#if LUA_AVAILABLE
    if (!L) {
        return;
    }
    LuaEntityReference::Create(L, this, entityId);
    luaL_getmetatable(L, "HephaestusEntity");
    lua_setmetatable(L, -2);
#else
    (void)L;
    (void)entityId;
#endif
}

std::filesystem::path LuaScriptEngine::ResolvePrefabPath(const std::string& prefabPath) const {
    namespace fs = std::filesystem;
    if (prefabPath.empty()) {
        return {};
    }

    if (Hephaestus::AssetPathPolicy::IsPluginContentKey(prefabPath)) {
        const fs::path mounted =
            Hephaestus::AssetPathPolicy::ResolvePortableKey(m_ProjectRoot, prefabPath);
        if (mounted.empty()) {
            Hephaestus::EngineLog::Write(
                Hephaestus::EngineLogLevel::Error,
                Hephaestus::AssetPathPolicy::UnresolvedPluginContentMessage(prefabPath) +
                    " (" + prefabPath + ")",
                "Prefab");
        }
        return mounted;
    }

    fs::path path(prefabPath);
    std::error_code ec;
    if (path.is_absolute() && fs::exists(path, ec)) {
        return path.lexically_normal();
    }

    auto tryPath = [&](const fs::path& candidate) -> fs::path {
        if (!candidate.empty() && fs::exists(candidate, ec)) {
            return candidate.lexically_normal();
        }
        return {};
    };

    if (!m_ProjectRoot.empty()) {
        fs::path resolved = tryPath(m_ProjectRoot / path);
        if (!resolved.empty()) {
            return resolved;
        }
        resolved = tryPath(m_ProjectRoot / "assets" / path);
        if (!resolved.empty()) {
            return resolved;
        }
        // If path already starts with assets/, projectRoot / path is enough
    }

    for (const auto& search : m_SearchPaths) {
        fs::path resolved = tryPath(search / path);
        if (!resolved.empty()) {
            return resolved;
        }
    }

    fs::path cwd = tryPath(fs::current_path() / path);
    if (!cwd.empty()) {
        return cwd;
    }
    return {};
}

PropertyValue LuaScriptEngine::ParsePropertyValueFromLua(lua_State* L, int index) {
#if LUA_AVAILABLE
    if (!L) {
        return PropertyValue{};
    }
    index = lua_absindex(L, index);
    const int type = lua_type(L, index);
    if (type == LUA_TNIL) {
        return PropertyValue{};
    }
    if (type == LUA_TBOOLEAN) {
        return PropertyValue(lua_toboolean(L, index) != 0);
    }
    if (type == LUA_TNUMBER) {
        return PropertyValue(lua_tonumber(L, index));
    }
    if (type == LUA_TSTRING) {
        return PropertyValue(std::string(lua_tostring(L, index)));
    }
    if (type == LUA_TTABLE) {
        // Prefer vec3 if x/y/z present
        lua_getfield(L, index, "x");
        const bool hasX = lua_isnumber(L, -1) != 0;
        double x = hasX ? lua_tonumber(L, -1) : 0.0;
        lua_pop(L, 1);
        lua_getfield(L, index, "y");
        const bool hasY = lua_isnumber(L, -1) != 0;
        double y = hasY ? lua_tonumber(L, -1) : 0.0;
        lua_pop(L, 1);
        lua_getfield(L, index, "z");
        const bool hasZ = lua_isnumber(L, -1) != 0;
        double z = hasZ ? lua_tonumber(L, -1) : 0.0;
        lua_pop(L, 1);
        lua_getfield(L, index, "w");
        const bool hasW = lua_isnumber(L, -1) != 0;
        double w = hasW ? lua_tonumber(L, -1) : 0.0;
        lua_pop(L, 1);

        if (hasX && hasY && hasZ && hasW) {
            return PropertyValue(Vec4(static_cast<float>(x), static_cast<float>(y),
                                      static_cast<float>(z), static_cast<float>(w)));
        }
        if (hasX && hasY && hasZ) {
            return PropertyValue(Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
        }
        if (hasX && hasY) {
            return PropertyValue(Vec2(static_cast<float>(x), static_cast<float>(y)));
        }

        // Generic object map (string keys)
        PropertyValue::Object obj;
        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                const char* key = lua_tostring(L, -2);
                if (key) {
                    obj.emplace(key, ParsePropertyValueFromLua(L, -1));
                }
            }
            lua_pop(L, 1);
        }
        if (!obj.empty()) {
            return PropertyValue(std::move(obj));
        }
    }
    return PropertyValue{};
#else
    (void)L;
    (void)index;
    return PropertyValue{};
#endif
}

Entity LuaScriptEngine::ExecuteSpawn(const PendingSpawn& spawn) {
    if (!m_Scene || spawn.prefabPath.empty()) {
        return {};
    }

    const std::filesystem::path resolved = ResolvePrefabPath(spawn.prefabPath);
    if (resolved.empty()) {
        std::cerr << "[Lua] Scene.instantiate: prefab not found: " << spawn.prefabPath << "\n";
        return {};
    }
    Entity parent = spawn.hasParent ? MakeEntity(spawn.parentId) : Entity{};
    if (spawn.hasParent && (!parent || !parent.HasComponent<TransformComponent>())) {
        std::cerr << "[Lua] Scene.instantiate: parent entity is no longer valid for " << spawn.prefabPath << "\n";
        return {};
    }

    PrefabAsset asset;
    std::string error;
    const std::string pathKey = resolved.generic_string();
    if (!PrefabOverrideSystem::LoadAsset(pathKey, asset, error)) {
        // Also try the original relative path key for cache/deserialize
        if (!PrefabOverrideSystem::LoadAsset(spawn.prefabPath, asset, error)) {
            std::cerr << "[Lua] Scene.instantiate: failed to load prefab '" << spawn.prefabPath
                      << "': " << error << "\n";
            return {};
        }
    }

    PrefabInstantiateOptions options;
    options.prefabPath = spawn.prefabPath;
    options.attachInstanceComponent = true;
    Entity root = PrefabInstantiator::Instantiate(*m_Scene, asset, options);
    if (!root) {
        std::cerr << "[Lua] Scene.instantiate: PrefabInstantiator returned null\n";
        return {};
    }

    if (parent && root.HasComponent<TransformComponent>()) {
        m_Scene->SetParent(root, parent);
    }
    if (root.HasComponent<TransformComponent>()) {
        auto& transform = root.GetComponent<TransformComponent>();
        if (spawn.hasPosition) {
            transform.position = spawn.position;
        }
        if (spawn.hasRotation) {
            transform.SetRotationDegrees(spawn.rotationDegrees);
        }
        if (root.HasComponent<PrefabInstanceComponent>()) {
            auto& inst = root.GetComponent<PrefabInstanceComponent>();
            PrefabComponentIO::CopyLocalTransform(inst.rootTransform, transform);
            inst.hasRootTransform = true;
        }
    }

    if (!spawn.vars.empty()) {
        // Merge vars onto every LuaScriptComponent in the prefab instance hierarchy.
        std::vector<entt::entity> instanceEntities;
        instanceEntities.push_back(static_cast<entt::entity>(root));
        if (root.HasComponent<TransformComponent>()) {
            std::function<void(Entity)> collect = [&](Entity entity) {
                if (!entity || !entity.HasComponent<TransformComponent>()) {
                    return;
                }
                for (entt::entity childHandle : entity.GetComponent<TransformComponent>().children) {
                    Entity child(childHandle, m_Scene);
                    if (!child) {
                        continue;
                    }
                    instanceEntities.push_back(childHandle);
                    collect(child);
                }
            };
            collect(root);
        }

        for (entt::entity handle : instanceEntities) {
            Entity entity(handle, m_Scene);
            if (!entity || !entity.HasComponent<LuaScriptComponent>()) {
                continue;
            }
            auto& lua = entity.GetComponent<LuaScriptComponent>();
            for (const auto& [key, value] : spawn.vars) {
                lua.variables[key] = value;
            }
        }
    }

    // Prefab instantiation creates CPU-side MeshRenderer components after the
    // host's scene mesh upload pass. Reuse an already-uploaded handle for the
    // same asset, or have the host load a mesh no entity has used yet.
    auto spawnedRenderers = m_Scene->GetEntitiesWith<MeshRendererComponent>();
    for (entt::entity spawnedHandle : spawnedRenderers) {
        Entity spawnedEntity(spawnedHandle, m_Scene);
        if (!spawnedEntity || !spawnedEntity.HasComponent<PrefabNodeComponent>()) {
            continue;
        }
        Entity instanceRoot = PrefabOverrideSystem::FindInstanceRoot(*m_Scene, spawnedEntity);
        if (!instanceRoot || static_cast<entt::entity>(instanceRoot) != static_cast<entt::entity>(root)) {
            continue;
        }

        auto& spawnedRenderer = spawnedEntity.GetComponent<MeshRendererComponent>();
        if (spawnedRenderer.HasValidMesh() || spawnedRenderer.meshAssetPath.empty()) {
            continue;
        }
        for (entt::entity candidateHandle : spawnedRenderers) {
            if (candidateHandle == spawnedHandle) {
                continue;
            }
            auto& candidate = m_Scene->GetRegistry().get<MeshRendererComponent>(candidateHandle);
            if (candidate.HasValidMesh() &&
                candidate.meshAssetPath == spawnedRenderer.meshAssetPath) {
                spawnedRenderer.meshHandle = candidate.meshHandle;
                spawnedRenderer.submeshCount = candidate.submeshCount;
                spawnedRenderer.materialIndices = candidate.materialIndices;
                break;
            }
        }
        if (!spawnedRenderer.HasValidMesh() && m_SpawnedMeshLoader) {
            m_SpawnedMeshLoader(spawnedRenderer);
        }
    }

    return root;
}

void LuaScriptEngine::ExecuteDestroy(uint32_t entityId) {
    if (!m_Scene) {
        return;
    }
    Entity entity = MakeEntity(entityId);
    if (!entity) {
        return;
    }

    // If this is a prefab instance child, destroy from the instance root.
    Entity destroyTarget = entity;
    if (entity.HasComponent<PrefabNodeComponent>() && !entity.HasComponent<PrefabInstanceComponent>()) {
        Entity root = PrefabOverrideSystem::FindInstanceRoot(*m_Scene, entity);
        if (root) {
            destroyTarget = root;
        }
    }

    if (destroyTarget.HasComponent<PrefabInstanceComponent>()) {
        PrefabInstantiator::DestroyInstance(*m_Scene, destroyTarget);
    } else {
        m_Scene->DestroyEntity(destroyTarget);
    }
}

void LuaScriptEngine::FlushDeferredSceneOps() {
    // Destrovers first, then spawns (plan order).
    std::vector<PendingDestroy> destroys = std::move(m_PendingDestroys);
    m_PendingDestroys.clear();
    for (const PendingDestroy& pending : destroys) {
        if (m_SceneReloadRequested) {
            ClearDeferredSceneOps();
            return;
        }
        ExecuteDestroy(pending.entityId);
        if (m_SceneReloadRequested) {
            ClearDeferredSceneOps();
            return;
        }
    }

    std::vector<PendingSpawn> spawns = std::move(m_PendingSpawns);
    m_PendingSpawns.clear();
    for (const PendingSpawn& pending : spawns) {
        if (m_SceneReloadRequested) {
            ClearDeferredSceneOps();
            return;
        }
        ExecuteSpawn(pending);
    }
}

void LuaScriptEngine::ClearDeferredSceneOps() {
    m_PendingSpawns.clear();
    m_PendingDestroys.clear();
}

int LuaScriptEngine::Lua_This_GetTransform(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }

    LuaEntityReference* selfId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusEntity"));
    if (!selfId) {
        lua_pushnil(L);
        return 1;
    }
    Entity entity = engine->MakeEntity(*selfId);
    if (!entity || !entity.HasComponent<TransformComponent>()) {
        lua_pushnil(L);
        return 1;
    }

    LuaEntityReference::Create(L, engine, static_cast<std::uint32_t>(*selfId));
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
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }
    LuaEntityReference* selfId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusEntity"));
    if (!selfId) {
        lua_pushnil(L);
        return 1;
    }
    TagComponent* tag = engine->GetTag(*selfId);
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
    if (!engine) {
        return 0;
    }
    LuaEntityReference* selfId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusEntity"));
    if (!selfId) {
        return 0;
    }
    const char* name = luaL_checkstring(L, 2);
    if (!name) {
        return 0;
    }
    Entity entity = engine->MakeEntity(*selfId);
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
    LuaEntityReference* selfId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusEntity"));
    bool valid = engine && selfId && static_cast<bool>(engine->MakeEntity(*selfId));
    lua_pushboolean(L, valid);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_This_GetParent(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    LuaEntityReference* selfId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusEntity"));
    Entity self = engine && selfId ? engine->MakeEntity(*selfId) : Entity{};
    const TransformComponent* transform = self ? engine->GetTransform(*selfId) : nullptr;
    if (!transform || transform->parent == entt::null || !engine->MakeEntity(static_cast<uint32_t>(transform->parent))) {
        lua_pushnil(L);
        return 1;
    }
    engine->PushEntityUserdata(L, static_cast<uint32_t>(transform->parent));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_This_GetId(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    LuaEntityReference* selfId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusEntity"));
    if (!engine || !selfId || !engine->MakeEntity(*selfId)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<std::uint32_t>(*selfId)));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

#if LUA_AVAILABLE
namespace {

// Converts a Lua call argument for a script in another language. Tables with
// x/y[/z[/w]] become vectors; anything else unsupported becomes nil.
PropertyValue LuaArgToProperty(lua_State* L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TBOOLEAN: return PropertyValue(lua_toboolean(L, index) != 0);
        case LUA_TNUMBER: return PropertyValue(static_cast<double>(lua_tonumber(L, index)));
        case LUA_TSTRING: return PropertyValue(std::string(lua_tostring(L, index)));
        case LUA_TTABLE: {
            float values[4] = {};
            int count = 0;
            for (const char* key : {"x", "y", "z", "w"}) {
                lua_getfield(L, index, key);
                const bool present = lua_isnumber(L, -1) != 0;
                if (present) {
                    values[count] = static_cast<float>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                if (!present) {
                    break;
                }
                ++count;
            }
            if (count == 2) return PropertyValue(Vec2(values[0], values[1]));
            if (count == 3) return PropertyValue(Vec3(values[0], values[1], values[2]));
            if (count == 4) return PropertyValue(Vec4(values[0], values[1], values[2], values[3]));
            return {};
        }
        default: return {};
    }
}

} // namespace
#endif

int LuaScriptEngine::CallNamedOnSlot(lua_State* L, LuaScriptSlot slot) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_State) {
        lua_pushboolean(L, false);
        return 1;
    }

    LuaEntityReference* selfId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusEntity"));
    if (!selfId) {
        lua_pushboolean(L, false);
        return 1;
    }

    const char* functionName = luaL_checkstring(L, 2);
    if (!functionName || functionName[0] == 0) {
        lua_pushboolean(L, false);
        return 1;
    }

    const int argCount = lua_gettop(L) - 2;

    ScriptInstance* instance = engine->FindInstance(*selfId, slot);
    if (!instance || !instance->valid || instance->environmentRef == kInvalidRef) {
        // The target's script may be written in a plugin language.
        bool handled = false;
        if (slot == LuaScriptSlot::Gameplay && engine->m_PluginScripts) {
            std::vector<PropertyValue> args;
            args.reserve(static_cast<std::size_t>(argCount));
            for (int i = 0; i < argCount; ++i) {
                args.push_back(LuaArgToProperty(L, 3 + i));
            }
            handled = engine->m_PluginScripts->CallEntityFunction(
                static_cast<std::uint32_t>(*selfId), functionName, args);
        }
        lua_pushboolean(L, handled);
        return 1;
    }

    Entity entity = engine->MakeEntity(*selfId);
    if (!entity) {
        lua_pushboolean(L, false);
        return 1;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, instance->environmentRef);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, false);
        return 1;
    }

    lua_getfield(L, -1, functionName);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        lua_pushboolean(L, false);
        return 1;
    }
    lua_remove(L, -2);

    lua_insert(L, 3);
    lua_remove(L, 1);
    lua_remove(L, 1);

    uint32_t previousEntity = engine->m_CurrentEntityId;
    float previousDelta = engine->m_CurrentDeltaTime;
    engine->m_CurrentEntityId = instance->entityId;
    engine->SetupThisForEntity(instance->entityId);

    if (lua_pcall(L, argCount, 0, 0) != LUA_OK) {
        engine->LogLuaError(functionName);
        engine->m_CurrentEntityId = previousEntity;
        engine->m_CurrentDeltaTime = previousDelta;
        if (previousEntity != kInvalidEntityId) {
            engine->SetupThisForEntity(previousEntity);
        }
        lua_pushboolean(L, false);
        return 1;
    }

    engine->m_CurrentEntityId = previousEntity;
    engine->m_CurrentDeltaTime = previousDelta;
    if (previousEntity != kInvalidEntityId) {
        engine->SetupThisForEntity(previousEntity);
    }

    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    (void)slot;
    return 0;
#endif
}

int LuaScriptEngine::Lua_This_Call(lua_State* L) {
    return CallNamedOnSlot(L, LuaScriptSlot::Gameplay);
}

int LuaScriptEngine::Lua_This_CallController(lua_State* L) {
    return CallNamedOnSlot(L, LuaScriptSlot::CharacterController);
}

CharacterComponent* LuaScriptEngine::CharacterFromLuaSelf(lua_State* L, const char* meta) {
#if LUA_AVAILABLE
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, meta));
    if (!pEntityId) {
        return nullptr;
    }
    Entity entity = MakeEntity(*pEntityId);
    if (!entity || !entity.HasComponent<CharacterComponent>()) {
        return nullptr;
    }
    return &entity.GetComponent<CharacterComponent>();
#else
    (void)L;
    (void)meta;
    return nullptr;
#endif
}

void LuaScriptEngine::LogBindingError(ScriptInstance* instance, const std::string& message) {
    if (instance && instance->lastBindingError == message) {
        return;
    }
    if (instance) {
        instance->lastBindingError = message;
    }
    std::cerr << "[Lua] " << message << std::endl;
}

void LuaScriptEngine::LogBindingErrorForEntity(uint32_t entityId, const std::string& message) {
    ScriptInstance* instance = FindInstance(entityId, LuaScriptSlot::CharacterController);
    if (!instance) {
        instance = FindInstance(entityId, LuaScriptSlot::Gameplay);
    }
    LogBindingError(instance, message);
}

namespace {

void PushVec3Table(lua_State* L, const Vec3& value) {
#if LUA_AVAILABLE
    lua_newtable(L);
    lua_pushnumber(L, value.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, value.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, value.z);
    lua_setfield(L, -2, "z");
#else
    (void)L;
    (void)value;
#endif
}

} // namespace

int LuaScriptEngine::Lua_This_GetAnimator(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }
    LuaEntityReference* selfId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusEntity"));
    if (!selfId) {
        lua_pushnil(L);
        return 1;
    }
    Entity entity = engine->MakeEntity(*selfId);
    if (!entity || !entity.HasComponent<CharacterComponent>()) {
        lua_pushnil(L);
        return 1;
    }
    LuaEntityReference::Create(L, engine, static_cast<std::uint32_t>(*selfId));
    luaL_getmetatable(L, "HephaestusAnimator");
    lua_setmetatable(L, -2);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_This_GetCharacter(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }
    LuaEntityReference* selfId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusEntity"));
    if (!selfId) {
        lua_pushnil(L);
        return 1;
    }
    Entity entity = engine->MakeEntity(*selfId);
    if (!entity || !entity.HasComponent<CharacterComponent>()) {
        lua_pushnil(L);
        return 1;
    }
    LuaEntityReference::Create(L, engine, static_cast<std::uint32_t>(*selfId));
    luaL_getmetatable(L, "HephaestusCharacter");
    lua_setmetatable(L, -2);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_Play(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine->CharacterFromLuaSelf(L, "HephaestusAnimator");
    const char* clipId = luaL_checkstring(L, 2);
    if (!character || !clipId) {
        lua_pushboolean(L, false);
        return 1;
    }
    std::string error;
    const bool ok = Hephaestus::CharacterAnimationSystem::Play(*character, clipId, error);
    if (!ok) {
        LuaEntityReference* id = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusAnimator"));
        engine->LogBindingErrorForEntity(id ? *id : 0, error);
    }
    lua_pushboolean(L, ok);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_CrossFade(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine->CharacterFromLuaSelf(L, "HephaestusAnimator");
    const char* clipId = luaL_checkstring(L, 2);
    const float duration = static_cast<float>(luaL_checknumber(L, 3));
    if (!character || !clipId) {
        lua_pushboolean(L, false);
        return 1;
    }
    std::string error;
    const bool ok = Hephaestus::CharacterAnimationSystem::CrossFade(*character, clipId, duration, error);
    if (!ok) {
        LuaEntityReference* id = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusAnimator"));
        engine->LogBindingErrorForEntity(id ? *id : 0, error);
    }
    lua_pushboolean(L, ok);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_Pause(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine ? engine->CharacterFromLuaSelf(L, "HephaestusAnimator") : nullptr;
    if (character) {
        Hephaestus::CharacterAnimationSystem::Pause(*character);
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_Resume(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine ? engine->CharacterFromLuaSelf(L, "HephaestusAnimator") : nullptr;
    if (character) {
        Hephaestus::CharacterAnimationSystem::Resume(*character);
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_Stop(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine ? engine->CharacterFromLuaSelf(L, "HephaestusAnimator") : nullptr;
    if (character) {
        Hephaestus::CharacterAnimationSystem::Stop(*character);
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_SetSpeed(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine->CharacterFromLuaSelf(L, "HephaestusAnimator");
    const float speed = static_cast<float>(luaL_checknumber(L, 2));
    if (!character) {
        lua_pushboolean(L, false);
        return 1;
    }
    const bool ok = Hephaestus::CharacterAnimationSystem::SetSpeed(*character, speed);
    if (!ok) {
        LuaEntityReference* id = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusAnimator"));
        engine->LogBindingErrorForEntity(id ? *id : 0,
                                "Animator:SetSpeed requires a speed greater than 0. Use Pause() to freeze.");
    }
    lua_pushboolean(L, ok);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_IsPlaying(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine ? engine->CharacterFromLuaSelf(L, "HephaestusAnimator") : nullptr;
    if (!character) {
        lua_pushboolean(L, false);
        return 1;
    }
    const bool playing = character->player.playing && !character->player.paused;
    if (lua_gettop(L) < 2 || lua_isnoneornil(L, 2)) {
        lua_pushboolean(L, playing);
        return 1;
    }
    const char* clipId = lua_tostring(L, 2);
    if (!clipId) {
        lua_pushboolean(L, false);
        return 1;
    }
    std::string error;
    const Hephaestus::AnimationClip* clip = Hephaestus::FindClip(character->clips, clipId, error);
    const std::string resolved = clip ? (clip->id.empty() ? clip->name : clip->id) : std::string();
    lua_pushboolean(L, playing && !resolved.empty() && character->player.currentClip == resolved);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_GetCurrentAnimation(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine ? engine->CharacterFromLuaSelf(L, "HephaestusAnimator") : nullptr;
    if (!character || character->player.currentClip.empty()) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, character->player.currentClip.c_str());
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_GetTime(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine ? engine->CharacterFromLuaSelf(L, "HephaestusAnimator") : nullptr;
    lua_pushnumber(L, character ? character->player.time : 0.0);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_SetTime(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine ? engine->CharacterFromLuaSelf(L, "HephaestusAnimator") : nullptr;
    const float seconds = static_cast<float>(luaL_checknumber(L, 2));
    if (character && !character->player.currentClip.empty()) {
        character->player.time = (std::max)(0.0f, seconds);
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_SetLayer(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine ? engine->CharacterFromLuaSelf(L, "HephaestusAnimator") : nullptr;
    CharacterAnimationLayer layer;
    layer.name = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    auto number = [L](const char* key, float& out) {
        lua_getfield(L, 3, key);
        if (lua_isnumber(L, -1)) out = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    };
    auto text = [L](const char* key, std::string& out) {
        lua_getfield(L, 3, key);
        if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
        lua_pop(L, 1);
    };
    text("mask", layer.maskRoot);
    text("exclude", layer.maskExclude);
    number("excludeBlend", layer.excludeBlend);
    number("weight", layer.targetWeight);
    number("fade", layer.fadeSeconds);
    number("rate", layer.rate);
    lua_getfield(L, 3, "clips");
    if (lua_istable(L, -1)) {
        const lua_Integer count = luaL_len(L, -1);
        for (lua_Integer i = 1; i <= count; ++i) {
            lua_rawgeti(L, -1, i);
            CharacterAnimationLayer::Clip clip;
            if (lua_isstring(L, -1)) {
                clip.id = lua_tostring(L, -1);
            } else if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "clip");
                if (lua_isstring(L, -1)) clip.id = lua_tostring(L, -1);
                lua_pop(L, 1);
                lua_getfield(L, -1, "weight");
                if (lua_isnumber(L, -1)) clip.weight = static_cast<float>(lua_tonumber(L, -1));
                lua_pop(L, 1);
                lua_getfield(L, -1, "reverse");
                clip.reverse = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            if (!clip.id.empty()) layer.clips.push_back(std::move(clip));
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, 3, "phase");
    const bool hasPhase = lua_isnumber(L, -1) != 0;
    const float phase = hasPhase ? static_cast<float>(lua_tonumber(L, -1)) : 0.0f;
    lua_pop(L, 1);
    std::string error = character ? std::string() : std::string("No character");
    if (character && Hephaestus::CharacterAnimationSystem::SetLayer(*character, layer, error)) {
        if (hasPhase) {
            // Phase 1 would wrap to 0, so a clip played through to its end holds its last pose.
            for (CharacterAnimationLayer& stored : character->layers) {
                if (stored.name == layer.name) {
                    stored.phase = std::clamp(phase, 0.0f, 0.9999f);
                }
            }
        }
        lua_pushboolean(L, true);
        return 1;
    }
    lua_pushboolean(L, false);
    lua_pushstring(L, error.c_str());
    return 2;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_ClearLayer(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine ? engine->CharacterFromLuaSelf(L, "HephaestusAnimator") : nullptr;
    const char* name = luaL_checkstring(L, 2);
    const float fade = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    lua_pushboolean(L, character && Hephaestus::CharacterAnimationSystem::ClearLayer(*character, name, fade));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Animator_GetDuration(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    CharacterComponent* character = engine ? engine->CharacterFromLuaSelf(L, "HephaestusAnimator") : nullptr;
    if (!character) {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    std::string error;
    const Hephaestus::AnimationClip* clip = nullptr;
    if (lua_gettop(L) >= 2 && !lua_isnoneornil(L, 2)) {
        const char* clipId = lua_tostring(L, 2);
        if (clipId) {
            clip = Hephaestus::FindClip(character->clips, clipId, error);
        }
    } else if (!character->player.currentClip.empty()) {
        clip = Hephaestus::FindClip(character->clips, character->player.currentClip, error);
    }
    if (!clip) {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    lua_pushnumber(L, (std::max)(0.0f, Hephaestus::ClipRangeEnd(*clip) - Hephaestus::ClipRangeStart(*clip)));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

bool LuaScriptEngine::PushSocketTable(lua_State* L, uint32_t entityId, const char* socketName, bool sceneWorld) {
#if LUA_AVAILABLE
    Entity entity = MakeEntity(entityId);
    if (!entity || !entity.HasComponent<CharacterComponent>() || !socketName) {
        lua_pushnil(L);
        return false;
    }
    CharacterComponent& character = entity.GetComponent<CharacterComponent>();
    Hephaestus::CharacterSocket socket;
    std::string error;
    if (!Hephaestus::CharacterRuntime::FindSocket(character.characterPath, socketName, socket, error)) {
        LogBindingErrorForEntity(entityId, error);
        lua_pushnil(L);
        return false;
    }
    Mat4 model{};
    if (!Hephaestus::EvaluateSocketWorld(character, socket, model, error)) {
        LogBindingErrorForEntity(entityId, error);
        lua_pushnil(L);
        return false;
    }
    Mat4 result = model;
    if (sceneWorld && m_Scene) {
        result = Mat4::Multiply(m_Scene->GetWorldTransform(entity), model);
    }
    TransformComponent decomposed;
    decomposed.SetFromMatrix(result);
    lua_newtable(L);
    PushVec3Table(L, decomposed.position);
    lua_setfield(L, -2, "position");
    PushVec3Table(L, decomposed.rotation);
    lua_setfield(L, -2, "rotation");
    return true;
#else
    (void)L;
    (void)entityId;
    (void)socketName;
    (void)sceneWorld;
    return false;
#endif
}

int LuaScriptEngine::Lua_Character_GetSocket(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusCharacter"));
    const char* name = luaL_checkstring(L, 2);
    if (!engine || !pEntityId || !name) {
        lua_pushnil(L);
        return 1;
    }
    engine->PushSocketTable(L, *pEntityId, name, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Character_GetSocketLocal(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusCharacter"));
    const char* name = luaL_checkstring(L, 2);
    if (!engine || !pEntityId || !name) {
        lua_pushnil(L);
        return 1;
    }
    engine->PushSocketTable(L, *pEntityId, name, false);
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
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusTransform"));
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

int LuaScriptEngine::Lua_TransformObj_GetWorldPosition(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusTransform"));
    Entity entity = engine ? engine->MakeEntity(*pEntityId) : Entity{};
    if (!entity || !engine->GetTransform(*pEntityId)) {
        lua_pushnil(L);
        return 1;
    }
    TransformComponent world;
    world.SetFromMatrix(engine->m_Scene->GetWorldTransform(entity));
    PushVec3Table(L, world.position);
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
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusTransform"));
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
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusTransform"));
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
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusTransform"));
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
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusTransform"));
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
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusTransform"));
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
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusTransform"));
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
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusTransform"));
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
// ═══════════════════════════════════════════════════════════════
// Camera Component Bindings (for CameraActor support)
// ═══════════════════════════════════════════════════════════════

int LuaScriptEngine::Lua_This_GetCamera(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }

    LuaEntityReference* selfId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusEntity"));
    if (!selfId) {
        lua_pushnil(L);
        return 1;
    }

    Entity entity = engine->MakeEntity(*selfId);
    if (!entity || !entity.HasComponent<CameraComponent>()) {
        lua_pushnil(L);
        return 1;
    }

    LuaEntityReference::Create(L, engine, static_cast<std::uint32_t>(*selfId));
    luaL_getmetatable(L, "HephaestusCamera");
    lua_setmetatable(L, -2);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_CameraObj_GetFov(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusCamera"));
    if (!pEntityId) {
        lua_pushnil(L);
        return 1;
    }
    
    Entity entity = engine->MakeEntity(*pEntityId);
    if (!entity || !entity.HasComponent<CameraComponent>()) {
        lua_pushnil(L);
        return 1;
    }
    
    auto& camera = entity.GetComponent<CameraComponent>();
    lua_pushnumber(L, camera.fov);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_CameraObj_SetFov(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        return 0;
    }
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusCamera"));
    if (!pEntityId) {
        return 0;
    }
    
    Entity entity = engine->MakeEntity(*pEntityId);
    if (!entity || !entity.HasComponent<CameraComponent>()) {
        return 0;
    }
    
    float fov = static_cast<float>(luaL_checknumber(L, 2));
    auto& camera = entity.GetComponent<CameraComponent>();
    camera.fov = fov;
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_CameraObj_GetNear(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusCamera"));
    if (!pEntityId) {
        lua_pushnil(L);
        return 1;
    }
    
    Entity entity = engine->MakeEntity(*pEntityId);
    if (!entity || !entity.HasComponent<CameraComponent>()) {
        lua_pushnil(L);
        return 1;
    }
    
    auto& camera = entity.GetComponent<CameraComponent>();
    lua_pushnumber(L, camera.nearPlane);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_CameraObj_SetNear(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        return 0;
    }
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusCamera"));
    if (!pEntityId) {
        return 0;
    }
    
    Entity entity = engine->MakeEntity(*pEntityId);
    if (!entity || !entity.HasComponent<CameraComponent>()) {
        return 0;
    }
    
    float nearClip = static_cast<float>(luaL_checknumber(L, 2));
    auto& camera = entity.GetComponent<CameraComponent>();
    camera.nearPlane = nearClip;
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_CameraObj_GetFar(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusCamera"));
    if (!pEntityId) {
        lua_pushnil(L);
        return 1;
    }
    
    Entity entity = engine->MakeEntity(*pEntityId);
    if (!entity || !entity.HasComponent<CameraComponent>()) {
        lua_pushnil(L);
        return 1;
    }
    
    auto& camera = entity.GetComponent<CameraComponent>();
    lua_pushnumber(L, camera.farPlane);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_CameraObj_SetFar(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        return 0;
    }
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusCamera"));
    if (!pEntityId) {
        return 0;
    }
    
    Entity entity = engine->MakeEntity(*pEntityId);
    if (!entity || !entity.HasComponent<CameraComponent>()) {
        return 0;
    }
    
    float farClip = static_cast<float>(luaL_checknumber(L, 2));
    auto& camera = entity.GetComponent<CameraComponent>();
    camera.farPlane = farClip;
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_CameraObj_IsActive(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusCamera"));
    if (!pEntityId) {
        lua_pushboolean(L, false);
        return 1;
    }
    
    Entity entity = engine->MakeEntity(*pEntityId);
    if (!entity || !entity.HasComponent<CameraComponent>()) {
        lua_pushboolean(L, false);
        return 1;
    }
    
    auto& camera = entity.GetComponent<CameraComponent>();
    lua_pushboolean(L, camera.isActive);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_CameraObj_SetActive(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_Scene) {
        return 0;
    }
    
    LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_checkudata(L, 1, "HephaestusCamera"));
    if (!pEntityId) {
        return 0;
    }
    
    Entity entity = engine->MakeEntity(*pEntityId);
    if (!entity || !entity.HasComponent<CameraComponent>()) {
        return 0;
    }
    
    bool active = lua_toboolean(L, 2) != 0;
    
    // If setting to active, deactivate all other cameras first
    if (active) {
        auto cameraView = engine->m_Scene->GetEntitiesWith<CameraComponent>();
        for (auto otherHandle : cameraView) {
            Entity other(otherHandle, engine->m_Scene);
            if (other != entity && other.HasComponent<CameraComponent>()) {
                other.GetComponent<CameraComponent>().isActive = false;
            }
        }
    }
    
    auto& camera = entity.GetComponent<CameraComponent>();
    camera.isActive = active;
    return 0;
#else
    (void)L;
    return 0;
#endif
}

// ═══════════════════════════════════════════════════════════════
// Scene Camera Query Bindings
// ═══════════════════════════════════════════════════════════════

int LuaScriptEngine::Lua_Scene_FindFirstCamera(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_Scene) {
        lua_pushnil(L);
        return 1;
    }
    
    auto cameraView = engine->m_Scene->GetEntitiesWith<CameraComponent>();
    for (auto entityHandle : cameraView) {
        // Return first camera found
        LuaEntityReference::Create(L, engine, static_cast<std::uint32_t>(entityHandle));
        luaL_getmetatable(L, "HephaestusEntity");
        lua_setmetatable(L, -2);
        return 1;
    }
    
    lua_pushnil(L);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Scene_FindCameras(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_Scene) {
        lua_newtable(L);
        return 1;
    }
    
    lua_newtable(L);
    int index = 1;
    
    auto cameraView = engine->m_Scene->GetEntitiesWith<CameraComponent>();
    for (auto entityHandle : cameraView) {
        LuaEntityReference::Create(L, engine, static_cast<std::uint32_t>(entityHandle));
        luaL_getmetatable(L, "HephaestusEntity");
        lua_setmetatable(L, -2);
        lua_rawseti(L, -2, index++);
    }
    
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Scene_GetActiveCamera(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_Scene) {
        lua_pushnil(L);
        return 1;
    }
    
    auto cameraView = engine->m_Scene->GetEntitiesWith<CameraComponent>();
    
    // First pass: find camera with isActive=true
    for (auto entityHandle : cameraView) {
        Entity entity(entityHandle, engine->m_Scene);
        auto& camera = entity.GetComponent<CameraComponent>();
        if (camera.isActive) {
            LuaEntityReference::Create(L, engine, static_cast<std::uint32_t>(entityHandle));
            luaL_getmetatable(L, "HephaestusEntity");
            lua_setmetatable(L, -2);
            return 1;
        }
    }
    
    // Second pass: return first camera if none active
    for (auto entityHandle : cameraView) {
        LuaEntityReference::Create(L, engine, static_cast<std::uint32_t>(entityHandle));
        luaL_getmetatable(L, "HephaestusEntity");
        lua_setmetatable(L, -2);
        return 1;
    }
    
    lua_pushnil(L);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Scene_SetActiveCamera(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_Scene) {
        return 0;
    }
    
    // Accept entity ID as number or userdata
    uint32_t targetEntityId = 0;
    if (lua_isnumber(L, 1)) {
        targetEntityId = static_cast<uint32_t>(lua_tonumber(L, 1));
    } else if (lua_isuserdata(L, 1)) {
        LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(lua_touserdata(L, 1));
        if (pEntityId) {
            targetEntityId = *pEntityId;
        }
    } else {
        return 0;
    }
    
    Entity targetEntity = engine->MakeEntity(targetEntityId);
    if (!targetEntity || !targetEntity.HasComponent<CameraComponent>()) {
        return 0;
    }
    
    // Deactivate all cameras, then activate the target
    auto cameraView = engine->m_Scene->GetEntitiesWith<CameraComponent>();
    for (auto entityHandle : cameraView) {
        Entity entity(entityHandle, engine->m_Scene);
        auto& camera = entity.GetComponent<CameraComponent>();
        camera.isActive = (entityHandle == static_cast<entt::entity>(targetEntity));
    }
    
    return 0;
#else
    (void)L;
    return 0;
#endif
}

void LuaScriptEngine::RegisterSceneBindings() {
#if LUA_AVAILABLE
    // Create Scene global table with camera query functions
    lua_newtable(m_State);
    
    lua_pushcfunction(m_State, Lua_Scene_FindFirstCamera);
    lua_setfield(m_State, -2, "FindFirstCamera");
    
    lua_pushcfunction(m_State, Lua_Scene_FindCameras);
    lua_setfield(m_State, -2, "FindCameras");
    
    lua_pushcfunction(m_State, Lua_Scene_GetActiveCamera);
    lua_setfield(m_State, -2, "GetActiveCamera");
    
    lua_pushcfunction(m_State, Lua_Scene_SetActiveCamera);
    lua_setfield(m_State, -2, "SetActiveCamera");

    lua_pushcfunction(m_State, Lua_Scene_Instantiate);
    lua_setfield(m_State, -2, "instantiate");

    lua_pushcfunction(m_State, Lua_Scene_Destroy);
    lua_setfield(m_State, -2, "destroy");

    lua_pushcfunction(m_State, Lua_Scene_FindAllWithPrefix);
    lua_setfield(m_State, -2, "findAllWithPrefix");

    lua_pushcfunction(m_State, Lua_Scene_Reload);
    lua_setfield(m_State, -2, "reload");
    lua_pushcfunction(m_State, Lua_Scene_Load);
    lua_setfield(m_State, -2, "load");
    
    lua_setglobal(m_State, "Scene");
    
    // Create the "Camera" metatable for camera component objects
    luaL_newmetatable(m_State, "HephaestusCamera");
    
    lua_pushcfunction(m_State, Lua_CameraObj_GetFov);
    lua_setfield(m_State, -2, "GetFov");
    
    lua_pushcfunction(m_State, Lua_CameraObj_SetFov);
    lua_setfield(m_State, -2, "SetFov");
    
    lua_pushcfunction(m_State, Lua_CameraObj_GetNear);
    lua_setfield(m_State, -2, "GetNear");
    
    lua_pushcfunction(m_State, Lua_CameraObj_SetNear);
    lua_setfield(m_State, -2, "SetNear");
    
    lua_pushcfunction(m_State, Lua_CameraObj_GetFar);
    lua_setfield(m_State, -2, "GetFar");
    
    lua_pushcfunction(m_State, Lua_CameraObj_SetFar);
    lua_setfield(m_State, -2, "SetFar");
    
    lua_pushcfunction(m_State, Lua_CameraObj_IsActive);
    lua_setfield(m_State, -2, "IsActive");
    
    lua_pushcfunction(m_State, Lua_CameraObj_SetActive);
    lua_setfield(m_State, -2, "SetActive");
    
    // Set __index to self for method lookup
    lua_pushvalue(m_State, -1);
    lua_setfield(m_State, -2, "__index");
    
    lua_pop(m_State, 1);
#endif
}

int LuaScriptEngine::Lua_Scene_Instantiate(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_Scene || !engine->m_State) {
        lua_pushnil(L);
        return 1;
    }

    const char* pathCStr = luaL_checkstring(L, 1);
    if (!pathCStr || pathCStr[0] == '\0') {
        std::cerr << "[Lua] Scene.instantiate: path required\n";
        lua_pushnil(L);
        return 1;
    }

    PendingSpawn spawn;
    spawn.prefabPath = pathCStr;

    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "position");
        if (lua_istable(L, -1)) {
            PropertyValue posVal = engine->ParsePropertyValueFromLua(L, -1);
            if (posVal.IsVec3()) {
                spawn.hasPosition = true;
                spawn.position = *posVal.AsVec3();
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "rotation");
        if (lua_istable(L, -1)) {
            PropertyValue rotVal = engine->ParsePropertyValueFromLua(L, -1);
            if (rotVal.IsVec3()) {
                spawn.hasRotation = true;
                spawn.rotationDegrees = *rotVal.AsVec3();
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "parent");
        if (!lua_isnil(L, -1)) {
            LuaEntityReference* parent = static_cast<LuaEntityReference*>(luaL_testudata(L, -1, "HephaestusEntity"));
            if (!parent) {
                return luaL_error(L, "Scene.instantiate: options.parent must be an entity");
            }
            spawn.hasParent = true;
            spawn.parentId = *parent;
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "vars");
        if (lua_istable(L, -1)) {
            PropertyValue varsVal = engine->ParsePropertyValueFromLua(L, -1);
            if (varsVal.IsObject()) {
                spawn.vars = *varsVal.AsObject();
            }
        }
        lua_pop(L, 1);
    }

    // Spawn immediately so the caller receives a live entity handle. Script Update
    // snapshots its entity list first, so adding LuaScriptComponents mid-tick is safe.
    // Destroy remains deferred via the queue.
    Entity root = engine->ExecuteSpawn(spawn);
    if (!root) {
        lua_pushnil(L);
        return 1;
    }

    engine->PushEntityUserdata(L, static_cast<uint32_t>(root));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Scene_Destroy(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_Scene) {
        return 0;
    }

    uint32_t entityId = 0;
    if (lua_isuserdata(L, 1)) {
        LuaEntityReference* pEntityId = static_cast<LuaEntityReference*>(luaL_testudata(L, 1, "HephaestusEntity"));
        if (pEntityId) {
            entityId = *pEntityId;
        }
    } else if (lua_isnumber(L, 1)) {
        entityId = static_cast<uint32_t>(lua_tonumber(L, 1));
    }

    if (entityId == 0 && !lua_isuserdata(L, 1)) {
        // Also accept "this" global style when userdata is nil - no-op
        return 0;
    }

    if (engine->m_UpdatingScripts) {
        PendingDestroy pending;
        pending.entityId = entityId;
        engine->m_PendingDestroys.push_back(pending);
    } else {
        engine->ExecuteDestroy(entityId);
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Scene_FindAllWithPrefix(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_Scene) {
        lua_newtable(L);
        return 1;
    }

    const char* prefix = luaL_optstring(L, 1, "");
    const std::string prefixStr = prefix ? prefix : "";

    lua_newtable(L);
    int index = 1;
    for (Entity entity : engine->m_Scene->FindEntitiesWithNamePrefix(prefixStr)) {
        if (!entity) {
            continue;
        }
        engine->PushEntityUserdata(L, static_cast<uint32_t>(entity));
        lua_rawseti(L, -2, index++);
    }
    return 1;
#else
    (void)L;
    return 0;
#endif
}

void LuaScriptEngine::RegisterAudioBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);

    lua_pushcfunction(m_State, Lua_Audio_isAvailable);
    lua_setfield(m_State, -2, "isAvailable");

    lua_pushcfunction(m_State, Lua_Audio_play);
    lua_setfield(m_State, -2, "play");

    lua_pushcfunction(m_State, Lua_Audio_stop);
    lua_setfield(m_State, -2, "stop");

    lua_pushcfunction(m_State, Lua_Audio_pause);
    lua_setfield(m_State, -2, "pause");

    lua_pushcfunction(m_State, Lua_Audio_resume);
    lua_setfield(m_State, -2, "resume");

    lua_pushcfunction(m_State, Lua_Audio_setVolume);
    lua_setfield(m_State, -2, "setVolume");

    lua_pushcfunction(m_State, Lua_Audio_isPlaying);
    lua_setfield(m_State, -2, "isPlaying");

    lua_pushcfunction(m_State, Lua_Audio_stopAll);
    lua_setfield(m_State, -2, "stopAll");

    lua_pushcfunction(m_State, Lua_Audio_setPitch);
    lua_setfield(m_State, -2, "setPitch");

    lua_pushcfunction(m_State, Lua_Audio_setPosition);
    lua_setfield(m_State, -2, "setPosition");

    lua_pushcfunction(m_State, Lua_Audio_setBusVolume);
    lua_setfield(m_State, -2, "setBusVolume");

    lua_pushcfunction(m_State, Lua_Audio_getBusVolume);
    lua_setfield(m_State, -2, "getBusVolume");

    lua_pushcfunction(m_State, Lua_Audio_getVoiceCount);
    lua_setfield(m_State, -2, "getVoiceCount");

    lua_setglobal(m_State, "Audio");
#endif
}

int LuaScriptEngine::Lua_Audio_isAvailable(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    bool available = engine && engine->m_AudioEngine && engine->m_AudioEngine->IsBackendAvailable();
    lua_pushboolean(L, available);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Audio_play(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_AudioEngine) {
        lua_pushinteger(L, 0);
        return 1;
    }

    const char* path = luaL_checkstring(L, 1);
    AudioEngine::PlayParams params;
    if (lua_istable(L, 2)) {
        auto number = [L](const char* key, float& out) {
            lua_getfield(L, 2, key);
            if (lua_isnumber(L, -1)) out = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
        };
        number("volume", params.volume);
        number("pitch", params.pitch);
        number("volumeJitter", params.volumeJitter);
        number("pitchJitter", params.pitchJitter);
        number("minDistance", params.minDistance);
        number("maxDistance", params.maxDistance);
        lua_getfield(L, 2, "loop");
        params.loop = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        lua_getfield(L, 2, "priority");
        if (lua_isnumber(L, -1)) params.priority = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 2, "bus");
        if (lua_isstring(L, -1)) params.bus = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "position");
        if (lua_istable(L, -1)) {
            params.spatial = true;
            lua_getfield(L, -1, "x");
            params.position.x = static_cast<float>(lua_tonumber(L, -1));
            lua_getfield(L, -2, "y");
            params.position.y = static_cast<float>(lua_tonumber(L, -1));
            lua_getfield(L, -3, "z");
            params.position.z = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 3);
        }
        lua_pop(L, 1);
    } else {
        if (lua_gettop(L) >= 2) {
            params.loop = lua_toboolean(L, 2) != 0;
        }
        if (lua_gettop(L) >= 3) {
            params.volume = static_cast<float>(luaL_checknumber(L, 3));
        }
    }

    AudioEngine::SoundHandle handle = engine->m_AudioEngine->Play(path, params);
    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Audio_stop(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_AudioEngine) {
        lua_pushboolean(L, false);
        return 1;
    }

    uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, engine->m_AudioEngine->Stop(handle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Audio_pause(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_AudioEngine) {
        lua_pushboolean(L, false);
        return 1;
    }

    uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, engine->m_AudioEngine->Pause(handle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Audio_resume(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_AudioEngine) {
        lua_pushboolean(L, false);
        return 1;
    }

    uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, engine->m_AudioEngine->Resume(handle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Audio_setVolume(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_AudioEngine) {
        lua_pushboolean(L, false);
        return 1;
    }

    uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float volume = static_cast<float>(luaL_checknumber(L, 2));
    lua_pushboolean(L, engine->m_AudioEngine->SetVolume(handle, volume));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Audio_isPlaying(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_AudioEngine) {
        lua_pushboolean(L, false);
        return 1;
    }

    uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, engine->m_AudioEngine->IsPlaying(handle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Audio_setPitch(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const float pitch = static_cast<float>(luaL_checknumber(L, 2));
    lua_pushboolean(L, engine && engine->m_AudioEngine && engine->m_AudioEngine->SetPitch(handle, pitch));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Audio_setPosition(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const Vec3 position(static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3)),
                        static_cast<float>(luaL_checknumber(L, 4)));
    lua_pushboolean(L, engine && engine->m_AudioEngine && engine->m_AudioEngine->SetPosition(handle, position));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Audio_setBusVolume(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const char* bus = luaL_checkstring(L, 1);
    const float volume = static_cast<float>(luaL_checknumber(L, 2));
    lua_pushboolean(L, engine && engine->m_AudioEngine && engine->m_AudioEngine->SetBusVolume(bus, volume));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Audio_getBusVolume(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const char* bus = luaL_checkstring(L, 1);
    lua_pushnumber(L, engine && engine->m_AudioEngine ? engine->m_AudioEngine->GetBusVolume(bus) : 1.0);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Audio_getVoiceCount(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    lua_pushinteger(L, engine && engine->m_AudioEngine ? engine->m_AudioEngine->GetActiveVoiceCount() : 0);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Audio_stopAll(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (engine && engine->m_AudioEngine) {
        engine->m_AudioEngine->StopAll();
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

void LuaScriptEngine::RegisterUIBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);

    lua_pushcfunction(m_State, Lua_UI_isAvailable);
    lua_setfield(m_State, -2, "isAvailable");

    lua_pushcfunction(m_State, Lua_UI_createSimpleMenu);
    lua_setfield(m_State, -2, "createSimpleMenu");

    lua_pushcfunction(m_State, Lua_UI_createScreen);
    lua_setfield(m_State, -2, "createScreen");

    lua_pushcfunction(m_State, Lua_UI_getRoot);
    lua_setfield(m_State, -2, "getRoot");

    lua_pushcfunction(m_State, Lua_UI_createPanel);
    lua_setfield(m_State, -2, "createPanel");

    lua_pushcfunction(m_State, Lua_UI_createText);
    lua_setfield(m_State, -2, "createText");

    lua_pushcfunction(m_State, Lua_UI_createButton);
    lua_setfield(m_State, -2, "createButton");

    lua_pushcfunction(m_State, Lua_UI_createImage);
    lua_setfield(m_State, -2, "createImage");

    lua_pushcfunction(m_State, Lua_UI_createVerticalBox);
    lua_setfield(m_State, -2, "createVerticalBox");

    lua_pushcfunction(m_State, Lua_UI_createHorizontalBox);
    lua_setfield(m_State, -2, "createHorizontalBox");

    lua_pushcfunction(m_State, Lua_UI_clearScreens);
    lua_setfield(m_State, -2, "clearScreens");

    lua_pushcfunction(m_State, Lua_UI_getScreenCount);
    lua_setfield(m_State, -2, "getScreenCount");

    lua_pushcfunction(m_State, Lua_UI_setScreenVisible);
    lua_setfield(m_State, -2, "setScreenVisible");

    lua_pushcfunction(m_State, Lua_UI_setScreenActive);
    lua_setfield(m_State, -2, "setScreenActive");

    lua_pushcfunction(m_State, Lua_UI_setViewportSize);
    lua_setfield(m_State, -2, "setViewportSize");

    lua_pushcfunction(m_State, Lua_UI_setScale);
    lua_setfield(m_State, -2, "setScale");

    lua_pushcfunction(m_State, Lua_UI_getScale);
    lua_setfield(m_State, -2, "getScale");

    lua_pushcfunction(m_State, Lua_UI_getDrawCommandCount);
    lua_setfield(m_State, -2, "getDrawCommandCount");

    lua_pushcfunction(m_State, Lua_UI_setPosition);
    lua_setfield(m_State, -2, "setPosition");

    lua_pushcfunction(m_State, Lua_UI_setSize);
    lua_setfield(m_State, -2, "setSize");

    lua_pushcfunction(m_State, Lua_UI_setVisible);
    lua_setfield(m_State, -2, "setVisible");

    lua_pushcfunction(m_State, Lua_UI_setOpacity);
    lua_setfield(m_State, -2, "setOpacity");

    lua_pushcfunction(m_State, Lua_UI_setZOrder);
    lua_setfield(m_State, -2, "setZOrder");

    lua_pushcfunction(m_State, Lua_UI_setText);
    lua_setfield(m_State, -2, "setText");

    lua_pushcfunction(m_State, Lua_UI_setImage);
    lua_setfield(m_State, -2, "setImage");

    lua_pushcfunction(m_State, Lua_UI_setSpacing);
    lua_setfield(m_State, -2, "setSpacing");

    lua_pushcfunction(m_State, Lua_UI_setCursorMode);
    lua_setfield(m_State, -2, "setCursorMode");

    lua_pushcfunction(m_State, Lua_UI_getCursorMode);
    lua_setfield(m_State, -2, "getCursorMode");

    lua_pushcfunction(m_State, Lua_UI_setOnClick);
    lua_setfield(m_State, -2, "setOnClick");

    lua_pushcfunction(m_State, Lua_UI_setOnHover);
    lua_setfield(m_State, -2, "setOnHover");

    lua_pushcfunction(m_State, Lua_UI_createGrid);
    lua_setfield(m_State, -2, "createGrid");

    lua_pushcfunction(m_State, Lua_UI_setGridColumns);
    lua_setfield(m_State, -2, "setGridColumns");

    lua_pushcfunction(m_State, Lua_UI_setGridSpacing);
    lua_setfield(m_State, -2, "setGridSpacing");

    lua_pushcfunction(m_State, Lua_UI_setEnabled);
    lua_setfield(m_State, -2, "setEnabled");

    lua_pushcfunction(m_State, Lua_UI_setPadding);
    lua_setfield(m_State, -2, "setPadding");

    lua_pushcfunction(m_State, Lua_UI_setMargin);
    lua_setfield(m_State, -2, "setMargin");

    lua_pushcfunction(m_State, Lua_UI_setMinSize);
    lua_setfield(m_State, -2, "setMinSize");

    lua_pushcfunction(m_State, Lua_UI_setMaxSize);
    lua_setfield(m_State, -2, "setMaxSize");

    lua_pushcfunction(m_State, Lua_UI_setAnchor);
    lua_setfield(m_State, -2, "setAnchor");

    lua_pushcfunction(m_State, Lua_UI_setAlignment);
    lua_setfield(m_State, -2, "setAlignment");

    lua_pushcfunction(m_State, Lua_UI_setBackgroundColor);
    lua_setfield(m_State, -2, "setBackgroundColor");

    lua_pushcfunction(m_State, Lua_UI_setBorderColor);
    lua_setfield(m_State, -2, "setBorderColor");

    lua_pushcfunction(m_State, Lua_UI_setBorderWidth);
    lua_setfield(m_State, -2, "setBorderWidth");

    lua_pushcfunction(m_State, Lua_UI_setCornerRadius);
    lua_setfield(m_State, -2, "setCornerRadius");

    lua_pushcfunction(m_State, Lua_UI_setTextColor);
    lua_setfield(m_State, -2, "setTextColor");

    lua_pushcfunction(m_State, Lua_UI_setFontSize);
    lua_setfield(m_State, -2, "setFontSize");

    lua_pushcfunction(m_State, Lua_UI_setFontBold);
    lua_setfield(m_State, -2, "setFontBold");

    lua_pushcfunction(m_State, Lua_UI_setFontItalic);
    lua_setfield(m_State, -2, "setFontItalic");

    lua_pushcfunction(m_State, Lua_UI_measureText);
    lua_setfield(m_State, -2, "measureText");
    lua_pushcfunction(m_State, Lua_UI_getLayoutSize);
    lua_setfield(m_State, -2, "getLayoutSize");
    lua_pushcfunction(m_State, Lua_UI_setTextAlignment);
    lua_setfield(m_State, -2, "setTextAlignment");

    lua_pushcfunction(m_State, Lua_UI_setTextShadow);
    lua_setfield(m_State, -2, "setTextShadow");

    lua_pushcfunction(m_State, Lua_UI_setWordWrap);
    lua_setfield(m_State, -2, "setWordWrap");

    lua_pushcfunction(m_State, Lua_UI_setStyle);
    lua_setfield(m_State, -2, "setStyle");

    lua_pushcfunction(m_State, Lua_UI_setButtonStyle);
    lua_setfield(m_State, -2, "setButtonStyle");

    lua_pushcfunction(m_State, Lua_UI_setFocused);
    lua_setfield(m_State, -2, "setFocused");

    lua_pushcfunction(m_State, Lua_UI_registerThemeStyle);
    lua_setfield(m_State, -2, "registerThemeStyle");

    lua_pushcfunction(m_State, Lua_UI_applyThemeStyle);
    lua_setfield(m_State, -2, "applyThemeStyle");

    lua_pushcfunction(m_State, Lua_UI_setChildAlignment);
    lua_setfield(m_State, -2, "setChildAlignment");

    lua_pushcfunction(m_State, Lua_UI_setImageScaleMode);
    lua_setfield(m_State, -2, "setImageScaleMode");

    lua_pushcfunction(m_State, Lua_UI_setImageTint);
    lua_setfield(m_State, -2, "setImageTint");

    lua_pushcfunction(m_State, Lua_UI_loadHUI);
    lua_setfield(m_State, -2, "loadHUI");

    lua_pushcfunction(m_State, Lua_UI_getWidget);
    lua_setfield(m_State, -2, "getWidget");

    lua_setglobal(m_State, "UI");

    // CursorMode enum constants
    lua_newtable(m_State);
    lua_pushstring(m_State, "game");
    lua_setfield(m_State, -2, "Game");
    lua_pushstring(m_State, "ui");
    lua_setfield(m_State, -2, "UI");
    lua_setglobal(m_State, "CursorMode");

    // UIAnchor enum constants
    lua_newtable(m_State);
    lua_pushstring(m_State, "topLeft");      lua_setfield(m_State, -2, "TopLeft");
    lua_pushstring(m_State, "topCenter");    lua_setfield(m_State, -2, "TopCenter");
    lua_pushstring(m_State, "topRight");     lua_setfield(m_State, -2, "TopRight");
    lua_pushstring(m_State, "centerLeft");   lua_setfield(m_State, -2, "CenterLeft");
    lua_pushstring(m_State, "center");       lua_setfield(m_State, -2, "Center");
    lua_pushstring(m_State, "centerRight");  lua_setfield(m_State, -2, "CenterRight");
    lua_pushstring(m_State, "bottomLeft");   lua_setfield(m_State, -2, "BottomLeft");
    lua_pushstring(m_State, "bottomCenter"); lua_setfield(m_State, -2, "BottomCenter");
    lua_pushstring(m_State, "bottomRight");  lua_setfield(m_State, -2, "BottomRight");
    lua_pushstring(m_State, "stretch");      lua_setfield(m_State, -2, "Stretch");
    lua_setglobal(m_State, "UIAnchor");

    // UIAlign enum constants
    lua_newtable(m_State);
    lua_pushstring(m_State, "left");    lua_setfield(m_State, -2, "Left");
    lua_pushstring(m_State, "center");  lua_setfield(m_State, -2, "Center");
    lua_pushstring(m_State, "right");   lua_setfield(m_State, -2, "Right");
    lua_pushstring(m_State, "top");     lua_setfield(m_State, -2, "Top");
    lua_pushstring(m_State, "bottom");  lua_setfield(m_State, -2, "Bottom");
    lua_pushstring(m_State, "stretch"); lua_setfield(m_State, -2, "Stretch");
    lua_setglobal(m_State, "UIAlign");

    // UIScaleMode enum constants
    lua_newtable(m_State);
    lua_pushstring(m_State, "stretch"); lua_setfield(m_State, -2, "Stretch");
    lua_pushstring(m_State, "fit");     lua_setfield(m_State, -2, "Fit");
    lua_pushstring(m_State, "fill");    lua_setfield(m_State, -2, "Fill");
    lua_pushstring(m_State, "center");  lua_setfield(m_State, -2, "Center");
    lua_setglobal(m_State, "UIScaleMode");
#endif
}

int LuaScriptEngine::Lua_UI_isAvailable(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const bool available = engine && engine->m_UISystem && engine->m_UIContext && engine->m_UIRenderer;
    lua_pushboolean(L, available);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_createSimpleMenu(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UISystem) {
        lua_pushinteger(L, 0);
        return 1;
    }

    std::shared_ptr<Hephaestus::HephaestusUI::UIScreen> screen =
        engine->m_UISystem->FindScreenByName("MainMenu");
    if (!screen) {
        screen = Hephaestus::HephaestusUI::CreateSimpleMenuScreen();
        if (engine->m_UIContext) {
            screen->SetViewportSize(engine->m_UIContext->GetLayoutSize());
        }
        engine->m_UISystem->AddScreen(screen);
    }

    const uint32_t screenHandle = engine->RegisterUIScreenHandle(screen);
    lua_pushinteger(L, static_cast<lua_Integer>(screenHandle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_createScreen(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UISystem) {
        lua_pushinteger(L, 0);
        return 1;
    }

    const char* screenName = luaL_checkstring(L, 1);
    std::shared_ptr<Hephaestus::HephaestusUI::UIScreen> screen =
        engine->m_UISystem->FindScreenByName(screenName);
    if (!screen) {
        screen = std::make_shared<Hephaestus::HephaestusUI::UIScreen>(screenName);
        if (engine->m_UIContext) {
            screen->SetViewportSize(engine->m_UIContext->GetLayoutSize());
        }
        engine->m_UISystem->AddScreen(screen);
    }

    const uint32_t handle = engine->RegisterUIScreenHandle(screen);
    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_getRoot(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushinteger(L, 0);
        return 1;
    }

    const uint32_t screenHandle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const std::shared_ptr<Hephaestus::HephaestusUI::UIScreen> screen = engine->ResolveUIScreen(screenHandle);
    if (!screen || !screen->GetRootCanvas()) {
        lua_pushinteger(L, 0);
        return 1;
    }

    const uint32_t rootHandle = engine->RegisterUIElementHandle(screen->GetRootCanvas());
    lua_pushinteger(L, static_cast<lua_Integer>(rootHandle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_createPanel(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushinteger(L, 0);
        return 1;
    }

    const uint32_t parentHandle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* parent = engine->ResolveUIElement(parentHandle);
    if (!parent) {
        lua_pushinteger(L, 0);
        return 1;
    }

    auto panel = std::make_unique<Hephaestus::HephaestusUI::UIPanel>();
    Hephaestus::HephaestusUI::UIElement* created = parent->AddChild(std::move(panel));
    const uint32_t handle = engine->RegisterUIElementHandle(created);
    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_createText(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushinteger(L, 0);
        return 1;
    }

    const uint32_t parentHandle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* text = luaL_optstring(L, 2, "");
    Hephaestus::HephaestusUI::UIElement* parent = engine->ResolveUIElement(parentHandle);
    if (!parent) {
        lua_pushinteger(L, 0);
        return 1;
    }

    auto element = std::make_unique<Hephaestus::HephaestusUI::UIText>(text);
    Hephaestus::HephaestusUI::UIElement* created = parent->AddChild(std::move(element));
    const uint32_t handle = engine->RegisterUIElementHandle(created);
    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_createButton(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushinteger(L, 0);
        return 1;
    }

    const uint32_t parentHandle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* label = luaL_optstring(L, 2, "Button");
    Hephaestus::HephaestusUI::UIElement* parent = engine->ResolveUIElement(parentHandle);
    if (!parent) {
        lua_pushinteger(L, 0);
        return 1;
    }

    auto element = std::make_unique<Hephaestus::HephaestusUI::UIButton>(label);
    Hephaestus::HephaestusUI::UIElement* created = parent->AddChild(std::move(element));
    const uint32_t handle = engine->RegisterUIElementHandle(created);
    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_createImage(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushinteger(L, 0);
        return 1;
    }

    const uint32_t parentHandle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* imageId = luaL_optstring(L, 2, "");
    Hephaestus::HephaestusUI::UIElement* parent = engine->ResolveUIElement(parentHandle);
    if (!parent) {
        lua_pushinteger(L, 0);
        return 1;
    }

    auto element = std::make_unique<Hephaestus::HephaestusUI::UIImage>(imageId);
    Hephaestus::HephaestusUI::UIElement* created = parent->AddChild(std::move(element));
    const uint32_t handle = engine->RegisterUIElementHandle(created);
    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_createVerticalBox(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushinteger(L, 0);
        return 1;
    }

    const uint32_t parentHandle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* parent = engine->ResolveUIElement(parentHandle);
    if (!parent) {
        lua_pushinteger(L, 0);
        return 1;
    }

    auto element = std::make_unique<Hephaestus::HephaestusUI::UIVerticalBox>();
    Hephaestus::HephaestusUI::UIElement* created = parent->AddChild(std::move(element));
    const uint32_t handle = engine->RegisterUIElementHandle(created);
    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_createHorizontalBox(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushinteger(L, 0);
        return 1;
    }

    const uint32_t parentHandle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* parent = engine->ResolveUIElement(parentHandle);
    if (!parent) {
        lua_pushinteger(L, 0);
        return 1;
    }

    auto element = std::make_unique<Hephaestus::HephaestusUI::UIHorizontalBox>();
    Hephaestus::HephaestusUI::UIElement* created = parent->AddChild(std::move(element));
    const uint32_t handle = engine->RegisterUIElementHandle(created);
    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_clearScreens(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (engine && engine->m_UISystem) {
        engine->m_UISystem->ClearScreens();
        engine->ResetUIHandleMaps();
    }
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_getScreenCount(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const size_t count = (engine && engine->m_UISystem) ? engine->m_UISystem->GetScreens().size() : 0;
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setScreenVisible(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UISystem) {
        lua_pushboolean(L, false);
        return 1;
    }

    const bool visible = lua_toboolean(L, 2) != 0;
    const std::shared_ptr<Hephaestus::HephaestusUI::UIScreen> screen = engine->ScreenFromArgument(L, 1);
    if (!screen) {
        lua_pushboolean(L, false);
        return 1;
    }

    screen->SetVisible(visible);
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

std::shared_ptr<Hephaestus::HephaestusUI::UIScreen> LuaScriptEngine::ScreenFromArgument(lua_State* L, int index) const {
#if LUA_AVAILABLE
    if (lua_type(L, index) == LUA_TNUMBER) {
        return ResolveUIScreen(static_cast<uint32_t>(lua_tointeger(L, index)));
    }
    return m_UISystem ? m_UISystem->FindScreenByName(luaL_checkstring(L, index)) : nullptr;
#else
    (void)L;
    (void)index;
    return nullptr;
#endif
}

int LuaScriptEngine::Lua_UI_setScreenActive(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UISystem) {
        lua_pushboolean(L, false);
        return 1;
    }

    const bool active = lua_toboolean(L, 2) != 0;
    const std::shared_ptr<Hephaestus::HephaestusUI::UIScreen> screen = engine->ScreenFromArgument(L, 1);
    if (!screen) {
        lua_pushboolean(L, false);
        return 1;
    }

    screen->SetActive(active);
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setScale(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UIContext) {
        lua_pushboolean(L, false);
        return 1;
    }
    engine->m_UIContext->SetUserScale(static_cast<float>(luaL_checknumber(L, 1)));
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_getScale(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    lua_pushnumber(L, engine && engine->m_UIContext ? engine->m_UIContext->GetUserScale() : 1.0);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setViewportSize(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UIContext || !engine->m_UISystem) {
        lua_pushboolean(L, false);
        return 1;
    }

    const float width = static_cast<float>(luaL_checknumber(L, 1));
    const float height = static_cast<float>(luaL_checknumber(L, 2));
    const Hephaestus::HephaestusUI::UIVec2 size{width, height};

    engine->m_UIContext->SetViewportSize(size);
    for (const std::shared_ptr<Hephaestus::HephaestusUI::UIScreen>& screen : engine->m_UISystem->GetScreens()) {
        if (screen) {
            screen->SetViewportSize(engine->m_UIContext->GetLayoutSize());
        }
    }

    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_getDrawCommandCount(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const size_t count = (engine && engine->m_UIRenderer)
        ? engine->m_UIRenderer->GetDrawCommands().size()
        : 0;
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setPosition(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) {
        lua_pushboolean(L, false);
        return 1;
    }

    const float x = static_cast<float>(luaL_checknumber(L, 2));
    const float y = static_cast<float>(luaL_checknumber(L, 3));
    element->SetPosition({x, y});
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setSize(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) {
        lua_pushboolean(L, false);
        return 1;
    }

    const float width = static_cast<float>(luaL_checknumber(L, 2));
    const float height = static_cast<float>(luaL_checknumber(L, 3));
    element->SetSize({width, height});
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setVisible(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) {
        lua_pushboolean(L, false);
        return 1;
    }

    const bool visible = lua_toboolean(L, 2) != 0;
    element->SetVisible(visible);
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setOpacity(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) {
        lua_pushboolean(L, false);
        return 1;
    }

    const float opacity = static_cast<float>(luaL_checknumber(L, 2));
    element->SetOpacity(opacity);
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setZOrder(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) {
        lua_pushboolean(L, false);
        return 1;
    }

    const int zOrder = static_cast<int>(luaL_checkinteger(L, 2));
    element->SetZOrder(zOrder);
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setText(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* text = luaL_checkstring(L, 2);
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) {
        lua_pushboolean(L, false);
        return 1;
    }

    if (auto* textElement = dynamic_cast<Hephaestus::HephaestusUI::UIText*>(element)) {
        textElement->SetText(text);
        lua_pushboolean(L, true);
        return 1;
    }

    if (auto* button = dynamic_cast<Hephaestus::HephaestusUI::UIButton*>(element)) {
        button->SetText(text);
        lua_pushboolean(L, true);
        return 1;
    }

    lua_pushboolean(L, false);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setImage(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* imageId = luaL_checkstring(L, 2);
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) {
        lua_pushboolean(L, false);
        return 1;
    }

    if (auto* image = dynamic_cast<Hephaestus::HephaestusUI::UIImage*>(element)) {
        image->SetImageId(imageId);
        lua_pushboolean(L, true);
        return 1;
    }

    lua_pushboolean(L, false);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setSpacing(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const float spacing = static_cast<float>(luaL_checknumber(L, 2));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) {
        lua_pushboolean(L, false);
        return 1;
    }

    if (auto* vertical = dynamic_cast<Hephaestus::HephaestusUI::UIVerticalBox*>(element)) {
        vertical->SetSpacing(spacing);
        lua_pushboolean(L, true);
        return 1;
    }

    if (auto* horizontal = dynamic_cast<Hephaestus::HephaestusUI::UIHorizontalBox*>(element)) {
        horizontal->SetSpacing(spacing);
        lua_pushboolean(L, true);
        return 1;
    }

    lua_pushboolean(L, false);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setCursorMode(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UISystem) {
        lua_pushboolean(L, false);
        return 1;
    }

    const char* modeStr = luaL_checkstring(L, 1);
    std::string mode = modeStr ? modeStr : "";
    for (auto& c : mode) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    using CursorMode = Hephaestus::HephaestusUI::CursorMode;

    if (mode == "ui") {
        engine->m_UISystem->SetCursorMode(CursorMode::UI);
    } else if (mode == "game") {
        engine->m_UISystem->SetCursorMode(CursorMode::Game);
    } else {
        luaL_error(L, "UI.setCursorMode: unknown mode '%s' (use 'ui' or 'game', or CursorMode.UI / CursorMode.Game)", modeStr);
        return 0;
    }

    // Always store the request so the host picks it up on the next tick
    // (covers calls made during onAwake before the first host input tick).
    const int legacyMode = (mode == "ui") ? 0 : 1;
    if (engine->m_InputSystem) {
        engine->m_InputSystem->SetCursorModeRequest(legacyMode);
    }

    // GLFW runtime: apply immediately since there is no host tick to consume the request
    if (engine->m_Window) {
        int glfwMode = (legacyMode == 1) ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
        glfwSetInputMode(engine->m_Window, GLFW_CURSOR, glfwMode);
        if (legacyMode == 1 && glfwRawMouseMotionSupported()) {
            glfwSetInputMode(engine->m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        } else if (legacyMode == 0) {
            glfwSetInputMode(engine->m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
    }
    engine->m_CurrentCursorMode = legacyMode;

    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_getCursorMode(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UISystem) {
        lua_pushstring(L, "game");
        return 1;
    }

    using CursorMode = Hephaestus::HephaestusUI::CursorMode;
    const char* result = (engine->m_UISystem->GetCursorMode() == CursorMode::UI) ? "ui" : "game";
    lua_pushstring(L, result);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setOnClick(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }

    const uint32_t elementHandle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (!lua_isfunction(L, 2)) {
        luaL_error(L, "UI.setOnClick: second argument must be a function");
        lua_pushboolean(L, false);
        return 1;
    }

    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(elementHandle);
    if (!element) {
        lua_pushboolean(L, false);
        return 1;
    }

    auto* button = dynamic_cast<Hephaestus::HephaestusUI::UIButton*>(element);
    if (!button) {
        lua_pushboolean(L, false);
        return 1;
    }

    // Remove old callback if present
    auto it = engine->m_UIButtonClickCallbacks.find(elementHandle);
    if (it != engine->m_UIButtonClickCallbacks.end()) {
        luaL_unref(L, LUA_REGISTRYINDEX, it->second);
        engine->m_UIButtonClickCallbacks.erase(it);
    }

    // Push the function to the top of the stack and create a reference
    lua_pushvalue(L, 2);
    const int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    engine->m_UIButtonClickCallbacks[elementHandle] = callbackRef;

    // Set the C++ callback that will invoke the Lua function
    button->SetOnClicked([engine, callbackRef]() {
        if (!engine || !engine->m_State) return;

        lua_rawgeti(engine->m_State, LUA_REGISTRYINDEX, callbackRef);
        if (lua_isfunction(engine->m_State, -1)) {
            if (lua_pcall(engine->m_State, 0, 0, 0) != LUA_OK) {
                const char* err = lua_tostring(engine->m_State, -1);
                std::cerr << "[Lua] UI.setOnClick callback error: " << (err ? err : "unknown") << "\n";
                lua_pop(engine->m_State, 1);
            }
        } else {
            lua_pop(engine->m_State, 1);
        }
    });

    lua_pushboolean(L, true);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// Helper: parse a Lua table into a UIStyle
// ---------------------------------------------------------------------------
Hephaestus::HephaestusUI::UIStyle LuaScriptEngine::ParseStyleTable(lua_State* L, int tableIndex) {
    Hephaestus::HephaestusUI::UIStyle style;
#if LUA_AVAILABLE
    if (!lua_istable(L, tableIndex)) return style;

    auto getFloat = [&](const char* field, float& out) {
        lua_getfield(L, tableIndex, field);
        if (lua_isnumber(L, -1)) out = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    };
    auto getBool = [&](const char* field, bool& out) {
        lua_getfield(L, tableIndex, field);
        if (!lua_isnil(L, -1)) out = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    };
    auto getString = [&](const char* field, std::string& out) {
        lua_getfield(L, tableIndex, field);
        if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
        lua_pop(L, 1);
    };
    auto getColor = [&](const char* field, Hephaestus::HephaestusUI::UIColor& out) {
        lua_getfield(L, tableIndex, field);
        if (lua_istable(L, -1)) {
            int ci = lua_gettop(L);
            lua_getfield(L, ci, "r"); if (lua_isnumber(L, -1)) out.r = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
            lua_getfield(L, ci, "g"); if (lua_isnumber(L, -1)) out.g = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
            lua_getfield(L, ci, "b"); if (lua_isnumber(L, -1)) out.b = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
            lua_getfield(L, ci, "a"); if (lua_isnumber(L, -1)) out.a = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
        }
        lua_pop(L, 1);
    };
    auto getSpacing = [&](const char* field, Hephaestus::HephaestusUI::UISpacing& out) {
        lua_getfield(L, tableIndex, field);
        if (lua_isnumber(L, -1)) {
            float v = static_cast<float>(lua_tonumber(L, -1));
            out = Hephaestus::HephaestusUI::UISpacing::Uniform(v);
        } else if (lua_istable(L, -1)) {
            int si = lua_gettop(L);
            lua_getfield(L, si, "left");   if (lua_isnumber(L, -1)) out.left   = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
            lua_getfield(L, si, "top");    if (lua_isnumber(L, -1)) out.top    = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
            lua_getfield(L, si, "right");  if (lua_isnumber(L, -1)) out.right  = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
            lua_getfield(L, si, "bottom"); if (lua_isnumber(L, -1)) out.bottom = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
        }
        lua_pop(L, 1);
    };

    getColor("backgroundColor", style.backgroundColor);
    getString("backgroundImageId", style.backgroundImageId);
    getColor("imageTint", style.imageTint);
    getColor("borderColor", style.borderColor);
    getFloat("borderWidth", style.borderWidth);
    getFloat("cornerRadius", style.cornerRadius);
    getColor("textColor", style.textColor);
    getString("fontFamily", style.fontFamily);
    getFloat("fontSize", style.fontSize);
    getBool("fontBold", style.fontBold);
    getBool("fontItalic", style.fontItalic);
    getSpacing("padding", style.padding);
    getFloat("opacity", style.opacity);
    getFloat("scale", style.scale);
    getBool("textShadow", style.textShadow);
    getColor("textShadowColor", style.textShadowColor);

    lua_getfield(L, tableIndex, "textAlignment");
    if (lua_isstring(L, -1)) {
        std::string val = lua_tostring(L, -1);
        if (val == "center") style.textAlignment = Hephaestus::HephaestusUI::UITextAlignment::Center;
        else if (val == "right") style.textAlignment = Hephaestus::HephaestusUI::UITextAlignment::Right;
        else style.textAlignment = Hephaestus::HephaestusUI::UITextAlignment::Left;
    }
    lua_pop(L, 1);

    lua_getfield(L, tableIndex, "imageScaleMode");
    if (lua_isstring(L, -1)) {
        std::string val = lua_tostring(L, -1);
        if (val == "fit") style.imageScaleMode = Hephaestus::HephaestusUI::UIImageScaleMode::Fit;
        else if (val == "fill") style.imageScaleMode = Hephaestus::HephaestusUI::UIImageScaleMode::Fill;
        else if (val == "center") style.imageScaleMode = Hephaestus::HephaestusUI::UIImageScaleMode::Center;
        else style.imageScaleMode = Hephaestus::HephaestusUI::UIImageScaleMode::Stretch;
    }
    lua_pop(L, 1);

    lua_getfield(L, tableIndex, "textShadowOffset");
    if (lua_istable(L, -1)) {
        int si = lua_gettop(L);
        lua_getfield(L, si, "x"); if (lua_isnumber(L, -1)) style.textShadowOffset.x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
        lua_getfield(L, si, "y"); if (lua_isnumber(L, -1)) style.textShadowOffset.y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    }
    lua_pop(L, 1);
#else
    (void)L; (void)tableIndex;
#endif
    return style;
}

void LuaScriptEngine::ApplyStyleToPanel(Hephaestus::HephaestusUI::UIElement* element,
                                         const Hephaestus::HephaestusUI::UIStyle& style) {
    if (auto* panel = dynamic_cast<Hephaestus::HephaestusUI::UIPanel*>(element)) {
        panel->SetLocalStyle(style);
    } else if (auto* button = dynamic_cast<Hephaestus::HephaestusUI::UIButton*>(element)) {
        button->SetNormalStyle(style);
        auto hovered = style;
        hovered.backgroundColor.r = std::min(1.0f, style.backgroundColor.r + 0.07f);
        hovered.backgroundColor.g = std::min(1.0f, style.backgroundColor.g + 0.07f);
        hovered.backgroundColor.b = std::min(1.0f, style.backgroundColor.b + 0.07f);
        button->SetHoveredStyle(hovered);
        auto pressed = style;
        pressed.backgroundColor.r = std::max(0.0f, style.backgroundColor.r - 0.05f);
        pressed.backgroundColor.g = std::max(0.0f, style.backgroundColor.g - 0.05f);
        pressed.backgroundColor.b = std::max(0.0f, style.backgroundColor.b - 0.05f);
        button->SetPressedStyle(pressed);
    } else {
        element->SetOwnedStyle(style);
    }
}

int LuaScriptEngine::Lua_UI_setOnHover(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (!lua_isfunction(L, 2)) {
        luaL_error(L, "UI.setOnHover: second argument must be a function");
        return 0;
    }

    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    auto* button = dynamic_cast<Hephaestus::HephaestusUI::UIButton*>(element);
    if (!button) { lua_pushboolean(L, false); return 1; }

    auto it = engine->m_UIHoverCallbacks.find(handle);
    if (it != engine->m_UIHoverCallbacks.end()) {
        luaL_unref(L, LUA_REGISTRYINDEX, it->second);
        engine->m_UIHoverCallbacks.erase(it);
    }

    lua_pushvalue(L, 2);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    engine->m_UIHoverCallbacks[handle] = ref;

    button->SetOnHovered([engine, ref](bool entered) {
        if (!engine || !engine->m_State) return;
        lua_rawgeti(engine->m_State, LUA_REGISTRYINDEX, ref);
        if (lua_isfunction(engine->m_State, -1)) {
            lua_pushboolean(engine->m_State, entered ? 1 : 0);
            if (lua_pcall(engine->m_State, 1, 0, 0) != LUA_OK) {
                lua_pop(engine->m_State, 1);
            }
        } else {
            lua_pop(engine->m_State, 1);
        }
    });

    lua_pushboolean(L, true);
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_createGrid(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushinteger(L, 0); return 1; }

    const uint32_t parentHandle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* parent = engine->ResolveUIElement(parentHandle);
    if (!parent) { lua_pushinteger(L, 0); return 1; }

    auto element = std::make_unique<Hephaestus::HephaestusUI::UIGridLayout>();
    Hephaestus::HephaestusUI::UIElement* created = parent->AddChild(std::move(element));
    const uint32_t handle = engine->RegisterUIElementHandle(created);
    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setGridColumns(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const int columns = static_cast<int>(luaL_checkinteger(L, 2));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    if (auto* grid = dynamic_cast<Hephaestus::HephaestusUI::UIGridLayout*>(element)) {
        grid->SetColumns(columns);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setGridSpacing(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const float hSpacing = static_cast<float>(luaL_checknumber(L, 2));
    const float vSpacing = static_cast<float>(luaL_optnumber(L, 3, hSpacing));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    if (auto* grid = dynamic_cast<Hephaestus::HephaestusUI::UIGridLayout*>(element)) {
        grid->SetHSpacing(hSpacing);
        grid->SetVSpacing(vSpacing);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setEnabled(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const bool enabled = lua_toboolean(L, 2) != 0;
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    element->SetEnabled(enabled);
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setPadding(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    int nargs = lua_gettop(L);
    Hephaestus::HephaestusUI::UISpacing padding;
    if (nargs == 2) {
        float v = static_cast<float>(luaL_checknumber(L, 2));
        padding = Hephaestus::HephaestusUI::UISpacing::Uniform(v);
    } else {
        padding.top    = static_cast<float>(luaL_checknumber(L, 2));
        padding.right  = static_cast<float>(luaL_checknumber(L, 3));
        padding.bottom = static_cast<float>(luaL_optnumber(L, 4, padding.top));
        padding.left   = static_cast<float>(luaL_optnumber(L, 5, padding.right));
    }
    element->SetPadding(padding);
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setMargin(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    int nargs = lua_gettop(L);
    Hephaestus::HephaestusUI::UISpacing margin;
    if (nargs == 2) {
        float v = static_cast<float>(luaL_checknumber(L, 2));
        margin = Hephaestus::HephaestusUI::UISpacing::Uniform(v);
    } else {
        margin.top    = static_cast<float>(luaL_checknumber(L, 2));
        margin.right  = static_cast<float>(luaL_checknumber(L, 3));
        margin.bottom = static_cast<float>(luaL_optnumber(L, 4, margin.top));
        margin.left   = static_cast<float>(luaL_optnumber(L, 5, margin.right));
    }
    element->SetMargin(margin);
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setMinSize(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    const float w = static_cast<float>(luaL_checknumber(L, 2));
    const float h = static_cast<float>(luaL_checknumber(L, 3));
    element->SetMinSize({w, h});
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setMaxSize(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    const float w = static_cast<float>(luaL_checknumber(L, 2));
    const float h = static_cast<float>(luaL_checknumber(L, 3));
    element->SetMaxSize({w, h});
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setAnchor(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* anchorStr = luaL_checkstring(L, 2);
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    std::string anchor = anchorStr ? anchorStr : "";
    for (auto& c : anchor) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    using AP = Hephaestus::HephaestusUI::UIAnchorPreset;
    AP preset = AP::TopLeft;
    if (anchor == "topcenter")       preset = AP::TopCenter;
    else if (anchor == "topright")   preset = AP::TopRight;
    else if (anchor == "centerleft") preset = AP::CenterLeft;
    else if (anchor == "center")     preset = AP::Center;
    else if (anchor == "centerright")preset = AP::CenterRight;
    else if (anchor == "bottomleft") preset = AP::BottomLeft;
    else if (anchor == "bottomcenter") preset = AP::BottomCenter;
    else if (anchor == "bottomright")preset = AP::BottomRight;
    else if (anchor == "stretch")    preset = AP::Stretch;

    element->SetAnchor({preset});
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setAlignment(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* hStr = luaL_checkstring(L, 2);
    const char* vStr = luaL_optstring(L, 3, nullptr);
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    auto parseH = [](const char* s) -> Hephaestus::HephaestusUI::UIHAlignment {
        if (!s) return Hephaestus::HephaestusUI::UIHAlignment::Left;
        std::string v = s;
        for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (v == "center") return Hephaestus::HephaestusUI::UIHAlignment::Center;
        if (v == "right")  return Hephaestus::HephaestusUI::UIHAlignment::Right;
        if (v == "stretch") return Hephaestus::HephaestusUI::UIHAlignment::Stretch;
        return Hephaestus::HephaestusUI::UIHAlignment::Left;
    };
    auto parseV = [](const char* s) -> Hephaestus::HephaestusUI::UIVAlignment {
        if (!s) return Hephaestus::HephaestusUI::UIVAlignment::Top;
        std::string v = s;
        for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (v == "center") return Hephaestus::HephaestusUI::UIVAlignment::Center;
        if (v == "bottom") return Hephaestus::HephaestusUI::UIVAlignment::Bottom;
        if (v == "stretch") return Hephaestus::HephaestusUI::UIVAlignment::Stretch;
        return Hephaestus::HephaestusUI::UIVAlignment::Top;
    };

    element->SetHAlignment(parseH(hStr));
    if (vStr) element->SetVAlignment(parseV(vStr));
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setBackgroundColor(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    const float r = static_cast<float>(luaL_checknumber(L, 2));
    const float g = static_cast<float>(luaL_checknumber(L, 3));
    const float b = static_cast<float>(luaL_checknumber(L, 4));
    const float a = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    Hephaestus::HephaestusUI::UIColor color{r, g, b, a};

    if (auto* panel = dynamic_cast<Hephaestus::HephaestusUI::UIPanel*>(element)) {
        auto style = panel->GetLocalStyle();
        style.backgroundColor = color;
        panel->SetLocalStyle(style);
        lua_pushboolean(L, true);
    } else if (auto* button = dynamic_cast<Hephaestus::HephaestusUI::UIButton*>(element)) {
        auto style = button->GetNormalStyle();
        style.backgroundColor = color;
        button->SetNormalStyle(style);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setBorderColor(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    const float r = static_cast<float>(luaL_checknumber(L, 2));
    const float g = static_cast<float>(luaL_checknumber(L, 3));
    const float b = static_cast<float>(luaL_checknumber(L, 4));
    const float a = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    Hephaestus::HephaestusUI::UIColor color{r, g, b, a};

    if (auto* panel = dynamic_cast<Hephaestus::HephaestusUI::UIPanel*>(element)) {
        auto style = panel->GetLocalStyle();
        style.borderColor = color;
        panel->SetLocalStyle(style);
        lua_pushboolean(L, true);
    } else if (auto* button = dynamic_cast<Hephaestus::HephaestusUI::UIButton*>(element)) {
        auto style = button->GetNormalStyle();
        style.borderColor = color;
        button->SetNormalStyle(style);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setBorderWidth(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const float width = static_cast<float>(luaL_checknumber(L, 2));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    if (auto* panel = dynamic_cast<Hephaestus::HephaestusUI::UIPanel*>(element)) {
        auto style = panel->GetLocalStyle();
        style.borderWidth = width;
        panel->SetLocalStyle(style);
        lua_pushboolean(L, true);
    } else if (auto* button = dynamic_cast<Hephaestus::HephaestusUI::UIButton*>(element)) {
        auto style = button->GetNormalStyle();
        style.borderWidth = width;
        button->SetNormalStyle(style);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setCornerRadius(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const float radius = static_cast<float>(luaL_checknumber(L, 2));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    if (auto* panel = dynamic_cast<Hephaestus::HephaestusUI::UIPanel*>(element)) {
        auto style = panel->GetLocalStyle();
        style.cornerRadius = radius;
        panel->SetLocalStyle(style);
        lua_pushboolean(L, true);
    } else if (auto* button = dynamic_cast<Hephaestus::HephaestusUI::UIButton*>(element)) {
        auto nStyle = button->GetNormalStyle();   nStyle.cornerRadius = radius;  button->SetNormalStyle(nStyle);
        auto hStyle = button->GetHoveredStyle();  hStyle.cornerRadius = radius;  button->SetHoveredStyle(hStyle);
        auto pStyle = button->GetPressedStyle();  pStyle.cornerRadius = radius;  button->SetPressedStyle(pStyle);
        auto fStyle = button->GetFocusedStyle();  fStyle.cornerRadius = radius;  button->SetFocusedStyle(fStyle);
        auto dStyle = button->GetDisabledStyle(); dStyle.cornerRadius = radius;  button->SetDisabledStyle(dStyle);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setTextColor(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    const float r = static_cast<float>(luaL_checknumber(L, 2));
    const float g = static_cast<float>(luaL_checknumber(L, 3));
    const float b = static_cast<float>(luaL_checknumber(L, 4));
    const float a = static_cast<float>(luaL_optnumber(L, 5, 1.0));

    if (auto* text = dynamic_cast<Hephaestus::HephaestusUI::UIText*>(element)) {
        text->SetColor({r, g, b, a});
        lua_pushboolean(L, true);
    } else if (auto* button = dynamic_cast<Hephaestus::HephaestusUI::UIButton*>(element)) {
        auto style = button->GetNormalStyle();
        style.textColor = {r, g, b, a};
        button->SetNormalStyle(style);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setFontSize(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const float size = static_cast<float>(luaL_checknumber(L, 2));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    if (auto* text = dynamic_cast<Hephaestus::HephaestusUI::UIText*>(element)) {
        auto font = text->GetFont();
        font.SetPointSize(size);
        text->SetFont(font);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setFontBold(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const bool bold = lua_toboolean(L, 2) != 0;
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    if (auto* text = dynamic_cast<Hephaestus::HephaestusUI::UIText*>(element)) {
        auto font = text->GetFont();
        font.SetBold(bold);
        text->SetFont(font);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setFontItalic(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const bool italic = lua_toboolean(L, 2) != 0;
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    if (auto* text = dynamic_cast<Hephaestus::HephaestusUI::UIText*>(element)) {
        auto font = text->GetFont();
        font.SetItalic(italic);
        text->SetFont(font);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_getLayoutSize(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UIContext) {
        lua_pushnil(L);
        return 1;
    }
    const auto size = engine->m_UIContext->GetLayoutSize();
    lua_pushnumber(L, size.x);
    lua_pushnumber(L, size.y);
    return 2;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_measureText(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    Hephaestus::HephaestusUI::UIFontKey key;
    const char* text = luaL_checkstring(L, 1);
    key.size = static_cast<float>(luaL_checknumber(L, 2));
    key.bold = lua_toboolean(L, 3) != 0;
    if (lua_isstring(L, 4)) {
        key.family = lua_tostring(L, 4);
    }
    const std::string root = engine ? engine->m_ProjectRoot.generic_string() : std::string();
    const auto measure = Hephaestus::HephaestusUI::MeasureHUIText(root, text, key);
    lua_pushnumber(L, measure.width);
    lua_pushnumber(L, measure.inkLeft);
    lua_pushnumber(L, measure.inkRight);
    return 3;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setTextAlignment(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* alignStr = luaL_checkstring(L, 2);
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    std::string align = alignStr ? alignStr : "";
    for (auto& c : align) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto alignment = Hephaestus::HephaestusUI::UITextAlignment::Left;
    if (align == "center") alignment = Hephaestus::HephaestusUI::UITextAlignment::Center;
    else if (align == "right") alignment = Hephaestus::HephaestusUI::UITextAlignment::Right;

    if (auto* text = dynamic_cast<Hephaestus::HephaestusUI::UIText*>(element)) {
        text->SetAlignment(alignment);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setTextShadow(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const bool enabled = lua_toboolean(L, 2) != 0;
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    if (auto* text = dynamic_cast<Hephaestus::HephaestusUI::UIText*>(element)) {
        text->SetShadow(enabled);
        if (lua_gettop(L) >= 6) {
            const float r = static_cast<float>(luaL_checknumber(L, 3));
            const float g = static_cast<float>(luaL_checknumber(L, 4));
            const float b = static_cast<float>(luaL_checknumber(L, 5));
            const float a = static_cast<float>(luaL_optnumber(L, 6, 0.5));
            text->SetShadowColor({r, g, b, a});
        }
        if (lua_gettop(L) >= 8) {
            const float ox = static_cast<float>(luaL_checknumber(L, 7));
            const float oy = static_cast<float>(luaL_checknumber(L, 8));
            text->SetShadowOffset({ox, oy});
        }
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setWordWrap(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const bool wrap = lua_toboolean(L, 2) != 0;
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    if (auto* text = dynamic_cast<Hephaestus::HephaestusUI::UIText*>(element)) {
        text->SetWordWrap(wrap);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setStyle(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    auto style = ParseStyleTable(L, 2);
    ApplyStyleToPanel(element, style);

    if (style.padding.left != 0 || style.padding.top != 0 || style.padding.right != 0 || style.padding.bottom != 0) {
        element->SetPadding(style.padding);
    }

    lua_pushboolean(L, true);
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setButtonStyle(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* stateStr = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    auto* button = dynamic_cast<Hephaestus::HephaestusUI::UIButton*>(element);
    if (!button) { lua_pushboolean(L, false); return 1; }

    auto style = ParseStyleTable(L, 3);
    std::string state = stateStr ? stateStr : "";
    for (auto& c : state) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (state == "normal")        button->SetNormalStyle(style);
    else if (state == "hovered")  button->SetHoveredStyle(style);
    else if (state == "pressed")  button->SetPressedStyle(style);
    else if (state == "focused")  button->SetFocusedStyle(style);
    else if (state == "disabled") button->SetDisabledStyle(style);
    else { lua_pushboolean(L, false); return 1; }

    lua_pushboolean(L, true);
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setFocused(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const bool focused = lua_toboolean(L, 2) != 0;
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    if (auto* button = dynamic_cast<Hephaestus::HephaestusUI::UIButton*>(element)) {
        button->SetFocused(focused);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_registerThemeStyle(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UISystem) { lua_pushboolean(L, false); return 1; }

    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    auto style = ParseStyleTable(L, 2);
    engine->m_UISystem->GetTheme().SetStyle(name, style);
    lua_pushboolean(L, true);
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_applyThemeStyle(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UISystem) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* name = luaL_checkstring(L, 2);
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    const Hephaestus::HephaestusUI::UIStyle* themeStyle = engine->m_UISystem->GetTheme().GetStyle(name);
    if (!themeStyle) { lua_pushboolean(L, false); return 1; }

    ApplyStyleToPanel(element, *themeStyle);

    if (themeStyle->padding.left != 0 || themeStyle->padding.top != 0 ||
        themeStyle->padding.right != 0 || themeStyle->padding.bottom != 0) {
        element->SetPadding(themeStyle->padding);
    }

    lua_pushboolean(L, true);
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setChildAlignment(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* alignStr = luaL_checkstring(L, 2);
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    std::string align = alignStr ? alignStr : "";
    for (auto& c : align) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (auto* vbox = dynamic_cast<Hephaestus::HephaestusUI::UIVerticalBox*>(element)) {
        auto ha = Hephaestus::HephaestusUI::UIHAlignment::Stretch;
        if (align == "left")    ha = Hephaestus::HephaestusUI::UIHAlignment::Left;
        if (align == "center")  ha = Hephaestus::HephaestusUI::UIHAlignment::Center;
        if (align == "right")   ha = Hephaestus::HephaestusUI::UIHAlignment::Right;
        vbox->SetChildHAlignment(ha);
        lua_pushboolean(L, true);
    } else if (auto* hbox = dynamic_cast<Hephaestus::HephaestusUI::UIHorizontalBox*>(element)) {
        auto va = Hephaestus::HephaestusUI::UIVAlignment::Stretch;
        if (align == "top")     va = Hephaestus::HephaestusUI::UIVAlignment::Top;
        if (align == "center")  va = Hephaestus::HephaestusUI::UIVAlignment::Center;
        if (align == "bottom")  va = Hephaestus::HephaestusUI::UIVAlignment::Bottom;
        hbox->SetChildVAlignment(va);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setImageScaleMode(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* modeStr = luaL_checkstring(L, 2);
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    std::string mode = modeStr ? modeStr : "";
    for (auto& c : mode) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto scaleMode = Hephaestus::HephaestusUI::UIImageScaleMode::Stretch;
    if (mode == "fit")    scaleMode = Hephaestus::HephaestusUI::UIImageScaleMode::Fit;
    if (mode == "fill")   scaleMode = Hephaestus::HephaestusUI::UIImageScaleMode::Fill;
    if (mode == "center") scaleMode = Hephaestus::HephaestusUI::UIImageScaleMode::Center;

    if (auto* image = dynamic_cast<Hephaestus::HephaestusUI::UIImage*>(element)) {
        image->SetScaleMode(scaleMode);
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

int LuaScriptEngine::Lua_UI_setImageTint(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) { lua_pushboolean(L, false); return 1; }

    const uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    Hephaestus::HephaestusUI::UIElement* element = engine->ResolveUIElement(handle);
    if (!element) { lua_pushboolean(L, false); return 1; }

    const float r = static_cast<float>(luaL_checknumber(L, 2));
    const float g = static_cast<float>(luaL_checknumber(L, 3));
    const float b = static_cast<float>(luaL_checknumber(L, 4));
    const float a = static_cast<float>(luaL_optnumber(L, 5, 1.0));

    if (auto* image = dynamic_cast<Hephaestus::HephaestusUI::UIImage*>(element)) {
        image->SetTint({r, g, b, a});
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L; return 0;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// HUI Asset Loading (UI.loadHUI / UI.getWidget)
// ─────────────────────────────────────────────────────────────────────────────

int LuaScriptEngine::Lua_UI_loadHUI(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UISystem) {
        lua_pushnil(L);
        lua_pushstring(L, "UI system not available");
        return 2;
    }

    const char* filePath = luaL_checkstring(L, 1);
    if (!filePath || filePath[0] == '\0') {
        lua_pushnil(L);
        lua_pushstring(L, "File path is empty");
        return 2;
    }

    // Resolve path relative to project root / search paths
    std::filesystem::path resolved(filePath);
    if (Hephaestus::AssetPathPolicy::IsPluginContentKey(filePath)) {
        resolved = Hephaestus::AssetPathPolicy::ResolvePortableKey(engine->m_ProjectRoot, filePath);
        if (resolved.empty()) {
            lua_pushnil(L);
            lua_pushstring(L, Hephaestus::AssetPathPolicy::UnresolvedPluginContentMessage(filePath).c_str());
            return 2;
        }
    } else if (!resolved.is_absolute()) {
        for (const auto& searchPath : engine->m_SearchPaths) {
            auto candidate = searchPath / filePath;
            if (std::filesystem::exists(candidate)) {
                resolved = candidate;
                break;
            }
        }
        if (!resolved.is_absolute() && !engine->m_ProjectRoot.empty()) {
            auto candidate = engine->m_ProjectRoot / filePath;
            if (std::filesystem::exists(candidate)) {
                resolved = candidate;
            }
        }
    }

    Hephaestus::HephaestusUI::HUIParser parser;
    Hephaestus::HephaestusUI::HUIDocument document;

    if (!parser.ParseFile(resolved.string(), document)) {
        lua_pushnil(L);
        lua_pushstring(L, parser.GetErrorString().c_str());
        return 2;
    }

    Hephaestus::HephaestusUI::HUIBuilder builder;
    auto result = builder.Build(document);

    if (!result.success || !result.screen) {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to build UI from .hui document");
        return 2;
    }

    engine->m_UISystem->AddScreen(result.screen);
    uint32_t screenHandle = engine->RegisterUIScreenHandle(result.screen);

    // Register all widget elements in the handle map
    std::unordered_map<std::string, uint32_t> widgetHandleMap;
    for (auto& [widgetId, element] : result.widgetMap) {
        uint32_t elemHandle = engine->RegisterUIElementHandle(element);
        widgetHandleMap[widgetId] = elemHandle;
    }
    engine->m_HUIWidgetMaps[screenHandle] = std::move(widgetHandleMap);

    // If a controller script is referenced, load it
    if (!result.controller.empty()) {
        std::filesystem::path controllerPath(result.controller);
        if (!controllerPath.is_absolute()) {
            // Try relative to the .hui file's directory
            auto huiDir = resolved.parent_path();
            auto candidate = huiDir / result.controller;
            if (std::filesystem::exists(candidate)) {
                controllerPath = candidate;
            } else {
                controllerPath = engine->ResolveScriptPath(result.controller);
            }
        }
        if (std::filesystem::exists(controllerPath)) {
            engine->LoadScript(controllerPath.string());
        } else {
            std::cerr << "[HUI] Controller script not found: " << result.controller << "\n";
        }
    }

    lua_pushinteger(L, screenHandle);
    lua_pushnil(L); // no error
    return 2;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_UI_getWidget(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }

    const uint32_t screenHandle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* widgetId = luaL_checkstring(L, 2);

    auto mapIt = engine->m_HUIWidgetMaps.find(screenHandle);
    if (mapIt == engine->m_HUIWidgetMaps.end()) {
        lua_pushnil(L);
        return 1;
    }

    auto widgetIt = mapIt->second.find(widgetId);
    if (widgetIt == mapIt->second.end()) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, widgetIt->second);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// display / destroy API — Managed HUI lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void LuaScriptEngine::RegisterDisplayDestroyBindings() {
#if LUA_AVAILABLE
    // display table
    lua_newtable(m_State);
    lua_pushcfunction(m_State, Lua_Display_ui);
    lua_setfield(m_State, -2, "ui");
    lua_setglobal(m_State, "display");

    // destroy table
    lua_newtable(m_State);
    lua_pushcfunction(m_State, Lua_Destroy_ui);
    lua_setfield(m_State, -2, "ui");
    lua_setglobal(m_State, "destroy");
#endif
}

std::string LuaScriptEngine::normalizeHUIPath(const std::string& rawPath) const {
    std::string path = rawPath;
    for (auto& c : path) {
        if (c == '\\') c = '/';
    }
    // Strip leading ./ if present
    if (path.size() >= 2 && path[0] == '.' && path[1] == '/') {
        path = path.substr(2);
    }
    return path;
}

uint32_t LuaScriptEngine::displayHUI(const std::string& filePath, std::string& outError) {
    if (!m_UISystem) {
        outError = "UI system not available";
        return 0;
    }

    if (filePath.empty()) {
        outError = "File path is empty";
        return 0;
    }

    // Validate extension
    {
        std::string ext;
        auto dotPos = filePath.rfind('.');
        if (dotPos != std::string::npos) {
            ext = filePath.substr(dotPos);
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext != ".hui") {
            outError = "Expected a .hui file, got: '" + filePath + "'";
            return 0;
        }
    }

    std::string normalizedPath = normalizeHUIPath(filePath);

    // Resolve to an absolute path
    std::filesystem::path resolved(filePath);
    if (Hephaestus::AssetPathPolicy::IsPluginContentKey(filePath)) {
        resolved = Hephaestus::AssetPathPolicy::ResolvePortableKey(m_ProjectRoot, filePath);
        if (resolved.empty()) {
            outError = Hephaestus::AssetPathPolicy::UnresolvedPluginContentMessage(filePath);
            return 0;
        }
    } else if (!resolved.is_absolute()) {
        bool found = false;
        for (const auto& searchPath : m_SearchPaths) {
            auto candidate = searchPath / filePath;
            if (std::filesystem::exists(candidate)) {
                resolved = candidate;
                found = true;
                break;
            }
        }
        if (!found && !m_ProjectRoot.empty()) {
            auto candidate = m_ProjectRoot / filePath;
            if (std::filesystem::exists(candidate)) {
                resolved = candidate;
            }
        }
    }

    if (!std::filesystem::exists(resolved)) {
        outError = "File not found: '" + filePath + "'";
        return 0;
    }

    // If this path is already loaded, destroy the old instance first (replace semantics)
    auto existingIt = m_HUIPathToHandle.find(normalizedPath);
    if (existingIt != m_HUIPathToHandle.end()) {
        destroyHUI(existingIt->second);
    }

    // Parse
    Hephaestus::HephaestusUI::HUIParser parser;
    Hephaestus::HephaestusUI::HUIDocument document;
    if (!parser.ParseFile(resolved.string(), document)) {
        outError = parser.GetErrorString();
        return 0;
    }

    // Build
    Hephaestus::HephaestusUI::HUIBuilder builder;
    auto result = builder.Build(document);
    if (!result.success || !result.screen) {
        outError = "Failed to build UI from .hui document";
        return 0;
    }

    // Add to the UI system and make visible
    result.screen->SetVisible(true);
    result.screen->SetActive(true);
    m_UISystem->AddScreen(result.screen);
    uint32_t screenHandle = RegisterUIScreenHandle(result.screen);

    // Register all widget element handles
    std::unordered_map<std::string, uint32_t> widgetHandleMap;
    for (auto& [widgetId, element] : result.widgetMap) {
        uint32_t elemHandle = RegisterUIElementHandle(element);
        widgetHandleMap[widgetId] = elemHandle;
    }
    m_HUIWidgetMaps[screenHandle] = std::move(widgetHandleMap);

    // Track the instance
    HUIInstance instance;
    instance.screenHandle = screenHandle;
    instance.sourcePath = normalizedPath;
    instance.alive = true;
    m_ActiveHUIInstances[screenHandle] = std::move(instance);
    m_HUIPathToHandle[normalizedPath] = screenHandle;

    // Push onto the current-UI stack
    m_HUIStack.push_back(screenHandle);

    // Auto-load controller script if referenced
    if (!result.controller.empty()) {
        std::filesystem::path controllerPath(result.controller);
        if (!controllerPath.is_absolute()) {
            auto huiDir = resolved.parent_path();
            auto candidate = huiDir / result.controller;
            if (std::filesystem::exists(candidate)) {
                controllerPath = candidate;
            } else {
                controllerPath = ResolveScriptPath(result.controller);
            }
        }
        if (std::filesystem::exists(controllerPath)) {
            LoadScript(controllerPath.string());
        } else {
            std::cerr << "[display.ui] Controller script not found: " << result.controller << "\n";
        }
    }

    return screenHandle;
}

bool LuaScriptEngine::destroyHUI(uint32_t screenHandle) {
    auto instanceIt = m_ActiveHUIInstances.find(screenHandle);
    if (instanceIt == m_ActiveHUIInstances.end() || !instanceIt->second.alive) {
        return false;
    }

    HUIInstance& inst = instanceIt->second;
    inst.alive = false;

    // Remove from path lookup
    m_HUIPathToHandle.erase(inst.sourcePath);

    // Remove from the stack
    m_HUIStack.erase(
        std::remove(m_HUIStack.begin(), m_HUIStack.end(), screenHandle),
        m_HUIStack.end());

    // Clean up Lua callbacks for widgets belonging to this screen
    auto widgetMapIt = m_HUIWidgetMaps.find(screenHandle);
    if (widgetMapIt != m_HUIWidgetMaps.end()) {
#if LUA_AVAILABLE
        for (auto& [widgetId, elemHandle] : widgetMapIt->second) {
            auto clickIt = m_UIButtonClickCallbacks.find(elemHandle);
            if (clickIt != m_UIButtonClickCallbacks.end()) {
                if (m_State) luaL_unref(m_State, LUA_REGISTRYINDEX, clickIt->second);
                m_UIButtonClickCallbacks.erase(clickIt);
            }
            auto hoverIt = m_UIHoverCallbacks.find(elemHandle);
            if (hoverIt != m_UIHoverCallbacks.end()) {
                if (m_State) luaL_unref(m_State, LUA_REGISTRYINDEX, hoverIt->second);
                m_UIHoverCallbacks.erase(hoverIt);
            }
            // Remove element handle mappings
            auto* elemPtr = ResolveUIElement(elemHandle);
            if (elemPtr) {
                m_UIElementReverseHandles.erase(elemPtr);
            }
            m_UIElementHandles.erase(elemHandle);
        }
#endif
        m_HUIWidgetMaps.erase(widgetMapIt);
    }

    // Remove the screen from the UI system
    auto screen = ResolveUIScreen(screenHandle);
    if (screen && m_UISystem) {
        m_UISystem->RemoveScreen(screen);
    }
    m_UIScreenHandles.erase(screenHandle);

    m_ActiveHUIInstances.erase(instanceIt);

    std::cout << "[destroy.ui] Destroyed UI handle " << screenHandle << "\n";
    return true;
}

void LuaScriptEngine::destroyAllHUIInstances() {
    // Copy handles because destroyHUI mutates the map
    std::vector<uint32_t> handles;
    handles.reserve(m_ActiveHUIInstances.size());
    for (auto& [handle, inst] : m_ActiveHUIInstances) {
        if (inst.alive) handles.push_back(handle);
    }
    for (uint32_t h : handles) {
        destroyHUI(h);
    }
}

int LuaScriptEngine::Lua_Display_ui(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        lua_pushstring(L, "Script engine not available");
        return 2;
    }

    const char* path = luaL_checkstring(L, 1);
    if (!path) {
        lua_pushnil(L);
        lua_pushstring(L, "Path argument is required");
        return 2;
    }

    std::string error;
    uint32_t handle = engine->displayHUI(path, error);
    if (handle == 0) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        std::cerr << "[display.ui] Error: " << error << "\n";
        return 2;
    }

    lua_pushinteger(L, handle);
    lua_pushnil(L); // no error
    return 2;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Destroy_ui(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }

    // Accept either an integer handle or the string "current"
    // lua_isstring is true for numbers too (Lua will convert them to strings).
    // Managed display handles are numeric, so only branch on actual strings here.
    if (lua_type(L, 1) == LUA_TSTRING) {
        const char* target = lua_tostring(L, 1);
        if (std::string(target) == "current") {
            if (engine->m_HUIStack.empty()) {
                std::cerr << "[destroy.ui] No current UI to destroy\n";
                lua_pushboolean(L, false);
                return 1;
            }
            uint32_t currentHandle = engine->m_HUIStack.back();
            lua_pushboolean(L, engine->destroyHUI(currentHandle));
            return 1;
        }
        if (std::string(target) == "all") {
            engine->destroyAllHUIInstances();
            lua_pushboolean(L, true);
            return 1;
        }
        std::cerr << "[destroy.ui] Unknown target: '" << target
                  << "'. Use a handle from display.ui(), \"current\", or \"all\".\n";
        lua_pushboolean(L, false);
        return 1;
    }

    if (lua_isinteger(L, 1) || lua_isnumber(L, 1)) {
        uint32_t handle = static_cast<uint32_t>(lua_tointeger(L, 1));
        if (handle == 0) {
            std::cerr << "[destroy.ui] Invalid handle: 0\n";
            lua_pushboolean(L, false);
            return 1;
        }
        lua_pushboolean(L, engine->destroyHUI(handle));
        return 1;
    }

    if (lua_isnil(L, 1)) {
        std::cerr << "[destroy.ui] Cannot destroy nil. Pass a handle from display.ui() or \"current\".\n";
        lua_pushboolean(L, false);
        return 1;
    }

    luaL_error(L, "destroy.ui expects a handle (number), \"current\", or \"all\"");
    return 0;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Scene_Reload(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_SceneReloadCallback) {
        lua_pushboolean(L, false);
        return 1;
    }

    bool requested = false;
    try {
        requested = engine->m_SceneReloadCallback(std::string());
        if (requested) {
            engine->m_SceneReloadRequested = true;
        } else {
            engine->LogBindingError(nullptr,
                "Scene.reload rejected: current runtime scene is not reloadable or a reload is already pending");
        }
    } catch (const std::exception& exception) {
        engine->LogBindingError(nullptr, std::string("Scene.reload failed: ") + exception.what());
    } catch (...) {
        engine->LogBindingError(nullptr, "Scene.reload failed");
    }
    lua_pushboolean(L, requested);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Scene_Load(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    const std::string path = luaL_checkstring(L, 1);
    if (!engine || !engine->m_SceneReloadCallback || path.empty()) {
        lua_pushboolean(L, false);
        return 1;
    }
    bool requested = false;
    try {
        requested = engine->m_SceneReloadCallback(path);
        if (requested) {
            engine->m_SceneReloadRequested = true;
        } else {
            engine->LogBindingError(nullptr, "Scene.load('" + path + "') rejected: a scene change is already pending "
                                             "or scenes cannot be changed here");
        }
    } catch (const std::exception& exception) {
        engine->LogBindingError(nullptr, std::string("Scene.load failed: ") + exception.what());
    } catch (...) {
        engine->LogBindingError(nullptr, "Scene.load failed");
    }
    lua_pushboolean(L, requested);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

Hephaestus::Navigation::NavMesh* LuaScriptEngine::EnsureNavMesh() {
    if (!m_NavBuildAttempted && m_Scene) {
        m_NavBuildAttempted = true;
        int skipped = 0;
        auto geometry = Hephaestus::Navigation::CollectSceneGeometry(*m_Scene, &skipped);
        const Hephaestus::Navigation::NavBuildSettings settings;
        const int links = Hephaestus::Navigation::AddVaultLinks(*m_Scene, settings, geometry);
        auto mesh = std::make_unique<Hephaestus::Navigation::NavMesh>();
        std::string error;
        if (mesh->Build(geometry, settings, error)) {
            Hephaestus::EngineLog::Write(
                Hephaestus::EngineLogLevel::Info,
                "Navmesh built: " + std::to_string(mesh->PolygonCount()) + " polygons from " +
                    std::to_string(geometry.triangles.size() / 3) + " triangles, " + std::to_string(links) +
                    " vault links" +
                    (skipped ? ", " + std::to_string(skipped) + " non-box static shapes left out" : ""),
                "Navigation");
            m_NavMesh = std::move(mesh);
        } else {
            Hephaestus::EngineLog::Write(Hephaestus::EngineLogLevel::Warning, "Navmesh not built: " + error,
                                         "Navigation");
        }
    }
    return m_NavMesh.get();
}

void LuaScriptEngine::RegisterNavBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);
    lua_pushcfunction(m_State, Lua_Nav_isReady);
    lua_setfield(m_State, -2, "isReady");
    lua_pushcfunction(m_State, Lua_Nav_rebuild);
    lua_setfield(m_State, -2, "rebuild");
    lua_pushcfunction(m_State, Lua_Nav_findPath);
    lua_setfield(m_State, -2, "findPath");
    lua_pushcfunction(m_State, Lua_Nav_closestPoint);
    lua_setfield(m_State, -2, "closestPoint");
    lua_pushcfunction(m_State, Lua_Nav_randomPoint);
    lua_setfield(m_State, -2, "randomPoint");
    lua_setglobal(m_State, "Nav");
#endif
}

bool LuaScriptEngine::ReadNavPoint(lua_State* L, int index, Vec3& out) {
#if LUA_AVAILABLE
    if (!lua_istable(L, index)) {
        return false;
    }
    const PropertyValue value = ParsePropertyValueFromLua(L, index);
    if (!value.IsVec3()) {
        return false;
    }
    out = *value.AsVec3();
    return true;
#else
    (void)L;
    (void)index;
    (void)out;
    return false;
#endif
}

int LuaScriptEngine::Lua_Nav_isReady(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    lua_pushboolean(L, engine && engine->EnsureNavMesh() != nullptr);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Nav_rebuild(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }
    engine->m_NavMesh.reset();
    engine->m_NavBuildAttempted = false;
    lua_pushboolean(L, engine->EnsureNavMesh() != nullptr);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Nav_findPath(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    Vec3 from;
    Vec3 to;
    auto* mesh = engine ? engine->EnsureNavMesh() : nullptr;
    std::vector<Vec3> corners;
    std::vector<unsigned char> linkStarts;
    if (!mesh || !engine->ReadNavPoint(L, 1, from) || !engine->ReadNavPoint(L, 2, to) ||
        !mesh->FindPath(from, to, corners, &linkStarts)) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, static_cast<int>(corners.size()), 0);
    for (size_t i = 0; i < corners.size(); ++i) {
        PushVec3Table(L, corners[i]);
        if (linkStarts[i]) {
            lua_pushboolean(L, 1);
            lua_setfield(L, -2, "link");
        }
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Nav_closestPoint(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    Vec3 point;
    Vec3 out;
    Vec3 extents(2.0f, 4.0f, 2.0f);
    auto* mesh = engine ? engine->EnsureNavMesh() : nullptr;
    if (mesh && lua_gettop(L) >= 2) {
        engine->ReadNavPoint(L, 2, extents);
    }
    if (!mesh || !engine->ReadNavPoint(L, 1, point) || !mesh->ClosestPoint(point, out, extents)) {
        lua_pushnil(L);
        return 1;
    }
    PushVec3Table(L, out);
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Nav_randomPoint(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    Vec3 center;
    Vec3 out;
    auto* mesh = engine ? engine->EnsureNavMesh() : nullptr;
    const float radius = static_cast<float>(luaL_optnumber(L, 2, 5.0));
    if (!mesh || !engine->ReadNavPoint(L, 1, center) ||
        !mesh->RandomPointAround(center, radius, engine->m_NavRandomState, out)) {
        lua_pushnil(L);
        return 1;
    }
    PushVec3Table(L, out);
    return 1;
#else
    (void)L;
    return 0;
#endif
}
