#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/ParticleSimulation.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <cmath>
#include <iostream>
#include <string>

namespace Particles = Hephaestus::Particles;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

ParticleEmitterComponent Emitter()
{
    ParticleEmitterComponent emitter;
    emitter.rate = 50.0f;
    emitter.duration = 10.0f;
    emitter.loop = true;
    emitter.lifetimeMin = emitter.lifetimeMax = 100.0f;
    emitter.maxParticles = 10000;
    emitter.speedMin = emitter.speedMax = 0.0f;
    return emitter;
}

size_t CountAfter(ParticleEmitterComponent emitter, float fps, float seconds)
{
    const Mat4 world = Mat4::Identity();
    const int steps = static_cast<int>(std::lround(seconds * fps));
    for (int i = 0; i < steps; ++i) Particles::StepEmitter(emitter, world, 1.0f / fps);
    return emitter.particles.size();
}

} // namespace

int main()
{
    bool ok = true;
    const Mat4 identity = Mat4::Identity();

    const size_t at30 = CountAfter(Emitter(), 30.0f, 1.0f);
    bool steady = at30 >= 49 && at30 <= 51;
    for (float fps : {60.0f, 144.0f, 240.0f}) {
        const size_t count = CountAfter(Emitter(), fps, 1.0f);
        steady = steady && (count + 1 >= at30) && (count <= at30 + 1);
    }
    ok &= Check(steady, "50 particles a second at 30, 60, 144 and 240 fps");

    ParticleEmitterComponent lifetime = Emitter();
    lifetime.rate = 10.0f;
    lifetime.lifetimeMin = lifetime.lifetimeMax = 0.5f;
    const size_t alive = CountAfter(lifetime, 60.0f, 3.0f);
    ok &= Check(alive >= 4 && alive <= 6, "particles die after their lifetime");

    ParticleEmitterComponent burst = Emitter();
    burst.rate = 0.0f;
    burst.burst = 20;
    burst.loop = false;
    burst.duration = 0.1f;
    burst.lifetimeMin = burst.lifetimeMax = 0.3f;
    Particles::StepEmitter(burst, identity, 1.0f / 60.0f);
    ok &= Check(burst.particles.size() == 20, "a burst emits all its particles at once");
    for (int i = 0; i < 30; ++i) Particles::StepEmitter(burst, identity, 1.0f / 60.0f);
    ok &= Check(Particles::IsFinished(burst), "a non-looping emitter finishes when its particles die");

    ParticleEmitterComponent capped = Emitter();
    capped.maxParticles = 8;
    ok &= Check(CountAfter(capped, 60.0f, 1.0f) == 8, "maxParticles caps the live count");

    ParticleEmitterComponent fall = Emitter();
    fall.rate = 0.0f;
    fall.gravity = 1.0f;
    Particles::StepEmitter(fall, identity, 1.0f / 240.0f);
    Particles::Burst(fall, identity, 1);
    for (int i = 0; i < 240; ++i) Particles::StepEmitter(fall, identity, 1.0f / 240.0f);
    ok &= Check(fall.particles.size() == 1 && std::abs(fall.particles[0].position.y + 4.905f) < 0.05f,
                "gravity drops a particle 4.9 m in a second");

    ParticleEmitterComponent cone = Emitter();
    cone.speedMin = cone.speedMax = 1.0f;
    cone.spreadDegrees = 30.0f;
    Mat4 sideways = Mat4::Identity();  // +Y column along +X
    sideways.m[0] = 0.0f; sideways.m[1] = -1.0f;
    sideways.m[4] = 1.0f; sideways.m[5] = 0.0f;
    Particles::StepEmitter(cone, sideways, 0.0f);
    Particles::Burst(cone, sideways, 200);
    bool inCone = true;
    for (const Particle& p : cone.particles) {
        inCone = inCone && p.velocity.x >= std::cos(30.0f * 3.14159265f / 180.0f) - 1e-4f;
    }
    ok &= Check(inCone && cone.particles.size() == 200, "particles leave within the cone around the emitter's +Y");

    ParticleEmitterComponent a = Emitter(), b = Emitter();
    a.speedMin = b.speedMin = 0.5f;
    a.speedMax = b.speedMax = 2.0f;
    a.spreadDegrees = b.spreadDegrees = 45.0f;
    for (int i = 0; i < 30; ++i) {
        Particles::StepEmitter(a, identity, 1.0f / 30.0f);
        Particles::StepEmitter(b, identity, 1.0f / 30.0f);
    }
    bool same = a.particles.size() == b.particles.size();
    for (size_t i = 0; same && i < a.particles.size(); ++i) {
        same = a.particles[i].position.x == b.particles[i].position.x &&
               a.particles[i].position.y == b.particles[i].position.y;
    }
    ok &= Check(same, "the same seed gives the same particles");

    Scene scene("Particles");
    Entity puff = scene.CreateEntity("Puff");
    auto& component = puff.AddComponent<ParticleEmitterComponent>(burst);
    component.particles.clear();
    component.started = false;
    component.destroyWhenDone = true;
    for (int i = 0; i < 40; ++i) Particles::UpdateScene(scene, 1.0f / 60.0f);
    bool removed = true;
    for (auto handle : scene.GetEntitiesWith<TagComponent>()) {
        removed = removed && Entity(handle, &scene).GetComponent<TagComponent>().tag != "Puff";
    }
    ok &= Check(removed, "destroyWhenDone removes a finished emitter's entity");

    {
        Scene saved("Saved");
        Entity source = saved.CreateEntity("Smoke");
        auto& authored = source.AddComponent<ParticleEmitterComponent>();
        authored.materialPath = "assets/materials/fx/Smoke.hematerial";
        authored.loop = false;
        authored.destroyWhenDone = true;
        authored.rate = 33.0f;
        authored.burst = 7;
        authored.maxParticles = 250;
        authored.spreadDegrees = 42.0f;
        authored.gravity = -0.25f;
        authored.sizeEnd = 1.75f;
        authored.colorStart = Vec4(0.5f, 0.25f, 0.125f, 0.75f);
        authored.seed = 99;
        Scene loaded("Loaded");
        const bool parsed = SceneSerializer::DeserializeFromString(loaded, SceneSerializer(saved).SerializeToString());
        const ParticleEmitterComponent* copy = nullptr;
        for (auto handle : loaded.GetEntitiesWith<ParticleEmitterComponent>()) {
            copy = &loaded.GetRegistry().get<ParticleEmitterComponent>(handle);
        }
        ok &= Check(parsed && copy && copy->materialPath == authored.materialPath && !copy->loop &&
                        copy->destroyWhenDone && copy->rate == 33.0f && copy->burst == 7 && copy->maxParticles == 250 &&
                        copy->spreadDegrees == 42.0f && copy->gravity == -0.25f && copy->sizeEnd == 1.75f &&
                        copy->colorStart.y == 0.25f && copy->colorStart.w == 0.75f && copy->seed == 99u,
                    "a particle emitter survives a scene save and load");
    }

    if (!ok) return 1;
    std::cout << "ParticleSimulationTests passed\n";
    return 0;
}
