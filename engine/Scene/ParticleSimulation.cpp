#include "ParticleSimulation.h"

#include "Components.h"
#include "Entity.h"
#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Hephaestus::Particles {
namespace {

constexpr float kGravity = 9.81f;
constexpr float kPi = 3.14159265358979f;

float NextUnit(ParticleEmitterComponent& emitter)
{
    uint32_t x = emitter.rngState ? emitter.rngState : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    emitter.rngState = x;
    return static_cast<float>(x >> 8) / 16777216.0f;
}

float Range(ParticleEmitterComponent& emitter, float low, float high)
{
    return low + (high - low) * NextUnit(emitter);
}

void Integrate(ParticleEmitterComponent& emitter, Particle& particle, float dt)
{
    particle.velocity.y -= kGravity * emitter.gravity * dt;
    if (emitter.drag > 0.0f) {
        particle.velocity = vscale(particle.velocity, std::exp(-emitter.drag * dt));
    }
    particle.position = vadd(particle.position, vscale(particle.velocity, dt));
    particle.rotation += particle.spin * dt;
    particle.age += dt;
}

/// age is how long ago, within this step, the particle left the emitter.
void EmitOne(ParticleEmitterComponent& emitter, const Mat4& world, float age)
{
    if (static_cast<int>(emitter.particles.size()) >= std::max(0, emitter.maxParticles)) {
        return;
    }
    const Vec3 origin(world.m[12], world.m[13], world.m[14]);
    Vec3 axis = vnorm(Vec3(world.m[4], world.m[5], world.m[6]));
    if (vlen(axis) <= 0.0f) axis = Vec3(0.0f, 1.0f, 0.0f);
    const Vec3 helper = std::abs(axis.y) < 0.99f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 tangent = vnorm(vcross(helper, axis));
    const Vec3 bitangent = vcross(axis, tangent);

    // Uniform over the cone's solid angle.
    const float cosMax = std::cos(std::clamp(emitter.spreadDegrees, 0.0f, 180.0f) * kPi / 180.0f);
    const float cosTheta = 1.0f - NextUnit(emitter) * (1.0f - cosMax);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = 2.0f * kPi * NextUnit(emitter);
    const Vec3 direction = vadd(vscale(axis, cosTheta),
                                vadd(vscale(tangent, sinTheta * std::cos(phi)), vscale(bitangent, sinTheta * std::sin(phi))));

    Particle particle;
    particle.position = origin;
    particle.velocity = vscale(direction, Range(emitter, emitter.speedMin, emitter.speedMax));
    particle.lifetime = std::max(1.0e-3f, Range(emitter, emitter.lifetimeMin, emitter.lifetimeMax));
    particle.rotation = 2.0f * kPi * NextUnit(emitter);
    particle.spin = Range(emitter, emitter.spinMin, emitter.spinMax) * kPi / 180.0f;
    particle.sizeScale = 1.0f + std::clamp(emitter.sizeJitter, 0.0f, 1.0f) * (2.0f * NextUnit(emitter) - 1.0f);
    if (age > 0.0f) Integrate(emitter, particle, age);
    if (particle.age < particle.lifetime) emitter.particles.push_back(particle);
}

void Start(ParticleEmitterComponent& emitter, const Mat4& world, bool playing)
{
    emitter.started = true;
    if (emitter.rngState == 0) emitter.rngState = emitter.seed * 2654435761u + 1u;
    emitter.playing = playing;
    emitter.time = 0.0f;
    emitter.emitCarry = 0.0f;
    if (playing) Burst(emitter, world, emitter.burst);
}

} // namespace

void Play(ParticleEmitterComponent& emitter, const Mat4& emitterWorld)
{
    Start(emitter, emitterWorld, true);
}

void Stop(ParticleEmitterComponent& emitter)
{
    emitter.started = true;
    emitter.playing = false;
}

void Burst(ParticleEmitterComponent& emitter, const Mat4& emitterWorld, int count)
{
    if (emitter.rngState == 0) emitter.rngState = emitter.seed * 2654435761u + 1u;
    for (int i = 0; i < count; ++i) EmitOne(emitter, emitterWorld, 0.0f);
}

bool IsFinished(const ParticleEmitterComponent& emitter)
{
    return emitter.started && !emitter.playing && emitter.particles.empty();
}

void StepEmitter(ParticleEmitterComponent& emitter, const Mat4& emitterWorld, float dt)
{
    if (!emitter.started) {
        Start(emitter, emitterWorld, emitter.playOnStart);
    }
    if (dt <= 0.0f) return;

    for (Particle& particle : emitter.particles) Integrate(emitter, particle, dt);
    emitter.particles.erase(std::remove_if(emitter.particles.begin(), emitter.particles.end(),
                                           [](const Particle& p) { return p.age >= p.lifetime; }),
                            emitter.particles.end());

    float remaining = dt;
    const float duration = std::max(1.0e-3f, emitter.duration);
    while (emitter.playing && remaining > 0.0f) {
        const float active = std::min(remaining, duration - emitter.time);
        const float rate = std::max(0.0f, emitter.rate);
        const float before = emitter.emitCarry;
        emitter.emitCarry += rate * active;
        const int count = static_cast<int>(std::floor(emitter.emitCarry));
        emitter.emitCarry -= static_cast<float>(count);
        for (int i = 0; i < count; ++i) {
            // When this particle's emission time came, measured back from the end of the active span.
            const float emittedAt = rate > 0.0f ? (static_cast<float>(i + 1) - before) / rate : 0.0f;
            EmitOne(emitter, emitterWorld, std::max(0.0f, remaining - std::min(active, emittedAt)));
        }
        emitter.time += active;
        remaining -= active;
        if (emitter.time >= duration - 1.0e-6f) {
            if (emitter.loop) {
                emitter.time = 0.0f;
                Burst(emitter, emitterWorld, emitter.burst);
            } else {
                emitter.playing = false;
            }
        }
    }
}

void UpdateScene(Scene& scene, float dt)
{
    std::vector<entt::entity> finished;
    auto view = scene.GetEntitiesWith<ParticleEmitterComponent>();
    for (auto handle : view) {
        auto& emitter = view.get<ParticleEmitterComponent>(handle);
        StepEmitter(emitter, scene.GetWorldTransform(Entity(handle, &scene)), dt);
        if (emitter.destroyWhenDone && IsFinished(emitter)) finished.push_back(handle);
    }
    for (entt::entity handle : finished) {
        Entity entity(handle, &scene);
        if (entity) scene.DestroyEntity(entity);
    }
}

} // namespace Hephaestus::Particles
