#include "ScriptPhysicsBindings.h"

#include <limits>
#include <utility>

#include "../Physics/CharacterControllerMath.h"
#include "../Physics/CharacterControllerSystem.h"
#include "../Physics/CharacterMotor.h"
#include "../Physics/JoltPhysicsEngine.h"
#include "../Physics/JoltPhysicsSystem.h"
#include "../Scene/Scene.h"

namespace {

JPH::BodyID FindBody(const ScriptPhysicsAccess& a, uint32_t entityId)
{
    const entt::entity handle = static_cast<entt::entity>(entityId);
    if (!a.scene || !a.physicsSystem || !a.scene->GetRegistry().valid(handle)) {
        return JPH::BodyID();
    }
    return a.physicsSystem->GetBodyID(handle);
}

bool FindMovableBody(const ScriptPhysicsAccess& a, uint32_t entityId, JPH::BodyID& outBody)
{
    if (!a.physicsEngine) {
        return false;
    }
    outBody = FindBody(a, entityId);
    return !outBody.IsInvalid();
}

bool GetCharacterState(const ScriptPhysicsAccess& a, uint32_t entityId, Physics::CharacterControllerState& out)
{
    return a.characterControllers
        && a.characterControllers->GetState(static_cast<entt::entity>(entityId), out);
}

template <typename Fn>
bool WithCharacterRuntime(const ScriptPhysicsAccessor& access, uint32_t entityId, Fn&& fn)
{
    const ScriptPhysicsAccess a = access();
    const entt::entity entity = static_cast<entt::entity>(entityId);
    if (!a.characterControllers || !a.characterControllers->HasRuntime(entity)) {
        return false;
    }
    fn(*a.characterControllers, entity);
    return true;
}

} // namespace

PhysicsBinding MakeScriptPhysicsBinding(ScriptPhysicsAccessor access)
{
    PhysicsBinding binding{};

    binding.hasBody = [access](uint32_t entityId) -> bool {
        return !FindBody(access(), entityId).IsInvalid();
    };

    binding.getLinearVelocity = [access](uint32_t entityId, Vec3& outVelocity) -> bool {
        const ScriptPhysicsAccess a = access();
        JPH::BodyID bodyId;
        if (!FindMovableBody(a, entityId, bodyId)) {
            return false;
        }
        const JPH::Vec3 vel = a.physicsEngine->GetBodyInterface().GetLinearVelocity(bodyId);
        outVelocity = Vec3(vel.GetX(), vel.GetY(), vel.GetZ());
        return true;
    };

    binding.setLinearVelocity = [access](uint32_t entityId, const Vec3& velocity, bool activate) -> bool {
        const ScriptPhysicsAccess a = access();
        JPH::BodyID bodyId;
        if (!FindMovableBody(a, entityId, bodyId)) {
            return false;
        }
        JPH::BodyInterface& bi = a.physicsEngine->GetBodyInterface();
        bi.SetLinearVelocity(bodyId, JPH::Vec3(velocity.x, velocity.y, velocity.z));
        if (activate) {
            bi.ActivateBody(bodyId);
        }
        return true;
    };

    binding.setAngularVelocity = [access](uint32_t entityId, const Vec3& velocity, bool activate) -> bool {
        const ScriptPhysicsAccess a = access();
        JPH::BodyID bodyId;
        if (!FindMovableBody(a, entityId, bodyId)) {
            return false;
        }
        JPH::BodyInterface& bi = a.physicsEngine->GetBodyInterface();
        bi.SetAngularVelocity(bodyId, JPH::Vec3(velocity.x, velocity.y, velocity.z));
        if (activate) {
            bi.ActivateBody(bodyId);
        }
        return true;
    };

    binding.setRotation = [access](uint32_t entityId, const Vec3& rotationEuler, bool activate) -> bool {
        const ScriptPhysicsAccess a = access();
        JPH::BodyID bodyId;
        if (!FindMovableBody(a, entityId, bodyId)) {
            return false;
        }
        const JPH::Quat qx = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), rotationEuler.x);
        const JPH::Quat qy = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), rotationEuler.y);
        const JPH::Quat qz = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), rotationEuler.z);
        JPH::BodyInterface& bi = a.physicsEngine->GetBodyInterface();
        bi.SetPositionAndRotation(bodyId, bi.GetPosition(bodyId), qx * qy * qz,
                                  activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        return true;
    };

    binding.raycast = [access](const std::vector<uint32_t>& ignoreEntityIds, const Vec3& origin, const Vec3& direction,
                               float maxDistance, PhysicsRaycastHit& outHit, const char* layerName,
                               std::string& outError, bool ignoreSensors) -> bool {
        const ScriptPhysicsAccess a = access();
        if (!a.physicsEngine || !a.physicsSystem) {
            return false;
        }
        const std::uint16_t* layer = nullptr;
        std::uint16_t resolved = 0;
        if (layerName && layerName[0] != '\0') {
            if (!a.physicsEngine->TryResolveLayerToken(layerName, resolved, outError)) {
                return false;
            }
            layer = &resolved;
        }
        std::vector<entt::entity> ignored;
        ignored.reserve(ignoreEntityIds.size());
        for (uint32_t id : ignoreEntityIds) {
            ignored.push_back(static_cast<entt::entity>(id));
        }
        Physics::RaycastHit hit{};
        if (!a.physicsSystem->Raycast(entt::null, origin, direction, maxDistance,
                                      hit, layer, ignoreSensors, &ignored)) {
            return false;
        }
        outHit.sensor = hit.sensor;
        outHit.hit = hit.hit;
        outHit.distance = hit.distance;
        outHit.point = hit.point;
        outHit.normal = hit.normal;
        outHit.entityId = (hit.entity != entt::null)
            ? static_cast<uint32_t>(hit.entity)
            : std::numeric_limits<uint32_t>::max();
        return true;
    };

    binding.isGrounded = [access](uint32_t entityId) -> bool {
        const ScriptPhysicsAccess a = access();
        if (!a.physicsEngine || !a.physicsSystem) {
            return false;
        }
        Physics::CharacterControllerState state;
        if (GetCharacterState(a, entityId, state)) {
            return state.grounded;
        }
        return Physics::CharacterMotor::IsGrounded(*a.physicsEngine, *a.physicsSystem,
                                                   static_cast<entt::entity>(entityId));
    };

    binding.move = [access](uint32_t entityId, float wishX, float wishZ, float speed) -> bool {
        const ScriptPhysicsAccess a = access();
        if (!a.physicsEngine || !a.physicsSystem) {
            return false;
        }
        const entt::entity entity = static_cast<entt::entity>(entityId);
        if (a.characterControllers && a.characterControllers->HasRuntime(entity)) {
            return a.characterControllers->SetLegacyMove(entity, wishX, wishZ, speed);
        }
        return Physics::CharacterMotor::Move(*a.physicsEngine, *a.physicsSystem, entity, wishX, wishZ, speed);
    };

    binding.jump = [access](uint32_t entityId, float impulse) -> bool {
        const ScriptPhysicsAccess a = access();
        if (!a.physicsEngine || !a.physicsSystem) {
            return false;
        }
        const entt::entity entity = static_cast<entt::entity>(entityId);
        if (a.characterControllers && a.characterControllers->HasRuntime(entity)) {
            a.characterControllers->JumpWithSpeed(entity, impulse);
            return true;
        }
        return Physics::CharacterMotor::Jump(*a.physicsEngine, *a.physicsSystem, entity, impulse);
    };

    return binding;
}

CharacterBinding MakeScriptCharacterBinding(ScriptPhysicsAccessor access)
{
    using Physics::CharacterControllerSystem;
    CharacterBinding binding;

    binding.move = [access](uint32_t entityId, float worldX, float worldZ) {
        return WithCharacterRuntime(access, entityId, [&](CharacterControllerSystem& c, entt::entity e) {
            c.SetMoveInput(e, worldX, worldZ);
        });
    };
    binding.jump = [access](uint32_t entityId) {
        return WithCharacterRuntime(access, entityId, [](CharacterControllerSystem& c, entt::entity e) {
            c.Jump(e);
        });
    };
    binding.setCrouched = [access](uint32_t entityId, bool crouched) {
        return WithCharacterRuntime(access, entityId, [&](CharacterControllerSystem& c, entt::entity e) {
            c.SetCrouched(e, crouched);
        });
    };
    binding.setSpeedMultiplier = [access](uint32_t entityId, float multiplier) {
        return WithCharacterRuntime(access, entityId, [&](CharacterControllerSystem& c, entt::entity e) {
            c.SetSpeedMultiplier(e, multiplier);
        });
    };
    binding.teleport = [access](uint32_t entityId, float x, float y, float z) {
        return WithCharacterRuntime(access, entityId, [&](CharacterControllerSystem& c, entt::entity e) {
            c.Teleport(e, Vec3(x, y, z));
        });
    };
    binding.setFacingYaw = [access](uint32_t entityId, float radians) {
        return WithCharacterRuntime(access, entityId, [&](CharacterControllerSystem& c, entt::entity e) {
            c.SetFacingYaw(e, radians);
        });
    };
    binding.getSpeedMultiplier = [access](uint32_t entityId, float& outMultiplier) {
        const ScriptPhysicsAccess a = access();
        if (!a.characterControllers) return false;
        outMultiplier = a.characterControllers->GetSpeedMultiplier(static_cast<entt::entity>(entityId));
        return true;
    };
    binding.isCrouched = [access](uint32_t entityId) {
        Physics::CharacterControllerState state;
        return GetCharacterState(access(), entityId, state) && state.crouched;
    };
    binding.setProne = [access](uint32_t entityId, bool prone) {
        return WithCharacterRuntime(access, entityId, [&](CharacterControllerSystem& c, entt::entity e) {
            c.SetProne(e, prone);
        });
    };
    binding.isProne = [access](uint32_t entityId) {
        Physics::CharacterControllerState state;
        return GetCharacterState(access(), entityId, state) && state.prone;
    };
    binding.isStandBlocked = [access](uint32_t entityId) {
        Physics::CharacterControllerState state;
        return GetCharacterState(access(), entityId, state) && state.standBlocked;
    };
    binding.isGrounded = [access](uint32_t entityId) {
        Physics::CharacterControllerState state;
        return GetCharacterState(access(), entityId, state) && state.grounded;
    };
    binding.getVelocity = [access](uint32_t entityId, Vec3& out) {
        Physics::CharacterControllerState state;
        if (!GetCharacterState(access(), entityId, state)) return false;
        out = state.velocity;
        return true;
    };
    binding.getGroundNormal = [access](uint32_t entityId, Vec3& out) {
        Physics::CharacterControllerState state;
        if (!GetCharacterState(access(), entityId, state)) return false;
        out = state.groundNormal;
        return true;
    };
    binding.getGroundVelocity = [access](uint32_t entityId, Vec3& out) {
        Physics::CharacterControllerState state;
        if (!GetCharacterState(access(), entityId, state)) return false;
        out = state.groundVelocity;
        return true;
    };
    binding.getGroundState = [access](uint32_t entityId, std::string& out) {
        Physics::CharacterControllerState state;
        if (!GetCharacterState(access(), entityId, state)) return false;
        out = Physics::CharacterGroundStateName(state.groundState);
        return true;
    };
    binding.ragdoll = [access](uint32_t entityId, const Vec3& impulse, const Vec3* point, std::string& outError) {
        const ScriptPhysicsAccess a = access();
        if (!a.scene || !a.physicsSystem) {
            outError = "Physics is not running.";
            return false;
        }
        Physics::RagdollStart start;
        start.impulse = impulse;
        if (point) {
            start.impulsePoint = *point;
            start.hasImpulsePoint = true;
        }
        return a.physicsSystem->Ragdolls().Start(*a.scene, static_cast<entt::entity>(entityId), start, outError);
    };
    binding.isRagdoll = [access](uint32_t entityId) {
        const ScriptPhysicsAccess a = access();
        return a.physicsSystem && a.physicsSystem->Ragdolls().IsActive(static_cast<entt::entity>(entityId));
    };

    return binding;
}
