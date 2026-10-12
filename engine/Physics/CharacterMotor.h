#pragma once

#include "JoltPhysicsSystem.h"
#include "JoltPhysicsEngine.h"

namespace Physics {

/**
 * Legacy rigid-body helper. Not the supported character controller.
 * New locomotion uses CharacterControllerComponent and JPH::CharacterVirtual.
 * This path remains for a dynamic body with no character controller.
 */
class CharacterMotor {
public:
    static bool IsGrounded(JoltPhysicsEngine& physicsEngine, JoltPhysicsSystem& physicsSystem,
                           entt::entity entity, float probeDistance = 0.25f);

    /// Set world-space XZ linear velocity from wish direction * speed; preserve Y.
    static bool Move(JoltPhysicsEngine& physicsEngine, JoltPhysicsSystem& physicsSystem,
                     entt::entity entity, float wishX, float wishZ, float speed);

    /// If grounded, set Y linear velocity to impulse.
    static bool Jump(JoltPhysicsEngine& physicsEngine, JoltPhysicsSystem& physicsSystem,
                     entt::entity entity, float impulse);
};

} // namespace Physics
