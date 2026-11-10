#pragma once

#include "../Core/Math.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <vector>

class Scene;

namespace Physics {

class JoltPhysicsEngine;
class JoltPhysicsSystem;

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

    void AfterStep(Scene& scene);
    void Clear();

private:
    struct Ragdoll;
    JoltPhysicsEngine& m_Engine;
    JoltPhysicsSystem& m_Physics;
    std::vector<std::unique_ptr<Ragdoll>> m_Ragdolls;
    std::uint32_t m_NextGroup = 1;

    void Destroy(Ragdoll& ragdoll);
};

} // namespace Physics
