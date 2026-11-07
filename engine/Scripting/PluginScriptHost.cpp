#include "PluginScriptHost.h"

#include "LuaScriptComponent.h"
#include "LuaScriptEngine.h"
#include "ScriptLanguageRegistry.h"
#include "../Core/EngineLog.h"
#include "../Input/InputSystem.h"
#include "../Physics/PhysicsContactEvents.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"

#include <cstring>

namespace {

PluginScriptHost& Self(void* host) {
    return *static_cast<PluginScriptHost*>(host);
}

uint32_t ToEntityId(HPEntityId entity) {
    return static_cast<uint32_t>(entity - 1);
}

HPEntityId ToHandle(uint32_t entityId) {
    return static_cast<HPEntityId>(entityId) + 1;
}

HPValue ToHPValue(const PropertyValue& value) {
    HPValue out{};
    if (const bool* b = value.AsBool()) {
        out.type = HP_VALUE_BOOL;
        out.boolean = *b ? 1 : 0;
    } else if (const double* n = value.AsNumber()) {
        out.type = HP_VALUE_NUMBER;
        out.number = *n;
    } else if (const std::string* s = value.AsString()) {
        out.type = HP_VALUE_STRING;
        out.string = s->c_str();
    } else if (const Vec2* v2 = value.AsVec2()) {
        out.type = HP_VALUE_VEC2;
        out.vector[0] = v2->x;
        out.vector[1] = v2->y;
    } else if (const Vec3* v3 = value.AsVec3()) {
        out.type = HP_VALUE_VEC3;
        out.vector[0] = v3->x;
        out.vector[1] = v3->y;
        out.vector[2] = v3->z;
    } else if (const Vec4* v4 = value.AsVec4()) {
        out.type = HP_VALUE_VEC4;
        out.vector[0] = v4->x;
        out.vector[1] = v4->y;
        out.vector[2] = v4->z;
        out.vector[3] = v4->w;
    }
    return out;
}

PropertyValue ToPropertyValue(const HPValue& value) {
    switch (value.type) {
        case HP_VALUE_BOOL: return PropertyValue(value.boolean != 0);
        case HP_VALUE_NUMBER: return PropertyValue(value.number);
        case HP_VALUE_STRING: return PropertyValue(std::string(value.string ? value.string : ""));
        case HP_VALUE_VEC2: return PropertyValue(Vec2(value.vector[0], value.vector[1]));
        case HP_VALUE_VEC3: return PropertyValue(Vec3(value.vector[0], value.vector[1], value.vector[2]));
        case HP_VALUE_VEC4:
            return PropertyValue(Vec4(value.vector[0], value.vector[1], value.vector[2], value.vector[3]));
        default: return {};
    }
}

size_t CopyOut(const std::string& text, char* buffer, size_t bufferSize) {
    const size_t needed = text.size() + 1;
    if (buffer && bufferSize >= needed) {
        std::memcpy(buffer, text.c_str(), needed);
    }
    return needed;
}

HPVec3 ToHPVec3(const Vec3& value) {
    return HPVec3{value.x, value.y, value.z};
}

std::filesystem::file_time_type WriteTime(const std::filesystem::path& path) {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    return ec ? std::filesystem::file_time_type{} : time;
}

} // namespace

PluginScriptHost::PluginScriptHost() {
    m_Api.structSize = sizeof(HPScriptHostAPI);
    m_Api.apiVersion = HP_SCRIPT_LANGUAGE_API_VERSION;
    m_Api.host = this;
    m_Api.log = &HostLog;
    m_Api.isEntityValid = &HostIsEntityValid;
    m_Api.findEntity = &HostFindEntity;
    m_Api.getEntityName = &HostGetEntityName;
    m_Api.getPosition = &HostGetPosition;
    m_Api.setPosition = &HostSetPosition;
    m_Api.getRotation = &HostGetRotation;
    m_Api.setRotation = &HostSetRotation;
    m_Api.getScale = &HostGetScale;
    m_Api.setScale = &HostSetScale;
    m_Api.isActionDown = &HostIsActionDown;
    m_Api.isActionPressed = &HostIsActionPressed;
    m_Api.isActionReleased = &HostIsActionReleased;
    m_Api.getAxis = &HostGetAxis;
    m_Api.callEntityFunction = &HostCallEntityFunction;
    m_Api.getProjectRoot = &HostGetProjectRoot;
}

PluginScriptHost::~PluginScriptHost() {
    OnSimulationStopped();
}

void PluginScriptHost::Log(HPLogLevel level, const std::string& message) const {
    const auto engineLevel = level == HP_LOG_ERROR ? Hephaestus::EngineLogLevel::Error
        : level == HP_LOG_WARNING ? Hephaestus::EngineLogLevel::Warning
        : Hephaestus::EngineLogLevel::Info;
    Hephaestus::EngineLog::Write(engineLevel, message, "Script");
}

void* PluginScriptHost::RuntimeFor(const HPScriptLanguage* language) {
    for (const Runtime& runtime : m_Runtimes) {
        if (runtime.language == language) {
            return runtime.handle;
        }
    }
    // Failures are remembered too, so a broken runtime is reported once per session.
    Runtime& runtime = m_Runtimes.emplace_back();
    runtime.language = language;
    runtime.handle = language->createRuntime(&m_Api);
    if (!runtime.handle) {
        Log(HP_LOG_ERROR, std::string(language->displayName) + " scripting runtime failed to start");
    }
    return runtime.handle;
}

void PluginScriptHost::DestroyInstance(uint32_t entityId) {
    const auto it = m_Instances.find(entityId);
    if (it == m_Instances.end()) {
        return;
    }
    const Instance instance = it->second;
    m_Instances.erase(it);
    instance.language->destroyInstance(instance.runtime, instance.handle);
}

void PluginScriptHost::Update(Scene& scene, float deltaTime) {
    m_Scene = &scene;
    m_UpdateOrder.clear();
    auto& registry = scene.GetRegistry();
    const ScriptLanguageRegistry& languages = ScriptLanguageRegistry::Instance();

    std::vector<uint32_t> stale;
    for (const auto& [entityId, instance] : m_Instances) {
        const auto handle = static_cast<entt::entity>(entityId);
        const LuaScriptComponent* component = registry.valid(handle)
            ? registry.try_get<LuaScriptComponent>(handle) : nullptr;
        if (!component || !component->enabled || component->scriptPath != instance.scriptPath) {
            stale.push_back(entityId);
        }
    }
    for (uint32_t entityId : stale) {
        DestroyInstance(entityId);
    }
    if (languages.Languages().empty()) {
        return;
    }

    auto view = registry.view<LuaScriptComponent>();
    std::vector<entt::entity> entities(view.begin(), view.end());
    for (entt::entity handle : entities) {
        const auto& component = registry.get<LuaScriptComponent>(handle);
        if (!component.enabled || component.scriptPath.empty()) {
            continue;
        }
        const HPScriptLanguage* language = languages.FindForPath(component.scriptPath);
        if (!language) {
            continue;
        }
        const uint32_t entityId = static_cast<uint32_t>(handle);
        const std::filesystem::path resolved = m_Lua
            ? m_Lua->ResolveScriptPath(component.scriptPath)
            : std::filesystem::path(component.scriptPath);
        const auto writeTime = WriteTime(resolved);

        auto existing = m_Instances.find(entityId);
        if (existing != m_Instances.end() && existing->second.writeTime != writeTime) {
            DestroyInstance(entityId);
            existing = m_Instances.end();
        }
        if (existing == m_Instances.end()) {
            const auto failed = m_Failed.find(entityId);
            if (failed != m_Failed.end() && failed->second.scriptPath == component.scriptPath &&
                failed->second.writeTime == writeTime) {
                continue;
            }
            void* runtime = RuntimeFor(language);
            if (!runtime) {
                continue;
            }
            std::vector<HPNamedValue> variables;
            variables.reserve(component.variables.size());
            for (const auto& [name, value] : component.variables) {
                variables.push_back(HPNamedValue{name.c_str(), ToHPValue(value)});
            }
            const std::string resolvedText = resolved.string();
            HPScriptInstanceInfo info{};
            info.entity = ToHandle(entityId);
            info.scriptPath = component.scriptPath.c_str();
            info.resolvedPath = resolvedText.c_str();
            info.variables = variables.data();
            info.variableCount = static_cast<uint32_t>(variables.size());
            char error[1024] = {};
            void* handleOut = language->createInstance(runtime, &info, error, sizeof(error));
            if (!handleOut) {
                m_Failed[entityId] = FailedScript{component.scriptPath, writeTime};
                Log(HP_LOG_ERROR, "[" + std::string(language->displayName) + "] " + component.scriptPath +
                    ": " + (error[0] ? error : "script failed to load"));
                continue;
            }
            m_Failed.erase(entityId);
            m_Instances[entityId] = Instance{language, runtime, handleOut, component.scriptPath, writeTime, false};
        }
        m_UpdateOrder.push_back(entityId);
    }

    for (uint32_t entityId : m_UpdateOrder) {
        auto it = m_Instances.find(entityId);
        if (it == m_Instances.end() || it->second.awake) {
            continue;
        }
        it->second.awake = true;
        if (it->second.language->awake) {
            it->second.language->awake(it->second.runtime, it->second.handle);
        }
    }
    for (uint32_t entityId : m_UpdateOrder) {
        auto it = m_Instances.find(entityId);
        if (it != m_Instances.end() && it->second.language->update) {
            it->second.language->update(it->second.runtime, it->second.handle, deltaTime);
        }
    }
}

void PluginScriptHost::LateUpdate(Scene& scene, float deltaTime) {
    m_Scene = &scene;
    for (uint32_t entityId : m_UpdateOrder) {
        auto it = m_Instances.find(entityId);
        if (it != m_Instances.end() && it->second.language->lateUpdate) {
            it->second.language->lateUpdate(it->second.runtime, it->second.handle, deltaTime);
        }
    }
}

void PluginScriptHost::DispatchContactEvents(Scene& scene,
                                             const std::vector<Physics::PhysicsContactEvent>& events) {
    m_Scene = &scene;
    for (const Physics::PhysicsContactEvent& event : events) {
        auto it = m_Instances.find(event.receiver);
        if (it == m_Instances.end() || !it->second.language->contact) {
            continue;
        }
        HPContactEvent contact{};
        contact.kind = static_cast<uint32_t>(event.kind);
        contact.other = event.otherAlive ? ToHandle(event.other) : 0;
        contact.hasContact = event.hasContact ? 1 : 0;
        contact.point = HPVec3{event.pointX, event.pointY, event.pointZ};
        contact.normal = HPVec3{event.normalX, event.normalY, event.normalZ};
        contact.relativeVelocity = HPVec3{event.relativeVelocityX, event.relativeVelocityY, event.relativeVelocityZ};
        contact.penetration = event.penetration;
        it->second.language->contact(it->second.runtime, it->second.handle, &contact);
    }
}

void PluginScriptHost::OnSimulationStopped() {
    while (!m_Instances.empty()) {
        DestroyInstance(m_Instances.begin()->first);
    }
    for (Runtime& runtime : m_Runtimes) {
        if (runtime.handle) {
            runtime.language->destroyRuntime(runtime.handle);
        }
    }
    m_Runtimes.clear();
    m_Failed.clear();
    m_UpdateOrder.clear();
}

void PluginScriptHost::RequestReloadAll() {
    while (!m_Instances.empty()) {
        DestroyInstance(m_Instances.begin()->first);
    }
    m_Failed.clear();
}

bool PluginScriptHost::CallEntityFunction(uint32_t entityId, const std::string& functionName,
                                          const std::vector<PropertyValue>& args) {
    auto it = m_Instances.find(entityId);
    if (it == m_Instances.end() || !it->second.language->call) {
        return false;
    }
    std::vector<HPValue> values;
    values.reserve(args.size());
    for (const PropertyValue& arg : args) {
        values.push_back(ToHPValue(arg));
    }
    return it->second.language->call(it->second.runtime, it->second.handle,
                                     functionName.c_str(), values.data(),
                                     static_cast<uint32_t>(values.size())) != 0;
}

bool PluginScriptHost::ForwardCall(uint32_t entityId, const char* functionName,
                                   const HPValue* args, uint32_t argCount) {
    if (!functionName || !*functionName) {
        return false;
    }
    std::vector<PropertyValue> values;
    values.reserve(argCount);
    for (uint32_t i = 0; i < argCount; ++i) {
        values.push_back(ToPropertyValue(args[i]));
    }
    if (CallEntityFunction(entityId, functionName, values)) {
        return true;
    }
    if (!m_Lua || !m_Scene || !m_Scene->GetRegistry().valid(static_cast<entt::entity>(entityId))) {
        return false;
    }
    return m_Lua->CallEntityFunction(functionName, Entity(static_cast<entt::entity>(entityId), m_Scene), values);
}

// ── Host API ────────────────────────────────────────────────────────────────

namespace {

TransformComponent* TransformOf(Scene* scene, HPEntityId entity) {
    if (!scene || entity == 0) {
        return nullptr;
    }
    auto& registry = scene->GetRegistry();
    const auto handle = static_cast<entt::entity>(ToEntityId(entity));
    return registry.valid(handle) ? registry.try_get<TransformComponent>(handle) : nullptr;
}

} // namespace

void PluginScriptHost::HostLog(void* host, uint32_t level, const char* message) {
    Self(host).Log(static_cast<HPLogLevel>(level), message ? message : "");
}

int32_t PluginScriptHost::HostIsEntityValid(void* host, HPEntityId entity) {
    Scene* scene = Self(host).m_Scene;
    return scene && entity != 0 && scene->GetRegistry().valid(static_cast<entt::entity>(ToEntityId(entity)));
}

HPEntityId PluginScriptHost::HostFindEntity(void* host, const char* name) {
    Scene* scene = Self(host).m_Scene;
    if (!scene || !name) {
        return 0;
    }
    Entity entity = scene->FindEntityByName(name);
    return entity ? ToHandle(static_cast<uint32_t>(static_cast<entt::entity>(entity))) : 0;
}

size_t PluginScriptHost::HostGetEntityName(void* host, HPEntityId entity, char* buffer, size_t bufferSize) {
    Scene* scene = Self(host).m_Scene;
    if (!scene || entity == 0) {
        return 0;
    }
    auto& registry = scene->GetRegistry();
    const auto handle = static_cast<entt::entity>(ToEntityId(entity));
    const TagComponent* tag = registry.valid(handle) ? registry.try_get<TagComponent>(handle) : nullptr;
    if (!tag) {
        return 0;
    }
    return CopyOut(tag->tag, buffer, bufferSize);
}

int32_t PluginScriptHost::HostGetPosition(void* host, HPEntityId entity, HPVec3* out) {
    TransformComponent* transform = TransformOf(Self(host).m_Scene, entity);
    if (!transform || !out) return 0;
    *out = ToHPVec3(transform->position);
    return 1;
}

int32_t PluginScriptHost::HostSetPosition(void* host, HPEntityId entity, HPVec3 value) {
    TransformComponent* transform = TransformOf(Self(host).m_Scene, entity);
    if (!transform) return 0;
    transform->position = Vec3(value.x, value.y, value.z);
    return 1;
}

int32_t PluginScriptHost::HostGetRotation(void* host, HPEntityId entity, HPVec3* out) {
    TransformComponent* transform = TransformOf(Self(host).m_Scene, entity);
    if (!transform || !out) return 0;
    *out = ToHPVec3(transform->rotation);
    return 1;
}

int32_t PluginScriptHost::HostSetRotation(void* host, HPEntityId entity, HPVec3 value) {
    TransformComponent* transform = TransformOf(Self(host).m_Scene, entity);
    if (!transform) return 0;
    transform->rotation = Vec3(value.x, value.y, value.z);
    return 1;
}

int32_t PluginScriptHost::HostGetScale(void* host, HPEntityId entity, HPVec3* out) {
    TransformComponent* transform = TransformOf(Self(host).m_Scene, entity);
    if (!transform || !out) return 0;
    *out = ToHPVec3(transform->scale);
    return 1;
}

int32_t PluginScriptHost::HostSetScale(void* host, HPEntityId entity, HPVec3 value) {
    TransformComponent* transform = TransformOf(Self(host).m_Scene, entity);
    if (!transform) return 0;
    transform->scale = Vec3(value.x, value.y, value.z);
    return 1;
}

int32_t PluginScriptHost::HostIsActionDown(void* host, const char* action) {
    const auto* input = Self(host).m_Input;
    return input && action && input->IsActionDown(action);
}

int32_t PluginScriptHost::HostIsActionPressed(void* host, const char* action) {
    const auto* input = Self(host).m_Input;
    return input && action && input->IsActionPressed(action);
}

int32_t PluginScriptHost::HostIsActionReleased(void* host, const char* action) {
    const auto* input = Self(host).m_Input;
    return input && action && input->IsActionReleased(action);
}

float PluginScriptHost::HostGetAxis(void* host, const char* axis) {
    const auto* input = Self(host).m_Input;
    return input && axis ? input->GetAxis(axis) : 0.0f;
}

int32_t PluginScriptHost::HostCallEntityFunction(void* host, HPEntityId entity, const char* function,
                                                 const HPValue* args, uint32_t argCount) {
    if (entity == 0) {
        return 0;
    }
    return Self(host).ForwardCall(ToEntityId(entity), function, args, argCount) ? 1 : 0;
}

size_t PluginScriptHost::HostGetProjectRoot(void* host, char* buffer, size_t bufferSize) {
    const LuaScriptEngine* lua = Self(host).m_Lua;
    if (!lua || lua->GetProjectRoot().empty()) {
        return 0;
    }
    return CopyOut(lua->GetProjectRoot().string(), buffer, bufferSize);
}
