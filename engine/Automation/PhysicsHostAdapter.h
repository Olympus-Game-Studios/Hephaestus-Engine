#pragma once

// Host-only translation from a live Jolt world into automation snapshots.
// HephaestusAutomationCore does not include this header.

#include "AutomationBackend.h"
#include "../Physics/CharacterControllerSystem.h"
#include "../Physics/CharacterMotor.h"
#include "../Physics/JoltPhysicsEngine.h"
#include "../Physics/JoltPhysicsSystem.h"
#include "../Scene/Scene.h"

#include <string>
#include <vector>

namespace Hephaestus::Automation {

inline bool PhysicsWorldReady(bool initialized, Physics::JoltPhysicsEngine* engine,
                              Physics::JoltPhysicsSystem* system, std::string& outError)
{
    if (!initialized || !engine || !system || !engine->GetPhysicsSystem()) {
        outError = "unavailable";
        return false;
    }
    return true;
}

inline void FillPhysicsSnapshot(const Physics::BodyObservation& source, Scene* scene,
                                AutomationBackend::PhysicsBodySnapshot& destination)
{
    destination.entity = source.entity;
    destination.entityValid = source.entity != entt::null && scene &&
                              scene->GetRegistry().valid(source.entity);
    destination.motionType = source.motionType;
    destination.active = source.active;
    destination.sleeping = source.sleeping;
    destination.sensor = source.sensor;
    destination.position = {source.position.x, source.position.y, source.position.z};
    destination.rotation = {source.rotationX, source.rotationY, source.rotationZ, source.rotationW};
    destination.linearVelocity = {source.linearVelocity.x, source.linearVelocity.y, source.linearVelocity.z};
    destination.angularVelocity = {source.angularVelocity.x, source.angularVelocity.y, source.angularVelocity.z};
    destination.debugBodyIndex = source.debugBodyIndex;
}

inline bool CastPhysicsRay(bool initialized, Physics::JoltPhysicsEngine* engine,
                           Physics::JoltPhysicsSystem* system, Scene* scene,
                           const AutomationBackend::PhysicsVec3& origin,
                           const AutomationBackend::PhysicsVec3& direction, float maxDistance,
                           entt::entity ignore, AutomationBackend::PhysicsRaycastResult& out,
                           std::string& outError, const std::string* layerName = nullptr)
{
    if (!PhysicsWorldReady(initialized, engine, system, outError)) return false;

    const std::uint16_t* layer = nullptr;
    std::uint16_t resolved = 0;
    if (layerName && !layerName->empty()) {
        if (!engine->TryResolveLayerToken(*layerName, resolved, outError)) return false;
        layer = &resolved;
    }

    Physics::RaycastHit hit;
    if (!system->Raycast(ignore, Vec3(origin.x, origin.y, origin.z),
                         Vec3(direction.x, direction.y, direction.z), maxDistance, hit, layer)) {
        outError = "unavailable";
        return false;
    }

    AutomationBackend::PhysicsRaycastResult result;
    result.hit = hit.hit;
    if (hit.hit) {
        result.entity = hit.entity;
        result.entityValid = hit.entity != entt::null && scene && scene->GetRegistry().valid(hit.entity);
        result.point = {hit.point.x, hit.point.y, hit.point.z};
        result.normal = {hit.normal.x, hit.normal.y, hit.normal.z};
        result.distance = hit.distance;
    }
    out = result;
    return true;
}

inline bool ListPhysicsSnapshots(bool initialized, Physics::JoltPhysicsEngine* engine,
                                 Physics::JoltPhysicsSystem* system, Scene* scene,
                                 std::vector<AutomationBackend::PhysicsBodySnapshot>& out,
                                 std::string& outError)
{
    if (!PhysicsWorldReady(initialized, engine, system, outError)) return false;

    std::vector<Physics::BodyObservation> observed;
    system->ObserveBodies(observed);
    out.clear();
    out.reserve(observed.size());
    for (const Physics::BodyObservation& body : observed) {
        AutomationBackend::PhysicsBodySnapshot snapshot;
        FillPhysicsSnapshot(body, scene, snapshot);
        out.push_back(std::move(snapshot));
    }
    return true;
}

inline bool ReadPhysicsSnapshot(bool initialized, Physics::JoltPhysicsEngine* engine,
                                Physics::JoltPhysicsSystem* system, Scene* scene, entt::entity entity,
                                AutomationBackend::PhysicsBodySnapshot& out, std::string& outError)
{
    if (!PhysicsWorldReady(initialized, engine, system, outError)) return false;

    Physics::BodyObservation observed;
    const Physics::BodyObservationStatus status = system->ObserveBody(entity, observed);
    if (status == Physics::BodyObservationStatus::Missing) {
        outError = "no_body";
        return false;
    }
    if (status != Physics::BodyObservationStatus::Ready) {
        outError = "unreadable";
        return false;
    }
    FillPhysicsSnapshot(observed, scene, out);
    return true;
}

inline bool ReadCharacterSnapshot(bool initialized, Physics::JoltPhysicsEngine* engine,
                                  Physics::JoltPhysicsSystem* system, Scene* scene, entt::entity entity,
                                  AutomationBackend::PhysicsBodySnapshot& pose, bool& grounded,
                                  std::string& outError,
                                  Physics::CharacterControllerSystem* controllers = nullptr)
{
    Physics::CharacterControllerState state;
    if (controllers && controllers->GetState(entity, state)) {
        pose = {};
        pose.entity = entity;
        pose.entityValid = entity != entt::null && scene && scene->GetRegistry().valid(entity);
        pose.position = {state.position.x, state.position.y, state.position.z};
        pose.linearVelocity = {state.velocity.x, state.velocity.y, state.velocity.z};
        pose.hasCharacterController = true;
        pose.groundState = Physics::CharacterGroundStateName(state.groundState);
        pose.groundNormal = {state.groundNormal.x, state.groundNormal.y, state.groundNormal.z};
        pose.groundVelocity = {state.groundVelocity.x, state.groundVelocity.y, state.groundVelocity.z};
        pose.supportingEntity = state.supportingEntity;
        pose.supportingEntityValid = state.supportingEntity != entt::null && scene
            && scene->GetRegistry().valid(state.supportingEntity);
        pose.planarSpeed = state.planarSpeed;
        pose.verticalSpeed = state.verticalSpeed;
        pose.crouched = state.crouched;
        pose.standBlocked = state.standBlocked;
        pose.onSteepSlope = state.onSteepSlope;
        grounded = state.grounded;
        outError.clear();
        return true;
    }
    if (!ReadPhysicsSnapshot(initialized, engine, system, scene, entity, pose, outError)) return false;
    grounded = Physics::CharacterMotor::IsGrounded(*engine, *system, entity);
    return true;
}

} // namespace Hephaestus::Automation
