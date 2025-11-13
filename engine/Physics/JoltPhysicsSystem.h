#pragma once

#include <memory>
#include <unordered_map>
#include <Jolt/Jolt.h>

#include "JoltPhysicsEngine.h"
#include "../Scene/Scene.h"

namespace Physics {

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

    // Enable/disable the system
    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    bool IsEnabled() const { return m_Enabled; }

private:
    JoltPhysicsEngine& m_PhysicsEngine;
    std::unordered_map<entt::entity, JPH::BodyID> m_EntityToBodyMap;
    std::unordered_map<uint32_t, entt::entity> m_BodyToEntityMap;
    bool m_Enabled = true;

    // Sync transform from component to physics body
    void SyncTransformToPhysics(Scene& scene, entt::entity entity, JPH::BodyID bodyId, bool activateBody);

    // Sync transform from physics body to component
    void SyncTransformFromPhysics(Scene& scene, entt::entity entity, JPH::BodyID bodyId);

    // Update entity bodies in the scene
    void UpdateEntityBodies(Scene& scene);
    void RemoveStaleBodies(Scene& scene);
};

} // namespace Physics
