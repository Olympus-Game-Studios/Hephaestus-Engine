#include "Automation/AutomationService.h"
#include "Automation/PhysicsAutomation.h"
#include "Automation/PhysicsHostAdapter.h"
#include "Physics/CharacterMotor.h"
#include "Physics/JoltPhysicsEngine.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <Jolt/Physics/Body/BodyLock.h>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace A = Hephaestus::Automation;

namespace {

bool Near(float a, float b)
{
    return std::fabs(a - b) < 1.0e-3f;
}

Entity AddBox(Scene& scene, const char* name, Vec3 position, Vec3 extents, bool isStatic, bool sensor)
{
    Entity entity = scene.CreateEntity(name);
    entity.GetComponent<TransformComponent>().position = position;
    entity.GetComponent<TransformComponent>().rotation = Vec3(0.0f, 0.4f, 0.0f);
    auto& body = entity.AddComponent<RigidBodyComponent>();
    body.isStatic = isStatic;
    body.isTrigger = sensor;
    body.useGravity = false;
    body.mass = isStatic ? 0.0f : 1.0f;
    body.linearVelocity = Vec3(0.25f, 0.0f, -0.5f);
    entity.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(extents));
    return entity;
}

class ContractBackend final : public A::AutomationBackend {
public:
    Scene scene{"PhysicsContract"};
    Hephaestus::PlayState play = Hephaestus::PlayState::Playing;
    A::HostMask host = A::HostMask::Editor;
    bool ready = true;
    int rayCalls = 0;
    entt::entity lastIgnore = entt::null;
    bool raySucceeds = true;
    std::string rayError = "unavailable";
    A::AutomationBackend::PhysicsRaycastResult ray{};
    std::vector<A::AutomationBackend::PhysicsBodySnapshot> bodies;
    entt::entity unreadable = entt::null;
    bool grounded = true;

    A::HostMask Host() const noexcept override { return host; }
    A::SceneAccess ActiveScene() noexcept override { return {&scene}; }
    Hephaestus::PlayState GetPlayState() const noexcept override { return play; }

    bool PhysicsRaycast(const A::AutomationBackend::PhysicsVec3&, const A::AutomationBackend::PhysicsVec3&,
                        float, entt::entity ignore, A::AutomationBackend::PhysicsRaycastResult& out,
                        std::string& outError, const std::string* = nullptr) override
    {
        ++rayCalls;
        lastIgnore = ignore;
        if (!ready) { outError = "unavailable"; return false; }
        if (!raySucceeds) { outError = rayError; return false; }
        out = ray;
        return true;
    }

    bool ListPhysicsBodies(std::vector<A::AutomationBackend::PhysicsBodySnapshot>& out, std::string& outError) override
    {
        if (!ready) { outError = "unavailable"; return false; }
        out = bodies;
        return true;
    }

    bool GetPhysicsBody(entt::entity entity, A::AutomationBackend::PhysicsBodySnapshot& out, std::string& outError) override
    {
        if (!ready) { outError = "unavailable"; return false; }
        if (entity == unreadable) { outError = "unreadable"; return false; }
        for (const auto& body : bodies) {
            if (body.entity == entity) { out = body; return true; }
        }
        outError = "no_body";
        return false;
    }

    bool GetCharacterState(entt::entity entity, A::AutomationBackend::PhysicsBodySnapshot& pose, bool& outGrounded,
                           std::string& outError) override
    {
        if (!GetPhysicsBody(entity, pose, outError)) return false;
        outGrounded = grounded;
        return true;
    }
};

bool LiveRead(Physics::JoltPhysicsEngine& engine, Physics::JoltPhysicsSystem& system, entt::entity entity,
              JPH::RVec3& position, JPH::Quat& rotation, JPH::Vec3& linear, bool& sensor, bool& active)
{
    const JPH::BodyID bodyId = system.GetBodyID(entity);
    if (bodyId.IsInvalid() || !engine.GetPhysicsSystem()) return false;
    JPH::BodyLockRead lock(engine.GetPhysicsSystem()->GetBodyLockInterface(), bodyId);
    if (!lock.Succeeded()) return false;
    const JPH::Body& body = lock.GetBody();
    position = body.GetPosition();
    rotation = body.GetRotation();
    linear = body.GetLinearVelocity();
    sensor = body.IsSensor();
    active = body.IsActive();
    return true;
}

} // namespace

int main()
{
    bool ok = true;
    auto check = [&](bool value, const char* message) {
        if (!value) std::cerr << "[FAIL] " << message << '\n';
        ok &= value;
    };

    ContractBackend backend;
    A::AutomationService service(backend);
    A::RegisterPhysicsAutomation(service);
    auto call = [&](std::string name, A::Json args = A::Json::object()) {
        auto future = service.Post({std::move(name), std::move(args)});
        service.Tick();
        return future.result.get();
    };

    backend.play = Hephaestus::PlayState::Editor;
    const auto gated = call("physics.list_bodies");
    check(!gated.Ok() && gated.error->code == A::ErrorCode::PlayModeRequired,
          "Editor not playing rejects physics.list_bodies");
    backend.play = Hephaestus::PlayState::Playing;

    Entity floorEntity = backend.scene.CreateEntity("Floor");
    Entity bareEntity = backend.scene.CreateEntity("Bare");
    const entt::entity floor = static_cast<entt::entity>(floorEntity);
    const entt::entity bare = static_cast<entt::entity>(bareEntity);
    Entity staleEntity = backend.scene.CreateEntity("Stale");
    const entt::entity stale = static_cast<entt::entity>(staleEntity);
    backend.scene.DestroyEntity(staleEntity);

    A::AutomationBackend::PhysicsBodySnapshot listed;
    listed.entity = floor;
    listed.entityValid = true;
    listed.motionType = "static";
    listed.active = false;
    listed.sleeping = false;
    listed.sensor = true;
    listed.position = {1.0f, 2.0f, 3.0f};
    listed.rotation = {0.1f, 0.2f, 0.3f, 0.4f};
    listed.linearVelocity = {4.0f, 5.0f, 6.0f};
    listed.angularVelocity = {7.0f, 8.0f, 9.0f};
    listed.debugBodyIndex = 12;
    A::AutomationBackend::PhysicsBodySnapshot expired = listed;
    expired.entity = stale;
    expired.entityValid = false;
    expired.motionType = "dynamic";
    A::AutomationBackend::PhysicsBodySnapshot orphan = listed;
    orphan.entity = entt::null;
    orphan.entityValid = false;
    orphan.motionType = "kinematic";
    backend.bodies = {listed, expired, orphan};
    backend.unreadable = bare;

    const auto listedResult = call("physics.list_bodies");
    check(listedResult.Ok() && listedResult.value["bodies"].size() == 3, "List returns readable snapshots");
    if (listedResult.Ok()) {
        const A::Json& body = listedResult.value["bodies"][0];
        check(body["entityStatus"] == "resolved" && body["motionType"] == "static" && body["sleeping"] == false &&
                  body["sensor"] == true && body["debugBodyIndex"] == 12 && body["position"][1] == 2.0f &&
                  body["rotation"][3] == 0.4f && body["linearVelocity"][0] == 4.0f,
              "Resolved body publishes the supplied fields");
        check(listedResult.value["bodies"][1]["entity"].is_null() &&
                  listedResult.value["bodies"][1]["entityStatus"] == "expired",
              "Invalid body key is expired and is not bound");
        check(listedResult.value["bodies"][2]["entity"].is_null() &&
                  listedResult.value["bodies"][2]["entityStatus"] == "none",
              "Null body key is none");
    }

    const std::string floorRef = service.Entities().Bind(backend.scene, floor).value;
    const auto got = call("physics.get_body", {{"entity", floorRef}});
    check(got.Ok() && got.value["entity"] == floorRef && got.value["angularVelocity"][2] == 9.0f,
          "get_body returns the same snapshot");
    Entity emptyEntity = backend.scene.CreateEntity("Empty");
    const std::string emptyRef = service.Entities().Bind(backend.scene, static_cast<entt::entity>(emptyEntity)).value;
    const auto missingBody = call("physics.get_body", {{"entity", emptyRef}});
    check(!missingBody.Ok() && missingBody.error->data.value("reason", "") == "no_body", "Missing body is no_body");
    const std::string bareRef = service.Entities().Bind(backend.scene, bare).value;
    const auto unreadable = call("physics.get_body", {{"entity", bareRef}});
    check(!unreadable.Ok() && unreadable.error->data.value("reason", "") == "unreadable",
          "Unreadable body is unreadable");
    const auto character = call("physics.get_character_state", {{"entity", floorRef}});
    check(character.Ok() && character.value["grounded"] == true && character.value["position"][0] == 1.0f &&
              !character.value.contains("angularVelocity"),
          "Character state is position, velocity, and grounded");
    const auto characterMissing = call("physics.get_character_state", {{"entity", bareRef}});
    check(!characterMissing.Ok() && characterMissing.error->data.value("reason", "") == "unreadable",
          "Character state does not invent grounded for an unreadable body");

    backend.ray.hit = true;
    backend.ray.entity = floor;
    backend.ray.entityValid = true;
    backend.ray.point = {0.0f, 1.0f, 0.0f};
    backend.ray.normal = {0.0f, 1.0f, 0.0f};
    backend.ray.distance = 1.5f;
    const auto hit = call("physics.raycast",
                          {{"origin", {0.0f, 2.0f, 0.0f}}, {"direction", {0.0f, -1.0f, 0.0f}}, {"maxDistance", 4.0f}});
    check(hit.Ok() && hit.value["hit"] == true && hit.value["entity"] == floorRef && hit.value["distance"] == 1.5f &&
              !hit.value.contains("entityStatus"),
          "Raycast hit binds the entity");

    backend.ray.hit = false;
    const auto miss = call("physics.raycast",
                           {{"origin", {0.0f, 2.0f, 0.0f}}, {"direction", {0.0f, 1.0f, 0.0f}}, {"maxDistance", 4.0f}});
    check(miss.Ok() && miss.value["hit"] == false && !miss.value.contains("entity"), "Raycast miss is hit false");

    backend.ray.hit = true;
    backend.ray.entity = entt::null;
    backend.ray.entityValid = false;
    const auto nullHit = call("physics.raycast",
                              {{"origin", {0.0f, 2.0f, 0.0f}}, {"direction", {0.0f, -1.0f, 0.0f}}, {"maxDistance", 4.0f}});
    check(nullHit.Ok() && nullHit.value["entity"].is_null() && !nullHit.value.contains("entityStatus"),
          "Raycast without an entity is null");

    const int callsBefore = backend.rayCalls;
    const auto badDirection = call("physics.raycast",
                                   {{"origin", {0.0f, 2.0f, 0.0f}}, {"direction", {0.0f, 0.0f, 0.0f}}, {"maxDistance", 4.0f}});
    check(!badDirection.Ok() && badDirection.error->data.value("reason", "") == "invalid_direction" &&
              backend.rayCalls == callsBefore,
          "Zero direction is invalid_direction and does not query");
    const auto badDistance = call("physics.raycast",
                                  {{"origin", {0.0f, 2.0f, 0.0f}}, {"direction", {0.0f, -1.0f, 0.0f}}, {"maxDistance", 0.0f}});
    check(!badDistance.Ok() && badDistance.error->code == A::ErrorCode::InvalidArguments &&
              backend.rayCalls == callsBefore,
          "Non-positive maxDistance is rejected");

    const auto expiredIgnore = call("physics.raycast",
                                    {{"origin", {0.0f, 2.0f, 0.0f}},
                                     {"direction", {0.0f, -1.0f, 0.0f}},
                                     {"maxDistance", 4.0f},
                                     {"ignore", "missing-ref"}});
    check(!expiredIgnore.Ok() && expiredIgnore.error->code == A::ErrorCode::EntityRefExpired &&
              backend.rayCalls == callsBefore,
          "Unresolved ignore is EntityRefExpired");
    const auto ignored = call("physics.raycast",
                              {{"origin", {0.0f, 2.0f, 0.0f}},
                               {"direction", {0.0f, -1.0f, 0.0f}},
                               {"maxDistance", 4.0f},
                               {"ignore", bareRef}});
    check(ignored.Ok() && backend.lastIgnore == bare && backend.rayCalls == callsBefore + 1,
          "Resolved ignore with no body is still passed through");

    backend.host = A::HostMask::Runtime;
    check(call("physics.list_bodies").Ok(), "Runtime host can list bodies");
    backend.ready = false;
    const auto unavailable = call("physics.list_bodies");
    check(!unavailable.Ok() && unavailable.error->code == A::ErrorCode::ToolUnavailable,
          "Missing physics world is unavailable");

    Scene world{"PhysicsWorld"};
    Physics::JoltPhysicsEngine engine;
    engine.Initialize();
    Physics::JoltPhysicsSystem physics(engine);
    Entity floorBody = AddBox(world, "Floor", Vec3(0.0f, 0.0f, 0.0f), Vec3(4.0f, 0.5f, 4.0f), true, true);
    Entity dynamicBody = AddBox(world, "Dynamic", Vec3(0.0f, 0.56f, 0.0f), Vec3(0.05f, 0.05f, 0.05f), false, false);
    const entt::entity floorId = static_cast<entt::entity>(floorBody);
    const entt::entity dynamicId = static_cast<entt::entity>(dynamicBody);
    physics.Update(world, 1.0f / 60.0f);

    std::vector<Physics::BodyObservation> observed;
    physics.ObserveBodies(observed);
    check(observed.size() == 2, "Both created bodies are readable after Update");

    Physics::BodyObservation floorSnapshot;
    Physics::BodyObservation dynamicSnapshot;
    check(physics.ObserveBody(floorId, floorSnapshot) == Physics::BodyObservationStatus::Ready, "Floor snapshot is ready");
    check(physics.ObserveBody(dynamicId, dynamicSnapshot) == Physics::BodyObservationStatus::Ready,
          "Dynamic snapshot is ready");

    JPH::RVec3 livePosition;
    JPH::Quat liveRotation;
    JPH::Vec3 liveLinear;
    bool liveSensor = false;
    bool liveActive = false;
    check(LiveRead(engine, physics, floorId, livePosition, liveRotation, liveLinear, liveSensor, liveActive),
          "Floor can be locked for comparison");
    check(floorSnapshot.motionType == "static" && floorSnapshot.sleeping == false && floorSnapshot.sensor == liveSensor &&
              liveSensor && floorSnapshot.active == liveActive &&
              floorSnapshot.debugBodyIndex == physics.GetBodyID(floorId).GetIndex() &&
              Near(floorSnapshot.position.y, static_cast<float>(livePosition.GetY())) &&
              Near(floorSnapshot.rotationW, liveRotation.GetW()) &&
              Near(floorSnapshot.linearVelocity.x, liveLinear.GetX()),
          "Floor snapshot matches the locked body and is not sleeping");

    check(LiveRead(engine, physics, dynamicId, livePosition, liveRotation, liveLinear, liveSensor, liveActive),
          "Dynamic body can be locked for comparison");
    const JPH::Vec3 liveAngular = engine.GetBodyInterface().GetAngularVelocity(physics.GetBodyID(dynamicId));
    check(dynamicSnapshot.motionType == "dynamic" && dynamicSnapshot.sensor == liveSensor && !liveSensor &&
              dynamicSnapshot.sleeping == (!dynamicSnapshot.active) && dynamicSnapshot.active == liveActive &&
              Near(dynamicSnapshot.position.x, static_cast<float>(livePosition.GetX())) &&
              Near(dynamicSnapshot.position.y, static_cast<float>(livePosition.GetY())) &&
              Near(dynamicSnapshot.rotationY, liveRotation.GetY()) &&
              Near(dynamicSnapshot.linearVelocity.z, liveLinear.GetZ()) &&
              Near(dynamicSnapshot.angularVelocity.x, liveAngular.GetX()) &&
              Near(dynamicSnapshot.angularVelocity.y, liveAngular.GetY()) &&
              Near(dynamicSnapshot.angularVelocity.z, liveAngular.GetZ()),
          "Dynamic snapshot matches the locked body");

    Physics::RaycastHit cast{};
    check(physics.Raycast(entt::null, Vec3(2.0f, 2.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 10.0f, cast) && cast.hit &&
              cast.entity == floorId,
          "Downward ray hits the floor");
    check(physics.Raycast(entt::null, Vec3(2.0f, 2.0f, 0.0f), Vec3(0.0f, -4.0f, 0.0f), 10.0f, cast) && cast.hit,
          "Non-unit direction is accepted by Raycast");
    check(physics.Raycast(entt::null, Vec3(2.0f, 2.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 10.0f, cast) && !cast.hit,
          "Ray aimed away is a miss");
    check(!physics.Raycast(entt::null, Vec3(2.0f, 2.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), 10.0f, cast),
          "Zero direction fails the query");
    check(physics.Raycast(floorId, Vec3(2.0f, 2.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 10.0f, cast) && !cast.hit,
          "Ignoring the floor reports a miss");
    check(Physics::CharacterMotor::IsGrounded(engine, physics, dynamicId), "Dynamic body probe is grounded on the floor");

    class JoltBackend final : public A::AutomationBackend {
    public:
        Scene& world;
        Physics::JoltPhysicsEngine& engine;
        Physics::JoltPhysicsSystem& physics;
        explicit JoltBackend(Scene& world, Physics::JoltPhysicsEngine& engine, Physics::JoltPhysicsSystem& physics)
            : world(world), engine(engine), physics(physics) {}
        A::HostMask Host() const noexcept override { return A::HostMask::Editor; }
        A::SceneAccess ActiveScene() noexcept override { return {&world}; }
        Hephaestus::PlayState GetPlayState() const noexcept override { return Hephaestus::PlayState::Playing; }
        bool PhysicsRaycast(const A::AutomationBackend::PhysicsVec3& origin,
                            const A::AutomationBackend::PhysicsVec3& direction, float maxDistance, entt::entity ignore,
                            A::AutomationBackend::PhysicsRaycastResult& out, std::string& outError,
                            const std::string* layerName = nullptr) override
        {
            return A::CastPhysicsRay(true, &engine, &physics, &world, origin, direction, maxDistance, ignore, out, outError, layerName);
        }
        bool ListPhysicsBodies(std::vector<A::AutomationBackend::PhysicsBodySnapshot>& out, std::string& outError) override
        {
            return A::ListPhysicsSnapshots(true, &engine, &physics, &world, out, outError);
        }
        bool GetPhysicsBody(entt::entity entity, A::AutomationBackend::PhysicsBodySnapshot& out, std::string& outError) override
        {
            return A::ReadPhysicsSnapshot(true, &engine, &physics, &world, entity, out, outError);
        }
        bool GetCharacterState(entt::entity entity, A::AutomationBackend::PhysicsBodySnapshot& pose, bool& outGrounded,
                               std::string& outError) override
        {
            return A::ReadCharacterSnapshot(true, &engine, &physics, &world, entity, pose, outGrounded, outError);
        }
    } joltBackend(world, engine, physics);

    A::AutomationService joltService(joltBackend);
    A::RegisterPhysicsAutomation(joltService);
    auto joltCall = [&](std::string name, A::Json args = A::Json::object()) {
        auto future = joltService.Post({std::move(name), std::move(args)});
        joltService.Tick();
        return future.result.get();
    };
    const auto liveList = joltCall("physics.list_bodies");
    check(liveList.Ok() && liveList.value["bodies"].size() == 2, "Tool list matches readable Jolt bodies");
    const std::string liveFloor = joltService.Entities().Bind(world, floorId).value;
    const std::string liveDynamic = joltService.Entities().Bind(world, dynamicId).value;
    const auto liveBody = joltCall("physics.get_body", {{"entity", liveDynamic}});
    const auto liveState = joltCall("physics.get_character_state", {{"entity", liveDynamic}});
    check(liveBody.Ok() && liveBody.value["motionType"] == "dynamic" && liveBody.value["sensor"] == false &&
              liveState.Ok() && liveState.value["grounded"] == true &&
              Near(liveState.value["linearVelocity"][2].get<float>(), dynamicSnapshot.linearVelocity.z),
          "Tool body and character state match the read snapshot");
    const auto liveHit = joltCall("physics.raycast",
                                  {{"origin", {2.0f, 2.0f, 0.0f}}, {"direction", {0.0f, -1.0f, 0.0f}}, {"maxDistance", 10.0f}});
    check(liveHit.Ok() && liveHit.value["hit"] == true && liveHit.value["entity"] == liveFloor, "Tool raycast hits the floor");

    world.DestroyEntity(dynamicBody);
    Physics::BodyObservation staleSnapshot;
    check(physics.ObserveBody(dynamicId, staleSnapshot) == Physics::BodyObservationStatus::Ready,
          "Destroyed entity keeps its body until the next Update");
    physics.ObserveBodies(observed);
    check(observed.size() == 2, "Stale body is still listed before the next Update");
    std::string staleError;
    A::AutomationBackend::PhysicsBodySnapshot discarded;
    check(A::ReadPhysicsSnapshot(true, &engine, &physics, &world, dynamicId, discarded, staleError),
          "Stale body can still be read");
    check(!discarded.entityValid, "Stale body is not reported as a live entity");

    physics.Update(world, 1.0f / 60.0f);
    check(physics.ObserveBody(dynamicId, staleSnapshot) == Physics::BodyObservationStatus::Missing,
          "Body is gone after the next Update");
    physics.ObserveBodies(observed);
    check(observed.size() == 1, "List skips the removed body");
    check(!A::ReadPhysicsSnapshot(true, &engine, &physics, &world, dynamicId, discarded, staleError) && staleError == "no_body",
          "get_body reports no_body after the body is removed");
    bool unusedGrounded = true;
    check(!A::ReadCharacterSnapshot(true, &engine, &physics, &world, dynamicId, discarded, unusedGrounded, staleError) &&
              staleError == "no_body" && unusedGrounded == true,
          "Character state does not clear grounded when the body is already gone");

    if (!ok) return 1;
    std::cout << "AutomationPhysicsTests passed\n";
    return 0;
}
