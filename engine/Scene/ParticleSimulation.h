#pragma once

#include "../Core/Math.h"

class Scene;
struct ParticleEmitterComponent;

namespace Hephaestus::Particles {

/// Advances one emitter by dt: ages and moves particles, removes dead ones and emits new ones from emitterWorld
/// (its translation is the origin, its +Y column the direction). The same total time gives the same particle
/// count however it is split into frames.
void StepEmitter(ParticleEmitterComponent& emitter, const Mat4& emitterWorld, float dt);

/// Restarts emission from the beginning of a cycle, including its burst.
void Play(ParticleEmitterComponent& emitter, const Mat4& emitterWorld);

/// Stops emitting; live particles finish their lives.
void Stop(ParticleEmitterComponent& emitter);

/// Emits count particles now, playing or not.
void Burst(ParticleEmitterComponent& emitter, const Mat4& emitterWorld, int count);

/// A started emitter that is not playing and has no particles left.
bool IsFinished(const ParticleEmitterComponent& emitter);

/// Steps every emitter in the scene and removes finished emitters marked destroyWhenDone.
void UpdateScene(Scene& scene, float dt);

} // namespace Hephaestus::Particles
