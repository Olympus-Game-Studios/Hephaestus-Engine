#pragma once

#include <memory>
#include <unordered_map>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>

#include <cstdint>
#include <memory>

#include "../Core/Math.h"
#include "PhysicsLayerConfig.h"

namespace JPH {
class BroadPhaseLayerInterfaceTable;
class ObjectLayerPairFilterTable;
class ObjectVsBroadPhaseLayerFilterTable;
}

namespace Physics {

/**
 * @class JoltPhysicsEngine
 * @brief Wrapper around Jolt Physics engine for Hephaestus Engine
 * 
 * Manages the Jolt physics world, body creation, and simulation updates.
 * Handles the conversion between engine math types and Jolt types.
 */
class JoltPhysicsEngine {
public:
    JoltPhysicsEngine();
    ~JoltPhysicsEngine();

    // Initialize the physics engine
    void Initialize();

    // Shutdown and cleanup
    void Shutdown();

    // Update physics simulation
    void Update(float deltaTime);

    // Get the physics system
    JPH::PhysicsSystem* GetPhysicsSystem() { return m_PhysicsSystem.get(); }
    JPH::BodyInterface& GetBodyInterface() { return m_PhysicsSystem->GetBodyInterface(); }

    // Physics world settings
    void SetGravity(const Vec3& gravity);
    Vec3 GetGravity() const;

    // Max bodies that can exist in the physics world
    static constexpr uint32_t MAX_BODIES = 65536;
    static constexpr uint32_t NUM_BODY_MUTEXES = 0; // Let Jolt determine
    static constexpr uint32_t MAX_BODY_PAIRS = 65536;
    static constexpr uint32_t MAX_CONTACT_CONSTRAINTS = 10240;  // Reduced from 20480

    // Get the allocators
    JPH::TempAllocator* GetTempAllocator() { return m_TempAllocator.get(); }
    JPH::JobSystem* GetJobSystem() { return m_JobSystem.get(); }

    // Historical 1 GiB block measured by PhysicsSettlingRepro before the
    // fallback allocator. The repro swaps this size in only while Initialize
    // still installs a raw TempAllocatorImpl.
    static constexpr size_t kStockTempAllocatorBytes = 1024ull * 1024ull * 1024ull;
    // 18 MiB: twice the heavier-scene peak (9039344 bytes), rounded up to 1 MiB.
    static constexpr size_t kTempAllocatorBytes = 18ull * 1024ull * 1024ull;
    std::unique_ptr<JPH::TempAllocator> ExchangeTempAllocatorForTest(std::unique_ptr<JPH::TempAllocator> replacement);

    // Per-play physics world. Global Jolt types stay registered across End/Begin.
    void BeginPlayWorld(const PhysicsLayerConfig& config);
    void EndPlayWorld();
    const PhysicsLayerConfig& ActiveLayerConfig() const { return m_LayerConfig; }
    JPH::ObjectLayer ObjectLayerFor(std::uint16_t gameplayLayer, bool isStatic) const;
    bool EncodedLayersShouldCollide(std::uint16_t gameplayA, bool staticA,
                                    std::uint16_t gameplayB, bool staticB) const;

private:
    // Allocators
    std::unique_ptr<JPH::JobSystemThreadPool> m_JobSystem;
    std::unique_ptr<JPH::TempAllocator> m_TempAllocator;
    std::unique_ptr<JPH::BroadPhaseLayerInterfaceTable> m_BroadPhase;
    std::unique_ptr<JPH::ObjectLayerPairFilterTable> m_ObjectPairFilter;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> m_ObjectVsBroadPhase;

    // Physics system
    std::unique_ptr<JPH::PhysicsSystem> m_PhysicsSystem;
    PhysicsLayerConfig m_LayerConfig = PhysicsLayerConfig::Compatibility();
    bool m_HoldsGlobalJolt = false;

    void BuildFilterTables(const PhysicsLayerConfig& config);
};

} // namespace Physics
