#include "CharacterMotor.h"

#include <cmath>

namespace Physics {

bool CharacterMotor::IsGrounded(JoltPhysicsEngine& physicsEngine, JoltPhysicsSystem& physicsSystem,
                                entt::entity entity, float probeDistance) {
    JPH::BodyID bodyId = physicsSystem.GetBodyID(entity);
    if (bodyId.IsInvalid() || probeDistance <= 0.0f) {
        return false;
    }

    JPH::BodyInterface& bi = physicsEngine.GetBodyInterface();
    JPH::RVec3 pos = bi.GetPosition(bodyId);

    // Probe from slightly above the body center downward.
    const float startOffset = 0.05f;
    Vec3 origin(static_cast<float>(pos.GetX()),
                static_cast<float>(pos.GetY()) + startOffset,
                static_cast<float>(pos.GetZ()));
    Vec3 direction(0.0f, -1.0f, 0.0f);

    RaycastHit hit{};
    if (!physicsSystem.Raycast(entity, origin, direction, probeDistance + startOffset, hit)) {
        return false;
    }
    return hit.hit && hit.normal.y > 0.35f;
}

bool CharacterMotor::Move(JoltPhysicsEngine& physicsEngine, JoltPhysicsSystem& physicsSystem,
                          entt::entity entity, float wishX, float wishZ, float speed) {
    JPH::BodyID bodyId = physicsSystem.GetBodyID(entity);
    if (bodyId.IsInvalid()) {
        return false;
    }

    float wx = wishX;
    float wz = wishZ;
    const float lenSq = wx * wx + wz * wz;
    if (lenSq > 1e-8f) {
        const float inv = 1.0f / std::sqrt(lenSq);
        wx *= inv;
        wz *= inv;
    } else {
        wx = 0.0f;
        wz = 0.0f;
    }

    JPH::BodyInterface& bi = physicsEngine.GetBodyInterface();
    JPH::Vec3 vel = bi.GetLinearVelocity(bodyId);
    float vy = vel.GetY();
    if (vy < 0.0f && IsGrounded(physicsEngine, physicsSystem, entity)) {
        // Do not accumulate downward velocity while walking on a contact.
        // This avoids repeatedly driving the capsule into the floor before
        // the next solver step.
        vy = 0.0f;
    }
    bi.SetLinearVelocity(bodyId, JPH::Vec3(wx * speed, vy, wz * speed));
    bi.ActivateBody(bodyId);
    return true;
}

bool CharacterMotor::Jump(JoltPhysicsEngine& physicsEngine, JoltPhysicsSystem& physicsSystem,
                          entt::entity entity, float impulse) {
    if (!IsGrounded(physicsEngine, physicsSystem, entity)) {
        return false;
    }

    JPH::BodyID bodyId = physicsSystem.GetBodyID(entity);
    if (bodyId.IsInvalid()) {
        return false;
    }

    JPH::BodyInterface& bi = physicsEngine.GetBodyInterface();
    JPH::Vec3 vel = bi.GetLinearVelocity(bodyId);
    bi.SetLinearVelocity(bodyId, JPH::Vec3(vel.GetX(), impulse, vel.GetZ()));
    bi.ActivateBody(bodyId);
    return true;
}

} // namespace Physics
