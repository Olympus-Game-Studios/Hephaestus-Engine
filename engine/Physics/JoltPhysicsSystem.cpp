#include "JoltPhysicsSystem.h"
#include "PhysicsStepClock.h"
#include "../Core/EngineLog.h"
#include "../Scene/Components.h"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Math/Float3.h>

#include "../Assets/MeshAsset.h"
#include "../Assets/ModelImporter.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <mutex>
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

JPH::RefConst<JPH::Shape> BuildMeshShapeFromMeshData(const MeshData& meshData, const Vec3& scale)
{
    if (meshData.vertices.empty() || meshData.indices.size() < 3) {
        return {};
    }

    JPH::VertexList vertices;
    vertices.resize(meshData.vertices.size());
    for (size_t i = 0; i < meshData.vertices.size(); ++i) {
        const Vertex& v = meshData.vertices[i];
        vertices[i] = JPH::Float3(v.pos[0] * scale.x, v.pos[1] * scale.y, v.pos[2] * scale.z);
    }

    JPH::IndexedTriangleList triangles;
    triangles.reserve(meshData.indices.size() / 3);
    for (size_t i = 0; i + 2 < meshData.indices.size(); i += 3) {
        uint32_t i0 = meshData.indices[i];
        uint32_t i1 = meshData.indices[i + 1];
        uint32_t i2 = meshData.indices[i + 2];
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
}

} // namespace

namespace Physics {

namespace {

std::uint32_t BodyKey(JPH::BodyID bodyId)
{
    return bodyId.GetIndexAndSequenceNumber();
}

std::uint32_t EntityKey(entt::entity entity)
{
    return static_cast<std::uint32_t>(entity);
}

} // namespace

struct PhysicsContactListener : public JPH::ContactListener {
    JPH::PhysicsSystem* system = nullptr;
    std::mutex mutex;
    std::array<PhysicsContactRecord, kPhysicsContactBufferCapacity> records{};
    int count = 0;
    bool overflow = false;

    void Clear()
    {
        std::lock_guard<std::mutex> lock(mutex);
        count = 0;
        overflow = false;
    }

    void Push(const PhysicsContactRecord& record)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (count >= kPhysicsContactBufferCapacity) {
            overflow = true;
            return;
        }
        records[static_cast<std::size_t>(count++)] = record;
    }

    void AppendManifold(PhysicsContactKind kind, const JPH::Body& body1, const JPH::Body& body2,
                        const JPH::ContactManifold& manifold)
    {
        PhysicsContactRecord record;
        record.kind = kind;
        record.body1 = BodyKey(body1.GetID());
        record.body2 = BodyKey(body2.GetID());
        record.entity1 = static_cast<std::uint32_t>(body1.GetUserData());
        record.entity2 = static_cast<std::uint32_t>(body2.GetUserData());
        record.sensor = body1.IsSensor() || body2.IsSensor();
        record.normalX = manifold.mWorldSpaceNormal.GetX();
        record.normalY = manifold.mWorldSpaceNormal.GetY();
        record.normalZ = manifold.mWorldSpaceNormal.GetZ();
        record.penetration = manifold.mPenetrationDepth;
        const JPH::uint pointCount = manifold.mRelativeContactPointsOn1.size();
        if (pointCount > 0) {
            JPH::uint deepest = 0;
            float deepestSeparation = -1.0e30f;
            for (JPH::uint index = 0; index < pointCount; ++index) {
                const JPH::Vec3 delta = manifold.mRelativeContactPointsOn2[index]
                    - manifold.mRelativeContactPointsOn1[index];
                const float separation = delta.Dot(manifold.mWorldSpaceNormal);
                if (separation > deepestSeparation) {
                    deepestSeparation = separation;
                    deepest = index;
                }
            }
            const JPH::RVec3 point1 = manifold.GetWorldSpaceContactPointOn1(deepest);
            const JPH::RVec3 point2 = manifold.GetWorldSpaceContactPointOn2(deepest);
            const JPH::Vec3 velocity1 = body1.GetPointVelocity(point1);
            const JPH::Vec3 velocity2 = body2.GetPointVelocity(point2);
            record.hasPoint = true;
            record.point1X = static_cast<float>(point1.GetX());
            record.point1Y = static_cast<float>(point1.GetY());
            record.point1Z = static_cast<float>(point1.GetZ());
            record.point2X = static_cast<float>(point2.GetX());
            record.point2Y = static_cast<float>(point2.GetY());
            record.point2Z = static_cast<float>(point2.GetZ());
            record.vel1X = velocity1.GetX();
            record.vel1Y = velocity1.GetY();
            record.vel1Z = velocity1.GetZ();
            record.vel2X = velocity2.GetX();
            record.vel2Y = velocity2.GetY();
            record.vel2Z = velocity2.GetZ();
        }
        Push(record);
    }

    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings&) override
    {
        AppendManifold(PhysicsContactKind::Added, body1, body2, manifold);
    }

    void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                            JPH::ContactSettings&) override
    {
        AppendManifold(PhysicsContactKind::Persisted, body1, body2, manifold);
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
    {
        PhysicsContactRecord record;
        record.kind = PhysicsContactKind::Removed;
        record.body1 = BodyKey(pair.GetBody1ID());
        record.body2 = BodyKey(pair.GetBody2ID());
        record.entity1 = kNullPhysicsEntity;
        record.entity2 = kNullPhysicsEntity;
        record.lastContact = system != nullptr
            && !system->WereBodiesInContact(pair.GetBody1ID(), pair.GetBody2ID());
        Push(record);
    }
};

JoltPhysicsSystem::JoltPhysicsSystem(JoltPhysicsEngine& physicsEngine)
    : m_PhysicsEngine(physicsEngine)
{
}

JoltPhysicsSystem::~JoltPhysicsSystem() {
    DetachContactListener();
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
    m_BodyPoseBinding.clear();
    m_PoseRejectionLogged.clear();
    ClearContactState();
}

void JoltPhysicsSystem::SetContactEventSink(std::function<void(const PhysicsContactEvent&)> sink)
{
    m_ContactSink = std::move(sink);
}

void JoltPhysicsSystem::EnsureContactListener()
{
    if (!m_ContactListener) {
        m_ContactListener = std::make_unique<PhysicsContactListener>();
    }
    JPH::PhysicsSystem* system = m_PhysicsEngine.GetPhysicsSystem();
    if (!system) {
        return;
    }
    m_ContactListener->system = system;
    if (system->GetContactListener() != m_ContactListener.get()) {
        system->SetContactListener(m_ContactListener.get());
    }
}

void JoltPhysicsSystem::DetachContactListener()
{
    JPH::PhysicsSystem* system = m_PhysicsEngine.GetPhysicsSystem();
    if (system && m_ContactListener && system->GetContactListener() == m_ContactListener.get()) {
        system->SetContactListener(nullptr);
    }
}

void JoltPhysicsSystem::ClearContactState()
{
    m_ContactOverlaps.clear();
    m_RetiredBodies.clear();
    m_FrameContactEvents.clear();
    if (m_ContactListener) {
        m_ContactListener->Clear();
    }
}

void JoltPhysicsSystem::DispatchContactEvents()
{
    if (!m_ContactListener) {
        return;
    }
    std::vector<PhysicsContactRecord> batch;
    bool overflow = false;
    {
        std::lock_guard<std::mutex> lock(m_ContactListener->mutex);
        batch.assign(m_ContactListener->records.begin(),
                     m_ContactListener->records.begin() + m_ContactListener->count);
        m_ContactListener->count = 0;
        overflow = m_ContactListener->overflow;
        m_ContactListener->overflow = false;
    }
    if (overflow) {
        Hephaestus::EngineLog::Write(
            Hephaestus::EngineLogLevel::Warning,
            "Physics contact buffer overflow; some contacts were dropped",
            "Physics");
    }

    auto resolve = [this](std::uint32_t body, std::uint32_t& entity, bool& live) {
        auto liveIt = m_BodyToEntityMap.find(body);
        if (liveIt != m_BodyToEntityMap.end()) {
            entity = EntityKey(liveIt->second);
            live = true;
            return;
        }
        auto retired = m_RetiredBodies.find(body);
        if (retired != m_RetiredBodies.end()) {
            entity = EntityKey(retired->second);
            live = false;
            return;
        }
        entity = kNullPhysicsEntity;
        live = false;
    };

    for (PhysicsContactRecord& record : batch) {
        if (record.kind != PhysicsContactKind::Removed) {
            continue;
        }
        resolve(record.body1, record.entity1, record.entity1Live);
        resolve(record.body2, record.entity2, record.entity2Live);
    }

    std::vector<PhysicsContactEvent> events;
    CollapseContactRecords(batch, m_ContactOverlaps, events);
    m_RetiredBodies.clear();
    m_FrameContactEvents.insert(m_FrameContactEvents.end(), events.begin(), events.end());
    if (!m_ContactSink) {
        return;
    }
    for (const PhysicsContactEvent& event : events) {
        m_ContactSink(event);
    }
}

void JoltPhysicsSystem::PrepareSceneBodies(Scene& scene) {
    if (!m_Enabled || !m_PhysicsEngine.GetPhysicsSystem()) {
        return;
    }
    UpdateEntityBodies(scene);
}

void JoltPhysicsSystem::ClearAccumulator() {
    m_Accumulator = 0.0f;
    m_LastSubstepCount = 0;
    m_StalledStepCount = 0;
}

struct KinematicSlice {
    JPH::BodyID bodyId;
    bool move = false;
    JPH::RVec3 startPos = JPH::RVec3::sZero();
    JPH::Quat startRot = JPH::Quat::sIdentity();
    JPH::RVec3 endPos = JPH::RVec3::sZero();
    JPH::Quat endRot = JPH::Quat::sIdentity();
};

void JoltPhysicsSystem::Update(Scene& scene, float deltaTime) {
    m_FrameContactEvents.clear();
    if (!m_Enabled) {
        return;
    }

    const PhysicsStepBudget budget = AdvancePhysicsAccumulator(m_Accumulator, deltaTime);
    m_Accumulator = budget.accumulator;
    m_LastSubstepCount = budget.substepCount;
    if (budget.stalled) {
        ++m_StalledStepCount;
    }

    auto stepBodies = [this, &scene](const std::vector<KinematicSlice>* kinematicSlices, int stepIndex, int substepCount) {
        PrepareSceneBodies(scene);
        if (kinematicSlices && substepCount > 0) {
            const float alpha = static_cast<float>(stepIndex) / static_cast<float>(substepCount);
            JPH::BodyInterface& bodyInterface = m_PhysicsEngine.GetBodyInterface();
            for (const KinematicSlice& slice : *kinematicSlices) {
                if (!slice.move || !bodyInterface.IsAdded(slice.bodyId)) {
                    continue;
                }
                const JPH::RVec3 position = slice.startPos + (slice.endPos - slice.startPos) * alpha;
                bodyInterface.MoveKinematic(slice.bodyId, position, slice.startRot.SLERP(slice.endRot, alpha), kPhysicsFixedDt);
            }
        }
        if (m_EntityToBodyMap.empty()) {
            return;
        }
        m_PhysicsEngine.Update(kPhysicsFixedDt);
        DispatchContactEvents();
        auto view = scene.GetEntitiesWith<TransformComponent, RigidBodyComponent>();
        for (auto handle : view) {
            entt::entity entity = handle;
            auto it = m_EntityToBodyMap.find(entity);
            if (it != m_EntityToBodyMap.end()) {
                SyncTransformFromPhysics(scene, entity, it->second);
            }
        }
        IntegrateMeshColliders(scene);
    };

    if (budget.substepCount <= 0) {
        PrepareSceneBodies(scene);
        return;
    }

    EnsureContactListener();

    std::vector<KinematicSlice> kinematicSlices;
    {
        JPH::BodyInterface& bodyInterface = m_PhysicsEngine.GetBodyInterface();
        auto view = scene.GetEntitiesWith<TransformComponent, RigidBodyComponent>();
        for (auto handle : view) {
            entt::entity entity = handle;
            auto& rigidBody = scene.GetRegistry().get<RigidBodyComponent>(entity);
            if (!rigidBody.IsKinematic()) {
                continue;
            }
            auto found = m_EntityToBodyMap.find(entity);
            if (found == m_EntityToBodyMap.end() || !bodyInterface.IsAdded(found->second)) {
                continue;
            }
            WorldPose endPose;
            if (!DecomposeEntityPose(scene, entity, endPose)) {
                continue;
            }
            KinematicSlice slice;
            slice.bodyId = found->second;
            slice.startPos = bodyInterface.GetPosition(slice.bodyId);
            slice.startRot = bodyInterface.GetRotation(slice.bodyId);
            slice.endPos = JPH::RVec3(endPose.position.x, endPose.position.y, endPose.position.z);
            slice.endRot = JPH::Quat(endPose.rotation.x, endPose.rotation.y, endPose.rotation.z, endPose.rotation.w);
            const double dx = slice.endPos.GetX() - slice.startPos.GetX();
            const double dy = slice.endPos.GetY() - slice.startPos.GetY();
            const double dz = slice.endPos.GetZ() - slice.startPos.GetZ();
            const float distanceSq = static_cast<float>(dx * dx + dy * dy + dz * dz);
            const float rotationDot = std::abs(slice.startRot.Dot(slice.endRot));
            slice.move = distanceSq > kPoseChangeEpsilon * kPoseChangeEpsilon
                || rotationDot < 1.0f - kPoseChangeEpsilon;
            kinematicSlices.push_back(slice);
        }
    }

    for (int step = 1; step <= budget.substepCount; ++step) {
        stepBodies(&kinematicSlices, step, budget.substepCount);
    }
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

bool JoltPhysicsSystem::DecomposeEntityPose(Scene& scene, entt::entity entity, WorldPose& out) const
{
    std::vector<RigidTransformSample> chain;
    entt::entity cursor = entity;
    int guard = 0;
    entt::registry& registry = scene.GetRegistry();
    while (cursor != entt::null && guard++ < 64) {
        if (!registry.valid(cursor) || !registry.all_of<TransformComponent>(cursor)) {
            return false;
        }
        const TransformComponent& transform = registry.get<TransformComponent>(cursor);
        chain.push_back(RigidTransformSample{transform.position, transform.rotation, transform.scale});
        cursor = transform.parent;
    }
    if (cursor != entt::null) {
        return false;
    }
    std::reverse(chain.begin(), chain.end());
    return TryComposeWorldPose(chain.data(), chain.size(), out);
}

void JoltPhysicsSystem::LogRejectedPose(entt::entity entity)
{
    if (!m_PoseRejectionLogged.insert(entity).second) {
        return;
    }
    Hephaestus::EngineLog::Write(
        Hephaestus::EngineLogLevel::Error,
        "Physics body skipped for entity " + std::to_string(static_cast<std::uint32_t>(entity))
            + ": a parent has non-uniform scale",
        "Physics");
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

    auto& authoredTransform = sceneEntity.GetComponent<TransformComponent>();
    auto& rigidBody = sceneEntity.GetComponent<RigidBodyComponent>();
    rigidBody.InvalidateBodyHandle();

    WorldPose worldPose;
    if (!DecomposeEntityPose(scene, entity, worldPose)) {
        LogRejectedPose(entity);
        return JPH::BodyID();
    }
    TransformComponent transform = authoredTransform;
    transform.position = worldPose.position;
    transform.rotation = worldPose.rotationEuler;
    transform.scale = worldPose.shapeScale;

    rigidBody.isStatic = rigidBody.isStatic || isStatic || rigidBody.mass <= 0.0f;
    const bool treatAsStatic = rigidBody.IsStatic();
    const bool treatAsKinematic = rigidBody.IsKinematic();
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
        if (!meshData) {
            return {};
        }
        return BuildMeshShapeFromMeshData(*meshData, transform.scale);
    };

    auto buildShape = [&](const PhysicsShapeComponent& shapeComp, const TransformComponent& transformComp,
                          const MeshRendererComponent* meshRenderer) -> JPH::RefConst<JPH::Shape> {
        Vec3 scale = transformComp.scale;
        Vec3 absScale = Vec3(std::abs(scale.x), std::abs(scale.y), std::abs(scale.z));

        // Compute mesh bounds center so we can offset the collision shape to match the visual mesh.
        // Many meshes have their origin at the bottom (e.g. character capsules) rather than
        // at the geometric center, so the physics shape must be shifted to align.
        Vec3 meshCenter(0.0f, 0.0f, 0.0f);
        Vec3 meshHalf(0.5f, 0.5f, 0.5f);
        if (meshRenderer && !sceneEntity.HasComponent<CharacterComponent>()) {
            Vec3 mn = meshRenderer->localBounds.min;
            Vec3 mx = meshRenderer->localBounds.max;
            meshCenter = Vec3((mn.x + mx.x) * 0.5f,
                              (mn.y + mx.y) * 0.5f,
                              (mn.z + mx.z) * 0.5f);
            meshHalf = Vec3(std::max((mx.x - mn.x) * 0.5f, 0.001f),
                            std::max((mx.y - mn.y) * 0.5f, 0.001f),
                            std::max((mx.z - mn.z) * 0.5f, 0.001f));
        }

        auto applyOffset = [&](const JPH::RefConst<JPH::Shape>& baseShape) -> JPH::RefConst<JPH::Shape> {
            if (!baseShape) {
                return {};
            }
            // Combine user-specified offset with the mesh bounds center offset.
            Vec3 totalOffset(
                shapeComp.offset.x + meshCenter.x,
                shapeComp.offset.y + meshCenter.y,
                shapeComp.offset.z + meshCenter.z
            );
            Vec3 offsetScaled = Vec3(
                totalOffset.x * scale.x,
                totalOffset.y * scale.y,
                totalOffset.z * scale.z
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
    // Pass the mesh renderer so buildShape can offset the collision shape to match
    // the mesh's geometric center (handles meshes with origin not at center).
    if (!shape && shapeComp) {
        shape = buildShape(*shapeComp, transform, renderer);
    }

    // If no explicit physics shape, derive from render bounds.
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

    const bool runtimeKinematic = treatAsKinematic || (treatAsStatic && treatAsSensor);
    const bool runtimeStatic = treatAsStatic && !runtimeKinematic;
    JPH::BodyCreationSettings bodySettings(
        shape,
        JPH::RVec3(transform.position.x, transform.position.y, transform.position.z),
        EulerXYZToQuat(transform.rotation),
        runtimeStatic ? JPH::EMotionType::Static
            : (runtimeKinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic),
        m_PhysicsEngine.ObjectLayerFor(rigidBody.layer, runtimeStatic)
    );

    bodySettings.mIsSensor = treatAsSensor;
    bodySettings.mCollideKinematicVsNonDynamic = false;
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
    bodySettings.mGravityFactor = runtimeKinematic || !rigidBody.UsesGravity() ? 0.0f : 1.0f;
    
    if (!runtimeStatic && !runtimeKinematic) {
        // Dynamic gameplay bodies can cross thin geometry between discrete
        // steps during frame spikes. Linear casting keeps their swept shape
        // in the collision query and prevents intermittent floor tunneling.
        bodySettings.mMotionQuality = JPH::EMotionQuality::LinearCast;

        const float targetMass = std::max(rigidBody.mass, 0.001f);
        bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
        JPH::MassProperties massProps = shape->GetMassProperties();
        massProps.ScaleToMass(targetMass);
        if (rigidBody.freezeRotation) {
            if (sceneEntity.HasComponent<CharacterComponent>()) {
                // Keep capsules upright but allow authored yaw (Physics.setRotation).
                bodySettings.mAllowedDOFs =
                    JPH::EAllowedDOFs::TranslationX |
                    JPH::EAllowedDOFs::TranslationY |
                    JPH::EAllowedDOFs::TranslationZ |
                    JPH::EAllowedDOFs::RotationY;
            } else {
                bodySettings.mAllowedDOFs =
                    JPH::EAllowedDOFs::TranslationX |
                    JPH::EAllowedDOFs::TranslationY |
                    JPH::EAllowedDOFs::TranslationZ;
            }
        }
        bodySettings.mMassPropertiesOverride = massProps;
    } else if (runtimeKinematic && rigidBody.freezeRotation) {
        bodySettings.mAllowedDOFs =
            JPH::EAllowedDOFs::TranslationX |
            JPH::EAllowedDOFs::TranslationY |
            JPH::EAllowedDOFs::TranslationZ;
    }

    JPH::BodyInterface& bodyInterface = m_PhysicsEngine.GetBodyInterface();

    JPH::Body* pBody = bodyInterface.CreateBody(bodySettings);
    if (!pBody) {
        std::cerr << "Failed to create Jolt physics body\n";
        return JPH::BodyID();
    }

    JPH::BodyID bodyId = pBody->GetID();
    pBody->SetUserData(static_cast<JPH::uint64>(EntityKey(entity)));
    bodyInterface.AddBody(bodyId, runtimeStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);

    // Map entity to body
    m_EntityToBodyMap[entity] = bodyId;
    m_BodyToEntityMap[BodyKey(bodyId)] = entity;
    m_BodyPoseBinding[entity] = BodyPoseBinding{worldPose.shapeScale, authoredTransform.parent};

    rigidBody.joltBodyId = bodyId.GetIndexAndSequenceNumber();

    std::cout << "Created physics body for entity" << std::endl;

    return bodyId;
}

void JoltPhysicsSystem::DestroyRigidBody(JPH::BodyID bodyId) {
    if (bodyId.IsInvalid()) {
        return;
    }

    auto it = m_BodyToEntityMap.find(BodyKey(bodyId));
    if (it != m_BodyToEntityMap.end()) {
        m_RetiredBodies[BodyKey(bodyId)] = it->second;
        m_BodyPoseBinding.erase(it->second);
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

entt::entity JoltPhysicsSystem::GetEntityForBody(JPH::BodyID bodyId) const {
    if (bodyId.IsInvalid()) {
        return entt::null;
    }
    auto it = m_BodyToEntityMap.find(BodyKey(bodyId));
    return it != m_BodyToEntityMap.end() ? it->second : entt::null;
}

bool JoltPhysicsSystem::Raycast(entt::entity ignoreEntity, const Vec3& origin, const Vec3& direction,
                                float maxDistance, RaycastHit& outHit, const std::uint16_t* gameplayLayer,
                                bool ignoreSensors) const {
    outHit = RaycastHit{};

    JPH::PhysicsSystem* physicsSystem = m_PhysicsEngine.GetPhysicsSystem();
    if (!physicsSystem || maxDistance <= 0.0f) {
        return false;
    }

    Vec3 dir = direction;
    float lenSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
    if (lenSq < 1e-8f) {
        return false;
    }
    float invLen = 1.0f / std::sqrt(lenSq);
    dir.x *= invLen;
    dir.y *= invLen;
    dir.z *= invLen;

    JPH::Vec3 start(origin.x, origin.y, origin.z);
    JPH::Vec3 end(origin.x + dir.x * maxDistance,
                  origin.y + dir.y * maxDistance,
                  origin.z + dir.z * maxDistance);
    JPH::RRayCast ray(JPH::RVec3(start), JPH::Vec3(end - start));

#ifdef RayCastResult
#undef RayCastResult
#endif

    JPH::RayCastResult rayCastResult;
    const JPH::NarrowPhaseQuery& query = physicsSystem->GetNarrowPhaseQuery();

    JPH::BodyID ignoreBody;
    if (ignoreEntity != entt::null) {
        ignoreBody = GetBodyID(ignoreEntity);
    }

    class IgnoreSensorsFilter final : public JPH::BodyFilter {
    public:
        explicit IgnoreSensorsFilter(JPH::BodyID ignore) : m_Ignore(ignore) {}
        bool ShouldCollide(const JPH::BodyID& inBodyID) const override { return inBodyID != m_Ignore; }
        bool ShouldCollideLocked(const JPH::Body& inBody) const override {
            return inBody.GetID() != m_Ignore && !inBody.IsSensor();
        }
    private:
        JPH::BodyID m_Ignore;
    };

    bool hit = false;
    if (ignoreSensors) {
        IgnoreSensorsFilter filter(ignoreBody);
        if (gameplayLayer) {
            const JPH::ObjectLayer queryLayer = m_PhysicsEngine.ObjectLayerFor(*gameplayLayer, false);
            hit = query.CastRay(ray, rayCastResult,
                                physicsSystem->GetDefaultBroadPhaseLayerFilter(queryLayer),
                                physicsSystem->GetDefaultLayerFilter(queryLayer),
                                filter);
        } else {
            hit = query.CastRay(ray, rayCastResult, {}, {}, filter);
        }
    } else if (gameplayLayer) {
        const JPH::ObjectLayer queryLayer = m_PhysicsEngine.ObjectLayerFor(*gameplayLayer, false);
        const JPH::DefaultBroadPhaseLayerFilter broadFilter = physicsSystem->GetDefaultBroadPhaseLayerFilter(queryLayer);
        const JPH::DefaultObjectLayerFilter objectFilter = physicsSystem->GetDefaultLayerFilter(queryLayer);
        if (!ignoreBody.IsInvalid()) {
            JPH::IgnoreSingleBodyFilter filter(ignoreBody);
            hit = query.CastRay(ray, rayCastResult, broadFilter, objectFilter, filter);
        } else {
            hit = query.CastRay(ray, rayCastResult, broadFilter, objectFilter);
        }
    } else if (!ignoreBody.IsInvalid()) {
        JPH::IgnoreSingleBodyFilter filter(ignoreBody);
        hit = query.CastRay(ray, rayCastResult, {}, {}, filter);
    } else {
        hit = query.CastRay(ray, rayCastResult);
    }

    if (!hit) {
        outHit.hit = false;
        return true;
    }

    outHit.hit = true;
    outHit.distance = rayCastResult.mFraction * maxDistance;
    outHit.point = Vec3(origin.x + dir.x * outHit.distance,
                        origin.y + dir.y * outHit.distance,
                        origin.z + dir.z * outHit.distance);
    outHit.entity = GetEntityForBody(rayCastResult.mBodyID);
    outHit.normal = Vec3(0.0f, 1.0f, 0.0f);

    {
        JPH::BodyLockRead lock(physicsSystem->GetBodyLockInterface(), rayCastResult.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& body = lock.GetBody();
            JPH::Vec3 n = body.GetWorldSpaceSurfaceNormal(
                rayCastResult.mSubShapeID2,
                JPH::RVec3(outHit.point.x, outHit.point.y, outHit.point.z));
            outHit.normal = Vec3(n.GetX(), n.GetY(), n.GetZ());
        }
    }

    return true;
}

Physics::BodyObservationStatus JoltPhysicsSystem::ObserveBody(entt::entity entity, BodyObservation& out) const
{
    const auto it = m_EntityToBodyMap.find(entity);
    if (it == m_EntityToBodyMap.end() || it->second.IsInvalid()) {
        return BodyObservationStatus::Missing;
    }

    const JPH::BodyID bodyId = it->second;
    JPH::PhysicsSystem* physicsSystem = m_PhysicsEngine.GetPhysicsSystem();
    if (!physicsSystem || !physicsSystem->GetBodyInterface().IsAdded(bodyId)) {
        return BodyObservationStatus::Unreadable;
    }

    JPH::BodyLockRead lock(physicsSystem->GetBodyLockInterface(), bodyId);
    if (!lock.Succeeded()) {
        return BodyObservationStatus::Unreadable;
    }

    const JPH::Body& body = lock.GetBody();
    const char* motionType = nullptr;
    switch (body.GetMotionType()) {
        case JPH::EMotionType::Static: motionType = "static"; break;
        case JPH::EMotionType::Kinematic: motionType = "kinematic"; break;
        case JPH::EMotionType::Dynamic: motionType = "dynamic"; break;
        default: break;
    }
    if (!motionType) {
        return BodyObservationStatus::Unreadable;
    }

    const bool active = body.IsActive();
    const bool dynamic = body.GetMotionType() == JPH::EMotionType::Dynamic;
    const JPH::RVec3 position = body.GetPosition();
    const JPH::Quat rotation = body.GetRotation();
    const JPH::Vec3 linear = body.GetLinearVelocity();
    const JPH::Vec3 angular = body.GetAngularVelocity();

    BodyObservation snapshot;
    snapshot.entity = entity;
    snapshot.motionType = motionType;
    snapshot.active = active;
    snapshot.sleeping = dynamic && !active;
    snapshot.sensor = body.IsSensor();
    snapshot.position = Vec3(static_cast<float>(position.GetX()),
                              static_cast<float>(position.GetY()),
                              static_cast<float>(position.GetZ()));
    snapshot.rotationX = rotation.GetX();
    snapshot.rotationY = rotation.GetY();
    snapshot.rotationZ = rotation.GetZ();
    snapshot.rotationW = rotation.GetW();
    snapshot.linearVelocity = Vec3(linear.GetX(), linear.GetY(), linear.GetZ());
    snapshot.angularVelocity = Vec3(angular.GetX(), angular.GetY(), angular.GetZ());
    snapshot.debugBodyIndex = bodyId.GetIndex();
    out = std::move(snapshot);
    return BodyObservationStatus::Ready;
}

void JoltPhysicsSystem::ObserveBodies(std::vector<BodyObservation>& out) const
{
    out.clear();
    out.reserve(m_EntityToBodyMap.size());
    for (const auto& entry : m_EntityToBodyMap) {
        BodyObservation body;
        if (ObserveBody(entry.first, body) == BodyObservationStatus::Ready) {
            out.push_back(std::move(body));
        }
    }
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
    m_BodyPoseBinding.clear();
    m_PoseRejectionLogged.clear();
    ClearContactState();
}

void JoltPhysicsSystem::SyncTransformToPhysics(Scene& scene, entt::entity entity, JPH::BodyID bodyId, bool activateBody) {
    Entity sceneEntity(entity, &scene);

    if (!sceneEntity.HasComponent<TransformComponent>() || !sceneEntity.HasComponent<RigidBodyComponent>()) {
        return;
    }

    auto& rigidBody = sceneEntity.GetComponent<RigidBodyComponent>();
    JPH::BodyInterface& bodyInterface = m_PhysicsEngine.GetBodyInterface();

    if (!bodyInterface.IsAdded(bodyId)) {
        return;
    }

    WorldPose worldPose;
    if (!DecomposeEntityPose(scene, entity, worldPose)) {
        return;
    }

    JPH::RVec3 position(worldPose.position.x, worldPose.position.y, worldPose.position.z);
    JPH::Quat rotation(worldPose.rotation.x, worldPose.rotation.y, worldPose.rotation.z, worldPose.rotation.w);
    if (!activateBody) {
        const JPH::RVec3 current = bodyInterface.GetPosition(bodyId);
        const JPH::Quat currentRotation = bodyInterface.GetRotation(bodyId);
        const float dx = static_cast<float>(current.GetX()) - worldPose.position.x;
        const float dy = static_cast<float>(current.GetY()) - worldPose.position.y;
        const float dz = static_cast<float>(current.GetZ()) - worldPose.position.z;
        const float positionDelta = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float rotationDot = std::abs(currentRotation.Dot(rotation));
        if (positionDelta <= kPoseChangeEpsilon && rotationDot >= 1.0f - kPoseChangeEpsilon) {
            return;
        }
    }
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

    if (!rigidBody.IsDynamic()) {
        return;
    }

    // Skip if body ID is invalid
    if (bodyId.IsInvalid()) {
        return;
    }

    // Safely check if body exists before accessing
    if (!bodyInterface.IsAdded(bodyId)) {
        return;
    }

    // Use GetPosition() (body origin) rather than GetCenterOfMassPosition()
    // so that offset collision shapes (RotatedTranslatedShape) don't cause
    // the entity transform to drift from its expected location.
    JPH::RVec3 joltPos = bodyInterface.GetPosition(bodyId);
    JPH::Quat joltRot = bodyInterface.GetRotation(bodyId);
    const Vec3 worldPosition(static_cast<float>(joltPos.GetX()),
                             static_cast<float>(joltPos.GetY()),
                             static_cast<float>(joltPos.GetZ()));
    const PoseQuat worldRotation{joltRot.GetX(), joltRot.GetY(), joltRot.GetZ(), joltRot.GetW()};

    if (transform.parent != entt::null) {
        WorldPose parentPose;
        if (!DecomposeEntityPose(scene, transform.parent, parentPose)) {
            return;
        }
        WorldPose selfPose;
        if (!DecomposeEntityPose(scene, entity, selfPose)) {
            return;
        }
        const Vec3 localPosition = WorldPointToLocal(
            parentPose.position, parentPose.rotation, selfPose.ancestorScale, worldPosition);
        const PoseQuat localRotation = WorldRotationToLocal(parentPose.rotation, worldRotation);
        transform.position = localPosition;
        transform.rotation = PoseQuatToEulerXYZ(localRotation);
    } else {
        transform.position = worldPosition;
        transform.rotation = PoseQuatToEulerXYZ(worldRotation);
    }

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
            WorldPose worldPose;
            if (!DecomposeEntityPose(scene, entity, worldPose)) {
                LogRejectedPose(entity);
                DestroyRigidBody(it->second);
                continue;
            }
            auto bindingIt = m_BodyPoseBinding.find(entity);
            const bool scaleChanged = bindingIt == m_BodyPoseBinding.end()
                || !PoseNearlyEqual(bindingIt->second.shapeScale, worldPose.shapeScale, kUniformScaleEpsilon);
            if (scaleChanged) {
                Hephaestus::EngineLog::Write(
                    Hephaestus::EngineLogLevel::Info,
                    "Physics body recreated because its shape scale changed",
                    "Physics");
                DestroyRigidBody(it->second);
                JPH::BodyID bodyId = CreateRigidBody(scene, entity, rigidBody.IsStatic(), rigidBody.IsSensor());
                if (!bodyId.IsInvalid()) {
                    rigidBody.joltBodyId = bodyId.GetIndexAndSequenceNumber();
                    SyncTransformToPhysics(scene, entity, bodyId, true);
                }
                continue;
            }
            auto& transform = sceneEntity.GetComponent<TransformComponent>();
            if (rigidBody.IsDynamic() && bindingIt->second.parent != transform.parent) {
                JPH::BodyInterface& bodyInterface = m_PhysicsEngine.GetBodyInterface();
                if (bodyInterface.IsAdded(it->second)) {
                    bodyInterface.SetPositionAndRotation(
                        it->second,
                        JPH::RVec3(worldPose.position.x, worldPose.position.y, worldPose.position.z),
                        JPH::Quat(worldPose.rotation.x, worldPose.rotation.y, worldPose.rotation.z, worldPose.rotation.w),
                        JPH::EActivation::Activate);
                }
            } else if (rigidBody.IsStatic() || rigidBody.IsSensor()) {
                SyncTransformToPhysics(scene, entity, it->second, false);
            }
            bindingIt->second.parent = transform.parent;
            bindingIt->second.shapeScale = worldPose.shapeScale;
        }
    }

    // Create static Jolt bodies for mesh colliders (complex collision) without rigid bodies
    auto meshView = scene.GetEntitiesWith<TransformComponent, MeshRendererComponent, MeshColliderComponent>();
    for (auto handle : meshView) {
        entt::entity entity = handle;
        Entity sceneEntity(entity, &scene);

        // Skip if already handled by rigid body system
        if (sceneEntity.HasComponent<RigidBodyComponent>()) {
            continue;
        }

        auto& collider = sceneEntity.GetComponent<MeshColliderComponent>();
        if (!collider.enabled || !collider.useComplexCollision) {
            continue;
        }

        WorldPose worldPose;
        if (!DecomposeEntityPose(scene, entity, worldPose)) {
            LogRejectedPose(entity);
            auto rejected = m_EntityToBodyMap.find(entity);
            if (rejected != m_EntityToBodyMap.end()) {
                DestroyRigidBody(rejected->second);
            }
            continue;
        }

        auto& renderer = sceneEntity.GetComponent<MeshRendererComponent>();
        auto it = m_EntityToBodyMap.find(entity);
        const auto binding = m_BodyPoseBinding.find(entity);
        const bool scaleChanged = it != m_EntityToBodyMap.end()
            && (binding == m_BodyPoseBinding.end()
                || !PoseNearlyEqual(binding->second.shapeScale, worldPose.shapeScale, kUniformScaleEpsilon));
        if (scaleChanged) {
            DestroyRigidBody(it->second);
            it = m_EntityToBodyMap.end();
        }
        if (it == m_EntityToBodyMap.end()) {
            const MeshData* meshData = GetMeshDataForPath(renderer.meshAssetPath, m_ProjectRoot);
            if (!meshData) {
                continue;
            }

            JPH::RefConst<JPH::Shape> shape = BuildMeshShapeFromMeshData(*meshData, worldPose.shapeScale);
            if (!shape) {
                continue;
            }

            const std::uint16_t meshLayer = m_PhysicsEngine.ActiveLayerConfig().HasLayer(kLayerWorld)
                ? kLayerWorld
                : kLayerDefault;
            const bool meshSensor = collider.isTrigger;
            JPH::BodyCreationSettings bodySettings(
                shape,
                JPH::RVec3(worldPose.position.x, worldPose.position.y, worldPose.position.z),
                JPH::Quat(worldPose.rotation.x, worldPose.rotation.y, worldPose.rotation.z, worldPose.rotation.w),
                meshSensor ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static,
                m_PhysicsEngine.ObjectLayerFor(meshLayer, !meshSensor)
            );
            bodySettings.mIsSensor = meshSensor;
            bodySettings.mGravityFactor = 0.0f;
            bodySettings.mCollideKinematicVsNonDynamic = false;

            JPH::BodyInterface& bodyInterface = m_PhysicsEngine.GetBodyInterface();
            JPH::Body* pBody = bodyInterface.CreateBody(bodySettings);
            if (!pBody) {
                std::cerr << "Failed to create Jolt mesh collider body\n";
                continue;
            }

            JPH::BodyID bodyId = pBody->GetID();
            pBody->SetUserData(static_cast<JPH::uint64>(EntityKey(entity)));
            bodyInterface.AddBody(bodyId, meshSensor ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);

            m_EntityToBodyMap[entity] = bodyId;
            m_BodyToEntityMap[BodyKey(bodyId)] = entity;
            m_BodyPoseBinding[entity] = BodyPoseBinding{worldPose.shapeScale, sceneEntity.GetComponent<TransformComponent>().parent};
        } else {
            JPH::BodyInterface& bodyInterface = m_PhysicsEngine.GetBodyInterface();
            if (bodyInterface.IsAdded(it->second)) {
                const JPH::RVec3 current = bodyInterface.GetPosition(it->second);
                const float dx = static_cast<float>(current.GetX()) - worldPose.position.x;
                const float dy = static_cast<float>(current.GetY()) - worldPose.position.y;
                const float dz = static_cast<float>(current.GetZ()) - worldPose.position.z;
                if (dx * dx + dy * dy + dz * dz > kPoseChangeEpsilon * kPoseChangeEpsilon) {
                    bodyInterface.SetPositionAndRotation(
                        it->second,
                        JPH::RVec3(worldPose.position.x, worldPose.position.y, worldPose.position.z),
                        JPH::Quat(worldPose.rotation.x, worldPose.rotation.y, worldPose.rotation.z, worldPose.rotation.w),
                        JPH::EActivation::DontActivate
                    );
                }
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
        bool hasMeshCollider = entityValid && registry.all_of<MeshColliderComponent, MeshRendererComponent, TransformComponent>(entity);
        bool useComplexMeshCollider = false;
        if (hasMeshCollider) {
            auto& collider = registry.get<MeshColliderComponent>(entity);
            useComplexMeshCollider = collider.enabled && collider.useComplexCollision;
        }

        if (!entityValid || (!hasRigidBody && !useComplexMeshCollider)) {
            JPH::BodyID bodyId = it->second;
            ++it;
            DestroyRigidBody(bodyId);
        } else {
            ++it;
        }
    }
}

namespace {

// Helper: Compute triangle normal from three vertices
Vec3 ComputeTriangleNormal(const Vec3& p0, const Vec3& p1, const Vec3& p2) {
    Vec3 edge1 = vsub(p1, p0);
    Vec3 edge2 = vsub(p2, p0);
    Vec3 normal = vcross(edge1, edge2);
    float len = vlen(normal);
    if (len > 1e-6f) {
        return vscale(normal, 1.0f / len);
    }
    return v3(0.0f, 1.0f, 0.0f); // Degenerate triangle, return up
}

// Helper: Closest point on triangle to a point
Vec3 ClosestPointOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    // Check if P in vertex region outside A
    Vec3 ab = vsub(b, a);
    Vec3 ac = vsub(c, a);
    Vec3 ap = vsub(p, a);
    float d1 = vdot(ab, ap);
    float d2 = vdot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a; // barycentric coordinates (1,0,0)

    // Check if P in vertex region outside B
    Vec3 bp = vsub(p, b);
    float d3 = vdot(ab, bp);
    float d4 = vdot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b; // barycentric coordinates (0,1,0)

    // Check if P in edge region of AB, if so return projection of P onto AB
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return vadd(a, vscale(ab, v)); // barycentric coordinates (1-v,v,0)
    }

    // Check if P in vertex region outside C
    Vec3 cp = vsub(p, c);
    float d5 = vdot(ab, cp);
    float d6 = vdot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c; // barycentric coordinates (0,0,1)

    // Check if P in edge region of AC, if so return projection of P onto AC
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return vadd(a, vscale(ac, w)); // barycentric coordinates (1-w,0,w)
    }

    // Check if P in edge region of BC, if so return projection of P onto BC
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return vadd(b, vscale(vsub(c, b), w)); // barycentric coordinates (0,1-w,w)
    }

    // P inside face region. Compute Q through its barycentric coordinates (u,v,w)
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return vadd(a, vadd(vscale(ab, v), vscale(ac, w)));
}

// Helper: Sphere-triangle collision test
// Returns true if colliding, fills outPenetration and outNormal
bool SphereTriangleCollision(
    const Vec3& sphereCenter, float sphereRadius,
    const Vec3& p0, const Vec3& p1, const Vec3& p2,
    float& outPenetration, Vec3& outNormal, Vec3& outContactPoint)
{
    // Find closest point on triangle to sphere center
    Vec3 closest = ClosestPointOnTriangle(sphereCenter, p0, p1, p2);
    
    // Check distance from closest point to sphere center
    Vec3 toCenter = vsub(sphereCenter, closest);
    float distSq = vdot(toCenter, toCenter);
    float radiusSq = sphereRadius * sphereRadius;
    
    if (distSq > radiusSq) {
        return false; // No collision
    }
    
    float dist = std::sqrt(distSq);
    outPenetration = sphereRadius - dist;
    outContactPoint = closest;
    
    if (dist > 1e-6f) {
        outNormal = vscale(toCenter, 1.0f / dist);
    } else {
        // Sphere center is exactly on the triangle, use triangle normal
        outNormal = ComputeTriangleNormal(p0, p1, p2);
    }
    
    return true;
}

// Store world-space triangle data for collision
struct WorldTriangle {
    Vec3 p0, p1, p2;
    Vec3 normal;
    AABB bounds;
};

} // namespace

void JoltPhysicsSystem::IntegrateMeshColliders(Scene& scene) {
    // Iterate over all mesh colliders (may or may not have RigidBodyComponent)
    auto meshColliderView = scene.GetEntitiesWith<TransformComponent, MeshColliderComponent>();
    
    for (auto meshColliderHandle : meshColliderView) {
        Entity meshColliderEntity(meshColliderHandle, &scene);
        
        // Skip if this mesh collider is also a rigid body (it handles collision via Jolt)
        if (meshColliderEntity.HasComponent<RigidBodyComponent>()) {
            continue;
        }

        // Skip if this mesh collider has a Jolt body (raycasts and collisions are handled by Jolt)
        if (m_EntityToBodyMap.find(meshColliderHandle) != m_EntityToBodyMap.end()) {
            continue;
        }
        
        const auto& meshCollider = meshColliderEntity.GetComponent<MeshColliderComponent>();
        if (!meshCollider.enabled) {
            continue;
        }

        if (!meshColliderEntity.HasComponent<MeshRendererComponent>()) {
            continue;
        }

        WorldPose meshPose;
        if (!DecomposeEntityPose(scene, meshColliderHandle, meshPose)) {
            LogRejectedPose(meshColliderHandle);
            continue;
        }
        const Mat4 meshWorldMat = scene.GetWorldTransform(meshColliderEntity);
        const auto& renderer = meshColliderEntity.GetComponent<MeshRendererComponent>();
        const MeshData* meshData = GetMeshDataForPath(renderer.meshAssetPath, m_ProjectRoot);
        if (!meshData || meshData->vertices.empty() || meshData->indices.size() < 3) {
            continue;
        }

        // Build world-space triangles with normals and bounds
        std::vector<WorldTriangle> worldTriangles;
        worldTriangles.reserve(meshData->indices.size() / 3);
        
        AABB meshWorldBounds;
        bool boundsInitialized = false;
        
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
            
            WorldTriangle tri;
            tri.p0 = Mat4::TransformPoint(meshWorldMat, Vec3(v0.pos[0], v0.pos[1], v0.pos[2]));
            tri.p1 = Mat4::TransformPoint(meshWorldMat, Vec3(v1.pos[0], v1.pos[1], v1.pos[2]));
            tri.p2 = Mat4::TransformPoint(meshWorldMat, Vec3(v2.pos[0], v2.pos[1], v2.pos[2]));
            tri.normal = ComputeTriangleNormal(tri.p0, tri.p1, tri.p2);
            
            Vec3 minV(std::min({tri.p0.x, tri.p1.x, tri.p2.x}),
                      std::min({tri.p0.y, tri.p1.y, tri.p2.y}),
                      std::min({tri.p0.z, tri.p1.z, tri.p2.z}));
            Vec3 maxV(std::max({tri.p0.x, tri.p1.x, tri.p2.x}),
                      std::max({tri.p0.y, tri.p1.y, tri.p2.y}),
                      std::max({tri.p0.z, tri.p1.z, tri.p2.z}));
            tri.bounds = AABB(minV, maxV);
            
            worldTriangles.push_back(tri);
            
            // Expand mesh world bounds
            if (!boundsInitialized) {
                meshWorldBounds = tri.bounds;
                boundsInitialized = true;
            } else {
                meshWorldBounds.min.x = std::min(meshWorldBounds.min.x, minV.x);
                meshWorldBounds.min.y = std::min(meshWorldBounds.min.y, minV.y);
                meshWorldBounds.min.z = std::min(meshWorldBounds.min.z, minV.z);
                meshWorldBounds.max.x = std::max(meshWorldBounds.max.x, maxV.x);
                meshWorldBounds.max.y = std::max(meshWorldBounds.max.y, maxV.y);
                meshWorldBounds.max.z = std::max(meshWorldBounds.max.z, maxV.z);
            }
        }

        if (worldTriangles.empty()) {
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
            
            if (!bodyInterface.IsAdded(bodyId)) {
                continue;
            }
            
            // Get body shape bounds for accurate collision radius
            // Get the shape and compute local bounds, then transform by body position
            JPH::RefConst<JPH::Shape> bodyShape = bodyInterface.GetShape(bodyId);
            float collisionRadius = 0.5f; // Default fallback
            
            if (bodyShape) {
                JPH::AABox localBounds = bodyShape->GetLocalBounds();
                Vec3 localMin(localBounds.mMin.GetX(), localBounds.mMin.GetY(), localBounds.mMin.GetZ());
                Vec3 localMax(localBounds.mMax.GetX(), localBounds.mMax.GetY(), localBounds.mMax.GetZ());
                Vec3 localHalfExtent = vscale(vsub(localMax, localMin), 0.5f);
                // Use a sphere approximation with radius = max half extent
                collisionRadius = std::max({localHalfExtent.x, localHalfExtent.y, localHalfExtent.z});
            }
            // Minimum collision radius to prevent tunneling
            collisionRadius = std::max(collisionRadius, 0.25f);
            
            // Sphere test uses COM (matches GetLocalBounds of offset capsules).
            // SetPosition writes body origin, so the push must be applied to origin.
            JPH::RVec3 currentOrigin = bodyInterface.GetPosition(bodyId);
            JPH::RVec3 currentCom = bodyInterface.GetCenterOfMassPosition(bodyId);
            Vec3 bodyCenter(currentCom.GetX(), currentCom.GetY(), currentCom.GetZ());
            
            // Build body AABB with collision margin
            const float collisionMargin = 0.05f;
            AABB bodyBounds(
                vsub(bodyCenter, v3(collisionRadius + collisionMargin, collisionRadius + collisionMargin, collisionRadius + collisionMargin)),
                vadd(bodyCenter, v3(collisionRadius + collisionMargin, collisionRadius + collisionMargin, collisionRadius + collisionMargin))
            );

            // Quick broad-phase: check mesh collider's overall bounds
            auto aabbOverlap = [](const AABB& a, const AABB& b) -> bool {
                return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
                       (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
                       (a.min.z <= b.max.z && a.max.z >= b.min.z);
            };

            if (!aabbOverlap(bodyBounds, meshWorldBounds)) {
                continue;
            }

            // Accumulate collision responses from all intersecting triangles
            Vec3 totalPush = v3(0.0f, 0.0f, 0.0f);
            int collisionCount = 0;
            float maxPenetration = 0.0f;
            
            // For grounding detection
            bool isGrounded = false;
            const float groundNormalThreshold = 0.7f; // cos(45°) - surfaces pointing up enough to stand on

            for (const auto& tri : worldTriangles) {
                // Expand triangle bounds by collision radius for broad phase
                AABB expandedTriBounds(
                    vsub(tri.bounds.min, v3(collisionRadius, collisionRadius, collisionRadius)),
                    vadd(tri.bounds.max, v3(collisionRadius, collisionRadius, collisionRadius))
                );
                
                if (!aabbOverlap(bodyBounds, expandedTriBounds)) {
                    continue;
                }

                // Detailed sphere-triangle collision test
                float penetration;
                Vec3 normal;
                Vec3 contactPoint;
                
                if (SphereTriangleCollision(bodyCenter, collisionRadius, tri.p0, tri.p1, tri.p2, 
                                           penetration, normal, contactPoint)) {
                    // Only respond if penetration is significant
                    if (penetration > 0.001f) {
                        // Check if this surface could be ground (pointing mostly up)
                        if (normal.y > groundNormalThreshold) {
                            isGrounded = true;
                        }
                        
                        // Accumulate push direction weighted by penetration
                        Vec3 push = vscale(normal, penetration);
                        totalPush = vadd(totalPush, push);
                        collisionCount++;
                        
                        if (penetration > maxPenetration) {
                            maxPenetration = penetration;
                        }
                    }
                }
            }

            if (collisionCount > 0 && maxPenetration > 0.001f) {
                // Average the push direction and apply
                Vec3 averagePush = vscale(totalPush, 1.0f / static_cast<float>(collisionCount));
                
                // Clamp maximum push to prevent wild teleportation
                float pushLen = vlen(averagePush);
                const float maxPushDistance = 0.5f;
                if (pushLen > maxPushDistance) {
                    averagePush = vscale(averagePush, maxPushDistance / pushLen);
                }
                
                // Apply the same world-space correction to the body origin.
                // Writing COM through SetPosition teleports offset capsules upward each tick.
                JPH::RVec3 newOrigin(
                    currentOrigin.GetX() + averagePush.x,
                    currentOrigin.GetY() + averagePush.y,
                    currentOrigin.GetZ() + averagePush.z
                );
                bodyInterface.SetPosition(bodyId, newOrigin, JPH::EActivation::Activate);
                SyncTransformFromPhysics(scene, dynamicHandle, bodyId);
                
                // Cancel velocity into the collision surface
                JPH::Vec3 currentVel = bodyInterface.GetLinearVelocity(bodyId);
                Vec3 vel(currentVel.GetX(), currentVel.GetY(), currentVel.GetZ());
                
                // Normalize push direction for velocity cancellation
                if (pushLen > 1e-6f) {
                    Vec3 pushDir = vscale(averagePush, 1.0f / pushLen);
                    float velIntoSurface = vdot(vel, pushDir);
                    
                    // Only cancel velocity if moving into the surface
                    if (velIntoSurface < 0.0f) {
                        Vec3 velCancel = vscale(pushDir, -velIntoSurface);
                        Vec3 newVel = vadd(vel, velCancel);
                        
                        // Apply friction on tangent plane if grounded
                        if (isGrounded) {
                            const float groundFriction = 0.95f;
                            // Extract horizontal component and apply friction
                            newVel.x *= groundFriction;
                            newVel.z *= groundFriction;
                            
                            // Snap small downward velocities to zero when grounded
                            if (newVel.y < 0.0f && newVel.y > -0.5f) {
                                newVel.y = 0.0f;
                            }
                        }
                        
                        bodyInterface.SetLinearVelocity(bodyId, JPH::Vec3(newVel.x, newVel.y, newVel.z));
                    }
                }
            }
        }
    }
}

} // namespace Physics
