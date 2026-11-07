#include "SimulationStep.h"

#include "../Animation/CharacterPlayback.h"
#include "../Audio/AudioEngine.h"
#include "../Physics/JoltPhysicsSystem.h"
#include "../Scene/Components.h"
#include "../Scene/Scene.h"
#include "../Scripting/LuaScriptEngine.h"
#include "../Scripting/PluginScriptHost.h"
#include "../Scripting/ScriptEngine.h"

namespace Hephaestus {

namespace {

template <typename Fn>
void RunPhysics(const SimulationStepContext& context, Physics::JoltPhysicsSystem*& physics, Fn&& fn)
{
    if (!physics) {
        return;
    }
    if (!context.onPhysicsError) {
        fn(*physics);
        return;
    }
    try {
        fn(*physics);
    } catch (const std::exception& e) {
        physics = nullptr;
        context.onPhysicsError(e);
    }
}

bool ReloadAtSafePoint(const SimulationStepContext& context)
{
    return context.reloadAtSafePoint && context.reloadAtSafePoint();
}

} // namespace

bool RunSimulationStep(const SimulationStepContext& context, float dt)
{
    Scene& scene = *context.scene;
    Physics::JoltPhysicsSystem* physics = context.physics;

    if (context.audio) {
        UpdateSceneAudioSources(scene, *context.audio);
    }
    RunPhysics(context, physics, [&](Physics::JoltPhysicsSystem& p) { p.PrepareSceneBodies(scene); });

    if (context.cppScripts) {
        context.cppScripts->UpdateScripts(dt);
    }
    if (context.lua) {
        context.lua->Update(scene, dt);
    }
    if (ReloadAtSafePoint(context)) return false;
    if (context.pluginScripts) {
        context.pluginScripts->Update(scene, dt);
        if (ReloadAtSafePoint(context)) return false;
    }

    // Fixed-step physics follows scripts so onUpdate can write this frame's motion.
    RunPhysics(context, physics, [&](Physics::JoltPhysicsSystem& p) {
        p.Update(scene, dt);
        if (context.lua) {
            context.lua->DispatchContactEvents(scene, p.FrameContactEvents());
        }
        if (context.pluginScripts) {
            context.pluginScripts->DispatchContactEvents(scene, p.FrameContactEvents());
        }
    });
    if (ReloadAtSafePoint(context)) return false;

    CharacterAnimationSystem::Tick(scene, dt);
    if (context.pluginScripts) {
        context.pluginScripts->DispatchAnimationFinished(scene);
    }
    if (context.lua) {
        context.lua->DispatchAnimationFinished(scene);
        context.lua->LateUpdate(scene, dt);
    }
    if (context.pluginScripts) {
        context.pluginScripts->LateUpdate(scene, dt);
    }
    if (ReloadAtSafePoint(context)) return false;

    if (context.cppScripts) {
        context.cppScripts->LateUpdateScripts(dt);
    }
    return !ReloadAtSafePoint(context);
}

void UpdateSceneAudioSources(Scene& scene, AudioEngine& audioEngine)
{
    auto audioView = scene.GetEntitiesWith<AudioSourceComponent>();
    for (auto entityHandle : audioView) {
        auto& audio = audioView.get<AudioSourceComponent>(entityHandle);

        if (!audio.enabled || audio.clipPath.empty()) {
            if (audio.runtimeHandle != 0) {
                audioEngine.Stop(audio.runtimeHandle);
                audio.runtimeHandle = 0;
            }
            audio.runtimeStarted = false;
            audio.runtimeLastClip.clear();
            continue;
        }

        if (audio.runtimeHandle != 0) {
            if (audio.runtimeLastClip != audio.clipPath || !audioEngine.IsPlaying(audio.runtimeHandle)) {
                audioEngine.Stop(audio.runtimeHandle);
                audio.runtimeHandle = 0;
            } else {
                audioEngine.SetVolume(audio.runtimeHandle, audio.volume);
            }
        }

        if (audio.runtimeHandle == 0 && audio.playOnStart && !audio.runtimeStarted) {
            audio.runtimeHandle = audioEngine.Play(audio.clipPath, audio.loop, audio.volume);
            audio.runtimeStarted = audio.runtimeHandle != 0;
            audio.runtimeLastClip = audio.clipPath;
        }
    }
}

} // namespace Hephaestus
