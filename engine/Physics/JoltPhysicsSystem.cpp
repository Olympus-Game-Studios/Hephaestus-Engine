#include "JoltPhysicsSystem.h"
#include "../Scene/Components.h"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Math/Float3.h>

#include "../Assets/MeshAsset.h"
#include "../Assets/ModelImporter.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#undef min
#undef max
#endif

namespace {

std::filesystem::path ResolveMeshAssetPath(const std::string& assetPath, const std::filesystem::path& projectRoot = {})
{
    namespace fs = std::filesystem;

    if (assetPath.empty()) {
        return {};
    }

    // Skip built-in resources
    if (assetPath.rfind("builtin://", 0) == 0) {
        return {};
    }

    fs::path path(assetPath);
    std::error_code ec;

    // Absolute path
    if (path.is_absolute() && fs::exists(path, ec)) {
        return path.lexically_normal();
    }

    auto tryRoots = [&](const fs::path& root) -> fs::path {
        if (root.empty()) {
            return {};
        }

        // root / assetPath
        fs::path candidate = (root / path).lexically_normal();
        if (fs::exists(candidate, ec)) {
            return candidate;
        }

        // root / assets / assetPath (handles passing paths without leading "assets/")
        candidate = (root / "assets" / path).lexically_normal();
        if (fs::exists(candidate, ec)) {
            return candidate;
        }

        return {};
    };

    // FIRST: Try the explicitly provided project root
    if (!projectRoot.empty()) {
        fs::path resolved = tryRoots(projectRoot);
        if (!resolved.empty()) {
            return resolved;
        }
    }

    auto tryWalkUp = [&](fs::path start) -> fs::path {
        fs::path cur = start;
        for (int depth = 0; depth < 10 && !cur.empty(); ++depth) {
            fs::path resolved = tryRoots(cur);
            if (!resolved.empty()) {
                return resolved;
            }
            if (!cur.has_parent_path()) {
                break;
            }
            cur = cur.parent_path();
        }
        return {};
    };

    // Try relative to CWD and parents
    {
        fs::path resolved = tryWalkUp(fs::current_path());
        if (!resolved.empty()) {
            return resolved;
        }
    }

    // Try relative to executable directory and parents
    fs::path exeDir;
#ifdef _WIN32
    std::wstring buffer(512, L'\0');
    while (true) {
        DWORD result = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (result == 0) {
            break;
        }
        if (result < buffer.size() - 1) {
            buffer.resize(result);
            exeDir = fs::path(buffer).parent_path();
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
#endif
    if (!exeDir.empty()) {
        fs::path resolved = tryWalkUp(exeDir);
        if (!resolved.empty()) {
            return resolved;
        }
    }

    // Last attempt: if there's an engine-like root with a "projects" folder, scan project roots.
    auto findProjectsRoot = [&](fs::path start) -> fs::path {
        fs::path cur = start;
        for (int depth = 0; depth < 10 && !cur.empty(); ++depth) {
            fs::path projects = (cur / "projects").lexically_normal();
            if (fs::exists(projects, ec) && fs::is_directory(projects, ec)) {
                return projects;
            }
            if (!cur.has_parent_path()) {
                break;
            }
            cur = cur.parent_path();
        }
        return {};
    };

    fs::path projectsRoot;
    projectsRoot = findProjectsRoot(fs::current_path());
    if (projectsRoot.empty() && !exeDir.empty()) {
        projectsRoot = findProjectsRoot(exeDir);
    }

    if (!projectsRoot.empty()) {
        for (const auto& entry : fs::directory_iterator(projectsRoot, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory(ec)) {
                continue;
            }
            fs::path projRoot = entry.path();
            fs::path resolved = tryRoots(projRoot);
            if (!resolved.empty()) {
                return resolved;
            }
        }
    }

    return {};
}

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

    // Integrate mesh colliders: push dynamic Jolt bodies out of mesh collider geometry
    IntegrateMeshColliders(scene);
}

namespace {

struct MeshCacheEntry {
    MeshData mesh;
    std::filesystem::file_time_type lastWrite{};
};

std::unordered_map<std::string, MeshCacheEntry> g_MeshCache;

const MeshData* GetMeshDataForPath(const std::string& meshAssetPath, const std::filesystem::path& projectRoot = {}) {
    std::filesystem::path meshPath = ResolveMeshAssetPath(meshAssetPath, projectRoot);
    if (meshPath.empty()) {
        return nullptr;
    }

    std::error_code ec;
    const std::string key = meshPath.lexically_normal().generic_string();
    auto lastWrite = std::filesystem::last_write_time(meshPath, ec);
    if (ec) {
        lastWrite = std::filesystem::file_time_type::min();
    }

    auto it = g_MeshCache.find(key);
    if (it != g_MeshCache.end() && it->second.lastWrite == lastWrite) {
        return &it->second.mesh;
    }

    MeshData mesh;
    std::string error;
    if (!ModelImporter::LoadModel(meshPath, mesh, error)) {
        std::cerr << "Failed to load mesh for collision: " << error << "\n";
        return nullptr;
    }

    MeshCacheEntry entry;
    entry.mesh = std::move(mesh);
    entry.lastWrite = lastWrite;
    auto result = g_MeshCache.emplace(key, std::move(entry));
    return &result.first->second.mesh;
}

} // namespace

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

    auto buildBoxFromMeshRenderer = [&](const MeshRendererComponent& renderer, const TransformComponent& transform) -> JPH::RefConst<JPH::Shape> {
        Vec3 min = renderer.localBounds.min;
        Vec3 max = renderer.localBounds.max;
        Vec3 half = Vec3(
            std::max((max.x - min.x) * 0.5f, 0.001f),
            std::max((max.y - min.y) * 0.5f, 0.001f),
            std::max((max.z - min.z) * 0.5f, 0.001f)
        );

        Vec3 scale = transform.scale;
        Vec3 halfScaled = Vec3(
            std::max(std::abs(scale.x) * half.x, 0.001f),
            std::max(std::abs(scale.y) * half.y, 0.001f),
            std::max(std::abs(scale.z) * half.z, 0.001f)
        );

        JPH::BoxShapeSettings boxSettings(JPH::Vec3(halfScaled.x, halfScaled.y, halfScaled.z));
        auto boxResult = boxSettings.Create();
        if (boxResult.HasError()) {
            std::cerr << "Failed to create mesh-bounds box collision shape: " << boxResult.GetError().c_str() << "\n";
            return {};
        }
        JPH::RefConst<JPH::Shape> boxShape = boxResult.Get();

        // If the mesh bounds are not centered, offset the shape accordingly.
        Vec3 center = Vec3(
            (min.x + max.x) * 0.5f,
            (min.y + max.y) * 0.5f,
            (min.z + max.z) * 0.5f
        );
        Vec3 centerScaled = Vec3(center.x * scale.x, center.y * scale.y, center.z * scale.z);

        const float centerLenSq = centerScaled.x * centerScaled.x + centerScaled.y * centerScaled.y + centerScaled.z * centerScaled.z;
        if (centerLenSq < 1e-6f) {
            return boxShape;
        }

        JPH::RotatedTranslatedShapeSettings rtSettings(
            JPH::Vec3(centerScaled.x, centerScaled.y, centerScaled.z),
            JPH::Quat::sIdentity(),
            boxShape
        );
        auto rtResult = rtSettings.Create();
        if (rtResult.HasError()) {
            std::cerr << "Failed to create rotated/translated collision shape: " << rtResult.GetError().c_str() << "\n";
            return boxShape;
        }
        return rtResult.Get();
    };

    auto buildMeshFromRenderer = [&](const MeshRendererComponent& renderer, const TransformComponent& transform) -> JPH::RefConst<JPH::Shape> {
        if (!rigidBody.IsStatic()) {
            return {};
        }

        const MeshData* meshData = GetMeshDataForPath(renderer.meshAssetPath);
        if (!meshData || meshData->vertices.empty() || meshData->indices.size() < 3) {
            return {};
        }

        JPH::VertexList vertices;
        vertices.resize(meshData->vertices.size());
        const Vec3 scale = transform.scale;
        for (size_t i = 0; i < meshData->vertices.size(); ++i) {
            const Vertex& v = meshData->vertices[i];
            vertices[i] = JPH::Float3(v.pos[0] * scale.x, v.pos[1] * scale.y, v.pos[2] * scale.z);
        }

        JPH::IndexedTriangleList triangles;
        triangles.reserve(meshData->indices.size() / 3);
        for (size_t i = 0; i + 2 < meshData->indices.size(); i += 3) {
            uint32_t i0 = meshData->indices[i];
            uint32_t i1 = meshData->indices[i + 1];
            uint32_t i2 = meshData->indices[i + 2];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }
            triangles.emplace_back(i0, i1, i2, 0);
        }

        if (triangles.empty()) {
            return {};
        }

        JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
        auto result = settings.Create();
        if (result.HasError()) {
            std::cerr << "Failed to create mesh collision shape: " << result.GetError().c_str() << "\n";
            return {};
        }
        return result.Get();
    };

    auto buildShape = [&](const PhysicsShapeComponent& shapeComp, const TransformComponent& transformComp) -> JPH::RefConst<JPH::Shape> {
        Vec3 scale = transformComp.scale;
        Vec3 absScale = Vec3(std::abs(scale.x), std::abs(scale.y), std::abs(scale.z));

        auto applyOffset = [&](const JPH::RefConst<JPH::Shape>& baseShape) -> JPH::RefConst<JPH::Shape> {
            if (!baseShape) {
                return {};
            }
            Vec3 offsetScaled = Vec3(
                shapeComp.offset.x * scale.x,
                shapeComp.offset.y * scale.y,
                shapeComp.offset.z * scale.z
            );
            const float offsetLenSq = offsetScaled.x * offsetScaled.x + offsetScaled.y * offsetScaled.y + offsetScaled.z * offsetScaled.z;
            if (offsetLenSq < 1e-6f) {
                return baseShape;
            }
            JPH::RotatedTranslatedShapeSettings rtSettings(
                JPH::Vec3(offsetScaled.x, offsetScaled.y, offsetScaled.z),
                JPH::Quat::sIdentity(),
                baseShape
            );
            auto rtResult = rtSettings.Create();
            if (rtResult.HasError()) {
                std::cerr << "Failed to create offset collision shape: " << rtResult.GetError().c_str() << "\n";
                return baseShape;
            }
            return rtResult.Get();
        };

        switch (shapeComp.type) {
            case PhysicsShapeType::Box: {
                Vec3 half = Vec3(
                    std::max(shapeComp.extents.x * absScale.x, 0.001f),
                    std::max(shapeComp.extents.y * absScale.y, 0.001f),
                    std::max(shapeComp.extents.z * absScale.z, 0.001f)
                );
                JPH::BoxShapeSettings settings(JPH::Vec3(half.x, half.y, half.z));
                auto result = settings.Create();
                if (!result.HasError()) {
                    return applyOffset(result.Get());
                }
                std::cerr << "Failed to create box collision shape: " << result.GetError().c_str() << "\n";
                break;
            }
            case PhysicsShapeType::Sphere: {
                float maxAxis = std::max(absScale.x, std::max(absScale.y, absScale.z));
                float radius = std::max(shapeComp.radius * maxAxis, 0.001f);
                JPH::SphereShapeSettings settings(radius);
                auto result = settings.Create();
                if (!result.HasError()) {
                    return applyOffset(result.Get());
                }
                std::cerr << "Failed to create sphere collision shape: " << result.GetError().c_str() << "\n";
                break;
            }
            case PhysicsShapeType::Capsule: {
                float radiusAxis = std::max(absScale.x, absScale.z);
                float radius = std::max(shapeComp.radius * radiusAxis, 0.001f);
                float halfHeight = std::max(shapeComp.height * 0.5f * absScale.y, 0.001f);
                JPH::CapsuleShapeSettings settings(halfHeight, radius);
                auto result = settings.Create();
                if (!result.HasError()) {
                    return applyOffset(result.Get());
                }
                std::cerr << "Failed to create capsule collision shape: " << result.GetError().c_str() << "\n";
                break;
            }
            case PhysicsShapeType::Mesh:
                // Handled outside (needs mesh renderer)
                break;
            default:
                break;
        }
        return {};
    };

    const MeshRendererComponent* renderer = sceneEntity.HasComponent<MeshRendererComponent>()
        ? &sceneEntity.GetComponent<MeshRendererComponent>()
        : nullptr;

    const MeshColliderComponent* meshCollider = sceneEntity.HasComponent<MeshColliderComponent>()
        ? &sceneEntity.GetComponent<MeshColliderComponent>()
        : nullptr;

    const PhysicsShapeComponent* shapeComp = sceneEntity.HasComponent<PhysicsShapeComponent>()
        ? &sceneEntity.GetComponent<PhysicsShapeComponent>()
        : nullptr;

    auto isDefaultBoxShape = [&](const PhysicsShapeComponent& s) -> bool {
        if (s.type != PhysicsShapeType::Box) {
            return false;
        }
        auto nearEq = [](float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; };
        return nearEq(s.extents.x, 0.5f) && nearEq(s.extents.y, 0.5f) && nearEq(s.extents.z, 0.5f)
            && nearEq(s.offset.x, 0.0f) && nearEq(s.offset.y, 0.0f) && nearEq(s.offset.z, 0.0f);
    };

    const bool wantsComplexMesh = (meshCollider && meshCollider->useComplexCollision);
    const bool canBuildMeshShape = wantsComplexMesh && renderer && treatAsStatic;
    const bool hasExplicitNonDefaultShape = (shapeComp && !isDefaultBoxShape(*shapeComp) && shapeComp->type != PhysicsShapeType::Mesh);

    // Prefer a mesh shape for static environment colliders when Complex Collision is enabled.
    // Also allow PhysicsShapeType::Mesh to force mesh-shape generation.
    if (renderer && treatAsStatic && ((shapeComp && shapeComp->type == PhysicsShapeType::Mesh) || (canBuildMeshShape && !hasExplicitNonDefaultShape))) {
        shape = buildMeshFromRenderer(*renderer, transform);
        if (!shape) {
            std::cerr << "[Jolt] MeshShape requested but failed (path='" << (renderer ? renderer->meshAssetPath : std::string())
                      << "', static=" << (treatAsStatic ? "true" : "false") << ")\n";
        }
    }

    // If we still don't have a shape, fall back to an explicit primitive shape if present.
    if (!shape && shapeComp) {
        shape = buildShape(*shapeComp, transform);
    }

    // If no explicit physics shape (or unsupported type), try deriving one from render bounds.
    if (!shape && renderer) {
        shape = buildBoxFromMeshRenderer(*renderer, transform);
    }

    // Final fallback
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

void JoltPhysicsSystem::ClearAllBodies() {
    // Destroy all bodies in the physics engine
    std::vector<JPH::BodyID> bodiesToDestroy;
    
    for (const auto& pair : m_EntityToBodyMap) {
        bodiesToDestroy.push_back(pair.second);
    }
    
    for (JPH::BodyID bodyId : bodiesToDestroy) {
        DestroyRigidBody(bodyId);
    }
    
    // Ensure maps are cleared
    m_EntityToBodyMap.clear();
    m_BodyToEntityMap.clear();
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

void JoltPhysicsSystem::IntegrateMeshColliders(Scene& scene) {
    // Iterate over all mesh colliders (may or may not have RigidBodyComponent)
    auto meshColliderView = scene.GetEntitiesWith<TransformComponent, MeshColliderComponent>();
    
    int meshColliderCount = 0;
    for (auto meshColliderHandle : meshColliderView) {
        meshColliderCount++;
        Entity meshColliderEntity(meshColliderHandle, &scene);
        
        // Skip if this mesh collider is also a rigid body (it handles collision via Jolt)
        if (meshColliderEntity.HasComponent<RigidBodyComponent>()) {
            std::cout << "[MeshCollider] Skipping entity (has RigidBody): " << static_cast<uint32_t>(meshColliderHandle) << "\n";
            continue;
        }
        
        auto& meshColliderTransform = meshColliderEntity.GetComponent<TransformComponent>();
        if (!meshColliderEntity.HasComponent<MeshRendererComponent>()) {
            std::cout << "[MeshCollider] Skipping entity (no MeshRenderer): " << static_cast<uint32_t>(meshColliderHandle) << "\n";
            continue;
        }

        const auto& renderer = meshColliderEntity.GetComponent<MeshRendererComponent>();
        std::cout << "[MeshCollider] Processing entity with mesh path: '" << renderer.meshAssetPath << "' (project root: " << m_ProjectRoot << ")\n";
        
        const MeshData* meshData = GetMeshDataForPath(renderer.meshAssetPath, m_ProjectRoot);
        if (!meshData) {
            std::cout << "[MeshCollider] Failed to load mesh data for path: '" << renderer.meshAssetPath << "'\n";
            continue;
        }
        if (meshData->vertices.empty() || meshData->indices.size() < 3) {
            std::cout << "[MeshCollider] Mesh data empty or invalid (verts=" << meshData->vertices.size() << ", indices=" << meshData->indices.size() << ")\n";
            continue;
        }
        
        std::cout << "[MeshCollider] Loaded mesh: " << meshData->vertices.size() << " verts, " << meshData->indices.size() << " indices\n";

        // Build world-space triangle bounds for this mesh collider
        // Compose TRS matrix manually: Translation * RotationX * RotationY * RotationZ * Scale
        Mat4 scaleMat = Mat4::Scale(meshColliderTransform.scale);
        Mat4 rotX = Mat4::RotationX(meshColliderTransform.rotation.x);
        Mat4 rotY = Mat4::RotationY(meshColliderTransform.rotation.y);
        Mat4 rotZ = Mat4::RotationZ(meshColliderTransform.rotation.z);
        Mat4 rotMat = Mat4::Multiply(Mat4::Multiply(rotX, rotY), rotZ);
        Mat4 meshWorldMat = Mat4::Multiply(Mat4::Multiply(Mat4::Translation(meshColliderTransform.position), rotMat), scaleMat);

        std::vector<AABB> triangleBounds;
        triangleBounds.reserve(meshData->indices.size() / 3);
        for (size_t i = 0; i + 2 < meshData->indices.size(); i += 3) {
            uint32_t i0 = meshData->indices[i];
            uint32_t i1 = meshData->indices[i + 1];
            uint32_t i2 = meshData->indices[i + 2];
            if (i0 >= meshData->vertices.size() || i1 >= meshData->vertices.size() || i2 >= meshData->vertices.size()) {
                continue;
            }

            const Vertex& v0 = meshData->vertices[i0];
            const Vertex& v1 = meshData->vertices[i1];
            const Vertex& v2 = meshData->vertices[i2];
            Vec3 p0 = Mat4::TransformPoint(meshWorldMat, Vec3(v0.pos[0], v0.pos[1], v0.pos[2]));
            Vec3 p1 = Mat4::TransformPoint(meshWorldMat, Vec3(v1.pos[0], v1.pos[1], v1.pos[2]));
            Vec3 p2 = Mat4::TransformPoint(meshWorldMat, Vec3(v2.pos[0], v2.pos[1], v2.pos[2]));

            Vec3 minV(std::min({p0.x, p1.x, p2.x}),
                      std::min({p0.y, p1.y, p2.y}),
                      std::min({p0.z, p1.z, p2.z}));
            Vec3 maxV(std::max({p0.x, p1.x, p2.x}),
                      std::max({p0.y, p1.y, p2.y}),
                      std::max({p0.z, p1.z, p2.z}));
            triangleBounds.emplace_back(minV, maxV);
        }

        if (triangleBounds.empty()) {
            continue;
        }

        // Check all dynamic Jolt bodies against this mesh collider
        auto dynamicBodiesView = scene.GetEntitiesWith<TransformComponent, RigidBodyComponent>();
        for (auto dynamicHandle : dynamicBodiesView) {
            Entity dynamicEntity(dynamicHandle, &scene);
            auto& dynamicRigidBody = dynamicEntity.GetComponent<RigidBodyComponent>();
            
            // Only integrate dynamic bodies (not static)
            if (dynamicRigidBody.IsStatic()) {
                continue;
            }

            auto bodyIt = m_EntityToBodyMap.find(dynamicHandle);
            if (bodyIt == m_EntityToBodyMap.end()) {
                continue;
            }

            JPH::BodyID bodyId = bodyIt->second;
            JPH::BodyInterface& bodyInterface = m_PhysicsEngine.GetBodyInterface();
            
            // Get current position via BodyInterface
            JPH::RVec3 currentPos = bodyInterface.GetCenterOfMassPosition(bodyId);
            AABB bodyBounds;
            {
                // Approximate body bounds from its AABB (requires getting position + bounds from shape)
                // For simplicity, use a reasonable assumption about body extent
                Vec3 pos(currentPos.GetX(), currentPos.GetY(), currentPos.GetZ());
                float boxSize = 0.5f;  // Conservative estimate
                bodyBounds = AABB(
                    vsub(pos, v3(boxSize, boxSize, boxSize)),
                    vadd(pos, v3(boxSize, boxSize, boxSize))
                );
            }

            // Quick broad-phase: check mesh collider's overall bounds
            const AABB& colliderBounds = renderer.localBounds;
            AABB meshColliderWorldBounds(
                Mat4::TransformPoint(meshWorldMat, colliderBounds.min),
                Mat4::TransformPoint(meshWorldMat, colliderBounds.max)
            );

            auto aabbOverlap = [](const AABB& a, const AABB& b) -> bool {
                return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
                       (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
                       (a.min.z <= b.max.z && a.max.z >= b.min.z);
            };

            if (!aabbOverlap(bodyBounds, meshColliderWorldBounds)) {
                continue;
            }

            // Narrow-phase: check body against each triangle
            bool colliding = false;
            Vec3 pushDirection;
            float maxDepth = 0.0f;

            for (const auto& triBound : triangleBounds) {
                if (!aabbOverlap(bodyBounds, triBound)) {
                    continue;
                }

                colliding = true;
                
                // Compute push direction as the direction from triangle center to body center
                Vec3 triCenter = v3(
                    (triBound.min.x + triBound.max.x) * 0.5f,
                    (triBound.min.y + triBound.max.y) * 0.5f,
                    (triBound.min.z + triBound.max.z) * 0.5f
                );
                Vec3 bodyCenter = v3(
                    (bodyBounds.min.x + bodyBounds.max.x) * 0.5f,
                    (bodyBounds.min.y + bodyBounds.max.y) * 0.5f,
                    (bodyBounds.min.z + bodyBounds.max.z) * 0.5f
                );
                Vec3 pushDir = vsub(bodyCenter, triCenter);
                float dist = vlen(pushDir);
                
                if (dist > 1e-6f) {
                    pushDir = vnorm(pushDir);
                    
                    // Compute overlap depth
                    float depth = 0.0f;
                    // Simple heuristic: use the smallest axis of the triangle bound
                    Vec3 triExtent = v3(
                        (triBound.max.x - triBound.min.x) * 0.5f,
                        (triBound.max.y - triBound.min.y) * 0.5f,
                        (triBound.max.z - triBound.min.z) * 0.5f
                    );
                    float minExtent = std::min({triExtent.x, triExtent.y, triExtent.z});
                    if (minExtent > 1e-6f) {
                        depth = minExtent + 0.01f; // Add small epsilon
                    }

                    if (depth > maxDepth) {
                        maxDepth = depth;
                        pushDirection = pushDir;
                    }
                }
            }

            if (colliding && maxDepth > 1e-6f) {
                // Apply impulse to push the body out
                Vec3 pushVec = vscale(pushDirection, maxDepth);
                
                // Move the body
                JPH::RVec3 currentPos = bodyInterface.GetCenterOfMassPosition(bodyId);
                JPH::RVec3 newPos(
                    currentPos.GetX() + pushVec.x,
                    currentPos.GetY() + pushVec.y,
                    currentPos.GetZ() + pushVec.z
                );
                bodyInterface.SetPosition(bodyId, newPos, JPH::EActivation::Activate);

                // Log collision for debugging
                std::cout << "[MeshCollider] Dynamic body colliding with mesh, pushing out: " << maxDepth << "m\n";
            }
        }
    }
}

} // namespace Physics
