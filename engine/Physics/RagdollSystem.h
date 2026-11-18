#pragma once

#include "../Core/Math.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>

#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class Scene;

namespace Physics {

class JoltPhysicsEngine;
class JoltPhysicsSystem;

// Collision group of every character controller's inner body; ragdoll parts never collide with it.
inline constexpr JPH::CollisionGroup::GroupID kCharacterProxyGroup = JPH::CollisionGroup::cInvalidGroup - 1;

struct RagdollStart {
    Vec3 impulse{0.0f, 0.0f, 0.0f};
    Vec3 impulsePoint{0.0f, 0.0f, 0.0f};
    bool hasImpulsePoint = false;
};

// Physics-driven poses for humanoid Characters. Start replaces the entity's Character Controller and the colliders
// of its children with jointed capsules built from the skeleton; after every physics step the capsules are written
// back into the Character's bone pose. Hits on the capsules report the entity.
class RagdollSystem {
public:
    RagdollSystem(JoltPhysicsEngine& engine, JoltPhysicsSystem& physics);
    ~RagdollSystem();

    RagdollSystem(const RagdollSystem&) = delete;
    RagdollSystem& operator=(const RagdollSystem&) = delete;

    bool Start(Scene& scene, entt::entity entity, const RagdollStart& start, std::string& outError);
    bool IsActive(entt::entity entity) const;
    int ActiveCount() const;
    std::vector<JPH::BodyID> PartBodies(entt::entity entity) const;
    // Whether a body is part of any ragdoll; character controllers move through them.
    bool IsPart(const JPH::BodyID& bodyId) const;

    void AfterStep(Scene& scene);
    void Clear();

private:
    struct Ragdoll;
    JoltPhysicsEngine& m_Engine;
    JoltPhysicsSystem& m_Physics;
    std::vector<std::unique_ptr<Ragdoll>> m_Ragdolls;
    std::uint32_t m_NextGroup = 1;
    std::unordered_set<std::uint32_t> m_PartBodies;

    void Destroy(Ragdoll& ragdoll);
};

} // namespace Physics
