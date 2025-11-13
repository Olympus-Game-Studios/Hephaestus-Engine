#include "JoltPhysicsSystem.h"
#include "../Scene/Components.h"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

JPH::Quat EulerXYZToQuat(const Vec3& euler)
{
    JPH::Quat qx = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), euler.x);
    JPH::Quat qy = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), euler.y);
    JPH::Quat qz = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), euler.z);
    return qx * qy * qz;
}

Vec3 QuatToEulerXYZ(const JPH::Quat& quat)
{
    Vec3 euler{};

    const float qw = quat.GetW();
    const float qx = quat.GetX();
    const float qy = quat.GetY();
    const float qz = quat.GetZ();

    const float sinr_cosp = 2.0f * (qw * qx + qy * qz);
    const float cosr_cosp = 1.0f - 2.0f * (qx * qx + qy * qy);
    euler.x = std::atan2(sinr_cosp, cosr_cosp);

    float sinp = 2.0f * (qw * qy - qz * qx);
    sinp = std::clamp(sinp, -1.0f, 1.0f);
    euler.y = std::asin(sinp);

    const float siny_cosp = 2.0f * (qw * qz + qx * qy);
    const float cosy_cosp = 1.0f - 2.0f * (qy * qy + qz * qz);
    euler.z = std::atan2(siny_cosp, cosy_cosp);

    return euler;
}

} // namespace

namespace Physics {

JoltPhysicsSystem::JoltPhysicsSystem(JoltPhysicsEngine& physicsEngine)
    : m_PhysicsEngine(physicsEngine)
{
}

JoltPhysicsSystem::~JoltPhysicsSystem() {
    // Clean up all bodies
    std::vector<JPH::BodyID> bodies;
    bodies.reserve(m_EntityToBodyMap.size());
    for (const auto& pair : m_EntityToBodyMap) {
        bodies.push_back(pair.second);
    }
    for (auto bodyId : bodies) {
        DestroyRigidBody(bodyId);
    }
    m_EntityToBodyMap.clear();
    m_BodyToEntityMap.clear();
}

void JoltPhysicsSystem::Update(Scene& scene, float deltaTime) {
    if (!m_Enabled) {
        return;
    }

    UpdateEntityBodies(scene);

    if (m_EntityToBodyMap.empty()) {
        return;
    }

    // Step the physics world and then push results back into components
    m_PhysicsEngine.Update(deltaTime);

    auto view = scene.GetEntitiesWith<TransformComponent, RigidBodyComponent>();
    for (auto handle : view) {
        entt::entity entity = handle;
        auto it = m_EntityToBodyMap.find(entity);
        if (it != m_EntityToBodyMap.end()) {
            SyncTransformFromPhysics(scene, entity, it->second);
        }
    }
}

JPH::BodyID JoltPhysicsSystem::CreateRigidBody(
    Scene& scene,
    entt::entity entity,
    bool isStatic,
    bool isSensor)
{
    Entity sceneEntity(entity, &scene);

    if (!sceneEntity.HasComponent<TransformComponent>() || !sceneEntity.HasComponent<RigidBodyComponent>()) {
        return JPH::BodyID();
    }

    auto& transform = sceneEntity.GetComponent<TransformComponent>();
    auto& rigidBody = sceneEntity.GetComponent<RigidBodyComponent>();
    rigidBody.InvalidateBodyHandle();

    rigidBody.isStatic = rigidBody.isStatic || isStatic || rigidBody.mass <= 0.0f;
    const bool treatAsStatic = rigidBody.IsStatic();
    const bool treatAsSensor = rigidBody.IsSensor() || isSensor;

    JPH::RefConst<JPH::Shape> shape;

    auto buildShape = [&](const PhysicsShapeComponent& shapeComp) -> JPH::RefConst<JPH::Shape> {
        switch (shapeComp.type) {
            case PhysicsShapeType::Box: {
                JPH::BoxShapeSettings settings(JPH::Vec3(
                    std::max(shapeComp.extents.x, 0.001f),
                    std::max(shapeComp.extents.y, 0.001f),
                    std::max(shapeComp.extents.z, 0.001f)));
                auto result = settings.Create();
                if (!result.HasError()) {
                    return result.Get();
                }
                std::cerr << "Failed to create box collision shape: " << result.GetError().c_str() << "\n";
                break;
            }
            case PhysicsShapeType::Sphere: {
                JPH::SphereShapeSettings settings(std::max(shapeComp.radius, 0.001f));
                auto result = settings.Create();
                if (!result.HasError()) {
                    return result.Get();
                }
                std::cerr << "Failed to create sphere collision shape: " << result.GetError().c_str() << "\n";
                break;
            }
            case PhysicsShapeType::Capsule: {
                float halfHeight = std::max(shapeComp.height * 0.5f, 0.001f);
                JPH::CapsuleShapeSettings settings(halfHeight, std::max(shapeComp.radius, 0.001f));
                auto result = settings.Create();
                if (!result.HasError()) {
                    return result.Get();
                }
                std::cerr << "Failed to create capsule collision shape: " << result.GetError().c_str() << "\n";
                break;
            }
            default:
                break;
        }
        return {};
    };

    if (sceneEntity.HasComponent<PhysicsShapeComponent>()) {
        shape = buildShape(sceneEntity.GetComponent<PhysicsShapeComponent>());
    }

    if (!shape) {
        JPH::BoxShapeSettings fallback(JPH::Vec3(0.5f, 0.5f, 0.5f));
        auto result = fallback.Create();
        if (result.HasError()) {
            std::cerr << "Failed to create fallback collision shape: " << result.GetError().c_str() << "\n";
            return JPH::BodyID();
        }
        shape = result.Get();
    }

    JPH::BodyCreationSettings bodySettings(
        shape,
        JPH::RVec3(transform.position.x, transform.position.y, transform.position.z),
        EulerXYZToQuat(transform.rotation),
        treatAsStatic ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic,
        0  // Object layer
    );

    bodySettings.mIsSensor = treatAsSensor;
    bodySettings.mLinearVelocity = JPH::Vec3(
        rigidBody.linearVelocity.x, 
        rigidBody.linearVelocity.y, 
        rigidBody.linearVelocity.z
    );
    bodySettings.mAngularVelocity = JPH::Vec3(
        rigidBody.angularVelocity.x,
        rigidBody.angularVelocity.y,
        rigidBody.angularVelocity.z
    );
    bodySettings.mRestitution = rigidBody.restitution;
    bodySettings.mFriction = rigidBody.friction;
    bodySettings.mLinearDamping = rigidBody.linearDamping;
    bodySettings.mAngularDamping = rigidBody.angularDamping;
    bodySettings.mGravityFactor = rigidBody.UsesGravity() ? 1.0f : 0.0f;
    
    if (!treatAsStatic) {
        const float targetMass = std::max(rigidBody.mass, 0.001f);
        bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
        JPH::MassProperties massProps = shape->GetMassProperties();
        massProps.ScaleToMass(targetMass);
        if (rigidBody.freezeRotation) {
            massProps.mInertia = JPH::Mat44::sZero();
        }
        bodySettings.mMassPropertiesOverride = massProps;
    }

    JPH::BodyInterface& bodyInterface = m_PhysicsEngine.GetBodyInterface();

    JPH::Body* pBody = bodyInterface.CreateBody(bodySettings);
    if (!pBody) {
        std::cerr << "Failed to create Jolt physics body\n";
        return JPH::BodyID();
    }

    JPH::BodyID bodyId = pBody->GetID();
    bodyInterface.AddBody(bodyId, treatAsStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);

    // Map entity to body
    m_EntityToBodyMap[entity] = bodyId;
    m_BodyToEntityMap[bodyId.GetIndex()] = entity;

    rigidBody.joltBodyId = bodyId.GetIndexAndSequenceNumber();

    std::cout << "Created physics body for entity" << std::endl;

    return bodyId;
}

void JoltPhysicsSystem::DestroyRigidBody(JPH::BodyID bodyId) {
    if (bodyId.IsInvalid()) {
        return;
    }

    auto it = m_BodyToEntityMap.find(bodyId.GetIndex());
    if (it != m_BodyToEntityMap.end()) {
        m_EntityToBodyMap.erase(it->second);
        m_BodyToEntityMap.erase(it);
    }

    JPH::BodyInterface& bodyInterface = m_PhysicsEngine.GetBodyInterface();
    if (bodyInterface.IsAdded(bodyId)) {
        bodyInterface.RemoveBody(bodyId);
    }
    bodyInterface.DestroyBody(bodyId);
}

JPH::BodyID JoltPhysicsSystem::GetBodyID(entt::entity entity) const {
    auto it = m_EntityToBodyMap.find(entity);
    return it != m_EntityToBodyMap.end() ? it->second : JPH::BodyID();
}

void JoltPhysicsSystem::SyncTransformToPhysics(Scene& scene, entt::entity entity, JPH::BodyID bodyId, bool activateBody) {
    Entity sceneEntity(entity, &scene);

    if (!sceneEntity.HasComponent<TransformComponent>() || !sceneEntity.HasComponent<RigidBodyComponent>()) {
        return;
    }

    auto& transform = sceneEntity.GetComponent<TransformComponent>();
    auto& rigidBody = sceneEntity.GetComponent<RigidBodyComponent>();
    JPH::BodyInterface& bodyInterface = m_PhysicsEngine.GetBodyInterface();

    if (!bodyInterface.IsAdded(bodyId)) {
        return;
    }

    JPH::RVec3 position(transform.position.x, transform.position.y, transform.position.z);
    JPH::Quat rotation = EulerXYZToQuat(transform.rotation);
    bodyInterface.SetPositionAndRotation(
        bodyId,
        position,
        rotation,
        activateBody && rigidBody.IsDynamic() ? JPH::EActivation::Activate : JPH::EActivation::DontActivate
    );

    if (activateBody && rigidBody.IsDynamic()) {
        bodyInterface.SetLinearVelocity(bodyId, JPH::Vec3(
            rigidBody.linearVelocity.x,
            rigidBody.linearVelocity.y,
            rigidBody.linearVelocity.z
        ));
        bodyInterface.SetAngularVelocity(bodyId, JPH::Vec3(
            rigidBody.angularVelocity.x,
            rigidBody.angularVelocity.y,
            rigidBody.angularVelocity.z
        ));
    }
}

void JoltPhysicsSystem::SyncTransformFromPhysics(Scene& scene, entt::entity entity, JPH::BodyID bodyId) {
    Entity sceneEntity(entity, &scene);

    if (!sceneEntity.HasComponent<TransformComponent>() || !sceneEntity.HasComponent<RigidBodyComponent>()) {
        return;
    }

    auto& transform = sceneEntity.GetComponent<TransformComponent>();
    auto& rigidBody = sceneEntity.GetComponent<RigidBodyComponent>();
    auto& bodyInterface = m_PhysicsEngine.GetBodyInterface();

    // Skip if body ID is invalid
    if (bodyId.IsInvalid()) {
        return;
    }

    // Safely check if body exists before accessing
    if (!bodyInterface.IsAdded(bodyId)) {
        return;
    }

    JPH::RVec3 joltPos = bodyInterface.GetCenterOfMassPosition(bodyId);
    transform.position.x = static_cast<float>(joltPos.GetX());
    transform.position.y = static_cast<float>(joltPos.GetY());
    transform.position.z = static_cast<float>(joltPos.GetZ());

    JPH::Quat joltRot = bodyInterface.GetRotation(bodyId);
    transform.rotation = QuatToEulerXYZ(joltRot);

    JPH::Vec3 linVel = bodyInterface.GetLinearVelocity(bodyId);
    rigidBody.linearVelocity.x = linVel.GetX();
    rigidBody.linearVelocity.y = linVel.GetY();
    rigidBody.linearVelocity.z = linVel.GetZ();

    JPH::Vec3 angVel = bodyInterface.GetAngularVelocity(bodyId);
    rigidBody.angularVelocity.x = angVel.GetX();
    rigidBody.angularVelocity.y = angVel.GetY();
    rigidBody.angularVelocity.z = angVel.GetZ();
}

void JoltPhysicsSystem::UpdateEntityBodies(Scene& scene) {
    RemoveStaleBodies(scene);

    auto view = scene.GetEntitiesWith<TransformComponent, RigidBodyComponent>();
    for (auto handle : view) {
        entt::entity entity = handle;
        Entity sceneEntity(entity, &scene);
        auto& rigidBody = sceneEntity.GetComponent<RigidBodyComponent>();

        auto it = m_EntityToBodyMap.find(entity);
        if (it == m_EntityToBodyMap.end()) {
            JPH::BodyID bodyId = CreateRigidBody(scene, entity, rigidBody.IsStatic(), rigidBody.IsSensor());
            if (!bodyId.IsInvalid()) {
                rigidBody.joltBodyId = bodyId.GetIndexAndSequenceNumber();
                SyncTransformToPhysics(scene, entity, bodyId, true);
            }
        } else {
            if (rigidBody.IsStatic() || rigidBody.IsSensor()) {
                SyncTransformToPhysics(scene, entity, it->second, false);
            }
        }
    }
}

void JoltPhysicsSystem::RemoveStaleBodies(Scene& scene) {
    entt::registry& registry = scene.GetRegistry();

    for (auto it = m_EntityToBodyMap.begin(); it != m_EntityToBodyMap.end(); ) {
        entt::entity entity = it->first;
        bool entityValid = registry.valid(entity);
        bool hasRigidBody = entityValid && registry.any_of<RigidBodyComponent>(entity);

        if (!entityValid || !hasRigidBody) {
            JPH::BodyID bodyId = it->second;
            ++it;
            DestroyRigidBody(bodyId);
        } else {
            ++it;
        }
    }
}

} // namespace Physics
