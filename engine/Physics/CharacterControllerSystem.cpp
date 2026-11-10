#include "CharacterControllerSystem.h"

#include "JoltPhysicsEngine.h"
#include "JoltPhysicsSystem.h"
#include "PhysicsStepClock.h"
#include "../Core/EngineLog.h"
#include "../Scene/CharacterControllerComponent.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Physics {
namespace {

constexpr float kTeleportDistance = 0.001f;
constexpr float kInnerShapeFraction = 0.9f;
constexpr float kScaleEpsilon = 1.0e-3f;
constexpr std::uint32_t kInvalidBodyKey = 0xFFFFFFFFu;

std::uint64_t EntityUserData(entt::entity entity)
{
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(entity));
}

ControllerAuthoredValues AuthoredFrom(const CharacterControllerComponent& component)
{
    ControllerAuthoredValues values;
    values.height = component.height;
    values.radius = component.radius;
    values.crouchedHeight = component.crouchedHeight;
    values.proneHeight = component.proneHeight;
    values.maxSlopeAngleDegrees = component.maxSlopeAngleDegrees;
    values.stepHeight = component.stepHeight;
    values.groundSnapDistance = component.groundSnapDistance;
    values.walkSpeed = component.walkSpeed;
    values.acceleration = component.acceleration;
    values.deceleration = component.deceleration;
    values.airAcceleration = component.airAcceleration;
    values.airControl = component.airControl;
    values.jumpSpeed = component.jumpSpeed;
    values.coyoteTime = component.coyoteTime;
    values.jumpBufferTime = component.jumpBufferTime;
    values.gravityScale = component.gravityScale;
    values.maxFallSpeed = component.maxFallSpeed;
    values.mass = component.mass;
    values.pushStrength = component.pushStrength;
    return values;
}

bool SameAuthored(const ControllerAuthoredValues& a, const ControllerAuthoredValues& b)
{
    auto near = [](float lhs, float rhs) { return std::abs(lhs - rhs) <= 1.0e-4f; };
    return near(a.height, b.height) && near(a.radius, b.radius) && near(a.crouchedHeight, b.crouchedHeight)
        && near(a.proneHeight, b.proneHeight)
        && near(a.maxSlopeAngleDegrees, b.maxSlopeAngleDegrees) && near(a.stepHeight, b.stepHeight)
        && near(a.groundSnapDistance, b.groundSnapDistance) && near(a.walkSpeed, b.walkSpeed)
        && near(a.acceleration, b.acceleration) && near(a.deceleration, b.deceleration)
        && near(a.airAcceleration, b.airAcceleration) && near(a.airControl, b.airControl)
        && near(a.jumpSpeed, b.jumpSpeed) && near(a.coyoteTime, b.coyoteTime)
        && near(a.jumpBufferTime, b.jumpBufferTime) && near(a.gravityScale, b.gravityScale)
        && near(a.maxFallSpeed, b.maxFallSpeed) && near(a.mass, b.mass)
        && near(a.pushStrength, b.pushStrength);
}

JPH::RefConst<JPH::Shape> MakeCapsule(float totalHeight, float radius, float fraction)
{
    const float cylinderHalf = 0.5f * (totalHeight - 2.0f * radius) * fraction;
    const float usedRadius = radius * fraction;
    JPH::Ref<JPH::Shape> capsule = new JPH::CapsuleShape(std::max(cylinderHalf, 0.001f), std::max(usedRadius, 0.001f));
    JPH::RotatedTranslatedShapeSettings settings(
        JPH::Vec3(0.0f, 0.5f * totalHeight, 0.0f),
        JPH::Quat::sIdentity(),
        capsule);
    return settings.Create().Get();
}

CharacterGroundState MapGround(JPH::CharacterBase::EGroundState state)
{
    switch (state) {
    case JPH::CharacterBase::EGroundState::OnGround: return CharacterGroundState::OnGround;
    case JPH::CharacterBase::EGroundState::OnSteepGround: return CharacterGroundState::OnSteepGround;
    case JPH::CharacterBase::EGroundState::NotSupported: return CharacterGroundState::NotSupported;
    default: return CharacterGroundState::InAir;
    }
}

Vec3 FromJolt(JPH::Vec3Arg value)
{
    return Vec3(value.GetX(), value.GetY(), value.GetZ());
}

Vec3 FromRVec(JPH::RVec3Arg value)
{
    return Vec3(static_cast<float>(value.GetX()),
                static_cast<float>(value.GetY()),
                static_cast<float>(value.GetZ()));
}

bool ScaleIsUniformOne(const Vec3& scale)
{
    return std::abs(scale.x - 1.0f) <= kScaleEpsilon
        && std::abs(scale.y - 1.0f) <= kScaleEpsilon
        && std::abs(scale.z - 1.0f) <= kScaleEpsilon;
}

class CharacterVirtualAccess final : public JPH::CharacterVirtual {
public:
    using JPH::CharacterVirtual::CharacterVirtual;
    void SetSupportingPlane(const JPH::Plane& plane) { mSupportingVolume = plane; }
};

} // namespace

struct CharacterControllerSystem::Runtime {
    JPH::RefConst<JPH::Shape> standShape;
    JPH::RefConst<JPH::Shape> crouchShape;
    JPH::RefConst<JPH::Shape> proneShape;
    JPH::RefConst<JPH::Shape> innerStand;
    JPH::RefConst<JPH::Shape> innerCrouch;
    JPH::RefConst<JPH::Shape> innerProne;
    std::unique_ptr<CharacterVirtualAccess> character;
    ControllerAuthoredValues applied;
    std::uint16_t layer = kLayerCharacter;
    PlanarWish wish{};
    bool jumpRequested = false;
    std::optional<float> jumpSpeedOverride;
    JumpGate jumpGate{};
    std::optional<float> legacySpeed;
    float speedMultiplier = 1.0f;
    bool crouchRequested = false;
    bool crouched = false;
    bool proneRequested = false;
    bool prone = false;
    bool standBlocked = false;
    bool loggedClamp = false;
    bool loggedMaxHits = false;
};

struct CharacterControllerSystem::RuntimeStore {
    std::unordered_map<entt::entity, Runtime> runtimes;
    std::unique_ptr<JPH::CharacterVsCharacterCollisionSimple> characters;
    std::unordered_set<std::uint32_t> loggedParent;
    std::unordered_set<std::uint32_t> loggedScale;
};

CharacterControllerSystem::CharacterControllerSystem(JoltPhysicsEngine& engine, JoltPhysicsSystem& physics)
    : m_Engine(engine)
    , m_Physics(physics)
    , m_Store(std::make_unique<RuntimeStore>())
{
    m_Store->characters = std::make_unique<JPH::CharacterVsCharacterCollisionSimple>();
}

CharacterControllerSystem::~CharacterControllerSystem()
{
    Clear();
    m_Physics.SetCharacterController(nullptr);
}

CharacterControllerSystem::Runtime* CharacterControllerSystem::Find(entt::entity entity)
{
    auto found = m_Store->runtimes.find(entity);
    return found == m_Store->runtimes.end() ? nullptr : &found->second;
}

const CharacterControllerSystem::Runtime* CharacterControllerSystem::Find(entt::entity entity) const
{
    auto found = m_Store->runtimes.find(entity);
    return found == m_Store->runtimes.end() ? nullptr : &found->second;
}

void CharacterControllerSystem::DestroyRuntime(entt::entity entity)
{
    auto found = m_Store->runtimes.find(entity);
    if (found == m_Store->runtimes.end()) {
        return;
    }
    Runtime& runtime = found->second;
    if (runtime.character) {
        const JPH::BodyID proxy = runtime.character->GetInnerBodyID();
        m_Store->characters->Remove(runtime.character.get());
        runtime.character.reset();
        m_Physics.UnregisterCharacterProxy(proxy);
    }
    m_Store->runtimes.erase(found);
}

void CharacterControllerSystem::Clear()
{
    std::vector<entt::entity> entities;
    entities.reserve(m_Store->runtimes.size());
    for (const auto& entry : m_Store->runtimes) {
        entities.push_back(entry.first);
    }
    for (entt::entity entity : entities) {
        DestroyRuntime(entity);
    }
    m_Scene = nullptr;
}

bool CharacterControllerSystem::HasRuntime(entt::entity entity) const
{
    return Find(entity) != nullptr && Find(entity)->character != nullptr;
}

int CharacterControllerSystem::ActiveCount() const
{
    return static_cast<int>(m_Store->runtimes.size());
}

std::uint32_t CharacterControllerSystem::ProxyBodyKey(entt::entity entity) const
{
    const Runtime* runtime = Find(entity);
    if (!runtime || !runtime->character) {
        return kInvalidBodyKey;
    }
    const JPH::BodyID proxy = runtime->character->GetInnerBodyID();
    return proxy.IsInvalid() ? kInvalidBodyKey : proxy.GetIndexAndSequenceNumber();
}

void CharacterControllerSystem::SetMoveInput(entt::entity entity, float worldX, float worldZ)
{
    Runtime* runtime = Find(entity);
    if (!runtime) {
        return;
    }
    runtime->wish = NormalizePlanarWish(worldX, worldZ);
    runtime->legacySpeed.reset();
}

void CharacterControllerSystem::SetSpeedMultiplier(entt::entity entity, float multiplier)
{
    Runtime* runtime = Find(entity);
    if (!runtime || !std::isfinite(multiplier)) {
        return;
    }
    runtime->speedMultiplier = std::max(0.0f, multiplier);
}

float CharacterControllerSystem::GetSpeedMultiplier(entt::entity entity) const
{
    const Runtime* runtime = Find(entity);
    return runtime ? runtime->speedMultiplier : 1.0f;
}

void CharacterControllerSystem::ClearMoveInput(entt::entity entity)
{
    Runtime* runtime = Find(entity);
    if (!runtime) {
        return;
    }
    runtime->wish = {};
    runtime->legacySpeed.reset();
}

void CharacterControllerSystem::Jump(entt::entity entity)
{
    Runtime* runtime = Find(entity);
    if (!runtime) {
        return;
    }
    runtime->jumpRequested = true;
    runtime->jumpSpeedOverride.reset();
}

void CharacterControllerSystem::JumpWithSpeed(entt::entity entity, float speed)
{
    Runtime* runtime = Find(entity);
    if (!runtime || !std::isfinite(speed)) {
        return;
    }
    runtime->jumpRequested = true;
    runtime->jumpSpeedOverride = speed;
}

bool CharacterControllerSystem::SetLegacyMove(entt::entity entity, float worldX, float worldZ, float speed)
{
    Runtime* runtime = Find(entity);
    if (!runtime || !std::isfinite(speed)) {
        return false;
    }
    runtime->wish = NormalizePlanarWish(worldX, worldZ);
    runtime->legacySpeed = std::max(0.0f, speed);
    return true;
}

void CharacterControllerSystem::SetCrouched(entt::entity entity, bool crouched)
{
    Runtime* runtime = Find(entity);
    if (!runtime) {
        return;
    }
    runtime->crouchRequested = crouched;
    if (crouched) {
        runtime->standBlocked = false;
    }
}

void CharacterControllerSystem::SetProne(entt::entity entity, bool prone)
{
    Runtime* runtime = Find(entity);
    if (!runtime) {
        return;
    }
    runtime->proneRequested = prone;
    if (prone) {
        runtime->standBlocked = false;
    }
}

void CharacterControllerSystem::SetFacingYaw(entt::entity entity, float radians)
{
    if (!m_Scene || !std::isfinite(radians) || !HasRuntime(entity)) {
        return;
    }
    if (!m_Scene->GetRegistry().valid(entity) || !m_Scene->GetRegistry().all_of<TransformComponent>(entity)) {
        return;
    }
    m_Scene->GetRegistry().get<TransformComponent>(entity).rotation.y = radians;
}

void CharacterControllerSystem::Teleport(entt::entity entity, const Vec3& worldFeetPosition)
{
    Runtime* runtime = Find(entity);
    if (!runtime || !runtime->character) {
        return;
    }
    runtime->character->SetLinearVelocity(JPH::Vec3::sZero());
    runtime->character->SetPosition(JPH::RVec3(worldFeetPosition.x, worldFeetPosition.y, worldFeetPosition.z));
    runtime->jumpRequested = false;
    runtime->jumpSpeedOverride.reset();
    JPH::PhysicsSystem* physics = m_Engine.GetPhysicsSystem();
    JPH::TempAllocator* allocator = m_Engine.GetTempAllocator();
    if (physics && allocator) {
        const JPH::ObjectLayer layer = m_Engine.ObjectLayerFor(runtime->layer, false);
        const auto broad = physics->GetDefaultBroadPhaseLayerFilter(layer);
        const auto objects = physics->GetDefaultLayerFilter(layer);
        JPH::BodyFilter bodies;
        JPH::ShapeFilter shapes;
        runtime->character->RefreshContacts(broad, objects, bodies, shapes, *allocator);
    }
    if (m_Scene && m_Scene->GetRegistry().valid(entity) && m_Scene->GetRegistry().all_of<TransformComponent>(entity)) {
        m_Scene->GetRegistry().get<TransformComponent>(entity).position = worldFeetPosition;
    }
}

bool CharacterControllerSystem::GetState(entt::entity entity, CharacterControllerState& out) const
{
    const Runtime* runtime = Find(entity);
    if (!runtime || !runtime->character) {
        return false;
    }
    const JPH::CharacterVirtual& character = *runtime->character;
    const Vec3 velocity = FromJolt(character.GetLinearVelocity());
    out = {};
    out.position = FromRVec(character.GetPosition());
    out.velocity = velocity;
    out.planarSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    out.verticalSpeed = velocity.y;
    out.groundState = MapGround(character.GetGroundState());
    out.grounded = out.groundState == CharacterGroundState::OnGround;
    out.groundNormal = FromJolt(character.GetGroundNormal());
    out.groundVelocity = FromJolt(character.GetGroundVelocity());
    out.supportingEntity = m_Physics.GetEntityForBody(character.GetGroundBodyID());
    out.crouched = runtime->crouched;
    out.prone = runtime->prone;
    out.standBlocked = runtime->standBlocked;
    out.onSteepSlope = out.groundState == CharacterGroundState::OnSteepGround;
    return true;
}

void CharacterControllerSystem::SyncLifecycle(Scene& scene)
{
    m_Scene = &scene;
    JPH::PhysicsSystem* physics = m_Engine.GetPhysicsSystem();
    if (!physics) {
        return;
    }

    std::unordered_set<entt::entity> live;
    auto view = scene.GetEntitiesWith<TransformComponent, CharacterControllerComponent>();
    for (auto handle : view) {
        const entt::entity entity = handle;
        const TransformComponent& transform = view.get<TransformComponent>(handle);
        const CharacterControllerComponent& component = view.get<CharacterControllerComponent>(handle);
        const std::uint32_t key = static_cast<std::uint32_t>(entity);
        if (transform.parent != entt::null) {
            if (m_Store->loggedParent.insert(key).second) {
                Hephaestus::EngineLog::Write(
                    Hephaestus::EngineLogLevel::Warning,
                    "Character controller must be a root entity and will not simulate",
                    "Character");
            }
            continue;
        }
        if (!ScaleIsUniformOne(transform.scale) && m_Store->loggedScale.insert(key).second) {
            Hephaestus::EngineLog::Write(
                Hephaestus::EngineLogLevel::Warning,
                "Character controller ignores non-unit scale; capsule dimensions stay in authored meters",
                "Character");
        }
        if (!component.enabled) {
            continue;
        }
        live.insert(entity);
    }

    std::vector<entt::entity> stale;
    for (const auto& entry : m_Store->runtimes) {
        if (!live.contains(entry.first)) {
            stale.push_back(entry.first);
        }
    }
    for (entt::entity entity : stale) {
        DestroyRuntime(entity);
    }

    for (entt::entity entity : live) {
        if (m_Store->runtimes.find(entity) == m_Store->runtimes.end()) {
            const auto& component = scene.GetRegistry().get<CharacterControllerComponent>(entity);
            const auto& transform = scene.GetRegistry().get<TransformComponent>(entity);
            bool clamped = false;
            ControllerAuthoredValues applied = ClampControllerAuthored(AuthoredFrom(component), clamped);
            const std::uint16_t layer = m_Engine.ActiveLayerConfig().Resolve(component.collisionLayer);
            if (clamped) {
                Hephaestus::EngineLog::Write(
                    Hephaestus::EngineLogLevel::Warning,
                    "Character controller values were clamped to the supported range",
                    "Character");
            }

            Runtime runtime;
            runtime.applied = applied;
            runtime.layer = layer;
            runtime.loggedClamp = clamped;
            runtime.standShape = MakeCapsule(applied.height, applied.radius, 1.0f);
            runtime.crouchShape = MakeCapsule(applied.crouchedHeight, applied.radius, 1.0f);
            runtime.innerStand = MakeCapsule(applied.height, applied.radius, kInnerShapeFraction);
            runtime.innerCrouch = MakeCapsule(applied.crouchedHeight, applied.radius, kInnerShapeFraction);
            runtime.proneShape = MakeCapsule(applied.proneHeight, applied.radius, 1.0f);
            runtime.innerProne = MakeCapsule(applied.proneHeight, applied.radius, kInnerShapeFraction);

            JPH::CharacterVirtualSettings settings;
            settings.mMass = applied.mass;
            settings.mMaxStrength = applied.pushStrength;
            settings.mShape = runtime.standShape;
            settings.mInnerBodyShape = runtime.innerStand;
            settings.mInnerBodyLayer = m_Engine.ObjectLayerFor(layer, false);
            settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -applied.radius);
            settings.mMaxSlopeAngle = JPH::DegreesToRadians(applied.maxSlopeAngleDegrees);
            settings.mEnhancedInternalEdgeRemoval = true;
            settings.mUp = JPH::Vec3::sAxisY();

            runtime.character = std::make_unique<CharacterVirtualAccess>(
                &settings,
                JPH::RVec3(transform.position.x, transform.position.y, transform.position.z),
                JPH::Quat::sIdentity(),
                EntityUserData(entity),
                physics);
            runtime.character->SetCharacterVsCharacterCollision(m_Store->characters.get());
            m_Store->characters->Add(runtime.character.get());
            const JPH::BodyID proxy = runtime.character->GetInnerBodyID();
            if (!proxy.IsInvalid()) {
                m_Physics.RegisterCharacterProxy(entity, proxy);
            }

            JPH::TempAllocator* allocator = m_Engine.GetTempAllocator();
            if (allocator) {
                const JPH::ObjectLayer objectLayer = m_Engine.ObjectLayerFor(layer, false);
                const auto broad = physics->GetDefaultBroadPhaseLayerFilter(objectLayer);
                const auto objects = physics->GetDefaultLayerFilter(objectLayer);
                JPH::BodyFilter bodies;
                JPH::ShapeFilter shapes;
                runtime.character->RefreshContacts(broad, objects, bodies, shapes, *allocator);
            }
            m_Store->runtimes.emplace(entity, std::move(runtime));
        }
    }
}

void CharacterControllerSystem::FixedStep(Scene& scene, float fixedDt)
{
    SyncLifecycle(scene);
    if (!m_Engine.GetPhysicsSystem() || !m_Engine.GetTempAllocator() || fixedDt <= 0.0f) {
        return;
    }

    std::vector<entt::entity> order;
    order.reserve(m_Store->runtimes.size());
    for (const auto& entry : m_Store->runtimes) {
        order.push_back(entry.first);
    }
    std::sort(order.begin(), order.end(), [](entt::entity a, entt::entity b) {
        return static_cast<std::uint32_t>(a) < static_cast<std::uint32_t>(b);
    });

    JPH::PhysicsSystem* physics = m_Engine.GetPhysicsSystem();
    JPH::TempAllocator& allocator = *m_Engine.GetTempAllocator();
    const Vec3 gravity = m_Engine.GetGravity();
    const float penetration = 1.5f * physics->GetPhysicsSettings().mPenetrationSlop;

    for (entt::entity entity : order) {
        Runtime* runtime = Find(entity);
        if (!runtime || !runtime->character) {
            continue;
        }
        if (!scene.GetRegistry().valid(entity)
            || !scene.GetRegistry().all_of<TransformComponent, CharacterControllerComponent>(entity)) {
            continue;
        }
        auto& transform = scene.GetRegistry().get<TransformComponent>(entity);
        const auto& component = scene.GetRegistry().get<CharacterControllerComponent>(entity);
        JPH::CharacterVirtual& character = *runtime->character;

        const Vec3 simulated = FromRVec(character.GetPosition());
        const Vec3 delta(transform.position.x - simulated.x,
                         transform.position.y - simulated.y,
                         transform.position.z - simulated.z);
        if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z > kTeleportDistance * kTeleportDistance) {
            Teleport(entity, transform.position);
        }

        bool clamped = false;
        ControllerAuthoredValues applied = ClampControllerAuthored(AuthoredFrom(component), clamped);
        const std::uint16_t layer = m_Engine.ActiveLayerConfig().Resolve(component.collisionLayer);
        if (clamped && !runtime->loggedClamp) {
            runtime->loggedClamp = true;
            Hephaestus::EngineLog::Write(
                Hephaestus::EngineLogLevel::Warning,
                "Character controller values were clamped to the supported range",
                "Character");
        }
        const bool shapeChanged = !SameAuthored(applied, runtime->applied);
        const bool layerChanged = layer != runtime->layer;
        if (layerChanged) {
            runtime->layer = layer;
            const JPH::BodyID proxy = character.GetInnerBodyID();
            if (!proxy.IsInvalid()) {
                m_Engine.GetBodyInterface().SetObjectLayer(proxy, m_Engine.ObjectLayerFor(layer, false));
            }
        }

        const JPH::ObjectLayer objectLayer = m_Engine.ObjectLayerFor(runtime->layer, false);
        const auto broad = physics->GetDefaultBroadPhaseLayerFilter(objectLayer);
        const auto objects = physics->GetDefaultLayerFilter(objectLayer);
        JPH::BodyFilter bodies;
        JPH::ShapeFilter shapes;

        if (shapeChanged) {
            runtime->standShape = MakeCapsule(applied.height, applied.radius, 1.0f);
            runtime->crouchShape = MakeCapsule(applied.crouchedHeight, applied.radius, 1.0f);
            runtime->innerStand = MakeCapsule(applied.height, applied.radius, kInnerShapeFraction);
            runtime->innerCrouch = MakeCapsule(applied.crouchedHeight, applied.radius, kInnerShapeFraction);
            runtime->proneShape = MakeCapsule(applied.proneHeight, applied.radius, 1.0f);
            runtime->innerProne = MakeCapsule(applied.proneHeight, applied.radius, kInnerShapeFraction);
            runtime->applied = applied;
            character.SetMass(applied.mass);
            character.SetMaxStrength(applied.pushStrength);
            character.SetMaxSlopeAngle(JPH::DegreesToRadians(applied.maxSlopeAngleDegrees));
            runtime->character->SetSupportingPlane(JPH::Plane(JPH::Vec3::sAxisY(), -applied.radius));
            const JPH::Shape* next = runtime->prone ? runtime->proneShape.GetPtr()
                : runtime->crouched ? runtime->crouchShape.GetPtr() : runtime->standShape.GetPtr();
            const JPH::Shape* inner = runtime->prone ? runtime->innerProne.GetPtr()
                : runtime->crouched ? runtime->innerCrouch.GetPtr() : runtime->innerStand.GetPtr();
            if (character.SetShape(next, penetration, broad, objects, bodies, shapes, allocator)) {
                character.SetInnerBodyShape(inner);
            }
        }

        // Stances from tallest to shortest: 0 standing, 1 crouched, 2 prone.
        const int current = runtime->prone ? 2 : (runtime->crouched ? 1 : 0);
        const int wanted = runtime->proneRequested ? 2 : (runtime->crouchRequested ? 1 : 0);
        if (wanted != current) {
            const JPH::Shape* next = wanted == 2 ? runtime->proneShape.GetPtr()
                : wanted == 1 ? runtime->crouchShape.GetPtr() : runtime->standShape.GetPtr();
            const JPH::Shape* inner = wanted == 2 ? runtime->innerProne.GetPtr()
                : wanted == 1 ? runtime->innerCrouch.GetPtr() : runtime->innerStand.GetPtr();
            if (character.SetShape(next, penetration, broad, objects, bodies, shapes, allocator)) {
                character.SetInnerBodyShape(inner);
                runtime->crouched = wanted == 1;
                runtime->prone = wanted == 2;
                runtime->standBlocked = false;
            } else if (wanted < current) {
                runtime->standBlocked = true;
            }
        }

        character.UpdateGroundVelocity();
        const CharacterGroundState ground = MapGround(character.GetGroundState());
        const Vec3 velocity = FromJolt(character.GetLinearVelocity());
        const Vec3 groundVelocity = FromJolt(character.GetGroundVelocity());
        const bool jumpRequested = runtime->jumpRequested;
        const float jumpSpeed = runtime->jumpSpeedOverride.value_or(runtime->applied.jumpSpeed);
        runtime->jumpRequested = false;
        runtime->jumpSpeedOverride.reset();

        VelocityStepInput step;
        step.ground = ground;
        step.velocity = velocity;
        step.groundVelocity = groundVelocity;
        step.wish = runtime->wish;
        const float scaledWalkSpeed = runtime->applied.walkSpeed * std::max(0.0f, runtime->speedMultiplier);
        step.targetSpeed = runtime->legacySpeed.value_or(scaledWalkSpeed);
        step.acceleration = runtime->applied.acceleration;
        step.deceleration = runtime->applied.deceleration;
        step.airAcceleration = runtime->applied.airAcceleration;
        step.airControl = runtime->applied.airControl;
        step.gravity = gravity;
        step.gravityScale = runtime->applied.gravityScale;
        step.fixedDt = fixedDt;
        step.maxFallSpeed = runtime->applied.maxFallSpeed;
        const JumpDecision jump = StepJumpGate(runtime->jumpGate, jumpRequested, ground, runtime->applied.coyoteTime,
                                               runtime->applied.jumpBufferTime, fixedDt);
        step.jump = jump.jump;
        step.coyoteJump = jump.coyote;
        step.jumpSpeed = jumpSpeed;
        step.instantPlanarSpeed = runtime->legacySpeed.has_value();
        const VelocityStepResult integrated = IntegrateCharacterVelocity(step);
        character.SetLinearVelocity(JPH::Vec3(integrated.velocity.x, integrated.velocity.y, integrated.velocity.z));

        JPH::CharacterVirtual::ExtendedUpdateSettings extended;
        extended.mStickToFloorStepDown = JPH::Vec3(0.0f, -runtime->applied.groundSnapDistance, 0.0f);
        extended.mWalkStairsStepUp = JPH::Vec3(0.0f, runtime->applied.stepHeight, 0.0f);
        extended.mWalkStairsMinStepForward = 0.02f;
        extended.mWalkStairsStepForwardTest = 0.15f;
        extended.mWalkStairsCosAngleForwardContact = std::cos(JPH::DegreesToRadians(75.0f));
        extended.mWalkStairsStepDownExtra = JPH::Vec3::sZero();
        const JPH::Vec3 press(gravity.x * runtime->applied.gravityScale,
                              gravity.y * runtime->applied.gravityScale,
                              gravity.z * runtime->applied.gravityScale);
        character.ExtendedUpdate(fixedDt, press, extended, broad, objects, bodies, shapes, allocator);
        if (character.GetMaxHitsExceeded() && !runtime->loggedMaxHits) {
            runtime->loggedMaxHits = true;
            Hephaestus::EngineLog::Write(
                Hephaestus::EngineLogLevel::Warning,
                "Character controller exceeded the contact cap; geometry is too complex for one step",
                "Character");
        }

        transform.position = FromRVec(character.GetPosition());
    }
}

} // namespace Physics
