#include "Construction/ConstructionSystem.h"
#include "Physics/CharacterControllerSystem.h"
#include "Physics/JoltPhysicsEngine.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Physics/PhysicsContactEvents.h"
#include "Physics/PhysicsLayerConfig.h"
#include "Physics/RagdollSystem.h"
#include "Scene/CharacterComponent.h"
#include "Scene/CharacterControllerComponent.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <Jolt/Physics/Body/BodyInterface.h>

#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool g_Pass = true;

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        g_Pass = false;
        return false;
    }
    std::cout << "[PASS] " << message << "\n";
    return true;
}

struct Session {
    Physics::JoltPhysicsEngine engine;
    Scene scene;
    Physics::JoltPhysicsSystem system;
    Physics::CharacterControllerSystem controllers;

    explicit Session(const char* name, Physics::PhysicsLayerConfig layers = Physics::PhysicsLayerConfig::Seed())
        : scene(name)
        , system(engine)
        , controllers(engine, system)
    {
        engine.Initialize();
        engine.BeginPlayWorld(std::move(layers));
        system.SetCharacterController(&controllers);
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void Step(int frames = 1)
    {
        for (int i = 0; i < frames; ++i) {
            system.Update(scene, 1.0f / 60.0f);
        }
    }
};

Entity AddStatic(Session& session, const char* name, Vec3 position, Vec3 halfExtents, std::uint16_t layer = Physics::kLayerWorld)
{
    Entity entity = session.scene.CreateEntity(name);
    entity.GetComponent<TransformComponent>().position = position;
    auto& body = entity.AddComponent<RigidBodyComponent>();
    body.SetMotion(RigidBodyMotion::Static);
    body.mass = 0.0f;
    body.useGravity = false;
    body.layer = layer;
    entity.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(halfExtents));
    return entity;
}

Entity AddCharacter(Session& session, Vec3 feet, const char* name = "Character")
{
    Entity entity = session.scene.CreateEntity(name);
    entity.GetComponent<TransformComponent>().position = feet;
    entity.AddComponent<CharacterControllerComponent>();
    return entity;
}

Physics::CharacterControllerState StateOf(Session& session, entt::entity entity)
{
    Physics::CharacterControllerState state;
    session.controllers.GetState(entity, state);
    return state;
}

void AddFloor(Session& session)
{
    AddStatic(session, "Floor", Vec3(0.0f, -0.5f, 0.0f), Vec3(30.0f, 0.5f, 30.0f));
}

bool ProxyActive(Session& session, entt::entity entity)
{
    const std::uint32_t key = session.controllers.ProxyBodyKey(entity);
    if (key == 0xFFFFFFFFu) {
        return false;
    }
    return session.engine.GetBodyInterface().IsActive(JPH::BodyID(key));
}

// A standing humanoid (Mixamo bone names, metres) with an evaluated pose, as a bound Character has.
void AddHumanoid(Entity entity, bool humanoid = true)
{
    struct BoneSpec { const char* name; int parent; Vec3 offset; };
    const BoneSpec specs[] = {
        {"Hips", -1, Vec3(0.0f, 1.0f, 0.0f)},        {"Spine", 0, Vec3(0.0f, 0.1f, 0.0f)},
        {"Spine1", 1, Vec3(0.0f, 0.1f, 0.0f)},       {"Neck", 2, Vec3(0.0f, 0.3f, 0.0f)},
        {"Head", 3, Vec3(0.0f, 0.1f, 0.0f)},         {"HeadTop_End", 4, Vec3(0.0f, 0.2f, 0.0f)},
        {"LeftArm", 3, Vec3(0.2f, -0.05f, 0.0f)},    {"LeftForeArm", 6, Vec3(0.3f, 0.0f, 0.0f)},
        {"LeftHand", 7, Vec3(0.25f, 0.0f, 0.0f)},    {"RightArm", 3, Vec3(-0.2f, -0.05f, 0.0f)},
        {"RightForeArm", 9, Vec3(-0.3f, 0.0f, 0.0f)}, {"RightHand", 10, Vec3(-0.25f, 0.0f, 0.0f)},
        {"LeftUpLeg", 0, Vec3(0.1f, -0.05f, 0.0f)},  {"LeftLeg", 12, Vec3(0.0f, -0.45f, 0.0f)},
        {"LeftFoot", 13, Vec3(0.0f, -0.45f, 0.0f)},  {"RightUpLeg", 0, Vec3(-0.1f, -0.05f, 0.0f)},
        {"RightLeg", 15, Vec3(0.0f, -0.45f, 0.0f)},  {"RightFoot", 16, Vec3(0.0f, -0.45f, 0.0f)},
    };
    auto& character = entity.AddComponent<CharacterComponent>();
    for (int i = 0; i < static_cast<int>(sizeof(specs) / sizeof(specs[0])); ++i) {
        Hephaestus::SkeletonBone bone;
        bone.name = std::string(humanoid ? "mixamorig:" : "bone") + (humanoid ? specs[i].name : std::to_string(i));
        bone.parentEval = specs[i].parent;
        bone.evalIndex = i;
        bone.localRest = Mat4::Translation(specs[i].offset);
        character.skeleton.bones.push_back(bone);
        character.localMatrices.push_back(bone.localRest);
    }
    Hephaestus::SkeletonBuilder::EvaluateWorld(character.skeleton, character.localMatrices, character.worldPose);
    character.runtimeReady = true;
}

Vec3 BoneWorld(Session& session, Entity entity, int bone)
{
    const Mat4 world = Mat4::Multiply(session.scene.GetWorldTransform(entity),
                                      entity.GetComponent<CharacterComponent>().worldPose[static_cast<size_t>(bone)]);
    return Vec3(world.m[12], world.m[13], world.m[14]);
}

struct CrateMeasure {
    float displacement = 0.0f;
    float speed = 0.0f;
    float finalY = 0.0f;
    int collisionEvents = 0;
};

CrateMeasure PushCrate(float mass, float pushStrength)
{
    Session session(mass < 50.0f ? "LightCrate" : "HeavyCrate");
    AddFloor(session);
    Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
    character.GetComponent<CharacterControllerComponent>().pushStrength = pushStrength;
    Entity crate = session.scene.CreateEntity("Crate");
    crate.GetComponent<TransformComponent>().position = Vec3(1.4f, 0.5f, 0.0f);
    auto& body = crate.AddComponent<RigidBodyComponent>();
    body.mass = mass;
    body.layer = Physics::kLayerDefault;
    crate.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
    const entt::entity characterId = static_cast<entt::entity>(character);
    const entt::entity crateId = static_cast<entt::entity>(crate);
    session.Step(20);
    const float startX = crate.GetComponent<TransformComponent>().position.x;
    session.controllers.SetMoveInput(characterId, 1.0f, 0.0f);
    int collisions = 0;
    for (int frame = 0; frame < 120; ++frame) {
        session.Step(1);
        for (const Physics::PhysicsContactEvent& event : session.system.FrameContactEvents()) {
            const bool pair = (event.receiver == static_cast<std::uint32_t>(characterId) && event.other == static_cast<std::uint32_t>(crateId))
                || (event.receiver == static_cast<std::uint32_t>(crateId) && event.other == static_cast<std::uint32_t>(characterId));
            if (pair && (event.kind == Physics::PhysicsEventKind::CollisionEnter
                         || event.kind == Physics::PhysicsEventKind::CollisionStay)) {
                ++collisions;
            }
        }
    }
    Physics::BodyObservation observation;
    const float speed = session.system.ObserveBody(crateId, observation) == Physics::BodyObservationStatus::Ready
        ? std::sqrt(observation.linearVelocity.x * observation.linearVelocity.x
                    + observation.linearVelocity.z * observation.linearVelocity.z)
        : 0.0f;
    CrateMeasure measure;
    measure.displacement = crate.GetComponent<TransformComponent>().position.x - startX;
    measure.speed = speed;
    measure.finalY = crate.GetComponent<TransformComponent>().position.y;
    measure.collisionEvents = collisions;
    return measure;
}

} // namespace

int main()
{
    {
        // Construction stairs (generated box colliders) are climbable with the default controller settings.
        Session session("ConstructionStairs");
        AddFloor(session);
        Entity stairs = session.scene.CreateEntity("Stairs");
        auto& shape = stairs.AddComponent<ConstructionShapeComponent>();
        shape.recipe = Hephaestus::Construction::Recipe::Stairs;
        shape.box.width = 1.6f;
        shape.box.depth = 4.0f;
        shape.box.height = 2.0f;
        shape.stairs.maxRise = CharacterControllerComponent{}.stepHeight * 0.95f;
        Entity landing = session.scene.CreateEntity("Landing");
        landing.GetComponent<TransformComponent>().position = Vec3(0.0f, 0.0f, 4.0f);
        auto& landingShape = landing.AddComponent<ConstructionShapeComponent>();
        landingShape.box.width = 4.0f;
        landingShape.box.depth = 4.0f;
        landingShape.box.height = 2.0f;
        Hephaestus::Construction::ConstructionSystem construction;
        construction.Update(session.scene);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, -4.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(10);
        Physics::RaycastHit stepHit;
        const bool tread = session.system.Raycast(entt::null, Vec3(0.0f, 5.0f, -1.25f), Vec3(0.0f, -1.0f, 0.0f), 10.0f, stepHit);
        Check(tread && std::fabs(stepHit.point.y - 0.5f) < 0.01f,
              "Construction stairs collide at their treads (hit y " + std::to_string(stepHit.point.y) + ")");
        session.controllers.SetMoveInput(id, 0.0f, 1.0f);
        float topY = -100.0f;
        float furthest = -100.0f;
        for (int frame = 0; frame < 240; ++frame) {
            session.Step(1);
            const auto state = StateOf(session, id);
            topY = std::max(topY, state.position.y);
            furthest = std::max(furthest, state.position.z);
            if (state.position.z > 3.5f) {
                session.controllers.ClearMoveInput(id);
            }
        }
        const Vec3 feet(0.0f, topY, furthest);
        Check(feet.y > 1.9f && feet.z > 2.5f,
              "A character walks up construction stairs onto the landing (feet at " + std::to_string(feet.y) + ", z " +
                  std::to_string(feet.z) + ")");
    }
    {
        // A triangular construction room: its sloped wall and floor are convex hull colliders.
        Session session("ConstructionHulls");
        Entity room = session.scene.CreateEntity("Triangle Room");
        auto& shape = room.AddComponent<ConstructionShapeComponent>();
        shape.recipe = Hephaestus::Construction::Recipe::Wall;
        shape.wall.points = {{0.0f, 0.0f}, {6.0f, 0.0f}, {0.0f, 6.0f}};
        shape.wall.closed = true;
        shape.box.height = 3.0f;
        shape.box.wallThickness = 0.2f;
        Hephaestus::Construction::ConstructionSystem construction;
        construction.Update(session.scene);
        session.Step(2);
        Physics::RaycastHit floorHit;
        const bool floor = session.system.Raycast(entt::null, Vec3(1.5f, 5.0f, 1.5f), Vec3(0.0f, -1.0f, 0.0f), 10.0f, floorHit);
        Check(floor && std::fabs(floorHit.point.y - 0.2f) < 0.02f,
              "A triangular room's floor hull collides at its top (hit y " + std::to_string(floorHit.point.y) + ")");
        Physics::RaycastHit wallHit;
        const Vec3 towardWall = vnorm(Vec3(1.0f, 0.0f, 1.0f));
        const bool wall = session.system.Raycast(entt::null, Vec3(1.0f, 1.5f, 1.0f), towardWall, 10.0f, wallHit);
        const float expected = 2.0f * std::sqrt(2.0f) - 0.1f;
        Check(wall && std::fabs(wallHit.distance - expected) < 0.03f,
              "The sloped wall hull stops a ray at its inner face (distance " + std::to_string(wallHit.distance) + ")");
    }
    {
        Session session("Lifecycle");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(1);
        Check(session.controllers.HasRuntime(id) && session.controllers.ActiveCount() == 1, "Creating a controller builds a runtime");
        Check(ProxyActive(session, id), "Inner proxy stays awake");
        character.RemoveComponent<CharacterControllerComponent>();
        session.Step(1);
        Check(!session.controllers.HasRuntime(id) && session.controllers.ActiveCount() == 0, "Removing the controller destroys the runtime");
        character.AddComponent<CharacterControllerComponent>();
        session.Step(1);
        Check(session.controllers.HasRuntime(id), "Creating the controller again builds a new runtime");
        for (int i = 0; i < 8; ++i) {
            character.RemoveComponent<CharacterControllerComponent>();
            session.Step(1);
            character.AddComponent<CharacterControllerComponent>();
            session.Step(1);
        }
        Check(session.controllers.HasRuntime(id) && session.controllers.ActiveCount() == 1, "Repeated create and destroy stays at one runtime");
    }

    {
        Session session("RenderInterpolation");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        auto& transform = character.GetComponent<TransformComponent>();
        session.Step(30);
        session.controllers.SetMoveInput(id, 1.0f, 0.0f);
        session.Step(30);
        transform.rotation.y = 1.0f;
        const float previousX = transform.position.x;
        session.system.Update(session.scene, 0.5f / 60.0f);
        session.system.Update(session.scene, 1.0f / 60.0f);
        const float currentX = transform.position.x;
        session.system.ApplyRenderInterpolation(session.scene);
        const float renderedX = transform.position.x;
        Check(std::abs(renderedX - (previousX + (currentX - previousX) * 0.5f)) < 1.0e-4f,
              "A moving character renders halfway through its last fixed step");
        Check(transform.rotation.y == 1.0f, "Character facing is not interpolated");
        session.system.RestoreSimulationPoses(session.scene);
        Check(transform.position.x == currentX, "Restoring puts back the character's stepped position");
    }

    {
        Session session("Fall");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 3.0f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(180);
        const auto state = StateOf(session, id);
        Check(state.grounded && state.position.y < 0.2f && state.position.y > -0.05f,
              "Character falls and lands on the floor (y=" + std::to_string(state.position.y) + ")");
    }

    {
        Session session("Move");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(30);
        const float restY = StateOf(session, id).position.y;
        session.controllers.SetMoveInput(id, 1.0f, 0.0f);
        session.Step(45);
        const auto moving = StateOf(session, id);
        Check(moving.planarSpeed > 4.5f && moving.planarSpeed < 5.3f && moving.position.x > 1.0f,
              "Movement accelerates to about walk speed (speed=" + std::to_string(moving.planarSpeed) + ")");
        session.controllers.ClearMoveInput(id);
        session.Step(20);
        const auto stopped = StateOf(session, id);
        Check(stopped.planarSpeed < 0.15f, "Clearing input decelerates on the ground");
        Check(std::fabs(stopped.position.y - restY) < 0.05f, "Walking on flat ground keeps the feet planted");
    }

    {
        Session session("Air");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(30);
        session.controllers.Jump(id);
        session.Step(2);
        const float before = StateOf(session, id).planarSpeed;
        session.controllers.SetMoveInput(id, 0.0f, 1.0f);
        session.Step(10);
        const auto airborne = StateOf(session, id);
        Check(!airborne.grounded && airborne.planarSpeed > before + 0.2f && airborne.planarSpeed < 2.0f,
              "Air acceleration is weaker than ground acceleration");
        const float coast = airborne.planarSpeed;
        session.controllers.ClearMoveInput(id);
        session.Step(8);
        const auto gliding = StateOf(session, id);
        Check(!gliding.grounded && gliding.planarSpeed > coast - 0.05f, "Airborne controller does not brake without input");
    }

    {
        Session session("Jump");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(30);
        session.controllers.Jump(id);
        session.Step(1);
        const auto jumped = StateOf(session, id);
        Check(jumped.verticalSpeed > 4.5f && jumped.verticalSpeed < 5.7f, "Jump leaves the ground with jump speed");
        session.Step(20);
        Check(!StateOf(session, id).grounded, "Jump stays airborne");
        session.Step(80);
        Check(StateOf(session, id).grounded, "Jump lands again");
    }

    {
        Session session("StairsUp");
        AddStatic(session, "Landing", Vec3(-1.0f, -0.5f, 0.0f), Vec3(2.0f, 0.5f, 2.0f));
        for (int i = 0; i < 6; ++i) {
            const float top = (i + 1) * 0.2f;
            const float centerX = 1.2f + i * 0.4f;
            const float halfY = 0.5f * (top + 1.0f);
            const float centerY = 0.5f * (top - 1.0f);
            AddStatic(session, "Step", Vec3(centerX, centerY, 0.0f), Vec3(0.2f, halfY, 1.0f));
        }
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        session.controllers.SetMoveInput(id, 1.0f, 0.0f);
        int air = 0;
        int samples = 0;
        float topY = -100.0f;
        for (int frame = 0; frame < 180; ++frame) {
            session.Step(1);
            const auto state = StateOf(session, id);
            if (state.position.x > 1.5f && state.position.x < 3.3f) {
                ++samples;
                topY = std::max(topY, state.position.y);
                if (state.groundState == Physics::CharacterGroundState::InAir) {
                    ++air;
                }
            }
            if (state.position.x > 3.1f) {
                session.controllers.ClearMoveInput(id);
            }
        }
        Check(topY > 0.9f, "Stairs up raise the character (y=" + std::to_string(topY) + ")");
        Check(samples > 10 && air * 5 < samples,
              "Stairs up do not flicker into InAir (air=" + std::to_string(air) + "/" + std::to_string(samples) + ")");
    }

    {
        Session session("StairsDown");
        AddStatic(session, "Landing", Vec3(-1.0f, -0.5f, 0.0f), Vec3(2.0f, 0.5f, 2.0f));
        for (int i = 0; i < 6; ++i) {
            const float top = (i + 1) * 0.2f;
            const float centerX = 1.2f + i * 0.4f;
            const float halfY = 0.5f * (top + 1.0f);
            const float centerY = 0.5f * (top - 1.0f);
            AddStatic(session, "Step", Vec3(centerX, centerY, 0.0f), Vec3(0.2f, halfY, 1.0f));
        }
        Entity character = AddCharacter(session, Vec3(3.2f, 1.25f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(30);
        Check(StateOf(session, id).position.y > 0.9f, "Stairs down start on the upper steps");
        session.controllers.SetMoveInput(id, -1.0f, 0.0f);
        int air = 0;
        int samples = 0;
        float bottomY = 100.0f;
        bool reachedBottom = false;
        for (int frame = 0; frame < 180; ++frame) {
            session.Step(1);
            const auto state = StateOf(session, id);
            if (state.position.x < 3.2f && state.position.x > 1.4f) {
                ++samples;
                if (state.groundState == Physics::CharacterGroundState::InAir) {
                    ++air;
                }
            }
            if (state.position.x < 0.8f && state.position.x > -2.0f) {
                bottomY = state.position.y;
                reachedBottom = true;
                session.controllers.ClearMoveInput(id);
                break;
            }
        }
        Check(reachedBottom && bottomY < 0.25f && bottomY > -0.1f,
              "Stairs down lower the character onto the landing (y=" + std::to_string(bottomY) + ")");
        Check(samples > 10 && air * 5 < samples,
              "Stairs down do not flicker into InAir (air=" + std::to_string(air) + "/" + std::to_string(samples) + ")");
    }

    {
        Session session("WalkableSlope");
        Entity ramp = AddStatic(session, "Ramp", Vec3(0.0f, 0.0f, 0.0f), Vec3(8.0f, 0.4f, 3.0f));
        ramp.GetComponent<TransformComponent>().rotation.z = 25.0f * 3.14159265f / 180.0f;
        Entity character = AddCharacter(session, Vec3(-2.5f, 1.5f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(60);
        const float startY = StateOf(session, id).position.y;
        Check(StateOf(session, id).grounded, "The walkable ramp supports the character before the climb");
        session.controllers.SetMoveInput(id, 1.0f, 0.0f);
        session.Step(90);
        const auto end = StateOf(session, id);
        Check(end.grounded && end.position.y > startY + 0.4f,
              "A slope under the limit is walkable (dy=" + std::to_string(end.position.y - startY) + ")");
    }

    {
        Session session("SteepSlope");
        AddStatic(session, "Floor", Vec3(0.0f, -0.5f, 0.0f), Vec3(6.0f, 0.5f, 3.0f));
        Entity ramp = AddStatic(session, "Steep", Vec3(1.6f, 1.2f, 0.0f), Vec3(2.4f, 0.15f, 2.0f));
        ramp.GetComponent<TransformComponent>().rotation.z = 65.0f * 3.14159265f / 180.0f;
        Entity character = AddCharacter(session, Vec3(-1.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        const float startY = StateOf(session, id).position.y;
        session.controllers.SetMoveInput(id, 1.0f, 0.0f);
        session.Step(90);
        const auto end = StateOf(session, id);
        Check(end.position.y < startY + 0.25f && end.position.y > startY - 0.15f && end.position.x < 1.4f,
              "A slope over the limit is not climbed (dy=" + std::to_string(end.position.y - startY)
                  + " x=" + std::to_string(end.position.x) + ")");
    }

    {
        Session session("Ledge");
        AddStatic(session, "High", Vec3(0.0f, -0.5f, 0.0f), Vec3(2.0f, 0.5f, 2.0f));
        AddStatic(session, "Low", Vec3(6.0f, -1.5f, 0.0f), Vec3(2.0f, 0.5f, 2.0f));
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        session.controllers.SetMoveInput(id, 1.0f, 0.0f);
        bool sawAir = false;
        float lowest = 10.0f;
        for (int frame = 0; frame < 90; ++frame) {
            session.Step(1);
            const auto state = StateOf(session, id);
            lowest = std::min(lowest, state.position.y);
            if (state.position.x > 2.2f && state.groundState == Physics::CharacterGroundState::InAir) {
                sawAir = true;
            }
        }
        Check(sawAir && lowest < -0.3f, "A ledge deeper than snap distance drops the character into the air");
    }

    {
        Session session("HorizontalPlatform");
        Entity platform = AddStatic(session, "Platform", Vec3(0.0f, -0.5f, 0.0f), Vec3(3.0f, 0.5f, 3.0f));
        platform.GetComponent<RigidBodyComponent>().SetMotion(RigidBodyMotion::Kinematic);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        const float startX = StateOf(session, id).position.x;
        for (int frame = 0; frame < 60; ++frame) {
            platform.GetComponent<TransformComponent>().position.x += 2.0f / 60.0f;
            session.Step(1);
        }
        const auto end = StateOf(session, id);
        Check(end.grounded && std::fabs((end.position.x - startX) - 2.0f) < 0.15f,
              "A stationary character tracks a horizontal platform (dx=" + std::to_string(end.position.x - startX) + ")");
    }

    {
        Session session("WalkPlatform");
        Entity platform = AddStatic(session, "Platform", Vec3(0.0f, -0.5f, 0.0f), Vec3(4.0f, 0.5f, 4.0f));
        platform.GetComponent<RigidBodyComponent>().SetMotion(RigidBodyMotion::Kinematic);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        const auto start = StateOf(session, id);
        session.controllers.SetMoveInput(id, 0.0f, 1.0f);
        for (int frame = 0; frame < 60; ++frame) {
            platform.GetComponent<TransformComponent>().position.x += 1.5f / 60.0f;
            session.Step(1);
        }
        const auto end = StateOf(session, id);
        Check(std::fabs((end.position.x - start.position.x) - 1.5f) < 0.2f && end.position.z > start.position.z + 1.0f,
              "Walking on a platform stays relative to the platform");
    }

    {
        Session session("VerticalPlatform");
        Entity platform = AddStatic(session, "Lift", Vec3(0.0f, -0.5f, 0.0f), Vec3(2.0f, 0.5f, 2.0f));
        platform.GetComponent<RigidBodyComponent>().SetMotion(RigidBodyMotion::Kinematic);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        const float startY = StateOf(session, id).position.y;
        int grounded = 0;
        for (int frame = 0; frame < 60; ++frame) {
            platform.GetComponent<TransformComponent>().position.y += 1.0f / 60.0f;
            session.Step(1);
            if (StateOf(session, id).grounded) {
                ++grounded;
            }
        }
        const float endY = StateOf(session, id).position.y;
        Check(std::fabs((endY - startY) - 1.0f) < 0.15f && grounded > 50,
              "A vertical platform keeps the character supported");
    }

    {
        Session session("JumpPlatform");
        Entity platform = AddStatic(session, "Mover", Vec3(0.0f, -0.5f, 0.0f), Vec3(3.0f, 0.5f, 3.0f));
        platform.GetComponent<RigidBodyComponent>().SetMotion(RigidBodyMotion::Kinematic);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        for (int frame = 0; frame < 10; ++frame) {
            platform.GetComponent<TransformComponent>().position.x += 3.0f / 60.0f;
            platform.GetComponent<TransformComponent>().position.y += 1.0f / 60.0f;
            session.Step(1);
        }
        session.controllers.Jump(id);
        platform.GetComponent<TransformComponent>().position.x += 3.0f / 60.0f;
        platform.GetComponent<TransformComponent>().position.y += 1.0f / 60.0f;
        session.Step(1);
        const auto jumped = StateOf(session, id);
        Check(jumped.velocity.x > 2.2f && jumped.velocity.x < 3.8f,
              "Jump keeps horizontal platform velocity (vx=" + std::to_string(jumped.velocity.x) + ")");
        Check(jumped.velocity.y > 4.0f,
              "Jump adds vertical speed on top of the platform (vy=" + std::to_string(jumped.velocity.y) + ")");
    }

    {
        Session session("RotatePlatform");
        Entity platform = AddStatic(session, "Spinner", Vec3(0.0f, -0.5f, 0.0f), Vec3(3.0f, 0.5f, 3.0f));
        platform.GetComponent<RigidBodyComponent>().SetMotion(RigidBodyMotion::Kinematic);
        Entity character = AddCharacter(session, Vec3(1.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        const auto start = StateOf(session, id);
        for (int frame = 0; frame < 90; ++frame) {
            platform.GetComponent<TransformComponent>().rotation.y += 0.02f;
            session.Step(1);
        }
        const auto end = StateOf(session, id);
        const float moved = std::sqrt((end.position.x - start.position.x) * (end.position.x - start.position.x)
                                       + (end.position.z - start.position.z) * (end.position.z - start.position.z));
        Check(moved > 0.2f, "A rotating platform moves the character by its tangential velocity (d=" + std::to_string(moved) + ")");
        Check(std::fabs(character.GetComponent<TransformComponent>().rotation.y) < 1.0e-4f,
              "A rotating platform does not inherit yaw onto the entity");
    }

    {
        Session session("Crouch");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        const float feet = StateOf(session, id).position.y;
        session.controllers.SetCrouched(id, true);
        session.Step(5);
        const auto crouched = StateOf(session, id);
        Check(crouched.crouched && std::fabs(crouched.position.y - feet) < 0.02f, "Crouch keeps the feet planted");
        AddStatic(session, "Ceiling", Vec3(1.5f, 1.55f, 0.0f), Vec3(1.0f, 0.25f, 1.0f));
        session.controllers.SetMoveInput(id, 1.0f, 0.0f);
        for (int frame = 0; frame < 90 && StateOf(session, id).position.x < 1.5f; ++frame) {
            session.Step(1);
        }
        session.controllers.SetCrouched(id, false);
        session.Step(5);
        const auto blocked = StateOf(session, id);
        Check(blocked.crouched && blocked.standBlocked, "Standing in a low tunnel is rejected");
        session.controllers.SetMoveInput(id, -1.0f, 0.0f);
        bool stood = false;
        for (int frame = 0; frame < 120; ++frame) {
            session.Step(1);
            const auto state = StateOf(session, id);
            if (state.position.x < 0.2f && !state.crouched && !state.standBlocked) {
                stood = true;
                break;
            }
        }
        Check(stood, "Leaving the tunnel stands automatically");
    }

    {
        Session session("Ceiling");
        AddFloor(session);
        AddStatic(session, "Lid", Vec3(0.0f, 2.3f, 0.0f), Vec3(2.0f, 0.2f, 2.0f));
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        session.controllers.Jump(id);
        float peak = 0.0f;
        bool hit = false;
        for (int frame = 0; frame < 40; ++frame) {
            session.Step(1);
            const auto state = StateOf(session, id);
            peak = std::max(peak, state.position.y);
            if (state.verticalSpeed <= 0.05f && state.position.y > 0.15f && !state.grounded) {
                hit = true;
            }
        }
        Check(hit && peak < 0.8f, "A ceiling stops the jump (peak=" + std::to_string(peak) + ")");
    }

    {
        const CrateMeasure proxyOnly = PushCrate(10.0f, 0.0f);
        const CrateMeasure both = PushCrate(10.0f, 1000.0f);
        const CrateMeasure heavy = PushCrate(500.0f, 1000.0f);
        std::cout << "[MEASURE] light pushStrength 0 displacement=" << proxyOnly.displacement
                  << " speed=" << proxyOnly.speed << " collisions=" << proxyOnly.collisionEvents << "\n";
        std::cout << "[MEASURE] light pushStrength 1000 displacement=" << both.displacement
                  << " speed=" << both.speed << " collisions=" << both.collisionEvents << "\n";
        std::cout << "[MEASURE] heavy displacement=" << heavy.displacement
                  << " speed=" << heavy.speed << " y=" << heavy.finalY
                  << " collisions=" << heavy.collisionEvents << "\n";
        Check(both.displacement > 0.2f && both.speed < 6.0f && both.finalY < 1.2f,
              "A 10 kg crate is pushed without being launched");
        Check(heavy.speed < 4.0f && heavy.finalY < 1.0f && heavy.finalY > 0.3f
                  && heavy.displacement < both.displacement * 0.6f,
              "A 500 kg crate stays on the floor and is not launched");
    }

    {
        Physics::PhysicsLayerConfig layers = Physics::PhysicsLayerConfig::Seed();
        layers.SetCollides(Physics::kLayerCharacter, Physics::kLayerWorld, false);
        Session session("Layers", layers);
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 1.0f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(60);
        Check(StateOf(session, id).position.y < -0.5f, "Turning Character versus World off lets the character fall through");
    }

    {
        Session session("Trigger");
        AddFloor(session);
        Entity trigger = AddStatic(session, "Trigger", Vec3(2.0f, 1.0f, 0.0f), Vec3(0.6f, 1.0f, 0.6f), Physics::kLayerTrigger);
        trigger.GetComponent<RigidBodyComponent>().isTrigger = true;
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        const auto triggerId = static_cast<std::uint32_t>(static_cast<entt::entity>(trigger));
        const auto characterKey = static_cast<std::uint32_t>(id);
        session.Step(15);
        session.controllers.SetMoveInput(id, 1.0f, 0.0f);
        bool enterCharacter = false;
        bool enterTrigger = false;
        bool stayCharacter = false;
        bool stayTrigger = false;
        bool exitCharacter = false;
        bool exitTrigger = false;
        for (int frame = 0; frame < 150; ++frame) {
            session.Step(1);
            for (const Physics::PhysicsContactEvent& event : session.system.FrameContactEvents()) {
                const bool characterSide = event.receiver == characterKey && event.other == triggerId;
                const bool triggerSide = event.receiver == triggerId && event.other == characterKey;
                if (event.kind == Physics::PhysicsEventKind::TriggerEnter) {
                    enterCharacter = enterCharacter || characterSide;
                    enterTrigger = enterTrigger || triggerSide;
                } else if (event.kind == Physics::PhysicsEventKind::TriggerStay) {
                    stayCharacter = stayCharacter || characterSide;
                    stayTrigger = stayTrigger || triggerSide;
                } else if (event.kind == Physics::PhysicsEventKind::TriggerExit) {
                    exitCharacter = exitCharacter || characterSide;
                    exitTrigger = exitTrigger || triggerSide;
                }
            }
        }
        Check(enterCharacter && enterTrigger && stayCharacter && stayTrigger && exitCharacter && exitTrigger,
              "Walking through a trigger reports enter, stay, and exit on both entities");
        Check(ProxyActive(session, id), "The proxy is still awake after the trigger");
    }

    {
        Session session("SensorFloor");
        Entity floor = AddStatic(session, "SensorFloor", Vec3(0.0f, -0.5f, 0.0f), Vec3(4.0f, 0.5f, 4.0f));
        floor.GetComponent<RigidBodyComponent>().isTrigger = true;
        Entity character = AddCharacter(session, Vec3(0.0f, 1.0f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(40);
        const auto state = StateOf(session, id);
        Check(!state.grounded && state.position.y < 0.0f, "A sensor does not support the character");
    }

    {
        Session session("TwoCharacters");
        AddFloor(session);
        Entity left = AddCharacter(session, Vec3(-0.2f, 0.05f, 0.0f), "Left");
        Entity right = AddCharacter(session, Vec3(0.8f, 0.05f, 0.0f), "Right");
        const entt::entity leftId = static_cast<entt::entity>(left);
        const entt::entity rightId = static_cast<entt::entity>(right);
        session.Step(15);
        session.controllers.SetMoveInput(leftId, 1.0f, 0.0f);
        session.controllers.SetMoveInput(rightId, -1.0f, 0.0f);
        session.Step(90);
        const auto a = StateOf(session, leftId);
        const auto b = StateOf(session, rightId);
        const float gap = std::fabs(a.position.x - b.position.x);
        Check(gap > 0.55f, "Two controllers do not occupy the same volume (gap=" + std::to_string(gap) + ")");
    }

    {
        auto simulate = []() {
            Session session("Determinism");
            AddFloor(session);
            Entity character = AddCharacter(session, Vec3(0.0f, 1.0f, 0.0f));
            const entt::entity id = static_cast<entt::entity>(character);
            session.Step(20);
            session.controllers.SetMoveInput(id, 0.6f, 0.8f);
            session.controllers.Jump(id);
            session.Step(90);
            return StateOf(session, id).position;
        };
        const Vec3 first = simulate();
        const Vec3 second = simulate();
        const float error = std::sqrt((first.x - second.x) * (first.x - second.x)
                                       + (first.y - second.y) * (first.y - second.y)
                                       + (first.z - second.z) * (first.z - second.z));
        Check(error < 0.001f, "The same build repeats within 1 mm (error=" + std::to_string(error) + ")");
    }

    {
        Session session("RigidBodyToController");
        AddFloor(session);
        Entity entity = session.scene.CreateEntity("Body");
        entity.GetComponent<TransformComponent>().position = Vec3(0.0f, 1.0f, 0.0f);
        auto& body = entity.AddComponent<RigidBodyComponent>();
        body.mass = 2.0f;
        entity.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.3f, 0.3f, 0.3f)));
        const entt::entity id = static_cast<entt::entity>(entity);
        session.Step(1);
        Physics::BodyObservation observation;
        Check(session.system.ObserveBody(id, observation) == Physics::BodyObservationStatus::Ready,
              "A rigid body exists before the controller");
        entity.AddComponent<CharacterControllerComponent>();
        session.Step(1);
        Check(session.controllers.HasRuntime(id)
                  && session.system.ObserveBody(id, observation) == Physics::BodyObservationStatus::Missing,
              "Adding a controller removes the normal rigid body");
        entity.RemoveComponent<CharacterControllerComponent>();
        session.Step(1);
        Check(!session.controllers.HasRuntime(id)
                  && session.system.ObserveBody(id, observation) == Physics::BodyObservationStatus::Ready,
              "Removing the controller lets the rigid body return");
    }

    {
        Session session("DisabledController");
        AddFloor(session);
        Entity entity = session.scene.CreateEntity("Disabled");
        entity.GetComponent<TransformComponent>().position = Vec3(0.0f, 1.0f, 0.0f);
        entity.AddComponent<RigidBodyComponent>().mass = 2.0f;
        entity.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.3f, 0.3f, 0.3f)));
        auto& controller = entity.AddComponent<CharacterControllerComponent>();
        controller.enabled = false;
        const entt::entity id = static_cast<entt::entity>(entity);
        session.Step(2);
        Physics::BodyObservation observation;
        Check(!session.controllers.HasRuntime(id)
                  && session.system.ObserveBody(id, observation) == Physics::BodyObservationStatus::Missing,
              "A disabled controller suppresses both the runtime and the rigid body");
    }

    {
        Session session("JumpOverride");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        character.GetComponent<CharacterControllerComponent>().jumpSpeed = 5.5f;
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(25);
        session.controllers.JumpWithSpeed(id, 3.0f);
        session.Step(1);
        const float overridden = StateOf(session, id).verticalSpeed;
        Check(overridden > 2.4f && overridden < 3.2f,
              "Physics.jump speed is used for that request (vy=" + std::to_string(overridden) + ")");
        session.controllers.JumpWithSpeed(id, 12.0f);
        session.Step(1);
        session.Step(90);
        Check(StateOf(session, id).grounded, "The character lands after the override jump");
        session.controllers.Jump(id);
        session.Step(1);
        const float normal = StateOf(session, id).verticalSpeed;
        Check(normal > 4.8f && normal < 5.7f,
              "A later Character.jump uses the component speed (vy=" + std::to_string(normal) + ")");
    }

    {
        Session session("SpeedMultiplier");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        CharacterControllerComponent& component = character.GetComponent<CharacterControllerComponent>();
        const float authoredSpeed = component.walkSpeed;
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(30);
        Check(std::fabs(session.controllers.GetSpeedMultiplier(id) - 1.0f) < 1.0e-4f,
              "A new runtime speed multiplier is 1");
        session.controllers.SetSpeedMultiplier(id, 1.6f);
        session.controllers.SetMoveInput(id, 1.0f, 0.0f);
        Check(std::fabs(session.controllers.GetSpeedMultiplier(id) - 1.6f) < 1.0e-4f,
              "SetMoveInput does not clear the speed multiplier");
        Check(std::fabs(component.walkSpeed - authoredSpeed) < 1.0e-4f,
              "The speed multiplier does not change authored walkSpeed");
        session.Step(45);
        const auto moving = StateOf(session, id);
        Check(moving.planarSpeed > authoredSpeed * 1.45f && moving.planarSpeed < authoredSpeed * 1.75f,
              "A 1.6 multiplier raises target speed (speed=" + std::to_string(moving.planarSpeed) + ")");
        Check(std::fabs(component.walkSpeed - authoredSpeed) < 1.0e-4f,
              "Stepping leaves authored walkSpeed unchanged");
        const auto crouched = StateOf(session, id);
        Check(!crouched.crouched && !crouched.standBlocked, "Standing controller reports neither crouch flag");
        component.enabled = false;
        session.Step(1);
        Check(!session.controllers.HasRuntime(id), "Disabling the controller destroys the runtime");
        Check(std::fabs(session.controllers.GetSpeedMultiplier(id) - 1.0f) < 1.0e-4f,
              "A destroyed runtime reports multiplier 1");
        component.enabled = true;
        session.Step(1);
        Check(session.controllers.HasRuntime(id)
                  && std::fabs(session.controllers.GetSpeedMultiplier(id) - 1.0f) < 1.0e-4f,
              "Recreating the runtime resets the speed multiplier to 1");
    }

    {
        Session session("IgnoreProxyRay");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        Entity target = AddStatic(session, "Target", Vec3(4.0f, 1.0f, 0.0f), Vec3(0.5f, 1.0f, 0.5f));
        const entt::entity characterId = static_cast<entt::entity>(character);
        const entt::entity targetId = static_cast<entt::entity>(target);
        session.Step(20);
        const Vec3 origin(0.0f, 0.9f, 0.0f);
        const Vec3 direction(1.0f, 0.0f, 0.0f);
        const std::uint16_t characterLayer = Physics::kLayerCharacter;
        Physics::RaycastHit selfHit;
        Check(session.system.Raycast(entt::null, origin, direction, 20.0f, selfHit, &characterLayer)
                  && selfHit.hit && selfHit.entity == characterId,
              "A ray that starts inside the player proxy hits the proxy first");
        Physics::RaycastHit ignored;
        Check(session.system.Raycast(characterId, origin, direction, 20.0f, ignored, &characterLayer)
                  && ignored.hit && ignored.entity == targetId,
              "Ignoring the player hits the target instead of the proxy");
        Check(ignored.entity != characterId, "The ignored player proxy is not the hit");
    }

    {
        Session session("IgnoreSensorsRay");
        AddFloor(session);
        Entity trigger = AddStatic(session, "Trigger", Vec3(2.0f, 1.0f, 0.0f), Vec3(0.4f, 1.0f, 0.4f),
                                   Physics::kLayerTrigger);
        trigger.GetComponent<RigidBodyComponent>().isTrigger = true;
        Entity target = AddStatic(session, "Target", Vec3(4.0f, 1.0f, 0.0f), Vec3(0.5f, 1.0f, 0.5f));
        session.Step(2);
        const Vec3 origin(0.0f, 1.0f, 0.0f);
        const Vec3 direction(1.0f, 0.0f, 0.0f);
        const std::uint16_t characterLayer = Physics::kLayerCharacter;
        Physics::RaycastHit sensorHit;
        Check(session.system.Raycast(entt::null, origin, direction, 20.0f, sensorHit, &characterLayer, false)
                  && sensorHit.hit && sensorHit.entity == static_cast<entt::entity>(trigger),
              "ignoreSensors false lets a trigger be hit");
        Physics::RaycastHit skipped;
        Check(session.system.Raycast(entt::null, origin, direction, 20.0f, skipped, &characterLayer, true)
                  && skipped.hit && skipped.entity == static_cast<entt::entity>(target),
              "ignoreSensors true skips the trigger");
    }

    {
        Session session("OwnHitBoxes");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        // A kinematic hit box parented inside the character's own capsule, as an enemy's hit zones are.
        Entity chest = session.scene.CreateEntity("Chest");
        auto& body = chest.AddComponent<RigidBodyComponent>();
        body.SetMotion(RigidBodyMotion::Kinematic);
        body.mass = 1.0f;
        body.useGravity = false;
        chest.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.2f, 0.3f, 0.2f)));
        session.scene.SetParent(chest, character);
        chest.GetComponent<TransformComponent>().position = Vec3(0.0f, 1.2f, 0.0f);
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        session.controllers.SetMoveInput(id, 1.0f, 0.0f);
        session.Step(60);
        const float x = character.GetComponent<TransformComponent>().position.x;
        Check(x > 3.0f && std::fabs(character.GetComponent<TransformComponent>().position.y) < 0.2f,
              "A character is not pushed or blocked by bodies parented under it (x=" + std::to_string(x) + ")");
    }

    {
        Session session("Ragdoll");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        AddHumanoid(character);
        Entity zone = session.scene.CreateEntity("Zone");
        auto& zoneBody = zone.AddComponent<RigidBodyComponent>();
        zoneBody.SetMotion(RigidBodyMotion::Kinematic);
        zoneBody.mass = 1.0f;
        zoneBody.useGravity = false;
        zone.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.2f, 0.3f, 0.2f)));
        session.scene.SetParent(zone, character);
        zone.GetComponent<TransformComponent>().position = Vec3(0.0f, 1.2f, 0.0f);
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        Physics::RagdollSystem& ragdolls = session.system.Ragdolls();
        std::string error;
        Physics::RagdollStart start;
        start.impulse = Vec3(60.0f, 0.0f, 0.0f);
        start.impulsePoint = BoneWorld(session, character, 4);
        start.hasImpulsePoint = true;
        const bool started = ragdolls.Start(session.scene, id, start, error);
        Check(started && ragdolls.PartBodies(id).size() == 11, "A humanoid Character becomes an 11-part ragdoll (" + error + ")");
        Check(!session.controllers.HasRuntime(id) && !zone.HasComponent<RigidBodyComponent>()
                  && character.GetComponent<CharacterComponent>().externalPose,
              "The ragdoll replaces the character controller and its children's colliders and stops the animation");
        const float headStartX = BoneWorld(session, character, 4).x;
        session.Step(240);
        const Vec3 hips = BoneWorld(session, character, 0);
        const Vec3 head = BoneWorld(session, character, 4);
        float lowest = 1e9f;
        bool finite = true;
        bool settled = true;
        JPH::BodyInterface& bodies = session.engine.GetBodyInterface();
        for (JPH::BodyID part : ragdolls.PartBodies(id)) {
            const JPH::RVec3 position = bodies.GetCenterOfMassPosition(part);
            lowest = std::min(lowest, static_cast<float>(position.GetY()));
            finite = finite && std::isfinite(static_cast<float>(position.GetY()));
            settled = settled && !bodies.IsActive(part);
        }
        Check(finite && settled && hips.y < 0.4f && head.y < 1.0f && lowest > -0.02f,
              "The ragdoll collapses onto the floor and comes to rest without falling through it (hips y=" +
                  std::to_string(hips.y) + ", head y=" + std::to_string(head.y) + ", lowest part=" +
                  std::to_string(lowest) + ")");
        Check(head.x > headStartX + 0.3f, "An impulse at the head pushes the body that way (head x=" + std::to_string(head.x) + ")");
        Physics::RaycastHit hit;
        session.system.Raycast(entt::null, Vec3(hips.x, 3.0f, hips.z), Vec3(0.0f, -1.0f, 0.0f), 5.0f, hit);
        Check(hit.hit && hit.entity == id, "A ray that hits a ragdoll part reports the character");

        // Another character walks straight across the body.
        std::vector<JPH::RVec3> before;
        for (JPH::BodyID part : ragdolls.PartBodies(id)) {
            before.push_back(bodies.GetCenterOfMassPosition(part));
        }
        Entity walker = AddCharacter(session, Vec3(hips.x - 2.5f, 0.05f, hips.z), "Walker");
        const entt::entity walkerId = static_cast<entt::entity>(walker);
        session.Step(10);
        session.controllers.SetMoveInput(walkerId, 1.0f, 0.0f);
        float highest = 0.0f;
        for (int i = 0; i < 90; ++i) {
            session.Step(1);
            highest = std::max(highest, walker.GetComponent<TransformComponent>().position.y);
        }
        const float walkedTo = walker.GetComponent<TransformComponent>().position.x;
        float moved = 0.0f;
        const auto parts = ragdolls.PartBodies(id);
        for (size_t i = 0; i < parts.size(); ++i) {
            moved = std::max(moved, static_cast<float>((bodies.GetCenterOfMassPosition(parts[i]) - before[i]).Length()));
        }
        Check(highest < 0.1f && walkedTo > hips.x + 2.0f && moved < 0.05f,
              "A character walks through a ragdoll without being lifted or pushing it (highest y=" +
                  std::to_string(highest) + ", reached x=" + std::to_string(walkedTo) + ", part moved " +
                  std::to_string(moved) + " m)");
        session.system.Raycast(entt::null, Vec3(hips.x, 3.0f, hips.z), Vec3(0.0f, -1.0f, 0.0f), 5.0f, hit);
        Check(hit.hit && hit.entity == id, "Rays still hit the ragdoll after a character walks through it");
        session.scene.DestroyEntity(character);
        session.Step(1);
        Check(ragdolls.ActiveCount() == 0, "Destroying the character removes its ragdoll");

        Entity robot = AddCharacter(session, Vec3(5.0f, 0.05f, 0.0f), "Robot");
        AddHumanoid(robot, false);
        session.Step(2);
        Check(!ragdolls.Start(session.scene, static_cast<entt::entity>(robot), Physics::RagdollStart{}, error)
                  && session.controllers.HasRuntime(static_cast<entt::entity>(robot)),
              "A skeleton without humanoid bones is refused and keeps its controller (" + error + ")");
    }

    {
        Session session("Prone");
        AddFloor(session);
        // An overhang 1.0 m up: room to lie under, not to crouch or stand.
        AddStatic(session, "Overhang", Vec3(3.0f, 1.25f, 0.0f), Vec3(1.5f, 0.25f, 2.0f));
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(20);
        session.controllers.SetProne(id, true);
        session.Step(2);
        Check(StateOf(session, id).prone && !StateOf(session, id).crouched, "SetProne lies the character down");
        session.controllers.SetMoveInput(id, 1.0f, 0.0f);
        session.Step(42);
        session.controllers.SetMoveInput(id, 0.0f, 0.0f);
        session.Step(10);
        const float x = character.GetComponent<TransformComponent>().position.x;
        Check(x > 2.0f && x < 4.0f, "A prone character fits under a 1 m overhang (x=" + std::to_string(x) + ")");
        session.controllers.SetProne(id, false);
        session.Step(2);
        const auto underneath = StateOf(session, id);
        Check(underneath.prone && underneath.standBlocked, "Getting up under the overhang is blocked");
        session.controllers.SetMoveInput(id, -1.0f, 0.0f);
        session.Step(120);
        const auto clear = StateOf(session, id);
        Check(!clear.prone && !clear.crouched && !clear.standBlocked, "Out from under it, the character stands up");
    }

    for (const float coyote : {0.0f, 0.12f}) {
        Session session("CoyoteTime");
        AddFloor(session);
        AddStatic(session, "Ledge", Vec3(-1.0f, 0.5f, 0.0f), Vec3(1.0f, 0.5f, 2.0f));
        Entity character = AddCharacter(session, Vec3(-1.5f, 1.05f, 0.0f));
        character.GetComponent<CharacterControllerComponent>().coyoteTime = coyote;
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(30);
        session.controllers.SetMoveInput(id, 1.0f, 0.0f);
        int steps = 0;
        while (StateOf(session, id).grounded && steps < 120) {
            session.Step(1);
            ++steps;
        }
        session.Step(3);
        session.controllers.Jump(id);
        session.Step(1);
        const float vy = StateOf(session, id).verticalSpeed;
        if (coyote > 0.0f) {
            Check(vy > 4.5f, "A jump just after walking off a ledge works with coyote time (vy=" + std::to_string(vy) + ")");
        } else {
            Check(vy < 0.0f, "Without coyote time a jump after walking off a ledge is ignored (vy=" +
                                 std::to_string(vy) + ")");
        }
    }

    for (const float buffer : {0.0f, 0.12f}) {
        Session session("JumpBuffer");
        AddFloor(session);
        Entity character = AddCharacter(session, Vec3(0.0f, 0.05f, 0.0f));
        character.GetComponent<CharacterControllerComponent>().jumpBufferTime = buffer;
        const entt::entity id = static_cast<entt::entity>(character);
        session.Step(30);
        session.controllers.Jump(id);
        session.Step(1);
        int steps = 0;
        while (!(StateOf(session, id).verticalSpeed < 0.0f &&
                 character.GetComponent<TransformComponent>().position.y < 0.15f) && steps < 120) {
            session.Step(1);
            ++steps;
        }
        session.controllers.Jump(id);
        float highest = 0.0f;
        for (int i = 0; i < 20; ++i) {
            session.Step(1);
            highest = std::max(highest, StateOf(session, id).verticalSpeed);
        }
        if (buffer > 0.0f) {
            Check(highest > 4.5f, "A jump pressed just before landing happens on landing (vy=" + std::to_string(highest) + ")");
        } else {
            Check(highest < 1.0f, "Without a jump buffer a jump pressed in the air is dropped (vy=" +
                                      std::to_string(highest) + ")");
        }
    }

    if (!g_Pass) {
        return 1;
    }
    std::cout << "CharacterControllerJoltTests passed\n";
    return 0;
}
