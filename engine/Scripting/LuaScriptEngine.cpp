#include "LuaScriptEngine.h"
#include "LuaScriptComponent.h"
#include "../Audio/AudioEngine.h"
#include "../UI/HephaestusUI/UIContext.h"
#include "../UI/HephaestusUI/UICanvas.h"
#include "../UI/HephaestusUI/UIElement.h"
#include "../UI/HephaestusUI/UIHorizontalBox.h"
#include "../UI/HephaestusUI/UIImage.h"
#include "../UI/HephaestusUI/UIPanel.h"
#include "../UI/HephaestusUI/UIRenderer.h"
#include "../UI/HephaestusUI/UIScreen.h"
#include "../UI/HephaestusUI/UISimpleMenuExample.h"
#include "../UI/HephaestusUI/UISystem.h"
#include "../UI/HephaestusUI/UIText.h"
#include "../UI/HephaestusUI/UIVerticalBox.h"
#include "../UI/HephaestusUI/UIButton.h"
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
#include <functional>

#include <GLFW/glfw3.h>

#ifndef LUA_AVAILABLE
    #define LUA_AVAILABLE 0
#endif

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

        if (!component.enabled || (component.scriptPath.empty() && component.scriptSource.empty())) {
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

    // Restore cursor mode to normal
    if (m_CurrentCursorMode != 0) {
        m_RuntimeInput.cursorModeRequest = 0;
        if (m_Window) {
            glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
        m_CurrentCursorMode = 0;
        m_HasLastMousePos = false;
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
    // Drop all cached instances so the next Play start always rebuilds from scene data.
    m_ScriptInstances.clear();
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

void LuaScriptEngine::SetRuntimeInputState(const RuntimeInputState& state) {
    m_RuntimeInput = state;
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
    // Release Lua callback references before clearing
#if LUA_AVAILABLE
    if (m_State) {
        for (const auto& [handle, ref] : m_UIButtonClickCallbacks) {
            luaL_unref(m_State, LUA_REGISTRYINDEX, ref);
        }
    }
#endif
    m_UIButtonClickCallbacks.clear();
    m_UIScreenHandles.clear();
    m_UIElementHandles.clear();
    m_UIElementReverseHandles.clear();
    m_NextUIScreenHandle = 1;
    m_NextUIElementHandle = 1;
}

void LuaScriptEngine::RegisterBindings() {
#if LUA_AVAILABLE
    RegisterEntityBindings();
    RegisterTransformBindings();
    RegisterInputBindings();
    RegisterUtilityBindings();
    RegisterApplicationBindings();
    RegisterCameraBindings();
    RegisterPhysicsBindings();
    RegisterAudioBindings();
    RegisterUIBindings();
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

    lua_pushcfunction(m_State, Lua_Input_setCursorMode);
    lua_setfield(m_State, -2, "setCursorMode");

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

void LuaScriptEngine::RegisterApplicationBindings() {
#if LUA_AVAILABLE
    lua_newtable(m_State);

    lua_pushcfunction(m_State, Lua_Application_quit);
    lua_setfield(m_State, -2, "quit");

    lua_setglobal(m_State, "Application");
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
    // Deployed game paths (scripts next to executable)
    add(current / "scripts");
    add(current / "assets" / "scripts");
    add(current / "assets");

    if (!m_ProjectRoot.empty()) {
        add(m_ProjectRoot);
        add(m_ProjectRoot / "assets");
        add(m_ProjectRoot / "assets" / "scripts");
        add(m_ProjectRoot / "scripts");
    }

    // Source tree paths (dev builds only)
    add(current / "engine" / "Game" / "scripts");
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

    if (component.scriptPath.empty() && component.scriptSource.empty()) {
        return nullptr;
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
    }

    if (hasEmbeddedSource) {
        bool needsReload = !instance.valid || sourceChanged;
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
        }
        return &instance;
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

    if (luaL_loadbuffer(m_State, code.c_str(), code.size(), debugName.c_str()) != LUA_OK) {
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
    if (!engine) {
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

    // Prefer RuntimeInputState (works in both GLFW and Qt paths)
    if (engine->m_RuntimeInput.valid) {
        bool pressed = (key >= 0 && key < RuntimeInputState::kMaxKeys)
                       ? engine->m_RuntimeInput.keys[key] : false;
        lua_pushboolean(L, pressed);
        return 1;
    }

    // Fallback: GLFW direct query
    if (engine->m_Window) {
        int state = glfwGetKey(engine->m_Window, key);
        lua_pushboolean(L, state == GLFW_PRESS || state == GLFW_REPEAT);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_isMouseButtonPressed(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushboolean(L, false);
        return 1;
    }

    int button = TranslateMouseButton(L, 1);
    if (button < GLFW_MOUSE_BUTTON_1 || button > GLFW_MOUSE_BUTTON_LAST) {
        lua_pushboolean(L, false);
        return 1;
    }

    // Prefer RuntimeInputState
    if (engine->m_RuntimeInput.valid) {
        bool pressed = (button >= 0 && button < RuntimeInputState::kMaxMouseButtons)
                       ? engine->m_RuntimeInput.mouseButtons[button] : false;
        lua_pushboolean(L, pressed);
        return 1;
    }

    // Fallback: GLFW direct query
    if (engine->m_Window) {
        int state = glfwGetMouseButton(engine->m_Window, button);
        lua_pushboolean(L, state == GLFW_PRESS);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
#else
    (void)L;
    return 0;
#endif
}

int LuaScriptEngine::Lua_Input_getMousePosition(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_pushnil(L);
        return 1;
    }

    double x = 0.0;
    double y = 0.0;

    // Prefer RuntimeInputState
    if (engine->m_RuntimeInput.valid) {
        x = engine->m_RuntimeInput.mouseX;
        y = engine->m_RuntimeInput.mouseY;
    } else if (engine->m_Window) {
        glfwGetCursorPos(engine->m_Window, &x, &y);
    } else {
        lua_pushnil(L);
        return 1;
    }

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

int LuaScriptEngine::Lua_Input_getMouseDelta(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine) {
        lua_newtable(L);
        lua_pushnumber(L, 0.0);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, 0.0);
        lua_setfield(L, -2, "y");
        return 1;
    }

    double dx = 0.0;
    double dy = 0.0;

    // RuntimeInputState path (Qt editor): host provides deltas
    if (engine->m_RuntimeInput.valid) {
        dx = engine->m_RuntimeInput.mouseDeltaX;
        dy = engine->m_RuntimeInput.mouseDeltaY;
    } else if (engine->m_Window) {
        // GLFW path: compute deltas from absolute position
        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(engine->m_Window, &mx, &my);
        if (engine->m_HasLastMousePos) {
            dx = mx - engine->m_LastMouseX;
            dy = my - engine->m_LastMouseY;
        }
        engine->m_LastMouseX = mx;
        engine->m_LastMouseY = my;
        engine->m_HasLastMousePos = true;
    }

    lua_newtable(L);
    lua_pushnumber(L, dx);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, dy);
    lua_setfield(L, -2, "y");
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
    // (covers calls made during onAwake before the first populateRuntimeInputState).
    engine->m_RuntimeInput.cursorModeRequest = newMode;

    // GLFW runtime: apply immediately since there is no host tick to consume the request
    if (engine->m_Window) {
        int glfwMode = (newMode == 1) ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
        glfwSetInputMode(engine->m_Window, GLFW_CURSOR, glfwMode);

        if (newMode == 1 && glfwRawMouseMotionSupported()) {
            glfwSetInputMode(engine->m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        } else if (newMode == 0) {
            glfwSetInputMode(engine->m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }

        engine->m_HasLastMousePos = false;
    }

    engine->m_CurrentCursorMode = newMode;
    return 0;
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
    
    lua_pushcfunction(m_State, Lua_This_GetCamera);
    lua_setfield(m_State, -2, "GetCamera");
    
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
// ═══════════════════════════════════════════════════════════════
// Camera Component Bindings (for CameraActor support)
// ═══════════════════════════════════════════════════════════════

int LuaScriptEngine::Lua_This_GetCamera(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->CurrentEntityValid()) {
        lua_pushnil(L);
        return 1;
    }
    
    Entity entity = engine->MakeEntity(engine->m_CurrentEntityId);
    if (!entity || !entity.HasComponent<CameraComponent>()) {
        lua_pushnil(L);
        return 1;
    }
    
    // Create camera userdata containing the entity ID
    uint32_t* pEntityId = static_cast<uint32_t*>(lua_newuserdata(L, sizeof(uint32_t)));
    *pEntityId = engine->m_CurrentEntityId;
    
    // Set its metatable to "HephaestusCamera"
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
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusCamera"));
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
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusCamera"));
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
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusCamera"));
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
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusCamera"));
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
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusCamera"));
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
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusCamera"));
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
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusCamera"));
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
    
    uint32_t* pEntityId = static_cast<uint32_t*>(luaL_checkudata(L, 1, "HephaestusCamera"));
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
        uint32_t* pEntityId = static_cast<uint32_t*>(lua_newuserdata(L, sizeof(uint32_t)));
        *pEntityId = static_cast<uint32_t>(entityHandle);
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
        uint32_t* pEntityId = static_cast<uint32_t*>(lua_newuserdata(L, sizeof(uint32_t)));
        *pEntityId = static_cast<uint32_t>(entityHandle);
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
            uint32_t* pEntityId = static_cast<uint32_t*>(lua_newuserdata(L, sizeof(uint32_t)));
            *pEntityId = static_cast<uint32_t>(entityHandle);
            luaL_getmetatable(L, "HephaestusEntity");
            lua_setmetatable(L, -2);
            return 1;
        }
    }
    
    // Second pass: return first camera if none active
    for (auto entityHandle : cameraView) {
        uint32_t* pEntityId = static_cast<uint32_t*>(lua_newuserdata(L, sizeof(uint32_t)));
        *pEntityId = static_cast<uint32_t>(entityHandle);
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
        uint32_t* pEntityId = static_cast<uint32_t*>(lua_touserdata(L, 1));
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
    bool loop = false;
    float volume = 1.0f;
    if (lua_gettop(L) >= 2) {
        loop = lua_toboolean(L, 2) != 0;
    }
    if (lua_gettop(L) >= 3) {
        volume = static_cast<float>(luaL_checknumber(L, 3));
    }

    AudioEngine::SoundHandle handle = engine->m_AudioEngine->Play(path, loop, volume);
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

    lua_setglobal(m_State, "UI");

    // CursorMode enum constants
    lua_newtable(m_State);
    lua_pushstring(m_State, "game");
    lua_setfield(m_State, -2, "Game");
    lua_pushstring(m_State, "ui");
    lua_setfield(m_State, -2, "UI");
    lua_setglobal(m_State, "CursorMode");
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
            screen->SetViewportSize(engine->m_UIContext->GetViewportSize());
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
            screen->SetViewportSize(engine->m_UIContext->GetViewportSize());
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

    const char* screenName = luaL_checkstring(L, 1);
    const bool visible = lua_toboolean(L, 2) != 0;
    const std::shared_ptr<Hephaestus::HephaestusUI::UIScreen> screen =
        engine->m_UISystem->FindScreenByName(screenName);
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

int LuaScriptEngine::Lua_UI_setScreenActive(lua_State* L) {
#if LUA_AVAILABLE
    LuaScriptEngine* engine = GetInstance();
    if (!engine || !engine->m_UISystem) {
        lua_pushboolean(L, false);
        return 1;
    }

    const char* screenName = luaL_checkstring(L, 1);
    const bool active = lua_toboolean(L, 2) != 0;
    const std::shared_ptr<Hephaestus::HephaestusUI::UIScreen> screen =
        engine->m_UISystem->FindScreenByName(screenName);
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
            screen->SetViewportSize(size);
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
    // (covers calls made during onAwake before the first populateRuntimeInputState).
    const int legacyMode = (mode == "ui") ? 0 : 1;
    engine->m_RuntimeInput.cursorModeRequest = legacyMode;

    // GLFW runtime: apply immediately since there is no host tick to consume the request
    if (engine->m_Window) {
        int glfwMode = (legacyMode == 1) ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
        glfwSetInputMode(engine->m_Window, GLFW_CURSOR, glfwMode);
        if (legacyMode == 1 && glfwRawMouseMotionSupported()) {
            glfwSetInputMode(engine->m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        } else if (legacyMode == 0) {
            glfwSetInputMode(engine->m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
        engine->m_HasLastMousePos = false;
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