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

#include "../Core/Math.h"

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

private:
    // Allocators
    std::unique_ptr<JPH::JobSystemThreadPool> m_JobSystem;
    std::unique_ptr<JPH::TempAllocatorImpl> m_TempAllocator;

    // Physics system
    std::unique_ptr<JPH::PhysicsSystem> m_PhysicsSystem;

    // Layer configuration
    void InitializeLayers();
    void InitializeCollisionFilters();
};

} // namespace Physics
