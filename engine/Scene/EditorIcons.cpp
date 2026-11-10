#include "EditorIcons.h"

#include "CharacterComponent.h"
#include "Components.h"
#include "../Scripting/LuaScriptComponent.h"

namespace Hephaestus {

namespace {

bool IsVisibleThing(const entt::registry& registry, entt::entity entity)
{
    return registry.any_of<MeshRendererComponent, CharacterComponent, PhysicsShapeComponent, RigidBodyComponent>(entity);
}

// A scripted parent of visible children (a target made of meshes, say) is already found through them.
bool HasVisibleDescendant(const entt::registry& registry, entt::entity entity)
{
    const auto* transform = registry.try_get<TransformComponent>(entity);
    if (!transform) {
        return false;
    }
    for (entt::entity child : transform->children) {
        if (registry.valid(child) && (IsVisibleThing(registry, child) || HasVisibleDescendant(registry, child))) {
            return true;
        }
    }
    return false;
}

} // namespace

const char* EditorIconFileName(EditorIcon icon)
{
    switch (icon) {
        case EditorIcon::PointLight: return "PointLight.png";
        case EditorIcon::SpotLight: return "SpotLight.png";
        case EditorIcon::DirectionalLight: return "DirectionalLight.png";
        case EditorIcon::AudioSource: return "AudioSource.png";
        case EditorIcon::ParticleEmitter: return "ParticleEmitter.png";
        case EditorIcon::Environment: return "Environment.png";
        case EditorIcon::ProbeVolume: return "ProbeVolume.png";
        case EditorIcon::Script: return "Script.png";
        case EditorIcon::Trigger: return "Trigger.png";
        case EditorIcon::Count: break;
    }
    return "";
}

std::optional<EditorIcon> EditorIconFor(const entt::registry& registry, entt::entity entity)
{
    if (const auto* light = registry.try_get<LightComponent>(entity)) {
        switch (light->type) {
            case LightType::Directional: return EditorIcon::DirectionalLight;
            case LightType::Spot: return EditorIcon::SpotLight;
            default: return EditorIcon::PointLight;
        }
    }
    if (registry.all_of<CameraComponent>(entity)) {
        return std::nullopt;
    }
    if (registry.all_of<ParticleEmitterComponent>(entity)) return EditorIcon::ParticleEmitter;
    if (registry.all_of<AudioSourceComponent>(entity)) return EditorIcon::AudioSource;
    if (registry.all_of<EnvironmentComponent>(entity)) return EditorIcon::Environment;
    if (registry.all_of<ProbeVolumeComponent>(entity)) return EditorIcon::ProbeVolume;
    if (const auto* body = registry.try_get<RigidBodyComponent>(entity);
        body && body->isTrigger && !registry.any_of<MeshRendererComponent, CharacterComponent>(entity)) {
        return EditorIcon::Trigger;
    }
    if (IsVisibleThing(registry, entity)) {
        return std::nullopt;
    }
    if (registry.all_of<LuaScriptComponent>(entity) && !HasVisibleDescendant(registry, entity)) {
        return EditorIcon::Script;
    }
    return std::nullopt;
}

} // namespace Hephaestus
