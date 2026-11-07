#pragma once

#include "ScriptLanguageAPI.h"

#include <cmath>
#include <cstdio>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#define HP_CPP_SCRIPT_SDK_VERSION 4u
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
    void (*animationFinished)(void* script, const char* clip);
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

inline Vec2 FromHP(HPVec2 v) { return {v.x, v.y}; }

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

inline std::vector<HPEntityId> ReadEntities(uint32_t (*get)(void*, const char*, HPEntityId*, uint32_t),
                                            const char* filter) {
    std::vector<HPEntityId> ids(get(Host().host, filter, nullptr, 0));
    if (!ids.empty()) {
        ids.resize(get(Host().host, filter, ids.data(), static_cast<uint32_t>(ids.size())));
    }
    return ids;
}

inline Vec3 ReadVec3(int32_t (*get)(void*, HPEntityId, HPVec3*), HPEntityId entity, bool* ok = nullptr) {
    HPVec3 out{};
    const bool read = get(Host().host, entity, &out) != 0;
    if (ok) {
        *ok = read;
    }
    return FromHP(out);
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

/// The physics body on an entity (Lua's Physics table). Rotations are radians.
class Body {
public:
    explicit Body(HPEntityId id) : m_Id(id) {}
    bool Exists() const { return detail::Host().hasBody(detail::Host().host, m_Id) != 0; }
    Vec3 GetLinearVelocity() const { return detail::ReadVec3(detail::Host().getLinearVelocity, m_Id); }
    bool SetLinearVelocity(Vec3 velocity, bool activate = true) const {
        return detail::Host().setLinearVelocity(detail::Host().host, m_Id, detail::ToHP(velocity), activate) != 0;
    }
    bool SetAngularVelocity(Vec3 velocity, bool activate = true) const {
        return detail::Host().setAngularVelocity(detail::Host().host, m_Id, detail::ToHP(velocity), activate) != 0;
    }
    bool SetRotation(Vec3 radians, bool activate = true) const {
        return detail::Host().setBodyRotation(detail::Host().host, m_Id, detail::ToHP(radians), activate) != 0;
    }
    /// Simple movement; uses the Character Controller when the entity has one.
    bool Move(float x, float z, float speed) const {
        return detail::Host().physicsMove(detail::Host().host, m_Id, x, z, speed) != 0;
    }
    bool Jump(float speed) const { return detail::Host().physicsJump(detail::Host().host, m_Id, speed) != 0; }
    bool IsGrounded() const { return detail::Host().physicsIsGrounded(detail::Host().host, m_Id) != 0; }

private:
    HPEntityId m_Id;
};

/// The Character Controller on an entity (Lua's Character table).
class CharacterController {
public:
    explicit CharacterController(HPEntityId id) : m_Id(id) {}
    /// The direction to walk this step, in world space.
    bool Move(float worldX, float worldZ) const {
        return detail::Host().characterMove(detail::Host().host, m_Id, worldX, worldZ) != 0;
    }
    bool Jump() const { return detail::Host().characterJump(detail::Host().host, m_Id) != 0; }
    bool SetCrouched(bool crouched) const {
        return detail::Host().characterSetCrouched(detail::Host().host, m_Id, crouched) != 0;
    }
    bool IsCrouched() const { return detail::Host().characterIsCrouched(detail::Host().host, m_Id) != 0; }
    bool IsStandBlocked() const { return detail::Host().characterIsStandBlocked(detail::Host().host, m_Id) != 0; }
    bool IsGrounded() const { return detail::Host().characterIsGrounded(detail::Host().host, m_Id) != 0; }
    std::string GetGroundState() const {
        return detail::ReadString(detail::Host().characterGetGroundState, m_Id);
    }
    Vec3 GetVelocity() const { return detail::ReadVec3(detail::Host().characterGetVelocity, m_Id); }
    Vec3 GetGroundNormal() const { return detail::ReadVec3(detail::Host().characterGetGroundNormal, m_Id); }
    Vec3 GetGroundVelocity() const { return detail::ReadVec3(detail::Host().characterGetGroundVelocity, m_Id); }
    bool SetSpeedMultiplier(float multiplier) const {
        return detail::Host().characterSetSpeedMultiplier(detail::Host().host, m_Id, multiplier) != 0;
    }
    float GetSpeedMultiplier() const { return detail::Host().characterGetSpeedMultiplier(detail::Host().host, m_Id); }
    bool Teleport(Vec3 position) const {
        return detail::Host().characterTeleport(detail::Host().host, m_Id, detail::ToHP(position)) != 0;
    }
    bool SetFacingYaw(float radians) const {
        return detail::Host().characterSetFacingYaw(detail::Host().host, m_Id, radians) != 0;
    }

private:
    HPEntityId m_Id;
};

/// Clip playback on an animated character. Clips are named by their id in the Character Editor.
class Animator {
public:
    explicit Animator(HPEntityId id) : m_Id(id) {}
    bool Play(std::string_view clip) const {
        return detail::Host().animationPlay(detail::Host().host, m_Id, std::string(clip).c_str()) != 0;
    }
    bool CrossFade(std::string_view clip, float seconds) const {
        return detail::Host().animationCrossFade(detail::Host().host, m_Id, std::string(clip).c_str(), seconds) != 0;
    }
    void Pause() const { detail::Host().animationPause(detail::Host().host, m_Id); }
    void Resume() const { detail::Host().animationResume(detail::Host().host, m_Id); }
    void Stop() const { detail::Host().animationStop(detail::Host().host, m_Id); }
    /// Must be above 0; use Pause() to freeze.
    bool SetSpeed(float multiplier) const {
        return detail::Host().animationSetSpeed(detail::Host().host, m_Id, multiplier) != 0;
    }
    bool IsPlaying() const { return detail::Host().animationIsPlaying(detail::Host().host, m_Id, nullptr) != 0; }
    bool IsPlaying(std::string_view clip) const {
        return detail::Host().animationIsPlaying(detail::Host().host, m_Id, std::string(clip).c_str()) != 0;
    }
    std::string GetCurrentAnimation() const { return detail::ReadString(detail::Host().animationGetCurrent, m_Id); }
    float GetTime() const { return detail::Host().animationGetTime(detail::Host().host, m_Id); }
    float GetDuration() const { return detail::Host().animationGetDuration(detail::Host().host, m_Id, nullptr); }
    float GetDuration(std::string_view clip) const {
        return detail::Host().animationGetDuration(detail::Host().host, m_Id, std::string(clip).c_str());
    }

private:
    HPEntityId m_Id;
};

/// A Camera component. Field of view in degrees.
class Camera {
public:
    explicit Camera(HPEntityId id) : m_Id(id) {}
    bool IsActive() const { return detail::Host().isCameraActive(detail::Host().host, m_Id) != 0; }
    /// Making a camera active makes every other camera inactive.
    void SetActive(bool active) const { detail::Host().setCameraActive(detail::Host().host, m_Id, active); }
    float GetFov() const { return Lens().fieldOfView; }
    void SetFov(float degrees) const { Edit([&](HPCameraLens& l) { l.fieldOfView = degrees; }); }
    float GetNear() const { return Lens().nearPlane; }
    void SetNear(float distance) const { Edit([&](HPCameraLens& l) { l.nearPlane = distance; }); }
    float GetFar() const { return Lens().farPlane; }
    void SetFar(float distance) const { Edit([&](HPCameraLens& l) { l.farPlane = distance; }); }

private:
    HPCameraLens Lens() const {
        HPCameraLens lens{};
        detail::Host().getCameraLens(detail::Host().host, m_Id, &lens);
        return lens;
    }
    template <class F>
    void Edit(F&& change) const {
        HPCameraLens lens{};
        if (detail::Host().getCameraLens(detail::Host().host, m_Id, &lens)) {
            change(lens);
            detail::Host().setCameraLens(detail::Host().host, m_Id, &lens);
        }
    }

    HPEntityId m_Id;
};

/// A socket on an animated character. Rotation is Euler radians.
struct Socket {
    Vec3 position;
    Vec3 rotation;
};

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
    void SetName(std::string_view name) const {
        detail::Host().setEntityName(detail::Host().host, m_Id, std::string(name).c_str());
    }

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

    /// Show or hide the entity's mesh (Lua's Mesh table).
    bool SetVisible(bool visible) const { return detail::Host().setMeshVisible(detail::Host().host, m_Id, visible) != 0; }
    bool IsVisible() const { return detail::Host().isMeshVisible(detail::Host().host, m_Id) != 0; }

    /// The entity's physics body. Body::Exists() says whether it has one.
    Body GetBody() const { return Body(m_Id); }
    /// The entity's Character Controller, or nothing.
    std::optional<CharacterController> GetController() const {
        return Has(HP_COMPONENT_CHARACTER_CONTROLLER) ? std::optional(CharacterController(m_Id)) : std::nullopt;
    }
    /// Animation control for an animated character, or nothing.
    std::optional<Animator> GetAnimator() const {
        return Has(HP_COMPONENT_CHARACTER) ? std::optional(Animator(m_Id)) : std::nullopt;
    }
    /// The entity's Camera component, or nothing.
    std::optional<Camera> GetCamera() const {
        return Has(HP_COMPONENT_CAMERA) ? std::optional(Camera(m_Id)) : std::nullopt;
    }
    /// A socket of an animated character, in world space or character space.
    std::optional<Socket> GetSocket(std::string_view name, bool worldSpace = true) const {
        HPVec3 position{};
        HPVec3 rotation{};
        if (!detail::Host().getSocket(detail::Host().host, m_Id, std::string(name).c_str(), worldSpace, &position,
                                      &rotation)) {
            return std::nullopt;
        }
        return Socket{detail::FromHP(position), detail::FromHP(rotation)};
    }

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

    /// Like Call, but runs the function in the entity's character controller script.
    template <class... A>
    bool CallController(std::string_view function, A&&... args) const {
        const Value values[] = {Value(), Value(std::forward<A>(args))...};
        HPValue raw[sizeof...(A) + 1];
        for (size_t i = 0; i < sizeof...(A); ++i) {
            raw[i] = values[i + 1].Raw();
        }
        return detail::Host().callControllerFunction(detail::Host().host, m_Id, std::string(function).c_str(), raw,
                                                     static_cast<uint32_t>(sizeof...(A))) != 0;
    }

private:
    bool Has(uint32_t kind) const { return detail::Host().hasComponent(detail::Host().host, m_Id, kind) != 0; }

    Vec3 Read(int32_t (*get)(void*, HPEntityId, HPVec3*)) const {
        HPVec3 out{};
        get(detail::Host().host, m_Id, &out);
        return detail::FromHP(out);
    }

    HPEntityId m_Id = 0;
};

namespace detail {
inline std::vector<Entity> ToEntities(const std::vector<HPEntityId>& ids) {
    std::vector<Entity> entities;
    entities.reserve(ids.size());
    for (HPEntityId id : ids) {
        entities.emplace_back(id);
    }
    return entities;
}
} // namespace detail

/// Options for Scene::Instantiate. Rotation is in degrees; vars override the
/// prefab scripts' Inspector values.
struct SpawnOptions {
    std::optional<Vec3> position;
    std::optional<Vec3> rotation;
    std::vector<std::pair<std::string, Value>> vars;
};

namespace Scene {
inline Entity Find(std::string_view name) {
    return Entity(detail::Host().findEntity(detail::Host().host, std::string(name).c_str()));
}
/// Every entity whose name starts with prefix.
inline std::vector<Entity> FindAllWithPrefix(std::string_view prefix) {
    return detail::ToEntities(detail::ReadEntities(detail::Host().findEntitiesWithPrefix, std::string(prefix).c_str()));
}
/// Spawns a prefab. Returns an empty entity when it fails.
inline Entity Instantiate(std::string_view path, const SpawnOptions& options = {}) {
    std::vector<HPNamedValue> vars;
    vars.reserve(options.vars.size());
    for (const auto& [name, value] : options.vars) {
        vars.push_back(HPNamedValue{name.c_str(), value.Raw()});
    }
    const HPVec3 position = detail::ToHP(options.position.value_or(Vec3{}));
    const HPVec3 rotation = detail::ToHP(options.rotation.value_or(Vec3{}));
    return Entity(detail::Host().instantiatePrefab(detail::Host().host, std::string(path).c_str(),
                                                   options.position ? &position : nullptr,
                                                   options.rotation ? &rotation : nullptr, vars.data(),
                                                   static_cast<uint32_t>(vars.size())));
}
/// Removes the entity (a whole prefab instance) once this frame's scripts have run.
inline void Destroy(Entity entity) { detail::Host().destroyEntity(detail::Host().host, entity.Id()); }
/// Restarts the current scene from its saved file at a safe point after this frame.
inline bool Reload() { return detail::Host().reloadScene(detail::Host().host) != 0; }
inline std::vector<Entity> FindCameras() {
    std::vector<HPEntityId> ids(detail::Host().findCameras(detail::Host().host, nullptr, 0));
    if (!ids.empty()) {
        ids.resize(detail::Host().findCameras(detail::Host().host, ids.data(), static_cast<uint32_t>(ids.size())));
    }
    return detail::ToEntities(ids);
}
inline Entity FindFirstCamera() {
    HPEntityId first = 0;
    detail::Host().findCameras(detail::Host().host, &first, 1);
    return Entity(first);
}
/// The camera the player sees through: the active one, else the first.
inline Entity GetActiveCamera() { return Entity(detail::Host().getActiveCamera(detail::Host().host)); }
inline void SetActiveCamera(Entity camera) { detail::Host().setCameraActive(detail::Host().host, camera.Id(), 1); }
} // namespace Scene

namespace Time {
/// Seconds since the last frame, the same as OnUpdate's deltaSeconds.
inline float DeltaTime() { return detail::Host().getDeltaTime(detail::Host().host); }
} // namespace Time

namespace Application {
/// Closes the game. In the editor, it stops Play.
inline void Quit() { detail::Host().quit(detail::Host().host); }
} // namespace Application

enum class MouseButton : int32_t { Left = 0, Right = 1, Middle = 2 };
enum class CursorMode : uint32_t { Normal = HP_CURSOR_NORMAL, Captured = HP_CURSOR_CAPTURED };

namespace Input {
/// Raw key check by name ("w", "space", "left_shift"). Prefer actions so players can use a pad.
inline bool IsKeyDown(std::string_view key) {
    return detail::Host().isKeyDown(detail::Host().host, std::string(key).c_str()) != 0;
}
inline bool IsMouseButtonDown(MouseButton button) {
    return detail::Host().isMouseButtonDown(detail::Host().host, static_cast<int32_t>(button)) != 0;
}
/// Mouse position in the window, in pixels.
inline Vec2 GetMousePosition() { return detail::FromHP(detail::Host().getMousePosition(detail::Host().host)); }
/// Mouse movement since the last frame, in pixels.
inline Vec2 GetMouseDelta() { return detail::FromHP(detail::Host().getMouseDelta(detail::Host().host)); }
inline Vec2 GetScrollDelta() { return detail::FromHP(detail::Host().getScrollDelta(detail::Host().host)); }
inline bool IsGamepadConnected() { return detail::Host().isGamepadConnected(detail::Host().host) != 0; }
/// Captured hides and locks the mouse for mouse look; Normal frees it for menus.
inline void SetCursorMode(CursorMode mode) {
    detail::Host().setCursorMode(detail::Host().host, static_cast<uint32_t>(mode));
}
inline CursorMode GetCursorMode() { return static_cast<CursorMode>(detail::Host().getCursorMode(detail::Host().host)); }
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

/// The free script camera (Lua's Camera table), separate from Camera components.
/// Call SetActive(true) first. Rotation is pitch, yaw and roll in radians.
namespace ViewCamera {
inline void SetActive(bool active) { detail::Host().viewCameraSetActive(detail::Host().host, active); }
inline bool IsActive() { return detail::Host().viewCameraIsActive(detail::Host().host) != 0; }
inline void SetPosition(Vec3 position) { detail::Host().viewCameraSetPosition(detail::Host().host, detail::ToHP(position)); }
inline Vec3 GetPosition() { return detail::FromHP(detail::Host().viewCameraGetPosition(detail::Host().host)); }
inline void SetRotation(Vec3 radians) { detail::Host().viewCameraSetRotation(detail::Host().host, detail::ToHP(radians)); }
inline Vec3 GetRotation() { return detail::FromHP(detail::Host().viewCameraGetRotation(detail::Host().host)); }
inline void Translate(Vec3 delta) { detail::Host().viewCameraTranslate(detail::Host().host, detail::ToHP(delta)); }
} // namespace ViewCamera

struct RaycastHit {
    Vec3 point;
    Vec3 normal;
    float distance = 0.0f;
    Entity entity; ///< Empty when the hit has no entity.
};

struct RaycastOptions {
    std::string layer; ///< A layer name from the collision matrix; empty for every layer.
    bool ignoreSensors = false;
    Entity ignore;     ///< An entity the ray passes through, usually the caster.
};

namespace Physics {
inline bool IsAvailable() { return detail::Host().isPhysicsAvailable(detail::Host().host) != 0; }
/// Casts a ray. Returns nothing when it hits nothing. An unknown layer is logged as an error.
inline std::optional<RaycastHit> Raycast(Vec3 origin, Vec3 direction, float maxDistance,
                                         const RaycastOptions& options = {}) {
    HPRaycastHit hit{};
    char error[512] = {};
    const bool found = detail::Host().raycast(detail::Host().host, options.ignore.Id(), detail::ToHP(origin),
                                              detail::ToHP(direction), maxDistance, options.layer.c_str(),
                                              options.ignoreSensors, &hit, error, sizeof(error)) != 0;
    if (error[0]) {
        LogError(std::string("Physics::Raycast: ") + error);
    }
    if (!found) {
        return std::nullopt;
    }
    return RaycastHit{detail::FromHP(hit.point), detail::FromHP(hit.normal), hit.distance, Entity(hit.entity)};
}
} // namespace Physics

/// A playing sound. 0 means it failed to start.
using Sound = uint32_t;

namespace Audio {
inline bool IsAvailable() { return detail::Host().isAudioAvailable(detail::Host().host) != 0; }
/// Plays a .wav or .mp3; volume is 0 to 1.
inline Sound Play(std::string_view path, bool loop = false, float volume = 1.0f) {
    return detail::Host().audioPlay(detail::Host().host, std::string(path).c_str(), loop, volume);
}
inline bool Stop(Sound sound) { return detail::Host().audioStop(detail::Host().host, sound) != 0; }
inline bool Pause(Sound sound) { return detail::Host().audioPause(detail::Host().host, sound) != 0; }
inline bool Resume(Sound sound) { return detail::Host().audioResume(detail::Host().host, sound) != 0; }
inline bool SetVolume(Sound sound, float volume) {
    return detail::Host().audioSetVolume(detail::Host().host, sound, volume) != 0;
}
inline bool IsPlaying(Sound sound) { return detail::Host().audioIsPlaying(detail::Host().host, sound) != 0; }
inline void StopAll() { detail::Host().audioStopAll(detail::Host().host); }
} // namespace Audio

enum class UIAnchor { TopLeft, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight, Stretch };
enum class UIAlign { Left, Center, Right, Top, Bottom, Stretch };
enum class UIScaleMode { Stretch, Fit, Fill, Center };
enum class ButtonState { Normal, Hovered, Pressed, Focused, Disabled };

namespace detail {
inline const char* Name(UIAnchor a) {
    static const char* names[] = {"topLeft", "topCenter", "topRight", "centerLeft", "center",
                                  "centerRight", "bottomLeft", "bottomCenter", "bottomRight", "stretch"};
    return names[static_cast<int>(a)];
}
inline const char* Name(UIAlign a) {
    static const char* names[] = {"left", "center", "right", "top", "bottom", "stretch"};
    return names[static_cast<int>(a)];
}
inline const char* Name(UIScaleMode m) {
    static const char* names[] = {"stretch", "fit", "fill", "center"};
    return names[static_cast<int>(m)];
}
inline const char* Name(ButtonState b) {
    static const char* names[] = {"normal", "hovered", "pressed", "focused", "disabled"};
    return names[static_cast<int>(b)];
}
} // namespace detail

/// A UI style, as a list of fields named like Lua's style tables:
/// backgroundColor, backgroundImageId, imageTint, borderColor, borderWidth,
/// cornerRadius, textColor, fontFamily, fontSize, fontBold, fontItalic,
/// padding (a number, or Vec4 top, right, bottom, left), opacity, scale,
/// textShadow, textShadowColor, textShadowOffset (Vec2), textAlignment
/// ("left", "center", "right") and imageScaleMode ("stretch", "fit", "fill", "center").
struct UIStyle {
    std::vector<std::pair<std::string, Value>> fields;
    UIStyle& Set(std::string name, Value value) {
        fields.emplace_back(std::move(name), std::move(value));
        return *this;
    }
};

namespace detail {
inline std::vector<HPNamedValue> Fields(const UIStyle& style) {
    std::vector<HPNamedValue> fields;
    fields.reserve(style.fields.size());
    for (const auto& [name, value] : style.fields) {
        fields.push_back(HPNamedValue{name.c_str(), value.Raw()});
    }
    return fields;
}
} // namespace detail

/// A UI widget: a panel, text, button, image, box or grid. Setters return
/// false when the widget is gone or is the wrong kind for that property.
class Widget {
public:
    Widget() = default;
    explicit Widget(uint32_t handle) : m_Handle(handle) {}
    uint32_t Handle() const { return m_Handle; }
    explicit operator bool() const { return m_Handle != 0; }

    bool SetPosition(Vec2 position) const { return Set(HP_UI_POSITION, Value(position)); }
    bool SetSize(Vec2 size) const { return Set(HP_UI_SIZE, Value(size)); }
    bool SetMinSize(Vec2 size) const { return Set(HP_UI_MIN_SIZE, Value(size)); }
    bool SetMaxSize(Vec2 size) const { return Set(HP_UI_MAX_SIZE, Value(size)); }
    bool SetVisible(bool visible) const { return Set(HP_UI_VISIBLE, Value(visible)); }
    bool SetEnabled(bool enabled) const { return Set(HP_UI_ENABLED, Value(enabled)); }
    bool SetFocused(bool focused) const { return Set(HP_UI_FOCUSED, Value(focused)); }
    bool SetOpacity(float opacity) const { return Set(HP_UI_OPACITY, Value(opacity)); }
    bool SetZOrder(int order) const { return Set(HP_UI_Z_ORDER, Value(order)); }
    bool SetText(std::string_view text) const { return Set(HP_UI_TEXT_CONTENT, Value(std::string(text))); }
    bool SetImage(std::string_view imageId) const { return Set(HP_UI_IMAGE_ID, Value(std::string(imageId))); }
    bool SetImageTint(Color tint) const { return Set(HP_UI_IMAGE_TINT, Value(tint)); }
    bool SetImageScaleMode(UIScaleMode mode) const { return Set(HP_UI_IMAGE_SCALE_MODE, Value(detail::Name(mode))); }
    bool SetSpacing(float spacing) const { return Set(HP_UI_SPACING, Value(spacing)); }
    bool SetChildAlignment(UIAlign alignment) const { return Set(HP_UI_CHILD_ALIGNMENT, Value(detail::Name(alignment))); }
    bool SetGridColumns(int columns) const { return Set(HP_UI_GRID_COLUMNS, Value(columns)); }
    bool SetGridSpacing(float horizontal, float vertical) const { return Set(HP_UI_GRID_SPACING, Value(Vec2{horizontal, vertical})); }
    bool SetPadding(float all) const { return Set(HP_UI_PADDING, Value(all)); }
    bool SetPadding(float top, float right, float bottom, float left) const {
        return Set(HP_UI_PADDING, Value(Vec4{top, right, bottom, left}));
    }
    bool SetMargin(float all) const { return Set(HP_UI_MARGIN, Value(all)); }
    bool SetMargin(float top, float right, float bottom, float left) const {
        return Set(HP_UI_MARGIN, Value(Vec4{top, right, bottom, left}));
    }
    bool SetAnchor(UIAnchor anchor) const { return Set(HP_UI_ANCHOR, Value(detail::Name(anchor))); }
    bool SetAlignment(UIAlign horizontal) const { return Set(HP_UI_H_ALIGNMENT, Value(detail::Name(horizontal))); }
    bool SetAlignment(UIAlign horizontal, UIAlign vertical) const {
        return SetAlignment(horizontal) && Set(HP_UI_V_ALIGNMENT, Value(detail::Name(vertical)));
    }
    bool SetBackgroundColor(Color color) const { return Set(HP_UI_BACKGROUND_COLOR, Value(color)); }
    bool SetBorderColor(Color color) const { return Set(HP_UI_BORDER_COLOR, Value(color)); }
    bool SetBorderWidth(float width) const { return Set(HP_UI_BORDER_WIDTH, Value(width)); }
    bool SetCornerRadius(float radius) const { return Set(HP_UI_CORNER_RADIUS, Value(radius)); }
    bool SetTextColor(Color color) const { return Set(HP_UI_TEXT_COLOR, Value(color)); }
    bool SetFontSize(float size) const { return Set(HP_UI_FONT_SIZE, Value(size)); }
    bool SetFontBold(bool bold) const { return Set(HP_UI_FONT_BOLD, Value(bold)); }
    bool SetFontItalic(bool italic) const { return Set(HP_UI_FONT_ITALIC, Value(italic)); }
    bool SetTextAlignment(UIAlign alignment) const { return Set(HP_UI_TEXT_ALIGNMENT, Value(detail::Name(alignment))); }
    bool SetWordWrap(bool wrap) const { return Set(HP_UI_WORD_WRAP, Value(wrap)); }
    bool SetTextShadow(bool enabled) const { return Set(HP_UI_TEXT_SHADOW, Value(enabled)); }
    bool SetTextShadow(bool enabled, Color color, Vec2 offset = {1, 1}) const {
        return SetTextShadow(enabled) && Set(HP_UI_TEXT_SHADOW_COLOR, Value(color)) &&
               Set(HP_UI_TEXT_SHADOW_OFFSET, Value(offset));
    }
    bool SetStyle(const UIStyle& style) const {
        const auto fields = detail::Fields(style);
        return detail::Host().uiSetStyle(detail::Host().host, m_Handle, nullptr, fields.data(),
                                         static_cast<uint32_t>(fields.size())) != 0;
    }
    bool SetButtonStyle(ButtonState state, const UIStyle& style) const {
        const auto fields = detail::Fields(style);
        return detail::Host().uiSetStyle(detail::Host().host, m_Handle, detail::Name(state), fields.data(),
                                         static_cast<uint32_t>(fields.size())) != 0;
    }
    bool ApplyThemeStyle(std::string_view name) const {
        return detail::Host().uiApplyThemeStyle(detail::Host().host, m_Handle, std::string(name).c_str()) != 0;
    }
    /// Clicking this button runs `function` on the entity's script, in any language.
    bool SetOnClick(Entity entity, std::string_view function) const {
        return detail::Host().uiSetOnClick(detail::Host().host, m_Handle, entity.Id(), std::string(function).c_str()) != 0;
    }
    /// Hovering this button runs `function(bool entered)` on the entity's script.
    bool SetOnHover(Entity entity, std::string_view function) const {
        return detail::Host().uiSetOnHover(detail::Host().host, m_Handle, entity.Id(), std::string(function).c_str()) != 0;
    }

    Widget AddPanel() const { return Add(HP_UI_PANEL, ""); }
    Widget AddText(std::string_view text = {}) const { return Add(HP_UI_TEXT, text); }
    Widget AddButton(std::string_view label = "Button") const { return Add(HP_UI_BUTTON, label); }
    Widget AddImage(std::string_view imageId = {}) const { return Add(HP_UI_IMAGE, imageId); }
    Widget AddVerticalBox() const { return Add(HP_UI_VERTICAL_BOX, ""); }
    Widget AddHorizontalBox() const { return Add(HP_UI_HORIZONTAL_BOX, ""); }
    Widget AddGrid() const { return Add(HP_UI_GRID, ""); }

private:
    bool Set(uint32_t property, const Value& value) const {
        const HPValue raw = value.Raw();
        return detail::Host().uiSet(detail::Host().host, m_Handle, property, &raw) != 0;
    }
    Widget Add(uint32_t kind, std::string_view text) const {
        return Widget(detail::Host().uiCreateWidget(detail::Host().host, m_Handle, kind, std::string(text).c_str()));
    }

    uint32_t m_Handle = 0;
};

/// A UI screen: a displayed or loaded .hui layout, or one built in code.
class Screen {
public:
    Screen() = default;
    explicit Screen(uint32_t handle) : m_Handle(handle) {}
    uint32_t Handle() const { return m_Handle; }
    explicit operator bool() const { return m_Handle != 0; }
    /// The root canvas, to add widgets to.
    Widget GetRoot() const { return Widget(detail::Host().uiGetRoot(detail::Host().host, m_Handle)); }
    /// A widget of a .hui layout by its ID.
    Widget GetWidget(std::string_view id) const {
        return Widget(detail::Host().uiGetWidget(detail::Host().host, m_Handle, std::string(id).c_str()));
    }

private:
    uint32_t m_Handle = 0;
};

namespace UI {
inline bool IsAvailable() { return detail::Host().uiIsAvailable(detail::Host().host) != 0; }
/// Shows a .hui layout (Lua's display.ui). Errors go to `error`, or the Console when it is null.
inline Screen Display(std::string_view path, std::string* error = nullptr) {
    char message[512] = {};
    const Screen screen(detail::Host().uiDisplay(detail::Host().host, std::string(path).c_str(), message, sizeof(message)));
    if (!screen) {
        if (error) *error = message;
        else LogError(std::string("UI::Display: ") + message);
    }
    return screen;
}
/// Removes a displayed layout (Lua's destroy.ui).
inline bool Destroy(Screen screen) { return screen && detail::Host().uiDestroy(detail::Host().host, screen.Handle()) != 0; }
/// Removes the most recently displayed layout (destroy.ui("current")).
inline bool DestroyCurrent() { return detail::Host().uiDestroy(detail::Host().host, 0) != 0; }
/// Removes every displayed layout (destroy.ui("all")).
inline void DestroyAll() { detail::Host().uiDestroyAll(detail::Host().host); }
/// Loads a .hui layout without display's bookkeeping (Lua's UI.loadHUI).
inline Screen Load(std::string_view path, std::string* error = nullptr) {
    char message[512] = {};
    const Screen screen(detail::Host().uiLoad(detail::Host().host, std::string(path).c_str(), message, sizeof(message)));
    if (!screen) {
        if (error) *error = message;
        else LogError(std::string("UI::Load: ") + message);
    }
    return screen;
}
inline Screen CreateScreen(std::string_view name) {
    return Screen(detail::Host().uiCreateScreen(detail::Host().host, std::string(name).c_str()));
}
inline Screen CreateSimpleMenu() { return Screen(detail::Host().uiCreateSimpleMenu(detail::Host().host)); }
inline void ClearScreens() { detail::Host().uiClearScreens(detail::Host().host); }
inline uint32_t GetScreenCount() { return detail::Host().uiGetScreenCount(detail::Host().host); }
inline bool SetScreenVisible(std::string_view name, bool visible) {
    return detail::Host().uiSetScreenVisible(detail::Host().host, std::string(name).c_str(), visible) != 0;
}
inline bool SetScreenActive(std::string_view name, bool active) {
    return detail::Host().uiSetScreenActive(detail::Host().host, std::string(name).c_str(), active) != 0;
}
inline bool SetViewportSize(float width, float height) {
    return detail::Host().uiSetViewportSize(detail::Host().host, width, height) != 0;
}
inline uint32_t GetDrawCommandCount() { return detail::Host().uiGetDrawCommandCount(detail::Host().host); }
inline bool RegisterThemeStyle(std::string_view name, const UIStyle& style) {
    const auto fields = detail::Fields(style);
    return detail::Host().uiRegisterThemeStyle(detail::Host().host, std::string(name).c_str(), fields.data(),
                                               static_cast<uint32_t>(fields.size())) != 0;
}
/// Normal frees the mouse for menus (Lua's CursorMode.UI); Captured gives it back to the game.
inline bool SetCursorMode(CursorMode mode) {
    return detail::Host().uiSetCursorMode(detail::Host().host, static_cast<uint32_t>(mode)) != 0;
}
inline CursorMode GetCursorMode() { return static_cast<CursorMode>(detail::Host().uiGetCursorMode(detail::Host().host)); }
} // namespace UI

/// Contact details for collision and trigger callbacks.
struct Contact {
    Vec3 point;
    Vec3 normal;
    Vec3 relativeVelocity;
    float penetration = 0.0f;
};

namespace detail {
inline void Awake(void* handle);
inline void Destroy(void* handle);
} // namespace detail

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
    /// Right after OnAwake, and OnDisable right before OnDestroy, as in Lua.
    virtual void OnEnable() {}
    virtual void OnDisable() {}
    virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }
    virtual void OnLateUpdate(float deltaSeconds) { (void)deltaSeconds; }
    /// Only for scripts that woke up, like Lua's onDestroy.
    virtual void OnDestroy() {}
    /// A non-looping clip of this entity's animated character finished.
    virtual void OnAnimationFinished(std::string_view clip) { (void)clip; }

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

    /// Clicking the button runs `function` (an OnCall name) on this script's entity.
    bool SetOnClick(Widget button, std::string_view function) const { return button.SetOnClick(m_Entity, function); }
    /// Hovering the button runs `function(bool entered)` on this script's entity.
    bool SetOnHover(Widget button, std::string_view function) const { return button.SetOnHover(m_Entity, function); }

    /// Physics::Raycast that passes through this script's entity, like Lua's Physics.raycast.
    std::optional<RaycastHit> Raycast(Vec3 origin, Vec3 direction, float maxDistance,
                                      std::string_view layer = {}) const {
        return Physics::Raycast(origin, direction, maxDistance, RaycastOptions{std::string(layer), false, m_Entity});
    }
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
    friend void detail::Awake(void*);
    friend void detail::Destroy(void*);

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
    bool m_Awake = false;
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
    if (script->m_Awake) {
        Guard(script, "OnDisable", [&] { script->OnDisable(); });
        Guard(script, "OnDestroy", [&] { script->OnDestroy(); });
    }
    Guard(script, "destructor", [&] { delete script; });
}

inline void Awake(void* handle) {
    Script* script = AsScript(handle);
    Guard(script, "OnAwake", [&] { script->OnAwake(); });
    script->m_Awake = true;
    Guard(script, "OnEnable", [&] { script->OnEnable(); });
}

inline void AnimationFinished(void* handle, const char* clip) {
    Guard(AsScript(handle), "OnAnimationFinished", [&] { AsScript(handle)->OnAnimationFinished(clip ? clip : ""); });
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
        &Create<T>, &Destroy, &Awake, &Update, &LateUpdate, &ContactEvent, &CallFunction, &AnimationFinished,
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
