#pragma once

#include "ScriptLanguageAPI.h"

#include <cmath>
#include <cstdio>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#define HP_CPP_SCRIPT_SDK_VERSION 1u
#define HP_CPP_SCRIPT_EXPORT_NAME "HP_GetCppScript"

extern "C" {

typedef struct HPCppScriptExports {
    uint32_t structSize;
    uint32_t sdkVersion;
    void* (*create)(const HPScriptHostAPI* host, const HPScriptInstanceInfo* info, char* error, size_t errorSize);
    void (*destroy)(void* script);
    void (*awake)(void* script);
    void (*update)(void* script, float deltaSeconds);
    void (*lateUpdate)(void* script, float deltaSeconds);
    void (*contact)(void* script, const HPContactEvent* event);
    int32_t (*call)(void* script, const char* function, const HPValue* args, uint32_t argCount);
} HPCppScriptExports;

typedef const HPCppScriptExports* (*HPGetCppScriptFn)(void);
}

namespace hp {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(Vec3 o) { return *this = *this + o; }
    Vec3& operator-=(Vec3 o) { return *this = *this - o; }
    float Length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 Normalized() const {
        const float length = Length();
        return length > 0.0f ? *this * (1.0f / length) : Vec3{};
    }
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

using Color = Vec4;

/// A value passed between scripts, in any language. Owns its string.
class Value {
public:
    Value() = default;
    Value(bool value) { m_Raw.type = HP_VALUE_BOOL; m_Raw.boolean = value ? 1 : 0; }
    Value(double value) { m_Raw.type = HP_VALUE_NUMBER; m_Raw.number = value; }
    Value(float value) : Value(static_cast<double>(value)) {}
    Value(int value) : Value(static_cast<double>(value)) {}
    Value(const char* value) : Value(std::string(value ? value : "")) {}
    Value(std::string value) : m_String(std::move(value)) { m_Raw.type = HP_VALUE_STRING; }
    Value(Vec2 v) { SetVector(HP_VALUE_VEC2, v.x, v.y, 0.0f, 0.0f); }
    Value(Vec3 v) { SetVector(HP_VALUE_VEC3, v.x, v.y, v.z, 0.0f); }
    Value(Vec4 v) { SetVector(HP_VALUE_VEC4, v.x, v.y, v.z, v.w); }
    explicit Value(const HPValue& raw) : m_Raw(raw) {
        if (raw.type == HP_VALUE_STRING) {
            m_String = raw.string ? raw.string : "";
        }
        m_Raw.string = nullptr;
    }

    bool IsNil() const { return m_Raw.type == HP_VALUE_NIL; }
    bool IsBool() const { return m_Raw.type == HP_VALUE_BOOL; }
    bool IsNumber() const { return m_Raw.type == HP_VALUE_NUMBER; }
    bool IsString() const { return m_Raw.type == HP_VALUE_STRING; }

    bool AsBool(bool fallback = false) const { return IsBool() ? m_Raw.boolean != 0 : fallback; }
    double AsNumber(double fallback = 0.0) const { return IsNumber() ? m_Raw.number : fallback; }
    float AsFloat(float fallback = 0.0f) const { return static_cast<float>(AsNumber(fallback)); }
    int AsInt(int fallback = 0) const { return IsNumber() ? static_cast<int>(std::lround(m_Raw.number)) : fallback; }
    const std::string& AsString() const { return m_String; }
    Vec2 AsVec2() const { return {m_Raw.vector[0], m_Raw.vector[1]}; }
    Vec3 AsVec3() const { return {m_Raw.vector[0], m_Raw.vector[1], m_Raw.vector[2]}; }
    Vec4 AsVec4() const { return {m_Raw.vector[0], m_Raw.vector[1], m_Raw.vector[2], m_Raw.vector[3]}; }

    /// Borrows this value's string, so the Value must outlive the result.
    HPValue Raw() const {
        HPValue raw = m_Raw;
        raw.string = IsString() ? m_String.c_str() : nullptr;
        return raw;
    }

private:
    void SetVector(uint32_t type, float x, float y, float z, float w) {
        m_Raw.type = type;
        m_Raw.vector[0] = x;
        m_Raw.vector[1] = y;
        m_Raw.vector[2] = z;
        m_Raw.vector[3] = w;
    }

    HPValue m_Raw{};
    std::string m_String;
};

using Args = std::span<const Value>;

namespace detail {

inline const HPScriptHostAPI* g_Host = nullptr;
inline thread_local const HPScriptInstanceInfo* g_Creating = nullptr;

inline const HPScriptHostAPI& Host() { return *g_Host; }

inline constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

inline Vec3 FromHP(HPVec3 v) { return {v.x, v.y, v.z}; }
inline HPVec3 ToHP(Vec3 v) { return {v.x, v.y, v.z}; }

inline std::string ReadString(size_t (*get)(void*, HPEntityId, char*, size_t), HPEntityId entity) {
    const size_t size = get(Host().host, entity, nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::string text(size, '\0');
    get(Host().host, entity, text.data(), text.size());
    text.resize(size - 1);
    return text;
}

} // namespace detail

inline void Log(std::string_view message) {
    detail::Host().log(detail::Host().host, HP_LOG_INFO, std::string(message).c_str());
}
inline void LogWarning(std::string_view message) {
    detail::Host().log(detail::Host().host, HP_LOG_WARNING, std::string(message).c_str());
}
inline void LogError(std::string_view message) {
    detail::Host().log(detail::Host().host, HP_LOG_ERROR, std::string(message).c_str());
}

/// A scene entity. Copy it freely; check IsValid() before using one you kept.
class Entity {
public:
    Entity() = default;
    explicit Entity(HPEntityId id) : m_Id(id) {}

    HPEntityId Id() const { return m_Id; }
    explicit operator bool() const { return m_Id != 0; }
    bool operator==(const Entity& other) const = default;

    bool IsValid() const { return m_Id != 0 && detail::Host().isEntityValid(detail::Host().host, m_Id); }
    std::string GetName() const { return detail::ReadString(detail::Host().getEntityName, m_Id); }

    Vec3 GetPosition() const { return Read(detail::Host().getPosition); }
    void SetPosition(Vec3 value) const { detail::Host().setPosition(detail::Host().host, m_Id, detail::ToHP(value)); }
    void Translate(Vec3 delta) const { SetPosition(GetPosition() + delta); }

    /// Degrees, like Lua's transforms.
    Vec3 GetRotation() const { return Read(detail::Host().getRotation) * (1.0f / detail::kDegreesToRadians); }
    void SetRotation(Vec3 degrees) const {
        detail::Host().setRotation(detail::Host().host, m_Id, detail::ToHP(degrees * detail::kDegreesToRadians));
    }
    void Rotate(Vec3 degrees) const { SetRotation(GetRotation() + degrees); }

    Vec3 GetScale() const { return Read(detail::Host().getScale); }
    void SetScale(Vec3 value) const { detail::Host().setScale(detail::Host().host, m_Id, detail::ToHP(value)); }

    /// Runs a function by name in this entity's script, whatever its language.
    /// Returns whether a script handled it.
    template <class... A>
    bool Call(std::string_view function, A&&... args) const {
        const Value values[] = {Value(), Value(std::forward<A>(args))...};
        HPValue raw[sizeof...(A) + 1];
        for (size_t i = 0; i < sizeof...(A); ++i) {
            raw[i] = values[i + 1].Raw();
        }
        return detail::Host().callEntityFunction(detail::Host().host, m_Id, std::string(function).c_str(), raw,
                                                 static_cast<uint32_t>(sizeof...(A))) != 0;
    }

private:
    Vec3 Read(int32_t (*get)(void*, HPEntityId, HPVec3*)) const {
        HPVec3 out{};
        get(detail::Host().host, m_Id, &out);
        return detail::FromHP(out);
    }

    HPEntityId m_Id = 0;
};

namespace Scene {
inline Entity Find(std::string_view name) {
    return Entity(detail::Host().findEntity(detail::Host().host, std::string(name).c_str()));
}
} // namespace Scene

namespace Input {
inline bool IsActionDown(std::string_view action) {
    return detail::Host().isActionDown(detail::Host().host, std::string(action).c_str()) != 0;
}
inline bool IsActionPressed(std::string_view action) {
    return detail::Host().isActionPressed(detail::Host().host, std::string(action).c_str()) != 0;
}
inline bool IsActionReleased(std::string_view action) {
    return detail::Host().isActionReleased(detail::Host().host, std::string(action).c_str()) != 0;
}
inline float GetAxis(std::string_view axis) {
    return detail::Host().getAxis(detail::Host().host, std::string(axis).c_str());
}
} // namespace Input

/// Contact details for collision and trigger callbacks.
struct Contact {
    Vec3 point;
    Vec3 normal;
    Vec3 relativeVelocity;
    float penetration = 0.0f;
};

/// Base class for a gameplay script. Override the callbacks you need.
class Script {
public:
    Script() {
        if (const HPScriptInstanceInfo* info = detail::g_Creating) {
            m_Entity = Entity(info->entity);
            m_ScriptPath = info->scriptPath ? info->scriptPath : "";
        }
    }
    virtual ~Script() = default;
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;

    virtual void OnAwake() {}
    virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }
    virtual void OnLateUpdate(float deltaSeconds) { (void)deltaSeconds; }
    virtual void OnDestroy() {}

    /// `contact` is null when the physics step had no contact point; exit callbacks never get one.
    /// `other` is empty when the other entity was destroyed.
    virtual void OnTriggerEnter(Entity other, const Contact* contact) { (void)other; (void)contact; }
    virtual void OnTriggerStay(Entity other, const Contact* contact) { (void)other; (void)contact; }
    virtual void OnTriggerExit(Entity other, const Contact* contact) { (void)other; (void)contact; }
    virtual void OnCollisionEnter(Entity other, const Contact* contact) { (void)other; (void)contact; }
    virtual void OnCollisionStay(Entity other, const Contact* contact) { (void)other; (void)contact; }
    virtual void OnCollisionExit(Entity other, const Contact* contact) { (void)other; (void)contact; }

    /// Another script ran Call(function, args...) on this entity. Return true when handled.
    virtual bool OnCall(std::string_view function, Args args) { (void)function; (void)args; return false; }

    /// The entity this script is attached to.
    Entity GetEntity() const { return m_Entity; }
    const std::string& GetScriptPath() const { return m_ScriptPath; }

protected:
    template <class T>
    T Field(const char* name, T fallback) const {
        const HPScriptInstanceInfo* info = detail::g_Creating;
        for (uint32_t i = 0; info && i < info->variableCount; ++i) {
            if (std::string_view(info->variables[i].name) == name) {
                return Convert(Value(info->variables[i].value), fallback);
            }
        }
        return fallback;
    }

private:
    template <class T>
    static T Convert(const Value& value, T fallback) {
        if constexpr (std::is_same_v<T, bool>) {
            return value.AsBool(fallback);
        } else if constexpr (std::is_integral_v<T>) {
            return value.IsNumber() ? static_cast<T>(std::llround(value.AsNumber())) : fallback;
        } else if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(value.AsNumber(fallback));
        } else if constexpr (std::is_same_v<T, std::string>) {
            return value.IsString() ? value.AsString() : fallback;
        } else if constexpr (std::is_same_v<T, Vec2>) {
            return value.IsNumber() || value.IsNil() ? fallback : value.AsVec2();
        } else if constexpr (std::is_same_v<T, Vec3>) {
            return value.IsNumber() || value.IsNil() ? fallback : value.AsVec3();
        } else {
            static_assert(std::is_same_v<T, Vec4>, "HP_FIELD supports bool, integers, float, double, std::string, Vec2, Vec3 and Vec4");
            return value.IsNumber() || value.IsNil() ? fallback : value.AsVec4();
        }
    }

    Entity m_Entity;
    std::string m_ScriptPath;
};

namespace detail {

inline void Report(const Script* script, const char* callback, const char* what) {
    if (g_Host) {
        const std::string message = (script ? script->GetScriptPath() : std::string("C++ script")) + ": " +
                                    callback + " threw: " + (what ? what : "unknown exception");
        g_Host->log(g_Host->host, HP_LOG_ERROR, message.c_str());
    }
}

template <class F>
void Guard(Script* script, const char* callback, F&& body) {
    try {
        body();
    } catch (const std::exception& e) {
        Report(script, callback, e.what());
    } catch (...) {
        Report(script, callback, nullptr);
    }
}

template <class T>
void* Create(const HPScriptHostAPI* host, const HPScriptInstanceInfo* info, char* error, size_t errorSize) {
    g_Host = host;
    g_Creating = info;
    Script* script = nullptr;
    try {
        script = new T();
    } catch (const std::exception& e) {
        std::snprintf(error, errorSize, "constructor threw: %s", e.what());
    } catch (...) {
        std::snprintf(error, errorSize, "constructor threw an unknown exception");
    }
    g_Creating = nullptr;
    return script;
}

inline Script* AsScript(void* script) { return static_cast<Script*>(script); }

inline void Destroy(void* handle) {
    Script* script = AsScript(handle);
    Guard(script, "OnDestroy", [&] { script->OnDestroy(); });
    Guard(script, "destructor", [&] { delete script; });
}

inline void Awake(void* handle) {
    Guard(AsScript(handle), "OnAwake", [&] { AsScript(handle)->OnAwake(); });
}

inline void Update(void* handle, float dt) {
    Guard(AsScript(handle), "OnUpdate", [&] { AsScript(handle)->OnUpdate(dt); });
}

inline void LateUpdate(void* handle, float dt) {
    Guard(AsScript(handle), "OnLateUpdate", [&] { AsScript(handle)->OnLateUpdate(dt); });
}

inline void ContactEvent(void* handle, const HPContactEvent* event) {
    Script* script = AsScript(handle);
    Contact contact{FromHP(event->point), FromHP(event->normal), FromHP(event->relativeVelocity), event->penetration};
    const Contact* details = event->hasContact ? &contact : nullptr;
    const Entity other(event->other);
    switch (event->kind) {
        case HP_CONTACT_TRIGGER_ENTER: Guard(script, "OnTriggerEnter", [&] { script->OnTriggerEnter(other, details); }); break;
        case HP_CONTACT_TRIGGER_STAY: Guard(script, "OnTriggerStay", [&] { script->OnTriggerStay(other, details); }); break;
        case HP_CONTACT_TRIGGER_EXIT: Guard(script, "OnTriggerExit", [&] { script->OnTriggerExit(other, details); }); break;
        case HP_CONTACT_COLLISION_ENTER: Guard(script, "OnCollisionEnter", [&] { script->OnCollisionEnter(other, details); }); break;
        case HP_CONTACT_COLLISION_STAY: Guard(script, "OnCollisionStay", [&] { script->OnCollisionStay(other, details); }); break;
        case HP_CONTACT_COLLISION_EXIT: Guard(script, "OnCollisionExit", [&] { script->OnCollisionExit(other, details); }); break;
        default: break;
    }
}

inline int32_t CallFunction(void* handle, const char* function, const HPValue* args, uint32_t argCount) {
    Script* script = AsScript(handle);
    bool handled = false;
    Guard(script, function, [&] {
        std::vector<Value> values;
        values.reserve(argCount);
        for (uint32_t i = 0; i < argCount; ++i) {
            values.emplace_back(args[i]);
        }
        handled = script->OnCall(function, Args(values));
    });
    return handled ? 1 : 0;
}

template <class T>
const HPCppScriptExports* ExportsFor() {
    static_assert(std::is_base_of_v<Script, T>, "HP_SCRIPT needs a class derived from hp::Script");
    static const HPCppScriptExports exports = {
        sizeof(HPCppScriptExports), HP_CPP_SCRIPT_SDK_VERSION,
        &Create<T>, &Destroy, &Awake, &Update, &LateUpdate, &ContactEvent, &CallFunction,
    };
    return &exports;
}

} // namespace detail
} // namespace hp

/// Declares an Inspector field with its default, e.g. HP_FIELD(float, speed, 5.0f)
/// or HP_FIELD(hp::Vec3, offset, 0, 1, 0). Values set in the Inspector replace the default.
#define HP_FIELD(type, name, ...) type name = this->Field<type>(#name, type{__VA_ARGS__})

/// Makes `Type` the script this file provides. Use it once per file.
#define HP_SCRIPT(Type)                                                                          \
    extern "C" HP_SCRIPT_LANGUAGE_EXPORT const HPCppScriptExports* HP_GetCppScript(void) {      \
        return ::hp::detail::ExportsFor<Type>();                                                 \
    }
