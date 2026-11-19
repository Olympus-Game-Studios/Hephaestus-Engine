#include "PluginScriptHost.h"

#include "LuaScriptComponent.h"
#include "LuaScriptEngine.h"
#include "ScriptLanguageRegistry.h"
#include "../Animation/CharacterPlayback.h"
#include "../Assets/MeshAsset.h"
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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
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

static_assert(static_cast<uint32_t>(LuaScriptEngine::UIWidgetProperty::ImageTint) == HP_UI_IMAGE_TINT &&
              static_cast<uint32_t>(LuaScriptEngine::UIWidgetProperty::Text) == HP_UI_TEXT_CONTENT &&
              static_cast<uint32_t>(LuaScriptEngine::UIWidgetProperty::ChildAlignment) == HP_UI_CHILD_ALIGNMENT,
              "HPUIProperty must list LuaScriptEngine::UIWidgetProperty in the same order");
static_assert(static_cast<uint32_t>(LuaScriptEngine::UIWidgetKind::Grid) == HP_UI_GRID,
              "HPUIWidgetKind must list LuaScriptEngine::UIWidgetKind in the same order");

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
        std::vector<uint32_t> ignored;
        if (ignore != 0) ignored.push_back(ToEntityId(ignore));
        const bool ok = physics->raycast(ignored,
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

    // Game UI

    static uint32_t UIDisplay(void* host, const char* path, char* error, size_t errorSize) {
        LuaScriptEngine* lua = Lua(host);
        std::string message = lua ? std::string() : "UI is not available";
        const uint32_t screen = lua && path ? lua->UIDisplay(path, message) : 0;
        if (screen == 0 && error && errorSize > 0) {
            std::snprintf(error, errorSize, "%s", message.c_str());
        }
        return screen;
    }

    static uint32_t UILoad(void* host, const char* path, char* error, size_t errorSize) {
        LuaScriptEngine* lua = Lua(host);
        std::string message = lua ? std::string() : "UI is not available";
        const uint32_t screen = lua && path ? lua->UILoadHUI(path, message) : 0;
        if (screen == 0 && error && errorSize > 0) {
            std::snprintf(error, errorSize, "%s", message.c_str());
        }
        return screen;
    }

    static int32_t UIDestroy(void* host, uint32_t screen) {
        LuaScriptEngine* lua = Lua(host);
        return lua && (screen == 0 ? lua->UIDestroyCurrent() : lua->UIDestroy(screen));
    }

    static PropertyValue::Object Fields(const HPNamedValue* fields, uint32_t count) {
        PropertyValue::Object object;
        for (uint32_t i = 0; fields && i < count; ++i) {
            if (fields[i].name) {
                object[fields[i].name] = ToPropertyValue(fields[i].value);
            }
        }
        return object;
    }

    static int32_t UISet(void* host, uint32_t widget, uint32_t property, const HPValue* value) {
        LuaScriptEngine* lua = Lua(host);
        if (!lua || !value || property > HP_UI_IMAGE_TINT) {
            return 0;
        }
        return lua->UISet(widget, static_cast<LuaScriptEngine::UIWidgetProperty>(property), ToPropertyValue(*value));
    }

    static int32_t UISetStyle(void* host, uint32_t widget, const char* buttonState, const HPNamedValue* fields,
                              uint32_t count) {
        LuaScriptEngine* lua = Lua(host);
        if (!lua) {
            return 0;
        }
        return buttonState ? lua->UISetButtonStyle(widget, buttonState, Fields(fields, count))
                           : lua->UISetStyle(widget, Fields(fields, count));
    }

    static int32_t UIRegisterThemeStyle(void* host, const char* name, const HPNamedValue* fields, uint32_t count) {
        LuaScriptEngine* lua = Lua(host);
        return lua && name && lua->UIRegisterThemeStyle(name, Fields(fields, count));
    }

    // The UI can outlive this host in some editor teardown orders, so callbacks hold a weak token.
    static std::function<void(const HPValue*, uint32_t)> Callback(void* host, HPEntityId entity, const char* function) {
        PluginScriptHost& self = Host(host);
        std::weak_ptr<bool> alive = self.m_Alive;
        const std::string name = function;
        return [&self, alive, entity, name](const HPValue* args, uint32_t count) {
            if (!alive.expired() && entity != 0) {
                self.ForwardCall(ToEntityId(entity), name.c_str(), args, count, LuaScriptSlot::Gameplay);
            }
        };
    }

    static int32_t UISetOnClick(void* host, uint32_t widget, HPEntityId entity, const char* function) {
        LuaScriptEngine* lua = Lua(host);
        if (!lua || !function || !*function) {
            return 0;
        }
        auto call = Callback(host, entity, function);
        return lua->UISetOnClick(widget, [call]() { call(nullptr, 0); });
    }

    static int32_t UISetOnHover(void* host, uint32_t widget, HPEntityId entity, const char* function) {
        LuaScriptEngine* lua = Lua(host);
        if (!lua || !function || !*function) {
            return 0;
        }
        auto call = Callback(host, entity, function);
        return lua->UISetOnHover(widget, [call](bool entered) {
            HPValue value{};
            value.type = HP_VALUE_BOOL;
            value.boolean = entered ? 1 : 0;
            call(&value, 1);
        });
    }

    static void InstallUI(HPScriptHostAPI& api) {
        api.uiIsAvailable = [](void* host) -> int32_t { return Lua(host) && Lua(host)->UIIsAvailable(); };
        api.uiDisplay = &UIDisplay;
        api.uiDestroy = &UIDestroy;
        api.uiDestroyAll = [](void* host) { if (Lua(host)) Lua(host)->UIDestroyAll(); };
        api.uiLoad = &UILoad;
        api.uiGetWidget = [](void* host, uint32_t screen, const char* id) -> uint32_t {
            return Lua(host) && id ? Lua(host)->UIGetWidget(screen, id) : 0;
        };
        api.uiCreateScreen = [](void* host, const char* name) -> uint32_t {
            return Lua(host) && name ? Lua(host)->UICreateScreen(name) : 0;
        };
        api.uiCreateSimpleMenu = [](void* host) -> uint32_t { return Lua(host) ? Lua(host)->UICreateSimpleMenu() : 0; };
        api.uiGetRoot = [](void* host, uint32_t screen) -> uint32_t { return Lua(host) ? Lua(host)->UIGetRoot(screen) : 0; };
        api.uiCreateWidget = [](void* host, uint32_t parent, uint32_t kind, const char* text) -> uint32_t {
            if (!Lua(host) || kind > HP_UI_GRID) {
                return 0;
            }
            return Lua(host)->UICreateWidget(parent, static_cast<LuaScriptEngine::UIWidgetKind>(kind), text ? text : "");
        };
        api.uiClearScreens = [](void* host) { if (Lua(host)) Lua(host)->UIClearScreens(); };
        api.uiGetScreenCount = [](void* host) -> uint32_t {
            return Lua(host) ? static_cast<uint32_t>(Lua(host)->UIGetScreenCount()) : 0;
        };
        api.uiSetScreenVisible = [](void* host, const char* name, int32_t visible) -> int32_t {
            return Lua(host) && name && Lua(host)->UISetScreenVisible(name, visible != 0);
        };
        api.uiSetScreenActive = [](void* host, const char* name, int32_t active) -> int32_t {
            return Lua(host) && name && Lua(host)->UISetScreenActive(name, active != 0);
        };
        api.uiSetViewportSize = [](void* host, float width, float height) -> int32_t {
            return Lua(host) && Lua(host)->UISetViewportSize(width, height);
        };
        api.uiGetDrawCommandCount = [](void* host) -> uint32_t {
            return Lua(host) ? static_cast<uint32_t>(Lua(host)->UIGetDrawCommandCount()) : 0;
        };
        api.uiSet = &UISet;
        api.uiSetStyle = &UISetStyle;
        api.uiRegisterThemeStyle = &UIRegisterThemeStyle;
        api.uiApplyThemeStyle = [](void* host, uint32_t widget, const char* name) -> int32_t {
            return Lua(host) && name && Lua(host)->UIApplyThemeStyle(widget, name);
        };
        api.uiSetCursorMode = [](void* host, uint32_t mode) -> int32_t {
            return Lua(host) && Lua(host)->UISetCursorMode(mode == HP_CURSOR_NORMAL);
        };
        api.uiGetCursorMode = [](void* host) -> uint32_t {
            return Lua(host) && Lua(host)->UIIsCursorModeUI() ? HP_CURSOR_NORMAL : HP_CURSOR_CAPTURED;
        };
        api.uiSetOnClick = &UISetOnClick;
        api.uiSetOnHover = &UISetOnHover;
        api.callControllerFunction = [](void* host, HPEntityId entity, const char* function, const HPValue* args,
                                        uint32_t argCount) -> int32_t {
            return entity != 0 &&
                   Host(host).ForwardCall(ToEntityId(entity), function, args, argCount, LuaScriptSlot::CharacterController);
        };
    }

    static void Install(HPScriptHostAPI& api) {
        InstallUI(api);
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
        api.archiveMount = &ArchiveMount;
        api.archiveFileCount = &ArchiveFileCount;
        api.archiveFileName = &ArchiveFileName;
        api.archiveReadFile = &ArchiveReadFile;
        api.meshCreate = &MeshCreate;
        api.colliderAddConvexHulls = &ColliderAddConvexHulls;
    }

    // Archives

    static void WriteError(const std::string& message, char* error, size_t errorSize) {
        if (error && errorSize > 0) {
            std::snprintf(error, errorSize, "%s", message.c_str());
        }
    }

    static int32_t ArchiveMount(void* host, const char* path, char* error, size_t errorSize) {
        std::string message;
        if (!path || !*path) {
            WriteError("no archive path given", error, errorSize);
            return 0;
        }
        if (!Host(host).m_Archives.Mount(std::filesystem::u8path(path), message)) {
            WriteError(message, error, errorSize);
            return 0;
        }
        return 1;
    }

    static uint32_t ArchiveFileCount(void* host) {
        return static_cast<uint32_t>(Host(host).m_Archives.Files().size());
    }

    static size_t ArchiveFileName(void* host, uint32_t index, char* buffer, size_t bufferSize) {
        const auto& files = Host(host).m_Archives.Files();
        return index < files.size() ? CopyOut(files[index], buffer, bufferSize) : 0;
    }

    static int32_t ArchiveReadFile(void* host, const char* virtualPath,
                                   void (*sink)(void* user, const void* data, size_t size), void* user,
                                   char* error, size_t errorSize) {
        if (!virtualPath || !sink) {
            WriteError("archiveReadFile needs a path and a sink", error, errorSize);
            return 0;
        }
        std::vector<uint8_t> bytes;
        std::string message;
        if (!Host(host).m_Archives.Read(virtualPath, bytes, message)) {
            WriteError(message, error, errorSize);
            return 0;
        }
        sink(user, bytes.data(), bytes.size());
        return 1;
    }

    // Runtime meshes

    static void Grow(AABB& box, bool& empty, const Vec3& p) {
        if (empty) {
            box = AABB(p, p);
            empty = false;
            return;
        }
        box.min = Vec3(std::min(box.min.x, p.x), std::min(box.min.y, p.y), std::min(box.min.z, p.z));
        box.max = Vec3(std::max(box.max.x, p.x), std::max(box.max.y, p.y), std::max(box.max.z, p.z));
    }

    static HPEntityId MeshCreate(void* host, const char* name, const HPMeshSection* sections, uint32_t sectionCount,
                                 char* error, size_t errorSize) {
        PluginScriptHost& self = Host(host);
        if (!self.m_Scene || !self.m_MeshSink.upload) {
            WriteError("runtime meshes are not available here", error, errorSize);
            return 0;
        }
        if (!sections || sectionCount == 0) {
            WriteError("meshCreate needs at least one section", error, errorSize);
            return 0;
        }
        MeshData mesh;
        std::vector<std::string> materials;
        bool meshEmpty = true;
        for (uint32_t s = 0; s < sectionCount; ++s) {
            const HPMeshSection& section = sections[s];
            if (section.structSize < sizeof(HPMeshSection) || (!section.vertices && section.vertexCount > 0) ||
                (!section.indices && section.indexCount > 0) || section.indexCount % 3 != 0) {
                WriteError("section " + std::to_string(s) + " is malformed", error, errorSize);
                return 0;
            }
            const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
            if (section.vertexCount > std::numeric_limits<uint32_t>::max() - base) {
                WriteError("the mesh has too many vertices", error, errorSize);
                return 0;
            }
            for (uint32_t i = 0; i < section.indexCount; ++i) {
                if (section.indices[i] >= section.vertexCount) {
                    WriteError("section " + std::to_string(s) + " has an index past its vertices", error, errorSize);
                    return 0;
                }
            }
            MeshData::Submesh submesh;
            submesh.indexOffset = static_cast<uint32_t>(mesh.indices.size());
            submesh.indexCount = section.indexCount;
            submesh.materialSlot = static_cast<int>(s);
            bool sectionEmpty = true;
            for (uint32_t v = 0; v < section.vertexCount; ++v) {
                const HPMeshVertex& in = section.vertices[v];
                Vertex out{};
                std::memcpy(out.pos, in.position, sizeof out.pos);
                std::memcpy(out.normal, in.normal, sizeof out.normal);
                std::memcpy(out.color, in.color, sizeof out.color);
                std::memcpy(out.texCoord, in.uv, sizeof out.texCoord);
                if (section.lightmapUvs) {
                    std::memcpy(out.lightmapUV, section.lightmapUvs + 2 * static_cast<size_t>(v), sizeof out.lightmapUV);
                }
                mesh.vertices.push_back(out);
                const Vec3 p(in.position[0], in.position[1], in.position[2]);
                Grow(submesh.bounds, sectionEmpty, p);
                Grow(mesh.bounds, meshEmpty, p);
            }
            for (uint32_t i = 0; i < section.indexCount; ++i) {
                mesh.indices.push_back(base + section.indices[i]);
            }
            mesh.submeshes.push_back(std::move(submesh));
            materials.emplace_back(section.material ? section.material : "");
        }
        if (mesh.indices.empty()) {
            WriteError("the mesh has no triangles", error, errorSize);
            return 0;
        }
        const std::string entityName = name && *name ? name : "Runtime Mesh";
        const size_t handle = self.m_MeshSink.upload(entityName, mesh);
        if (handle == MeshRendererComponent::InvalidMesh) {
            WriteError("the mesh could not be uploaded", error, errorSize);
            return 0;
        }
        self.m_SessionMeshes.push_back(handle);
        Entity entity = self.m_Scene->CreateEntity(entityName);
        auto& renderer = entity.AddComponent<MeshRendererComponent>();
        renderer.SetMesh(handle, mesh.bounds, {}, false, static_cast<int>(sectionCount));
        renderer.appliedMaterialPaths = std::move(materials);
        renderer.materialIndices.assign(sectionCount, -1);
        return ToHandle(static_cast<uint32_t>(static_cast<entt::entity>(entity)));
    }

    // Static collision

    static int32_t ColliderAddConvexHulls(void* host, HPEntityId owner, const float* points, const uint32_t* hullSizes,
                                          uint32_t hullCount, char* error, size_t errorSize) {
        PluginScriptHost& self = Host(host);
        if (!Valid(host, owner)) {
            WriteError("colliderAddConvexHulls needs an existing entity", error, errorSize);
            return 0;
        }
        if (hullCount > 0 && (!points || !hullSizes)) {
            WriteError("colliderAddConvexHulls needs points and hull sizes", error, errorSize);
            return 0;
        }
        // Check every hull before creating anything, so a bad one adds nothing.
        size_t total = 0;
        for (uint32_t h = 0; h < hullCount; ++h) {
            if (hullSizes[h] < 4) {
                WriteError("hull " + std::to_string(h) + " has fewer than 4 points", error, errorSize);
                return 0;
            }
            total += hullSizes[h];
        }
        for (size_t i = 0; i < total * 3; ++i) {
            if (!std::isfinite(points[i])) {
                WriteError("a hull point is not finite", error, errorSize);
                return 0;
            }
        }
        Scene& scene = *self.m_Scene;
        Entity ownerEntity(static_cast<entt::entity>(ToEntityId(owner)), &scene);
        const float* p = points;
        for (uint32_t h = 0; h < hullCount; ++h) {
            float sum[3] = {0.0f, 0.0f, 0.0f};
            for (uint32_t k = 0; k < hullSizes[h]; ++k) {
                for (int c = 0; c < 3; ++c) sum[c] += p[3 * k + c];
            }
            const float inverse = 1.0f / static_cast<float>(hullSizes[h]);
            const Vec3 center(sum[0] * inverse, sum[1] * inverse, sum[2] * inverse);
            Entity collider = scene.CreateEntity("Collision");
            collider.GetComponent<TransformComponent>().position = center;
            PhysicsShapeComponent shape;
            shape.type = PhysicsShapeType::ConvexHull;
            for (uint32_t k = 0; k < hullSizes[h]; ++k) {
                shape.points.push_back(Vec3(p[3 * k] - center.x, p[3 * k + 1] - center.y, p[3 * k + 2] - center.z));
            }
            collider.AddComponent<PhysicsShapeComponent>(shape);
            auto& body = collider.AddComponent<RigidBodyComponent>();
            body.isStatic = true;
            body.motion = RigidBodyMotion::Static;
            body.mass = 0.0f;
            body.useGravity = false;
            body.layer = 1;
            scene.SetParent(collider, ownerEntity, false);
            p += 3 * static_cast<size_t>(hullSizes[h]);
        }
        return 1;
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

void PluginScriptHost::DestroyInstance(uint64_t key) {
    const auto it = m_Instances.find(key);
    if (it == m_Instances.end()) {
        return;
    }
    const Instance instance = it->second;
    m_Instances.erase(it);
    instance.language->destroyInstance(instance.runtime, instance.handle);
}

namespace {

// The script, and its Inspector values, that a slot of an entity runs.
struct ScriptSource {
    const std::string* path = nullptr;
    const std::unordered_map<std::string, PropertyValue>* variables = nullptr;
};

ScriptSource SourceFor(entt::registry& registry, entt::entity handle, LuaScriptSlot slot) {
    if (!registry.valid(handle)) {
        return {};
    }
    if (slot == LuaScriptSlot::Gameplay) {
        const auto* component = registry.try_get<LuaScriptComponent>(handle);
        return component && component->enabled && !component->scriptPath.empty()
            ? ScriptSource{&component->scriptPath, &component->variables} : ScriptSource{};
    }
    const auto* character = registry.try_get<CharacterComponent>(handle);
    return character && character->runController && !character->controllerScriptPath.empty()
        ? ScriptSource{&character->controllerScriptPath, &character->controllerVariables} : ScriptSource{};
}

} // namespace

uint64_t PluginScriptHost::Key(uint32_t entityId, LuaScriptSlot slot) {
    return (static_cast<uint64_t>(entityId) << 1) | (slot == LuaScriptSlot::CharacterController ? 1u : 0u);
}

uint32_t PluginScriptHost::EntityOf(uint64_t key) {
    return static_cast<uint32_t>(key >> 1);
}

LuaScriptSlot PluginScriptHost::SlotOf(uint64_t key) {
    return (key & 1u) ? LuaScriptSlot::CharacterController : LuaScriptSlot::Gameplay;
}

void PluginScriptHost::Update(Scene& scene, float deltaTime) {
    m_Scene = &scene;
    m_DeltaTime = deltaTime;
    m_UpdateOrder.clear();
    auto& registry = scene.GetRegistry();
    const ScriptLanguageRegistry& languages = ScriptLanguageRegistry::Instance();

    if (m_Lua) {
        m_Lua->BeginScriptPass();
    }
    UpdateGameModules(deltaTime);
    std::vector<uint64_t> stale;
    for (const auto& [key, instance] : m_Instances) {
        const ScriptSource source = SourceFor(registry, static_cast<entt::entity>(EntityOf(key)), SlotOf(key));
        if (!source.path || *source.path != instance.scriptPath) {
            stale.push_back(key);
        }
    }
    for (uint64_t key : stale) {
        DestroyInstance(key);
    }
    if (languages.Languages().empty()) {
        if (m_Lua) {
            m_Lua->EndScriptPass(scene);
        }
        PruneDestroyed();
        return;
    }

    // Gameplay scripts first, then character controllers, as Lua orders them.
    std::vector<uint64_t> candidates;
    for (entt::entity handle : registry.view<LuaScriptComponent>()) {
        candidates.push_back(Key(static_cast<uint32_t>(handle), LuaScriptSlot::Gameplay));
    }
    for (entt::entity handle : registry.view<CharacterComponent>()) {
        candidates.push_back(Key(static_cast<uint32_t>(handle), LuaScriptSlot::CharacterController));
    }
    for (uint64_t key : candidates) {
        const uint32_t entityId = EntityOf(key);
        ScriptSource source = SourceFor(registry, static_cast<entt::entity>(entityId), SlotOf(key));
        if (!source.path) {
            continue;
        }
        const std::string scriptPath = *source.path;
        const HPScriptLanguage* language = languages.FindForPath(scriptPath);
        if (!language) {
            continue;
        }
        const std::filesystem::path resolved = m_Lua
            ? m_Lua->ResolveScriptPath(scriptPath)
            : std::filesystem::path(scriptPath);
        const auto writeTime = WriteTime(resolved);

        auto existing = m_Instances.find(key);
        if (existing != m_Instances.end() && existing->second.writeTime != writeTime) {
            DestroyInstance(key);
            existing = m_Instances.end();
            // The old script's teardown may have spawned, which can move component storage.
            source = SourceFor(registry, static_cast<entt::entity>(entityId), SlotOf(key));
            if (!source.path || *source.path != scriptPath) {
                continue;
            }
        }
        if (existing == m_Instances.end()) {
            const auto failed = m_Failed.find(key);
            if (failed != m_Failed.end() && failed->second.scriptPath == scriptPath &&
                failed->second.writeTime == writeTime) {
                continue;
            }
            void* runtime = RuntimeFor(language);
            if (!runtime) {
                continue;
            }
            std::vector<HPNamedValue> variables;
            variables.reserve(source.variables->size());
            for (const auto& [name, value] : *source.variables) {
                variables.push_back(HPNamedValue{name.c_str(), ToHPValue(value)});
            }
            const std::string resolvedText = resolved.string();
            const std::string path = scriptPath;
            HPScriptInstanceInfo info{};
            info.entity = ToHandle(entityId);
            info.scriptPath = path.c_str();
            info.resolvedPath = resolvedText.c_str();
            info.variables = variables.data();
            info.variableCount = static_cast<uint32_t>(variables.size());
            char error[1024] = {};
            void* handleOut = language->createInstance(runtime, &info, error, sizeof(error));
            if (!handleOut) {
                m_Failed[key] = FailedScript{path, writeTime};
                Log(HP_LOG_ERROR, "[" + std::string(language->displayName) + "] " + path +
                    ": " + (error[0] ? error : "script failed to load"));
                continue;
            }
            m_Failed.erase(key);
            m_Instances[key] = Instance{language, runtime, handleOut, path, writeTime, false};
        }
        m_UpdateOrder.push_back(key);
    }

    for (uint64_t key : m_UpdateOrder) {
        if (ReloadPending()) {
            break;
        }
        auto it = m_Instances.find(key);
        if (it == m_Instances.end() || it->second.awake) {
            continue;
        }
        it->second.awake = true;
        if (it->second.language->awake) {
            it->second.language->awake(it->second.runtime, it->second.handle);
        }
    }
    for (uint64_t key : m_UpdateOrder) {
        if (ReloadPending()) {
            break;
        }
        auto it = m_Instances.find(key);
        if (it != m_Instances.end() && it->second.language->update) {
            it->second.language->update(it->second.runtime, it->second.handle, deltaTime);
        }
    }
    if (m_Lua) {
        m_Lua->EndScriptPass(scene);
    }
    PruneDestroyed();
}

bool PluginScriptHost::ReloadPending() const {
    return m_Lua && m_Lua->IsSceneReloadRequested();
}

bool PluginScriptHost::EntityAlive(uint32_t entityId) const {
    return m_Scene && m_Scene->GetRegistry().valid(static_cast<entt::entity>(entityId));
}

void PluginScriptHost::PruneDestroyed() {
    std::vector<uint64_t> gone;
    for (const auto& [key, instance] : m_Instances) {
        if (!EntityAlive(EntityOf(key))) {
            gone.push_back(key);
        }
    }
    for (uint64_t key : gone) {
        DestroyInstance(key);
    }
}

void PluginScriptHost::LateUpdate(Scene& scene, float deltaTime) {
    m_Scene = &scene;
    m_DeltaTime = deltaTime;
    PruneDestroyed();
    for (uint64_t key : m_UpdateOrder) {
        if (ReloadPending()) {
            break;
        }
        auto it = m_Instances.find(key);
        if (it != m_Instances.end() && EntityAlive(EntityOf(key)) && it->second.language->lateUpdate) {
            it->second.language->lateUpdate(it->second.runtime, it->second.handle, deltaTime);
        }
    }
}

void PluginScriptHost::DispatchContactEvents(Scene& scene,
                                             const std::vector<Physics::PhysicsContactEvent>& events) {
    m_Scene = &scene;
    PruneDestroyed();
    for (const Physics::PhysicsContactEvent& event : events) {
        if (ReloadPending()) {
            break;
        }
        HPContactEvent contact{};
        contact.kind = static_cast<uint32_t>(event.kind);
        contact.other = event.otherAlive ? ToHandle(event.other) : 0;
        contact.hasContact = event.hasContact ? 1 : 0;
        contact.point = HPVec3{event.pointX, event.pointY, event.pointZ};
        contact.normal = HPVec3{event.normalX, event.normalY, event.normalZ};
        contact.relativeVelocity = HPVec3{event.relativeVelocityX, event.relativeVelocityY, event.relativeVelocityZ};
        contact.penetration = event.penetration;
        for (LuaScriptSlot slot : {LuaScriptSlot::CharacterController, LuaScriptSlot::Gameplay}) {
            auto it = m_Instances.find(Key(event.receiver, slot));
            if (it != m_Instances.end() && EntityAlive(event.receiver) && it->second.language->contact) {
                it->second.language->contact(it->second.runtime, it->second.handle, &contact);
            }
        }
    }
}

void PluginScriptHost::DispatchAnimationFinished(Scene& scene) {
    m_Scene = &scene;
    PruneDestroyed();
    // Callbacks can destroy characters, so the finished clips are read before any runs.
    std::vector<std::pair<uint32_t, std::string>> finished;
    auto view = scene.GetRegistry().view<CharacterComponent>();
    for (entt::entity handle : view) {
        const CharacterComponent& character = view.get<CharacterComponent>(handle);
        if (character.player.finishedThisFrame && !character.player.finishedClip.empty()) {
            finished.emplace_back(static_cast<uint32_t>(handle), character.player.finishedClip);
        }
    }
    for (const auto& [entityId, clip] : finished) {
        for (LuaScriptSlot slot : {LuaScriptSlot::CharacterController, LuaScriptSlot::Gameplay}) {
            if (ReloadPending() || !EntityAlive(entityId)) {
                break;
            }
            auto it = m_Instances.find(Key(entityId, slot));
            if (it == m_Instances.end()) {
                continue;
            }
            const HPScriptLanguage& language = *it->second.language;
            if (language.structSize >= offsetof(HPScriptLanguage, animationFinished) + sizeof(language.animationFinished) &&
                language.animationFinished) {
                language.animationFinished(it->second.runtime, it->second.handle, clip.c_str());
            }
        }
    }
}

void PluginScriptHost::UpdateGameModules(float deltaTime) {
    if (!m_GameModulesStarted) {
        m_GameModulesStarted = true;
        for (const HPGameModule* module : ScriptLanguageRegistry::Instance().GameModules()) {
            void* handle = module->beginPlay(&m_Api);
            if (handle) {
                m_GameSessions.push_back({module, handle});
            } else {
                Log(HP_LOG_ERROR, std::string("Game module '") + module->displayName + "' failed to start");
            }
        }
    }
    for (const GameSession& session : m_GameSessions) {
        if (ReloadPending()) {
            return;
        }
        session.module->update(session.handle, deltaTime);
    }
}

void PluginScriptHost::StopGameModules() {
    // Reverse start order, so a module started later never outlives one it may depend on.
    while (!m_GameSessions.empty()) {
        const GameSession session = m_GameSessions.back();
        m_GameSessions.pop_back();
        session.module->endPlay(session.handle);
    }
    m_GameModulesStarted = false;
    m_Archives.Clear();
    for (size_t handle : m_SessionMeshes) {
        if (m_MeshSink.release) {
            m_MeshSink.release(handle);
        }
    }
    m_SessionMeshes.clear();
}

void PluginScriptHost::OnSimulationStopped() {
    StopGameModules();
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
                                          const std::vector<PropertyValue>& args, LuaScriptSlot slot) {
    auto it = m_Instances.find(Key(entityId, slot));
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
                                   const HPValue* args, uint32_t argCount, LuaScriptSlot slot) {
    if (!functionName || !*functionName) {
        return false;
    }
    std::vector<PropertyValue> values;
    values.reserve(argCount);
    for (uint32_t i = 0; i < argCount; ++i) {
        values.push_back(ToPropertyValue(args[i]));
    }
    if (CallEntityFunction(entityId, functionName, values, slot)) {
        return true;
    }
    if (!m_Lua || !m_Scene || !m_Scene->GetRegistry().valid(static_cast<entt::entity>(entityId))) {
        return false;
    }
    return m_Lua->CallEntityFunction(functionName, Entity(static_cast<entt::entity>(entityId), m_Scene), values, slot);
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
    return Self(host).ForwardCall(ToEntityId(entity), function, args, argCount, LuaScriptSlot::Gameplay) ? 1 : 0;
}

size_t PluginScriptHost::HostGetProjectRoot(void* host, char* buffer, size_t bufferSize) {
    const LuaScriptEngine* lua = Self(host).m_Lua;
    if (!lua || lua->GetProjectRoot().empty()) {
        return 0;
    }
    return CopyOut(lua->GetProjectRoot().string(), buffer, bufferSize);
}
