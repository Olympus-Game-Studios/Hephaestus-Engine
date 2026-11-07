#pragma once

#include "../Core/PropertyValue.h"
#include "../PluginAPI/ScriptLanguageAPI.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class LuaScriptEngine;
class Scene;

namespace Hephaestus {
class InputSystem;
}
namespace Physics {
struct PhysicsContactEvent;
}

/**
 * Runs scripts written in plugin languages (see ScriptLanguageAPI.h) next to
 * the built-in Lua engine. A script component whose file extension a plugin
 * language claims is run here; every other script stays with Lua.
 *
 * Scripts are created lazily on the first Update of a play session, reloaded
 * when their file changes, and torn down by OnSimulationStopped.
 */
class PluginScriptHost {
public:
    PluginScriptHost();
    ~PluginScriptHost();
    PluginScriptHost(const PluginScriptHost&) = delete;
    PluginScriptHost& operator=(const PluginScriptHost&) = delete;

    /// Lua resolves script paths and receives cross-language calls.
    void SetLuaEngine(LuaScriptEngine* lua) { m_Lua = lua; }
    void SetInputSystem(Hephaestus::InputSystem* input) { m_Input = input; }

    void Update(Scene& scene, float deltaTime);
    void LateUpdate(Scene& scene, float deltaTime);
    void DispatchContactEvents(Scene& scene, const std::vector<Physics::PhysicsContactEvent>& events);

    /// Destroys every script and language runtime. The next Update starts fresh.
    void OnSimulationStopped();
    void RequestReloadAll();

    /// Calls a function on the plugin-language script attached to an entity.
    /// Returns true when a script handled it.
    bool CallEntityFunction(uint32_t entityId, const std::string& functionName,
                            const std::vector<PropertyValue>& args);

    std::size_t InstanceCount() const { return m_Instances.size(); }

private:
    struct HostApi;

    struct Runtime {
        const HPScriptLanguage* language = nullptr;
        void* handle = nullptr;
    };
    struct Instance {
        const HPScriptLanguage* language = nullptr;
        void* runtime = nullptr;
        void* handle = nullptr;
        std::string scriptPath;
        std::filesystem::file_time_type writeTime{};
        bool awake = false;
    };
    struct FailedScript {
        std::string scriptPath;
        std::filesystem::file_time_type writeTime{};
    };

    void* RuntimeFor(const HPScriptLanguage* language);
    void DestroyInstance(uint32_t entityId);
    void Log(HPLogLevel level, const std::string& message) const;
    bool ForwardCall(uint32_t entityId, const char* functionName, const HPValue* args, uint32_t argCount);

    static void HostLog(void* host, uint32_t level, const char* message);
    static int32_t HostIsEntityValid(void* host, HPEntityId entity);
    static HPEntityId HostFindEntity(void* host, const char* name);
    static size_t HostGetEntityName(void* host, HPEntityId entity, char* buffer, size_t bufferSize);
    static int32_t HostGetPosition(void* host, HPEntityId entity, HPVec3* out);
    static int32_t HostSetPosition(void* host, HPEntityId entity, HPVec3 value);
    static int32_t HostGetRotation(void* host, HPEntityId entity, HPVec3* out);
    static int32_t HostSetRotation(void* host, HPEntityId entity, HPVec3 value);
    static int32_t HostGetScale(void* host, HPEntityId entity, HPVec3* out);
    static int32_t HostSetScale(void* host, HPEntityId entity, HPVec3 value);
    static int32_t HostIsActionDown(void* host, const char* action);
    static int32_t HostIsActionPressed(void* host, const char* action);
    static int32_t HostIsActionReleased(void* host, const char* action);
    static float HostGetAxis(void* host, const char* axis);
    static int32_t HostCallEntityFunction(void* host, HPEntityId entity, const char* function,
                                          const HPValue* args, uint32_t argCount);
    static size_t HostGetProjectRoot(void* host, char* buffer, size_t bufferSize);

    HPScriptHostAPI m_Api{};
    LuaScriptEngine* m_Lua = nullptr;
    Hephaestus::InputSystem* m_Input = nullptr;
    Scene* m_Scene = nullptr;
    float m_DeltaTime = 0.0f;
    std::shared_ptr<bool> m_Alive = std::make_shared<bool>(true);
    std::vector<Runtime> m_Runtimes;
    std::unordered_map<uint32_t, Instance> m_Instances;
    std::unordered_map<uint32_t, FailedScript> m_Failed;
    std::vector<uint32_t> m_UpdateOrder;
};
