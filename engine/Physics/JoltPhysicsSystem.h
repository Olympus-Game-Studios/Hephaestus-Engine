#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <Jolt/Jolt.h>

#include "JoltPhysicsEngine.h"
#include "../Scene/Scene.h"
#include "../Core/Math.h"

namespace Physics {

struct RaycastHit {
    bool hit = false;
    float distance = 0.0f;
    Vec3 point{0.0f, 0.0f, 0.0f};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    entt::entity entity = entt::null;
};

// A snapshot is published only after the body is added and every field below
// was read under a successful lock. Callers must not treat a failed read as
// a default transform, velocity, or sensor flag.
enum class BodyObservationStatus {
    Ready,
    Missing,
    Unreadable,
};

struct BodyObservation {
    entt::entity entity = entt::null;
    std::string motionType;
    bool active = false;
    bool sleeping = false;
    bool sensor = false;
    Vec3 position;
    float rotationX = 0.0f;
    float rotationY = 0.0f;
    float rotationZ = 0.0f;
    float rotationW = 0.0f;
    Vec3 linearVelocity;
    Vec3 angularVelocity;
    std::uint32_t debugBodyIndex = 0;
};

/**
 * @class JoltPhysicsSystem
 * @brief ECS system that manages physics simulation using Jolt Physics
 * 
 * Updates and synchronizes physics bodies with entity transforms and components.
 * Handles creation, destruction, and synchronization of physics bodies.
 */
class JoltPhysicsSystem {
public:
    explicit JoltPhysicsSystem(JoltPhysicsEngine& physicsEngine);
    ~JoltPhysicsSystem();

    // Update physics simulation for the scene
    void Update(Scene& scene, float deltaTime);

    // Create a physics body for an entity
    JPH::BodyID CreateRigidBody(
        Scene& scene,
        entt::entity entity,
        bool isStatic = false,
        bool isSensor = false
    );

    // Destroy a physics body
    void DestroyRigidBody(JPH::BodyID bodyId);

    // Get physics body ID from entity
    JPH::BodyID GetBodyID(entt::entity entity) const;

    // Reverse lookup body -> entity
    entt::entity GetEntityForBody(JPH::BodyID bodyId) const;

    // World raycast; optionally ignores a single entity's body. Fills outHit.
    bool Raycast(entt::entity ignoreEntity, const Vec3& origin, const Vec3& direction,
                 float maxDistance, RaycastHit& outHit, const std::uint16_t* gameplayLayer = nullptr) const;

    BodyObservationStatus ObserveBody(entt::entity entity, BodyObservation& out) const;
    void ObserveBodies(std::vector<BodyObservation>& out) const;

    // Enable/disable the system
    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    bool IsEnabled() const { return m_Enabled; }

    // Set the project root for resolving relative mesh paths
    void SetProjectRoot(const std::filesystem::path& root) { m_ProjectRoot = root; }
    const std::filesystem::path& GetProjectRoot() const { return m_ProjectRoot; }

    // Clear all physics bodies (used when exiting play mode)
    void ClearAllBodies();

private:
    JoltPhysicsEngine& m_PhysicsEngine;
    std::unordered_map<entt::entity, JPH::BodyID> m_EntityToBodyMap;
    std::unordered_map<uint32_t, entt::entity> m_BodyToEntityMap;
    std::filesystem::path m_ProjectRoot;
    bool m_Enabled = true;

    // Sync transform from component to physics body
    void SyncTransformToPhysics(Scene& scene, entt::entity entity, JPH::BodyID bodyId, bool activateBody);

    // Sync transform from physics body to component
    void SyncTransformFromPhysics(Scene& scene, entt::entity entity, JPH::BodyID bodyId);

    // Update entity bodies in the scene
    void UpdateEntityBodies(Scene& scene);
    void RemoveStaleBodies(Scene& scene);

    // Integrate mesh colliders with dynamic bodies
    void IntegrateMeshColliders(Scene& scene);
};

} // namespace Physics
