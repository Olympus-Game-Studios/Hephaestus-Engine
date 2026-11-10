#include "EditorIcons.h"

#include "CharacterComponent.h"
#include "Components.h"
#include "../Scripting/LuaScriptComponent.h"

namespace Hephaestus {

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
    if (registry.any_of<MeshRendererComponent, CharacterComponent, PhysicsShapeComponent, RigidBodyComponent>(entity)) {
        return std::nullopt;
    }
    if (registry.all_of<LuaScriptComponent>(entity)) return EditorIcon::Script;
    return std::nullopt;
}

} // namespace Hephaestus
