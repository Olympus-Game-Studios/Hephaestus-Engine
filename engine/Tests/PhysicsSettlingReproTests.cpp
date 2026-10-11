// Stage 9A commit 1 — observational settling repro on the pre-fix engine.
//
// Stepping, layer filters, global Jolt init, and the 1 GiB TempAllocatorImpl
// installed by JoltPhysicsEngine::Initialize are unchanged. When that stock
// allocator is still in place, the test swaps in a same-sized recorder that
// delegates every allocation to TempAllocatorImpl and samples GetUsage inside
// Allocate (the high-water mark is gone by the time Update returns).
//
// Recorded result (commit 1, Jolt 5.4.1, stock 1 GiB TempAllocatorImpl).
// Do not rewrite these lines when a later commit changes the production allocator.
//   HISTORIC: survived. 20 cycles x 300 frames at 1/60. Peak GetUsage 9028112 bytes.
//             The documented ~50-60 frame settling crash did not reproduce.
//   HEAVIER:  survived. 300 frames at 1/60. Peak GetUsage 9039344 bytes.
//   Commit 2 fixed block = max(9039344 * 2, one page), rounded up to 1 MiB
//             = 18874368 bytes (18 MiB). The 8-box peak is not the sizing input.

#include "Core/EngineLog.h"
#include "Physics/JoltPhysicsEngine.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr float kFixedDt = 1.0f / 60.0f;
constexpr int kFrames = 300;
constexpr int kHistoricCycles = 20;
constexpr size_t kStockTempBytes = Physics::JoltPhysicsEngine::kStockTempAllocatorBytes;

class PeakRecordingTempAllocator final : public JPH::TempAllocator {
public:
    explicit PeakRecordingTempAllocator(size_t bytes)
        : m_Impl(bytes) {}

    void* Allocate(JPH::uint inSize) override
    {
        void* address = m_Impl.Allocate(inSize);
        const size_t usage = m_Impl.GetUsage();
        if (usage > m_Peak) {
            m_Peak = usage;
        }
        return address;
    }

    void Free(void* inAddress, JPH::uint inSize) override
    {
        m_Impl.Free(inAddress, inSize);
    }

    size_t Peak() const { return m_Peak; }

private:
    JPH::TempAllocatorImpl m_Impl;
    size_t m_Peak = 0;
};

struct SceneBodies {
    entt::entity sample = entt::null;
    std::vector<entt::entity> movers;
};

SceneBodies AddStack(Scene& scene, int count, const Vec3& origin, float jitterScale)
{
    SceneBodies created;
    for (int i = 0; i < count; ++i) {
        Entity box = scene.CreateEntity("StackBox");
        const float jitter = jitterScale * static_cast<float>(i + 1);
        box.GetComponent<TransformComponent>().position = Vec3(
            origin.x + ((i % 2 == 0) ? -jitter : jitter),
            origin.y + 0.5f + static_cast<float>(i) * 1.05f,
            origin.z + 0.012f * static_cast<float>(i));
        auto& body = box.AddComponent<RigidBodyComponent>();
        body.mass = 1.0f;
        body.isStatic = false;
        body.useGravity = true;
        box.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
        if (created.sample == entt::null) {
            created.sample = static_cast<entt::entity>(box);
        }
    }
    return created;
}

void AddFloor(Scene& scene)
{
    Entity floor = scene.CreateEntity("Floor");
    floor.GetComponent<TransformComponent>().position = Vec3(0.0f, -0.5f, 0.0f);
    auto& body = floor.AddComponent<RigidBodyComponent>();
    body.isStatic = true;
    body.mass = 0.0f;
    body.useGravity = false;
    floor.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(30.0f, 0.5f, 30.0f)));
}

SceneBodies BuildHistoricScene(Scene& scene)
{
    AddFloor(scene);
    return AddStack(scene, 8, Vec3(0.0f, 0.0f, 0.0f), 0.02f);
}

SceneBodies BuildHeavierScene(Scene& scene)
{
    AddFloor(scene);
    SceneBodies created;
    for (int pile = 0; pile < 8; ++pile) {
        const float px = static_cast<float>(pile % 4) * 4.0f - 6.0f;
        const float pz = static_cast<float>(pile / 4) * 4.0f - 2.0f;
        SceneBodies pileBodies = AddStack(scene, 8, Vec3(px, 0.0f, pz), 0.015f);
        if (created.sample == entt::null) {
            created.sample = pileBodies.sample;
        }
    }

    for (int mover = 0; mover < 4; ++mover) {
        Entity entity = scene.CreateEntity("Mover");
        entity.GetComponent<TransformComponent>().position = Vec3(
            static_cast<float>(mover) * 3.0f - 4.5f, 1.5f, 6.0f);
        auto& body = entity.AddComponent<RigidBodyComponent>();
        body.mass = 1.0f;
        body.isStatic = false;
        body.useGravity = false;
        body.linearVelocity = Vec3(0.6f, 0.0f, 0.0f);
        entity.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.4f, 0.4f, 0.4f)));
        created.movers.push_back(static_cast<entt::entity>(entity));
    }

    for (int sensor = 0; sensor < 8; ++sensor) {
        Entity entity = scene.CreateEntity("Sensor");
        entity.GetComponent<TransformComponent>().position = Vec3(
            0.04f * static_cast<float>(sensor), 1.0f, 0.04f * static_cast<float>(sensor));
        auto& body = entity.AddComponent<RigidBodyComponent>();
        body.mass = 1.0f;
        body.isStatic = false;
        body.isTrigger = true;
        body.useGravity = false;
        entity.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.75f, 0.75f, 0.75f)));
    }
    return created;
}

void NudgeMovers(Physics::JoltPhysicsEngine& engine, Physics::JoltPhysicsSystem& system,
                 const std::vector<entt::entity>& movers)
{
    if (!engine.GetPhysicsSystem()) {
        return;
    }
    JPH::BodyInterface& bodyInterface = engine.GetBodyInterface();
    for (entt::entity entity : movers) {
        const JPH::BodyID bodyId = system.GetBodyID(entity);
        if (bodyId.IsInvalid() || !bodyInterface.IsAdded(bodyId)) {
            continue;
        }
        bodyInterface.SetLinearVelocity(bodyId, JPH::Vec3(0.6f, 0.0f, 0.0f));
    }
}

struct RunResult {
    bool survived = false;
    int faultFrame = -1;
    std::string fault;
    size_t peakBytes = 0;
    bool measuredPeak = false;
    bool sampleBodyValid = false;
};

RunResult RunScene(Physics::JoltPhysicsEngine& engine, int frames, bool heavier, size_t& ioPeak)
{
    RunResult result;
    PeakRecordingTempAllocator* recorder = nullptr;
    const bool stock = dynamic_cast<JPH::TempAllocatorImpl*>(engine.GetTempAllocator()) != nullptr;
    if (stock) {
        auto replacement = std::make_unique<PeakRecordingTempAllocator>(kStockTempBytes);
        recorder = replacement.get();
        engine.ExchangeTempAllocatorForTest(std::move(replacement)).reset();
        result.measuredPeak = true;
    }

    {
        Scene scene(heavier ? "HeavierSettling" : "HistoricSettling");
        Physics::JoltPhysicsSystem system(engine);
        const SceneBodies bodies = heavier ? BuildHeavierScene(scene) : BuildHistoricScene(scene);
        try {
            for (int frame = 0; frame < frames; ++frame) {
                if (!bodies.movers.empty()) {
                    NudgeMovers(engine, system, bodies.movers);
                }
                system.Update(scene, kFixedDt);
                if ((frame % 50) == 0) {
                    std::cout << (heavier ? "heavier" : "historic") << " frame " << frame << std::endl;
                }
            }
            result.sampleBodyValid = !system.GetBodyID(bodies.sample).IsInvalid();
            system.ClearAllBodies();
            result.survived = true;
        } catch (const std::exception& exception) {
            result.fault = exception.what();
            result.survived = false;
        } catch (...) {
            result.fault = "unknown C++ exception";
            result.survived = false;
        }
    }

    if (recorder) {
        result.peakBytes = recorder->Peak();
        if (result.peakBytes > ioPeak) {
            ioPeak = result.peakBytes;
        }
    }
    return result;
}

} // namespace

int main()
{
    std::cout << "PhysicsSettlingRepro stockTempBytes=" << kStockTempBytes
              << " frames=" << kFrames << " historicCycles=" << kHistoricCycles << std::endl;

    bool ok = true;
    size_t historicPeak = 0;
    bool historicMeasured = false;
    bool historicSurvived = true;
    std::string historicFault;

    for (int cycle = 0; cycle < kHistoricCycles; ++cycle) {
        std::cout << "historic cycle " << cycle << " begin" << std::endl;
        Physics::JoltPhysicsEngine engine;
        engine.Initialize();
        const RunResult cycleResult = RunScene(engine, kFrames, false, historicPeak);
        historicMeasured = historicMeasured || cycleResult.measuredPeak;
        if (!cycleResult.survived || !cycleResult.sampleBodyValid) {
            historicSurvived = false;
            historicFault = cycleResult.fault.empty()
                ? "sample body missing after 300 steps"
                : cycleResult.fault;
            std::cout << "RESULT historic survived=no cycle=" << cycle
                      << " fault=" << historicFault << std::endl;
            engine.Shutdown();
            ok = false;
            break;
        }
        engine.Shutdown();
        std::cout << "historic cycle " << cycle << " end peakBytes=" << historicPeak << std::endl;
    }

    if (historicSurvived) {
        std::cout << "RESULT historic survived=yes cycles=" << kHistoricCycles
                  << " frames=" << kFrames
                  << " peakBytes=" << historicPeak
                  << " measured=" << (historicMeasured ? "yes" : "no") << std::endl;
    }

    size_t heavierPeak = 0;
    bool heavierMeasured = false;
    bool heavierSurvived = false;
    std::string heavierFault;
    {
        Physics::JoltPhysicsEngine engine;
        engine.Initialize();
        const RunResult heavier = RunScene(engine, kFrames, true, heavierPeak);
        heavierMeasured = heavier.measuredPeak;
        heavierSurvived = heavier.survived && heavier.sampleBodyValid;
        heavierFault = heavier.fault.empty() && heavier.survived && !heavier.sampleBodyValid
            ? "sample body missing after 300 steps"
            : heavier.fault;
        engine.Shutdown();
    }

    std::cout << "RESULT heavier survived=" << (heavierSurvived ? "yes" : "no")
              << " frames=" << kFrames
              << " peakBytes=" << heavierPeak
              << " measured=" << (heavierMeasured ? "yes" : "no");
    if (!heavierFault.empty()) {
        std::cout << " fault=" << heavierFault;
    }
    std::cout << std::endl;

    if (historicMeasured && historicPeak == 0) {
        std::cerr << "[FAIL] historic peak was not observed\n";
        ok = false;
    }
    if (heavierMeasured && heavierPeak == 0) {
        std::cerr << "[FAIL] heavier peak was not observed\n";
        ok = false;
    }
    if (!historicSurvived || !heavierSurvived) {
        ok = false;
    }

    const Hephaestus::EngineLogSnapshot log = Hephaestus::EngineLog::Snapshot();
    for (const Hephaestus::EngineLogEntry& entry : log.entries) {
        if (entry.category == "Physics" && entry.message.find("malloc fallback") != std::string::npos) {
            std::cerr << "[FAIL] temp allocator used the malloc fallback: " << entry.message << "\n";
            ok = false;
        }
    }

    if (ok) {
        std::cout << "[PASS] PhysicsSettlingRepro\n";
        return 0;
    }
    std::cerr << "[FAIL] PhysicsSettlingRepro\n";
    return 1;
}
