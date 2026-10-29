#pragma once

#include <exception>
#include <functional>

class AudioEngine;
class LuaScriptEngine;
class PluginScriptHost;
class Scene;
class ScriptEngine;
namespace Physics {
class JoltPhysicsSystem;
}

namespace Hephaestus {

// Systems one gameplay frame drives. Null members are skipped.
struct SimulationStepContext {
    Scene* scene = nullptr;
    AudioEngine* audio = nullptr;
    Physics::JoltPhysicsSystem* physics = nullptr;
    LuaScriptEngine* lua = nullptr;
    PluginScriptHost* pluginScripts = nullptr;
    ScriptEngine* cppScripts = nullptr;
    // Processes a pending scene reload; returns true when the rest of the frame must be skipped.
    std::function<bool()> reloadAtSafePoint;
    // When set, physics exceptions are reported here and physics is skipped for the rest of the frame.
    std::function<void(const std::exception&)> onPhysicsError;
};

// Runs one gameplay frame in the order shared by Editor Play and the packaged game.
// Returns false when a scene reload ended the frame early.
bool RunSimulationStep(const SimulationStepContext& context, float dt);

void UpdateSceneAudioSources(Scene& scene, AudioEngine& audioEngine);

} // namespace Hephaestus
