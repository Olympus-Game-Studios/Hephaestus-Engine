/**
 * @file CharacterControllerSystem.h
 * @brief Jolt CharacterVirtual locomotion. Not the animation CharacterComponent.
 *
 * Jump is legal only while the ground state is OnGround. Coyote time and jump
 * buffering are not part of General Character Controller 1.0. A later
 * jump-policy or launch API would be required before a caller can jump after
 * leaving a ledge. This milestone does not add that API.
 */

#pragma once

#include "CharacterControllerMath.h"

#include "../Core/Math.h"

#include <entt/entt.hpp>
#include <cstdint>
#include <memory>

class Scene;

namespace Physics {

class JoltPhysicsEngine;
class JoltPhysicsSystem;

struct CharacterControllerState {
    Vec3 position{};
    Vec3 velocity{};
    float planarSpeed = 0.0f;
    float verticalSpeed = 0.0f;
    CharacterGroundState groundState = CharacterGroundState::InAir;
    bool grounded = false;
    Vec3 groundNormal{0.0f, 1.0f, 0.0f};
    Vec3 groundVelocity{};
    entt::entity supportingEntity = entt::null;
    bool crouched = false;
    bool prone = false;
    bool standBlocked = false;
    bool onSteepSlope = false;
};

class CharacterControllerSystem {
public:
    CharacterControllerSystem(JoltPhysicsEngine& engine, JoltPhysicsSystem& physics);
    ~CharacterControllerSystem();

    CharacterControllerSystem(const CharacterControllerSystem&) = delete;
    CharacterControllerSystem& operator=(const CharacterControllerSystem&) = delete;

    void SyncLifecycle(Scene& scene);
    void FixedStep(Scene& scene, float fixedDt);
    void Clear();

    void SetMoveInput(entt::entity entity, float worldX, float worldZ);
    void ClearMoveInput(entt::entity entity);
    void SetSpeedMultiplier(entt::entity entity, float multiplier);
    float GetSpeedMultiplier(entt::entity entity) const;
    void Jump(entt::entity entity);
    void JumpWithSpeed(entt::entity entity, float speed);
    bool SetLegacyMove(entt::entity entity, float worldX, float worldZ, float speed);
    void SetCrouched(entt::entity entity, bool crouched);
    // Prone takes priority over crouch; getting up waits until the taller capsule fits.
    void SetProne(entt::entity entity, bool prone);
    void SetFacingYaw(entt::entity entity, float radians);
    void Teleport(entt::entity entity, const Vec3& worldFeetPosition);
    bool GetState(entt::entity entity, CharacterControllerState& out) const;
    bool HasRuntime(entt::entity entity) const;

    int ActiveCount() const;
    std::uint32_t ProxyBodyKey(entt::entity entity) const;

private:
    struct Runtime;

    JoltPhysicsEngine& m_Engine;
    JoltPhysicsSystem& m_Physics;
    Scene* m_Scene = nullptr;
    struct RuntimeStore;
    std::unique_ptr<RuntimeStore> m_Store;

    Runtime* Find(entt::entity entity);
    const Runtime* Find(entt::entity entity) const;
    void DestroyRuntime(entt::entity entity);
};

} // namespace Physics
