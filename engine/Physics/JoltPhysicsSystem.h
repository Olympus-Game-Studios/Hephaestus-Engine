#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <Jolt/Jolt.h>

#include "JoltPhysicsEngine.h"
#include "PhysicsContactEvents.h"
#include "PhysicsWorldPose.h"
#include "RagdollSystem.h"
#include "../Scene/Scene.h"
#include "../Core/Math.h"

namespace Physics {

struct PhysicsContactListener;

struct RaycastHit {
    bool hit = false;
    float distance = 0.0f;
    Vec3 point{0.0f, 0.0f, 0.0f};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    entt::entity entity = entt::null;
    bool sensor = false;
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
class CharacterControllerSystem;

class JoltPhysicsSystem {
public:
    explicit JoltPhysicsSystem(JoltPhysicsEngine& physicsEngine);
    ~JoltPhysicsSystem();

    // Non-owning. The controller updates inside each fixed substep and owns its proxies.
    void SetCharacterController(CharacterControllerSystem* controller);
    CharacterControllerSystem* GetCharacterController() const { return m_CharacterController; }
    void RegisterCharacterProxy(entt::entity entity, JPH::BodyID bodyId);
    void UnregisterCharacterProxy(JPH::BodyID bodyId);
    bool IsCharacterProxy(JPH::BodyID bodyId) const;
    // Bodies owned by another system that should report `entity` in raycasts and contacts.
    void RegisterExternalBody(entt::entity entity, JPH::BodyID bodyId);
    void UnregisterExternalBody(JPH::BodyID bodyId);
    RagdollSystem& Ragdolls() { return *m_Ragdolls; }
    const RagdollSystem& Ragdolls() const { return *m_Ragdolls; }

    // When true, a character proxy contact does not let the solver push a dynamic body.
    // CharacterVirtual::HandleContact remains the push. Sensors are unchanged.
    void SetNeutralizeProxyDynamicResponse(bool neutralize);
    bool NeutralizeProxyDynamicResponse() const { return m_NeutralizeProxyDynamicResponse; }

    // Creates and removes bodies without stepping the simulation.
    void PrepareSceneBodies(Scene& scene);

    // Fixed-step update. frameDt is accumulated; each substep is 1/60 s.
    void Update(Scene& scene, float deltaTime);

    // Call RestoreSimulationPoses after rendering and before anything else reads or writes transforms.
    void ApplyRenderInterpolation(Scene& scene);
    void RestoreSimulationPoses(Scene& scene);

    void ClearAccumulator();
    int LastSubstepCount() const { return m_LastSubstepCount; }
    int StalledStepCount() const { return m_StalledStepCount; }
    int BodyCount() const { return static_cast<int>(m_EntityToBodyMap.size()); }
    int FrameContactCount() const { return static_cast<int>(m_FrameContactEvents.size()); }
    float Accumulator() const { return m_Accumulator; }

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
                 float maxDistance, RaycastHit& outHit, const std::uint16_t* gameplayLayer = nullptr,
                 bool ignoreSensors = false,
                 const std::vector<entt::entity>* extraIgnoredEntities = nullptr) const;

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

    void SetContactEventSink(std::function<void(const PhysicsContactEvent&)> sink);
    const std::vector<PhysicsContactEvent>& FrameContactEvents() const { return m_FrameContactEvents; }

private:
    JoltPhysicsEngine& m_PhysicsEngine;
    std::unordered_map<entt::entity, JPH::BodyID> m_EntityToBodyMap;
    std::unordered_map<uint32_t, entt::entity> m_BodyToEntityMap;
    std::filesystem::path m_ProjectRoot;
    bool m_Enabled = true;
    float m_Accumulator = 0.0f;
    int m_LastSubstepCount = 0;
    int m_StalledStepCount = 0;

    // Sync transform from component to physics body
    void SyncTransformToPhysics(Scene& scene, entt::entity entity, JPH::BodyID bodyId, bool activateBody);

    // Sync transform from physics body to component
    void SyncTransformFromPhysics(Scene& scene, entt::entity entity, JPH::BodyID bodyId);

    // Update entity bodies in the scene
    void UpdateEntityBodies(Scene& scene);
    void RemoveStaleBodies(Scene& scene);
    bool DecomposeEntityPose(Scene& scene, entt::entity entity, WorldPose& out) const;
    void LogRejectedPose(entt::entity entity);

    // Integrate mesh colliders with dynamic bodies
    void IntegrateMeshColliders(Scene& scene);

    struct BodyPoseBinding {
        Vec3 shapeScale{1.0f, 1.0f, 1.0f};
        entt::entity parent = entt::null;
    };

    struct InterpolatedPose {
        Vec3 previousPosition;
        Vec3 currentPosition;
        Vec3 previousRotation;
        Vec3 currentRotation;
        bool rotates = false;
        bool applied = false;
    };

    std::unordered_map<entt::entity, InterpolatedPose> m_InterpolatedPoses;
    void CaptureInterpolationStart(Scene& scene);
    void CaptureInterpolationEnd(Scene& scene);

    std::unordered_map<entt::entity, BodyPoseBinding> m_BodyPoseBinding;
    std::unordered_set<entt::entity> m_PoseRejectionLogged;
    std::unordered_map<std::uint64_t, PhysicsOverlapState> m_ContactOverlaps;
    std::unordered_map<std::uint32_t, entt::entity> m_RetiredBodies;
    std::function<void(const PhysicsContactEvent&)> m_ContactSink;
    std::vector<PhysicsContactEvent> m_FrameContactEvents;
    std::unique_ptr<PhysicsContactListener> m_ContactListener;
    CharacterControllerSystem* m_CharacterController = nullptr;
    std::unordered_set<std::uint32_t> m_CharacterProxies;
    std::unordered_set<entt::entity> m_ControllerBodySkipLogged;
    bool m_NeutralizeProxyDynamicResponse = false;

    void EnsureContactListener();
    void DetachContactListener();
    void DispatchContactEvents();
    void ClearContactState();

    // Last, so ragdolls are destroyed while the body maps they unregister from still exist.
    std::unique_ptr<RagdollSystem> m_Ragdolls;
};

} // namespace Physics
