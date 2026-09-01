#pragma once

#include "JoltPhysicsSystem.h"
#include "JoltPhysicsEngine.h"

namespace Physics {

/**
 * Thin FPS capsule helper on top of an existing rigid body.
 * Not CharacterVirtual — wish-dir XZ move, gravity from Jolt, short ground probe.
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
