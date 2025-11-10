#include "JoltPhysicsSystem.h"
#include "../Scene/Components.h"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <iostream>

namespace Physics {

JoltPhysicsSystem::JoltPhysicsSystem(JoltPhysicsEngine& physicsEngine)
    : m_PhysicsEngine(physicsEngine)
{
}

JoltPhysicsSystem::~JoltPhysicsSystem() {
    // Clean up all bodies
    for (auto& [entity, bodyId] : m_EntityToBodyMap) {
        DestroyRigidBody(bodyId);
    }
    m_EntityToBodyMap.clear();
    m_BodyToEntityMap.clear();
}

void JoltPhysicsSystem::Update(Scene& scene, float deltaTime) {
    if (!m_Enabled) {
        return;
    }

    // Update the physics engine
    m_PhysicsEngine.Update(deltaTime);

    // Update entity bodies
    UpdateEntityBodies(scene);

    // Sync physics transforms back to components
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

    if (!sceneEntity.HasComponent<TransformComponent>()) {
        return JPH::BodyID();
    }

    auto& transform = sceneEntity.GetComponent<TransformComponent>();
    auto& rigidBody = sceneEntity.GetComponent<RigidBodyComponent>();

    // Create default box shape - using new style
    JPH::ShapeSettings::ShapeResult shapeResult = JPH::BoxShapeSettings(JPH::Vec3(0.5f, 0.5f, 0.5f)).Create();
    JPH::RefConst<JPH::Shape> shape = shapeResult.Get();

    if (sceneEntity.HasComponent<PhysicsShapeComponent>()) {
        auto& shapeComp = sceneEntity.GetComponent<PhysicsShapeComponent>();
        
        switch (shapeComp.type) {
            case PhysicsShapeType::Box: {
                JPH::BoxShapeSettings boxSettings(JPH::Vec3(shapeComp.extents.x, shapeComp.extents.y, shapeComp.extents.z));
                shape = boxSettings.Create().Get();
                break;
            }
            case PhysicsShapeType::Sphere: {
                JPH::SphereShapeSettings sphereSettings(shapeComp.radius);
                shape = sphereSettings.Create().Get();
                break;
            }
            case PhysicsShapeType::Capsule: {
                JPH::CapsuleShapeSettings capsuleSettings(shapeComp.height * 0.5f, shapeComp.radius);
                shape = capsuleSettings.Create().Get();
                break;
            }
            default:
                break;
        }
    }

    // Create the body settings
    JPH::BodyCreationSettings bodySettings(
        shape,
        JPH::RVec3(transform.position.x, transform.position.y, transform.position.z),
        JPH::Quat(),
        isStatic ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic,
        0  // Object layer
    );

    bodySettings.mLinearVelocity = JPH::Vec3(
        rigidBody.linearVelocity.x, 
        rigidBody.linearVelocity.y, 
        rigidBody.linearVelocity.z
    );
    bodySettings.mRestitution = rigidBody.restitution;
    bodySettings.mFriction = rigidBody.friction;
    bodySettings.mLinearDamping = rigidBody.linearDamping;
    bodySettings.mAngularDamping = rigidBody.angularDamping;
    
    if (!isStatic) {
        bodySettings.mMassPropertiesOverride.mMass = rigidBody.mass;
    }

    // Create the body
    JPH::Body* pBody = m_PhysicsEngine.GetBodyInterface().CreateBody(bodySettings);
    if (!pBody) {
        std::cerr << "Failed to create Jolt physics body\n";
        return JPH::BodyID();
    }

    JPH::BodyID bodyId = pBody->GetID();

    // Map entity to body
    m_EntityToBodyMap[entity] = bodyId;
    m_BodyToEntityMap[bodyId.GetIndex()] = entity;

    std::cout << "Created physics body for entity\n";

    return bodyId;
}

void JoltPhysicsSystem::DestroyRigidBody(JPH::BodyID bodyId) {
    auto it = m_BodyToEntityMap.find(bodyId.GetIndex());
    if (it != m_BodyToEntityMap.end()) {
        m_EntityToBodyMap.erase(it->second);
        m_BodyToEntityMap.erase(it);
    }

    m_PhysicsEngine.GetBodyInterface().DestroyBody(bodyId);
}

JPH::BodyID JoltPhysicsSystem::GetBodyID(entt::entity entity) const {
    auto it = m_EntityToBodyMap.find(entity);
    return it != m_EntityToBodyMap.end() ? it->second : JPH::BodyID();
}

void JoltPhysicsSystem::SyncTransformToPhysics(Scene& scene, entt::entity entity, JPH::BodyID bodyId) {
    Entity sceneEntity(entity, &scene);

    if (!sceneEntity.HasComponent<TransformComponent>()) {
        return;
    }

    auto& transform = sceneEntity.GetComponent<TransformComponent>();
    auto& bodyInterface = m_PhysicsEngine.GetBodyInterface();

    if (bodyInterface.IsAdded(bodyId)) {
        JPH::RVec3 joltPos(transform.position.x, transform.position.y, transform.position.z);
        bodyInterface.SetPosition(bodyId, joltPos, JPH::EActivation::DontActivate);
    }
}

void JoltPhysicsSystem::SyncTransformFromPhysics(Scene& scene, entt::entity entity, JPH::BodyID bodyId) {
    Entity sceneEntity(entity, &scene);

    if (!sceneEntity.HasComponent<TransformComponent>()) {
        return;
    }

    auto& transform = sceneEntity.GetComponent<TransformComponent>();
    auto& bodyInterface = m_PhysicsEngine.GetBodyInterface();

    if (bodyInterface.IsAdded(bodyId)) {
        JPH::RVec3 joltPos = bodyInterface.GetCenterOfMassPosition(bodyId);
        transform.position.x = static_cast<float>(joltPos.GetX());
        transform.position.y = static_cast<float>(joltPos.GetY());
        transform.position.z = static_cast<float>(joltPos.GetZ());
    }
}

void JoltPhysicsSystem::UpdateEntityBodies(Scene& scene) {
    auto view = scene.GetEntitiesWith<TransformComponent, RigidBodyComponent>();

    // Create bodies for entities that don't have them
    for (auto handle : view) {
        entt::entity entity = handle;
        if (m_EntityToBodyMap.find(entity) == m_EntityToBodyMap.end()) {
            Entity sceneEntity(entity, &scene);
            auto& rigidBody = sceneEntity.GetComponent<RigidBodyComponent>();
            CreateRigidBody(scene, entity, rigidBody.isStatic, rigidBody.isTrigger);
        }
    }

    // Sync transforms for existing bodies
    for (auto& [entity, bodyId] : m_EntityToBodyMap) {
        Entity sceneEntity(entity, &scene);
        if (sceneEntity.HasComponent<TransformComponent>()) {
            SyncTransformToPhysics(scene, entity, bodyId);
        }
    }
}

} // namespace Physics
