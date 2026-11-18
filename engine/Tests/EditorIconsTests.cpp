#include "Scene/CharacterComponent.h"
#include "Scene/Components.h"
#include "Scene/EditorIcons.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scripting/LuaScriptComponent.h"

#include <iostream>
#include <optional>

using Hephaestus::EditorIcon;
using Hephaestus::EditorIconFor;

namespace {

bool Check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

std::optional<EditorIcon> IconOf(Scene& scene, Entity entity)
{
    return EditorIconFor(scene.GetRegistry(), static_cast<entt::entity>(entity));
}

} // namespace

int main()
{
    bool ok = true;
    Scene scene("Icons");

    Entity spot = scene.CreateEntity("Spot");
    spot.AddComponent<LightComponent>().type = LightType::Spot;
    ok &= Check(IconOf(scene, spot) == EditorIcon::SpotLight, "a spot light shows the spot icon");

    Entity particles = scene.CreateEntity("Embers");
    particles.AddComponent<ParticleEmitterComponent>();
    ok &= Check(IconOf(scene, particles) == EditorIcon::ParticleEmitter, "a particle emitter has an icon");

    Entity audio = scene.CreateEntity("Speaker");
    audio.AddComponent<AudioSourceComponent>();
    ok &= Check(IconOf(scene, audio) == EditorIcon::AudioSource, "an audio source has an icon");

    Entity environment = scene.CreateEntity("Environment");
    environment.AddComponent<EnvironmentComponent>();
    ok &= Check(IconOf(scene, environment) == EditorIcon::Environment, "the environment has an icon");

    Entity director = scene.CreateEntity("WaveDirector");
    director.AddComponent<LuaScriptComponent>();
    ok &= Check(IconOf(scene, director) == EditorIcon::Script, "a script-only entity has an icon");

    Entity camera = scene.CreateEntity("Camera");
    camera.AddComponent<CameraComponent>();
    camera.AddComponent<LuaScriptComponent>();
    ok &= Check(!IconOf(scene, camera), "a camera shows its mesh instead of an icon");

    Entity hitZone = scene.CreateEntity("HitZone");
    hitZone.AddComponent<RigidBodyComponent>();
    hitZone.AddComponent<LuaScriptComponent>();
    ok &= Check(!IconOf(scene, hitZone), "a scripted physics body gets no icon");

    Entity trigger = scene.CreateEntity("Trigger");
    trigger.AddComponent<PhysicsShapeComponent>();
    trigger.AddComponent<RigidBodyComponent>().isTrigger = true;
    ok &= Check(IconOf(scene, trigger) == EditorIcon::Trigger, "an invisible trigger volume has an icon");

    Entity enemy = scene.CreateEntity("Enemy");
    enemy.AddComponent<CharacterComponent>();
    enemy.AddComponent<LuaScriptComponent>();
    ok &= Check(!IconOf(scene, enemy), "a character is already visible");

    Entity target = scene.CreateEntity("SteelTarget");
    target.AddComponent<LuaScriptComponent>();
    Entity plate = scene.CreateEntity("Plate");
    plate.AddComponent<MeshRendererComponent>();
    scene.SetParent(plate, target);
    ok &= Check(!IconOf(scene, target), "a script object with visible children gets no pin");

    Entity group = scene.CreateEntity("Group");
    ok &= Check(!IconOf(scene, group), "an empty entity gets no icon");

    for (size_t i = 0; i < static_cast<size_t>(EditorIcon::Count); ++i) {
        ok &= Check(*Hephaestus::EditorIconFileName(static_cast<EditorIcon>(i)) != '\0', "every icon has a file name");
    }

    if (ok) {
        std::cout << "EditorIconsTests passed\n";
    }
    return ok ? 0 : 1;
}
