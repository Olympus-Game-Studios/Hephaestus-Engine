#include "PluginScriptHost.h"

#include "LuaScriptComponent.h"
#include "LuaScriptEngine.h"
#include "ScriptLanguageRegistry.h"
#include "../Animation/CharacterPlayback.h"
#include "../Audio/AudioEngine.h"
#include "../Core/EngineLog.h"
#include "../Input/InputMap.h"
#include "../Input/InputSystem.h"
#include "../Physics/PhysicsContactEvents.h"
#include "../Scene/CharacterComponent.h"
#include "../Scene/CharacterControllerComponent.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

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

struct PluginScriptHost::HostApi {
    static PluginScriptHost& Host(void* host) { return *static_cast<PluginScriptHost*>(host); }
    static LuaScriptEngine* Lua(void* host) { return Host(host).m_Lua; }

    static bool Valid(void* host, HPEntityId entity) {
        Scene* scene = Host(host).m_Scene;
        return scene && entity != 0 && scene->GetRegistry().valid(static_cast<entt::entity>(ToEntityId(entity)));
    }

    // Only for entities that exist, so a stale handle never reaches the bindings.
    static uint32_t Id(void* host, HPEntityId entity) {
        return Valid(host, entity) ? ToEntityId(entity) : kNoEntity;
    }

    static constexpr uint32_t kNoEntity = std::numeric_limits<uint32_t>::max();

    static uint32_t WriteEntities(const std::vector<entt::entity>& entities, HPEntityId* out, uint32_t capacity) {
        for (uint32_t i = 0; out && i < capacity && i < entities.size(); ++i) {
            out[i] = ToHandle(static_cast<uint32_t>(entities[i]));
        }
        return static_cast<uint32_t>(entities.size());
    }

    static bool Out(HPVec3* out, bool ok, const Vec3& value) {
        if (ok && out) {
            *out = ToHPVec3(value);
        }
        return ok;
    }

    static CharacterComponent* Character(void* host, HPEntityId entity) {
        Scene* scene = Host(host).m_Scene;
        return Valid(host, entity)
            ? scene->GetRegistry().try_get<CharacterComponent>(static_cast<entt::entity>(ToEntityId(entity)))
            : nullptr;
    }

    static CameraComponent* Camera(void* host, HPEntityId entity) {
        Scene* scene = Host(host).m_Scene;
        return Valid(host, entity)
            ? scene->GetRegistry().try_get<CameraComponent>(static_cast<entt::entity>(ToEntityId(entity)))
            : nullptr;
    }

    static AudioEngine* Audio(void* host) {
        LuaScriptEngine* lua = Lua(host);
        return lua ? lua->GetAudioEngine() : nullptr;
    }

    static const PhysicsBinding* Physics(void* host) {
        LuaScriptEngine* lua = Lua(host);
        return lua ? &lua->GetPhysicsBinding() : nullptr;
    }

    static const CharacterBinding* Characters(void* host) {
        LuaScriptEngine* lua = Lua(host);
        return lua ? &lua->GetCharacterBinding() : nullptr;
    }

    static const CameraBinding* ViewCamera(void* host) {
        LuaScriptEngine* lua = Lua(host);
        return lua ? &lua->GetCameraBinding() : nullptr;
    }

    // Time and application

    static float GetDeltaTime(void* host) { return Host(host).m_DeltaTime; }

    static void Quit(void* host) {
        if (LuaScriptEngine* lua = Lua(host)) {
            lua->RequestQuit();
        }
    }

    // Entities and scene

    static int32_t SetEntityName(void* host, HPEntityId entity, const char* name) {
        LuaScriptEngine* lua = Lua(host);
        return lua && name && Valid(host, entity) && lua->SetEntityName(ToEntityId(entity), name);
    }

    static int32_t HasComponent(void* host, HPEntityId entity, uint32_t kind) {
        if (!Valid(host, entity)) {
            return 0;
        }
        auto& registry = Host(host).m_Scene->GetRegistry();
        const auto handle = static_cast<entt::entity>(ToEntityId(entity));
        switch (kind) {
            case HP_COMPONENT_CAMERA: return registry.all_of<CameraComponent>(handle);
            case HP_COMPONENT_CHARACTER: return registry.all_of<CharacterComponent>(handle);
            case HP_COMPONENT_MESH: return registry.all_of<MeshRendererComponent>(handle);
            case HP_COMPONENT_CHARACTER_CONTROLLER: return registry.all_of<CharacterControllerComponent>(handle);
            default: return 0;
        }
    }

    static uint32_t FindEntitiesWithPrefix(void* host, const char* prefix, HPEntityId* out, uint32_t capacity) {
        Scene* scene = Host(host).m_Scene;
        if (!scene) {
            return 0;
        }
        std::vector<entt::entity> entities;
        for (Entity entity : scene->FindEntitiesWithNamePrefix(prefix ? prefix : "")) {
            if (entity) {
                entities.push_back(static_cast<entt::entity>(entity));
            }
        }
        return WriteEntities(entities, out, capacity);
    }

    static HPEntityId InstantiatePrefab(void* host, const char* path, const HPVec3* position,
                                        const HPVec3* rotationDegrees, const HPNamedValue* vars, uint32_t varCount) {
        LuaScriptEngine* lua = Lua(host);
        if (!lua || !path || !*path) {
            return 0;
        }
        LuaScriptEngine::PrefabSpawn spawn;
        spawn.prefabPath = path;
        if (position) {
            spawn.hasPosition = true;
            spawn.position = Vec3(position->x, position->y, position->z);
        }
        if (rotationDegrees) {
            spawn.hasRotation = true;
            spawn.rotationDegrees = Vec3(rotationDegrees->x, rotationDegrees->y, rotationDegrees->z);
        }
        for (uint32_t i = 0; vars && i < varCount; ++i) {
            if (vars[i].name) {
                spawn.vars[vars[i].name] = ToPropertyValue(vars[i].value);
            }
        }
        const Entity root = lua->SpawnPrefab(spawn);
        return root ? ToHandle(static_cast<uint32_t>(static_cast<entt::entity>(root))) : 0;
    }

    static int32_t DestroyEntity(void* host, HPEntityId entity) {
        LuaScriptEngine* lua = Lua(host);
        if (!lua || !Valid(host, entity)) {
            return 0;
        }
        lua->DestroyEntity(ToEntityId(entity));
        return 1;
    }

    static int32_t ReloadScene(void* host) {
        LuaScriptEngine* lua = Lua(host);
        return lua && lua->RequestSceneReload();
    }

    static int32_t SetMeshVisible(void* host, HPEntityId entity, int32_t visible) {
        LuaScriptEngine* lua = Lua(host);
        return lua && Valid(host, entity) && lua->SetMeshVisible(ToEntityId(entity), visible != 0);
    }

    static int32_t IsMeshVisible(void* host, HPEntityId entity) {
        LuaScriptEngine* lua = Lua(host);
        return lua && Valid(host, entity) && lua->IsMeshVisible(ToEntityId(entity));
    }

    // Raw input

    static int32_t IsKeyDown(void* host, const char* key) {
        const auto* input = Host(host).m_Input;
        const int code = key ? Hephaestus::ResolveInputControl(Hephaestus::InputBindingType::Keyboard, key) : -1;
        return input && code >= 0 && input->IsKeyDown(code);
    }

    static int32_t IsMouseButtonDown(void* host, int32_t button) {
        const auto* input = Host(host).m_Input;
        return input && button >= 0 && button <= 7 && input->IsMouseButtonDown(button);
    }

    static HPVec2 GetMousePosition(void* host) {
        const auto* input = Host(host).m_Input;
        return input ? HPVec2{static_cast<float>(input->GetMouseX()), static_cast<float>(input->GetMouseY())}
                     : HPVec2{0.0f, 0.0f};
    }

    static HPVec2 GetMouseDelta(void* host) {
        const auto* input = Host(host).m_Input;
        return input ? HPVec2{static_cast<float>(input->GetMouseDeltaX()), static_cast<float>(input->GetMouseDeltaY())}
                     : HPVec2{0.0f, 0.0f};
    }

    static HPVec2 GetScrollDelta(void* host) {
        const auto* input = Host(host).m_Input;
        return input ? HPVec2{static_cast<float>(input->GetScrollDeltaX()), static_cast<float>(input->GetScrollDeltaY())}
                     : HPVec2{0.0f, 0.0f};
    }

    static int32_t IsGamepadConnected(void* host) {
        const auto* input = Host(host).m_Input;
        return input && input->IsGamepadConnected();
    }

    static void SetCursorMode(void* host, uint32_t mode) {
        if (LuaScriptEngine* lua = Lua(host)) {
            lua->ApplyCursorMode(mode == HP_CURSOR_CAPTURED ? 1 : 0);
        }
    }

    static uint32_t GetCursorMode(void* host) {
        LuaScriptEngine* lua = Lua(host);
        return lua && lua->GetCursorMode() == 1 ? HP_CURSOR_CAPTURED : HP_CURSOR_NORMAL;
    }

    // Script view camera

    static void ViewCameraSetActive(void* host, int32_t active) {
        const CameraBinding* camera = ViewCamera(host);
        if (camera && camera->setActive) camera->setActive(active != 0);
    }

    static int32_t ViewCameraIsActive(void* host) {
        const CameraBinding* camera = ViewCamera(host);
        return camera && camera->isActive && camera->isActive();
    }

    static void ViewCameraSetPosition(void* host, HPVec3 position) {
        const CameraBinding* camera = ViewCamera(host);
        if (camera && camera->setPosition) camera->setPosition(Vec3(position.x, position.y, position.z));
    }

    static HPVec3 ViewCameraGetPosition(void* host) {
        const CameraBinding* camera = ViewCamera(host);
        return camera && camera->getPosition ? ToHPVec3(camera->getPosition()) : HPVec3{0.0f, 0.0f, 0.0f};
    }

    static void ViewCameraSetRotation(void* host, HPVec3 radians) {
        const CameraBinding* camera = ViewCamera(host);
        if (camera && camera->setRotation) camera->setRotation(Vec3(radians.x, radians.y, radians.z));
    }

    static HPVec3 ViewCameraGetRotation(void* host) {
        const CameraBinding* camera = ViewCamera(host);
        return camera && camera->getRotation ? ToHPVec3(camera->getRotation()) : HPVec3{0.0f, 0.0f, 0.0f};
    }

    static void ViewCameraTranslate(void* host, HPVec3 delta) {
        const CameraBinding* camera = ViewCamera(host);
        if (!camera) {
            return;
        }
        const Vec3 d(delta.x, delta.y, delta.z);
        if (camera->translate) {
            camera->translate(d);
        } else if (camera->getPosition && camera->setPosition) {
            camera->setPosition(vadd(camera->getPosition(), d));
        }
    }

    // Camera components

    static uint32_t FindCameras(void* host, HPEntityId* out, uint32_t capacity) {
        Scene* scene = Host(host).m_Scene;
        if (!scene) {
            return 0;
        }
        std::vector<entt::entity> cameras;
        for (auto handle : scene->GetEntitiesWith<CameraComponent>()) {
            cameras.push_back(handle);
        }
        return WriteEntities(cameras, out, capacity);
    }

    static HPEntityId GetActiveCamera(void* host) {
        LuaScriptEngine* lua = Lua(host);
        const Entity camera = lua ? lua->FindActiveCamera() : Entity{};
        return camera ? ToHandle(static_cast<uint32_t>(static_cast<entt::entity>(camera))) : 0;
    }

    static int32_t SetCameraActive(void* host, HPEntityId camera, int32_t active) {
        LuaScriptEngine* lua = Lua(host);
        return lua && Valid(host, camera) && lua->SetCameraActive(ToEntityId(camera), active != 0);
    }

    static int32_t IsCameraActive(void* host, HPEntityId entity) {
        const CameraComponent* camera = Camera(host, entity);
        return camera && camera->isActive;
    }

    static int32_t GetCameraLens(void* host, HPEntityId entity, HPCameraLens* out) {
        const CameraComponent* camera = Camera(host, entity);
        if (!camera || !out) {
            return 0;
        }
        *out = HPCameraLens{camera->fov, camera->nearPlane, camera->farPlane};
        return 1;
    }

    static int32_t SetCameraLens(void* host, HPEntityId entity, const HPCameraLens* lens) {
        CameraComponent* camera = Camera(host, entity);
        if (!camera || !lens) {
            return 0;
        }
        camera->fov = lens->fieldOfView;
        camera->nearPlane = lens->nearPlane;
        camera->farPlane = lens->farPlane;
        return 1;
    }

    // Physics

    static int32_t IsPhysicsAvailable(void* host) {
        const PhysicsBinding* physics = Physics(host);
        return physics && physics->hasBody;
    }

    static int32_t HasBody(void* host, HPEntityId entity) {
        const PhysicsBinding* physics = Physics(host);
        const uint32_t id = Id(host, entity);
        return physics && physics->hasBody && id != kNoEntity && physics->hasBody(id);
    }

    static int32_t GetLinearVelocity(void* host, HPEntityId entity, HPVec3* out) {
        const PhysicsBinding* physics = Physics(host);
        const uint32_t id = Id(host, entity);
        Vec3 velocity{};
        return Out(out, physics && physics->getLinearVelocity && id != kNoEntity &&
                            physics->getLinearVelocity(id, velocity), velocity);
    }

    static int32_t SetLinearVelocity(void* host, HPEntityId entity, HPVec3 velocity, int32_t activate) {
        const PhysicsBinding* physics = Physics(host);
        const uint32_t id = Id(host, entity);
        return physics && physics->setLinearVelocity && id != kNoEntity &&
               physics->setLinearVelocity(id, Vec3(velocity.x, velocity.y, velocity.z), activate != 0);
    }

    static int32_t SetAngularVelocity(void* host, HPEntityId entity, HPVec3 velocity, int32_t activate) {
        const PhysicsBinding* physics = Physics(host);
        const uint32_t id = Id(host, entity);
        return physics && physics->setAngularVelocity && id != kNoEntity &&
               physics->setAngularVelocity(id, Vec3(velocity.x, velocity.y, velocity.z), activate != 0);
    }

    static int32_t SetBodyRotation(void* host, HPEntityId entity, HPVec3 radians, int32_t activate) {
        const PhysicsBinding* physics = Physics(host);
        const uint32_t id = Id(host, entity);
        return physics && physics->setRotation && id != kNoEntity &&
               physics->setRotation(id, Vec3(radians.x, radians.y, radians.z), activate != 0);
    }

    static int32_t Raycast(void* host, HPEntityId ignore, HPVec3 origin, HPVec3 direction, float maxDistance,
                           const char* layer, int32_t ignoreSensors, HPRaycastHit* out, char* error, size_t errorSize) {
        const PhysicsBinding* physics = Physics(host);
        if (!physics || !physics->raycast) {
            return 0;
        }
        PhysicsRaycastHit hit{};
        std::string message;
        const bool ok = physics->raycast(ignore == 0 ? kNoEntity : ToEntityId(ignore),
                                         Vec3(origin.x, origin.y, origin.z),
                                         Vec3(direction.x, direction.y, direction.z), maxDistance, hit,
                                         layer && *layer ? layer : nullptr, message, ignoreSensors != 0);
        if (!message.empty() && error && errorSize > 0) {
            std::snprintf(error, errorSize, "%s", message.c_str());
        }
        if (!ok || !hit.hit) {
            return 0;
        }
        if (out) {
            out->point = ToHPVec3(hit.point);
            out->normal = ToHPVec3(hit.normal);
            out->distance = hit.distance;
            out->entity = hit.entityId != kNoEntity ? ToHandle(hit.entityId) : 0;
        }
        return 1;
    }

    static int32_t PhysicsIsGrounded(void* host, HPEntityId entity) {
        const PhysicsBinding* physics = Physics(host);
        const uint32_t id = Id(host, entity);
        return physics && physics->isGrounded && id != kNoEntity && physics->isGrounded(id);
    }

    static int32_t PhysicsMove(void* host, HPEntityId entity, float x, float z, float speed) {
        const PhysicsBinding* physics = Physics(host);
        const uint32_t id = Id(host, entity);
        return physics && physics->move && id != kNoEntity && physics->move(id, x, z, speed);
    }

    static int32_t PhysicsJump(void* host, HPEntityId entity, float speed) {
        const PhysicsBinding* physics = Physics(host);
        const uint32_t id = Id(host, entity);
        return physics && physics->jump && id != kNoEntity && physics->jump(id, speed);
    }

    // Character controllers

    template <class Member, class... A>
    static int32_t CharacterCall(void* host, HPEntityId entity, Member member, A&&... args) {
        const CharacterBinding* binding = Characters(host);
        const uint32_t id = Id(host, entity);
        return binding && binding->*member && id != kNoEntity && (binding->*member)(id, std::forward<A>(args)...);
    }

    static int32_t CharacterMove(void* host, HPEntityId e, float x, float z) {
        return CharacterCall(host, e, &CharacterBinding::move, x, z);
    }
    static int32_t CharacterJump(void* host, HPEntityId e) { return CharacterCall(host, e, &CharacterBinding::jump); }
    static int32_t CharacterSetCrouched(void* host, HPEntityId e, int32_t crouched) {
        return CharacterCall(host, e, &CharacterBinding::setCrouched, crouched != 0);
    }
    static int32_t CharacterIsCrouched(void* host, HPEntityId e) {
        return CharacterCall(host, e, &CharacterBinding::isCrouched);
    }
    static int32_t CharacterIsStandBlocked(void* host, HPEntityId e) {
        return CharacterCall(host, e, &CharacterBinding::isStandBlocked);
    }
    static int32_t CharacterIsGrounded(void* host, HPEntityId e) {
        return CharacterCall(host, e, &CharacterBinding::isGrounded);
    }
    static size_t CharacterGetGroundState(void* host, HPEntityId e, char* buffer, size_t bufferSize) {
        std::string state;
        return CharacterCall(host, e, &CharacterBinding::getGroundState, state) ? CopyOut(state, buffer, bufferSize)
                                                                                 : 0;
    }
    static int32_t CharacterGetVelocity(void* host, HPEntityId e, HPVec3* out) {
        Vec3 value{};
        return Out(out, CharacterCall(host, e, &CharacterBinding::getVelocity, value) != 0, value);
    }
    static int32_t CharacterGetGroundNormal(void* host, HPEntityId e, HPVec3* out) {
        Vec3 value{};
        return Out(out, CharacterCall(host, e, &CharacterBinding::getGroundNormal, value) != 0, value);
    }
    static int32_t CharacterGetGroundVelocity(void* host, HPEntityId e, HPVec3* out) {
        Vec3 value{};
        return Out(out, CharacterCall(host, e, &CharacterBinding::getGroundVelocity, value) != 0, value);
    }
    static int32_t CharacterSetSpeedMultiplier(void* host, HPEntityId e, float multiplier) {
        return CharacterCall(host, e, &CharacterBinding::setSpeedMultiplier, multiplier);
    }
    static float CharacterGetSpeedMultiplier(void* host, HPEntityId e) {
        float multiplier = 1.0f;
        CharacterCall(host, e, &CharacterBinding::getSpeedMultiplier, multiplier);
        return multiplier;
    }
    static int32_t CharacterTeleport(void* host, HPEntityId e, HPVec3 position) {
        return CharacterCall(host, e, &CharacterBinding::teleport, position.x, position.y, position.z);
    }
    static int32_t CharacterSetFacingYaw(void* host, HPEntityId e, float radians) {
        return CharacterCall(host, e, &CharacterBinding::setFacingYaw, radians);
    }

    // Animated characters

    static void LogAnimationError(void* host, HPEntityId entity, const std::string& error) {
        if (!error.empty()) {
            const char* name = "";
            if (Scene* scene = Host(host).m_Scene) {
                if (const auto* tag = scene->GetRegistry().try_get<TagComponent>(static_cast<entt::entity>(ToEntityId(entity)))) {
                    name = tag->tag.c_str();
                }
            }
            Host(host).Log(HP_LOG_ERROR, std::string("Animation on '") + name + "': " + error);
        }
    }

    static int32_t AnimationPlay(void* host, HPEntityId entity, const char* clip) {
        CharacterComponent* character = Character(host, entity);
        std::string error;
        const bool ok = character && clip && Hephaestus::CharacterAnimationSystem::Play(*character, clip, error);
        LogAnimationError(host, entity, error);
        return ok;
    }

    static int32_t AnimationCrossFade(void* host, HPEntityId entity, const char* clip, float seconds) {
        CharacterComponent* character = Character(host, entity);
        std::string error;
        const bool ok = character && clip &&
                        Hephaestus::CharacterAnimationSystem::CrossFade(*character, clip, seconds, error);
        LogAnimationError(host, entity, error);
        return ok;
    }

    static int32_t AnimationPause(void* host, HPEntityId entity) {
        CharacterComponent* character = Character(host, entity);
        if (character) Hephaestus::CharacterAnimationSystem::Pause(*character);
        return character != nullptr;
    }

    static int32_t AnimationResume(void* host, HPEntityId entity) {
        CharacterComponent* character = Character(host, entity);
        if (character) Hephaestus::CharacterAnimationSystem::Resume(*character);
        return character != nullptr;
    }

    static int32_t AnimationStop(void* host, HPEntityId entity) {
        CharacterComponent* character = Character(host, entity);
        if (character) Hephaestus::CharacterAnimationSystem::Stop(*character);
        return character != nullptr;
    }

    static int32_t AnimationSetSpeed(void* host, HPEntityId entity, float speed) {
        CharacterComponent* character = Character(host, entity);
        const bool ok = character && Hephaestus::CharacterAnimationSystem::SetSpeed(*character, speed);
        if (character && !ok) {
            LogAnimationError(host, entity, "SetSpeed requires a speed greater than 0. Use Pause() to freeze.");
        }
        return ok;
    }

    static int32_t AnimationIsPlaying(void* host, HPEntityId entity, const char* clip) {
        const CharacterComponent* character = Character(host, entity);
        return character && Hephaestus::CharacterAnimationSystem::IsPlaying(*character, clip);
    }

    static size_t AnimationGetCurrent(void* host, HPEntityId entity, char* buffer, size_t bufferSize) {
        const CharacterComponent* character = Character(host, entity);
        return character && !character->player.currentClip.empty()
            ? CopyOut(character->player.currentClip, buffer, bufferSize) : 0;
    }

    static float AnimationGetTime(void* host, HPEntityId entity) {
        const CharacterComponent* character = Character(host, entity);
        return character ? character->player.time : 0.0f;
    }

    static float AnimationGetDuration(void* host, HPEntityId entity, const char* clip) {
        const CharacterComponent* character = Character(host, entity);
        return character ? Hephaestus::CharacterAnimationSystem::ClipDuration(*character, clip) : 0.0f;
    }

    static int32_t GetSocket(void* host, HPEntityId entity, const char* socket, int32_t worldSpace,
                             HPVec3* position, HPVec3* rotation) {
        LuaScriptEngine* lua = Lua(host);
        Vec3 p{};
        Vec3 r{};
        if (!lua || !socket || !Valid(host, entity) ||
            !lua->EvaluateSocket(ToEntityId(entity), socket, worldSpace != 0, p, r)) {
            return 0;
        }
        Out(position, true, p);
        Out(rotation, true, r);
        return 1;
    }

    // Audio

    static int32_t IsAudioAvailable(void* host) {
        AudioEngine* audio = Audio(host);
        return audio && audio->IsBackendAvailable();
    }

    static uint32_t AudioPlay(void* host, const char* path, int32_t loop, float volume) {
        AudioEngine* audio = Audio(host);
        return audio && path ? audio->Play(path, loop != 0, volume) : 0;
    }

    static int32_t AudioStop(void* host, uint32_t sound) {
        AudioEngine* audio = Audio(host);
        return audio && audio->Stop(sound);
    }

    static int32_t AudioPause(void* host, uint32_t sound) {
        AudioEngine* audio = Audio(host);
        return audio && audio->Pause(sound);
    }

    static int32_t AudioResume(void* host, uint32_t sound) {
        AudioEngine* audio = Audio(host);
        return audio && audio->Resume(sound);
    }

    static int32_t AudioSetVolume(void* host, uint32_t sound, float volume) {
        AudioEngine* audio = Audio(host);
        return audio && audio->SetVolume(sound, volume);
    }

    static int32_t AudioIsPlaying(void* host, uint32_t sound) {
        AudioEngine* audio = Audio(host);
        return audio && audio->IsPlaying(sound);
    }

    static void AudioStopAll(void* host) {
        if (AudioEngine* audio = Audio(host)) {
            audio->StopAll();
        }
    }

    static void Install(HPScriptHostAPI& api) {
        api.getDeltaTime = &GetDeltaTime;
        api.quit = &Quit;
        api.setEntityName = &SetEntityName;
        api.hasComponent = &HasComponent;
        api.findEntitiesWithPrefix = &FindEntitiesWithPrefix;
        api.instantiatePrefab = &InstantiatePrefab;
        api.destroyEntity = &DestroyEntity;
        api.reloadScene = &ReloadScene;
        api.setMeshVisible = &SetMeshVisible;
        api.isMeshVisible = &IsMeshVisible;
        api.isKeyDown = &IsKeyDown;
        api.isMouseButtonDown = &IsMouseButtonDown;
        api.getMousePosition = &GetMousePosition;
        api.getMouseDelta = &GetMouseDelta;
        api.getScrollDelta = &GetScrollDelta;
        api.isGamepadConnected = &IsGamepadConnected;
        api.setCursorMode = &SetCursorMode;
        api.getCursorMode = &GetCursorMode;
        api.viewCameraSetActive = &ViewCameraSetActive;
        api.viewCameraIsActive = &ViewCameraIsActive;
        api.viewCameraSetPosition = &ViewCameraSetPosition;
        api.viewCameraGetPosition = &ViewCameraGetPosition;
        api.viewCameraSetRotation = &ViewCameraSetRotation;
        api.viewCameraGetRotation = &ViewCameraGetRotation;
        api.viewCameraTranslate = &ViewCameraTranslate;
        api.findCameras = &FindCameras;
        api.getActiveCamera = &GetActiveCamera;
        api.setCameraActive = &SetCameraActive;
        api.isCameraActive = &IsCameraActive;
        api.getCameraLens = &GetCameraLens;
        api.setCameraLens = &SetCameraLens;
        api.isPhysicsAvailable = &IsPhysicsAvailable;
        api.hasBody = &HasBody;
        api.getLinearVelocity = &GetLinearVelocity;
        api.setLinearVelocity = &SetLinearVelocity;
        api.setAngularVelocity = &SetAngularVelocity;
        api.setBodyRotation = &SetBodyRotation;
        api.raycast = &Raycast;
        api.physicsIsGrounded = &PhysicsIsGrounded;
        api.physicsMove = &PhysicsMove;
        api.physicsJump = &PhysicsJump;
        api.characterMove = &CharacterMove;
        api.characterJump = &CharacterJump;
        api.characterSetCrouched = &CharacterSetCrouched;
        api.characterIsCrouched = &CharacterIsCrouched;
        api.characterIsStandBlocked = &CharacterIsStandBlocked;
        api.characterIsGrounded = &CharacterIsGrounded;
        api.characterGetGroundState = &CharacterGetGroundState;
        api.characterGetVelocity = &CharacterGetVelocity;
        api.characterGetGroundNormal = &CharacterGetGroundNormal;
        api.characterGetGroundVelocity = &CharacterGetGroundVelocity;
        api.characterSetSpeedMultiplier = &CharacterSetSpeedMultiplier;
        api.characterGetSpeedMultiplier = &CharacterGetSpeedMultiplier;
        api.characterTeleport = &CharacterTeleport;
        api.characterSetFacingYaw = &CharacterSetFacingYaw;
        api.animationPlay = &AnimationPlay;
        api.animationCrossFade = &AnimationCrossFade;
        api.animationPause = &AnimationPause;
        api.animationResume = &AnimationResume;
        api.animationStop = &AnimationStop;
        api.animationSetSpeed = &AnimationSetSpeed;
        api.animationIsPlaying = &AnimationIsPlaying;
        api.animationGetCurrent = &AnimationGetCurrent;
        api.animationGetTime = &AnimationGetTime;
        api.animationGetDuration = &AnimationGetDuration;
        api.getSocket = &GetSocket;
        api.isAudioAvailable = &IsAudioAvailable;
        api.audioPlay = &AudioPlay;
        api.audioStop = &AudioStop;
        api.audioPause = &AudioPause;
        api.audioResume = &AudioResume;
        api.audioSetVolume = &AudioSetVolume;
        api.audioIsPlaying = &AudioIsPlaying;
        api.audioStopAll = &AudioStopAll;
    }
};

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
    HostApi::Install(m_Api);
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
    m_DeltaTime = deltaTime;
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

    if (m_Lua) {
        m_Lua->BeginScriptPass();
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
    if (m_Lua) {
        m_Lua->EndScriptPass(scene);
    }
}

void PluginScriptHost::LateUpdate(Scene& scene, float deltaTime) {
    m_Scene = &scene;
    m_DeltaTime = deltaTime;
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
