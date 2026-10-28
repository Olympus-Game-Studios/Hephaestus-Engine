#include "Core/EngineLog.h"
#include "Physics/CharacterMotor.h"
#include "Physics/JoltPhysicsEngine.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Physics/PhysicsLayerConfig.h"
#include "Physics/PhysicsStepClock.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <cmath>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    std::cout << "[PASS] " << message << "\n";
    return true;
}

} // namespace

int main()
{
    bool pass = true;
    Physics::JoltPhysicsEngine engine;
    engine.Initialize();
    JPH::Factory* factory = JPH::Factory::sInstance;
    pass &= Check(factory != nullptr, "global Jolt factory exists after Initialize");

    Physics::PhysicsLayerConfig config = Physics::PhysicsLayerConfig::Seed();
    engine.BeginPlayWorld(config);
    pass &= Check(JPH::Factory::sInstance == factory, "BeginPlayWorld keeps the global factory");
    pass &= Check(engine.EncodedLayersShouldCollide(0, false, 1, true),
                  "config A: Default dynamic collides with World static");
    pass &= Check(!engine.EncodedLayersShouldCollide(0, true, 1, true),
                  "static-vs-static stays off while the gameplay pair is on");

    engine.EndPlayWorld();
    pass &= Check(engine.GetPhysicsSystem() == nullptr, "EndPlayWorld destroys the PhysicsSystem");
    pass &= Check(JPH::Factory::sInstance == factory, "EndPlayWorld does not unregister Jolt");

    config.SetCollides(Physics::kLayerDefault, Physics::kLayerWorld, false);
    engine.BeginPlayWorld(config);
    pass &= Check(!engine.EncodedLayersShouldCollide(0, false, 1, true),
                  "config B: Default dynamic does not collide with World static");
    pass &= Check(JPH::Factory::sInstance == factory, "second Play keeps the same factory");

    engine.Shutdown();
    pass &= Check(JPH::Factory::sInstance == nullptr, "last engine shutdown releases the factory");

    auto simulateDrop = [](bool defaultHitsWorld) {
        Physics::JoltPhysicsEngine world;
        world.Initialize();
        Physics::PhysicsLayerConfig config = Physics::PhysicsLayerConfig::Seed();
        config.SetCollides(Physics::kLayerDefault, Physics::kLayerWorld, defaultHitsWorld);
        world.BeginPlayWorld(config);
        bool ready = false;
        float y = 0.0f;
        {
            Scene scene(defaultHitsWorld ? "FilterOn" : "FilterOff");
            Physics::JoltPhysicsSystem system(world);
            Entity floor = scene.CreateEntity("Floor");
            floor.GetComponent<TransformComponent>().position = Vec3(0.0f, -0.5f, 0.0f);
            auto& floorBody = floor.AddComponent<RigidBodyComponent>();
            floorBody.SetMotion(RigidBodyMotion::Static);
            floorBody.mass = 0.0f;
            floorBody.useGravity = false;
            floorBody.layer = Physics::kLayerWorld;
            floor.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(4.0f, 0.5f, 4.0f)));
            Entity box = scene.CreateEntity("Box");
            box.GetComponent<TransformComponent>().position = Vec3(0.0f, 3.0f, 0.0f);
            auto& boxBody = box.AddComponent<RigidBodyComponent>();
            boxBody.mass = 1.0f;
            boxBody.layer = Physics::kLayerDefault;
            box.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            const entt::entity boxId = static_cast<entt::entity>(box);
            for (int frame = 0; frame < 90; ++frame) {
                system.Update(scene, 1.0f / 60.0f);
            }
            Physics::BodyObservation observation;
            ready = system.ObserveBody(boxId, observation) == Physics::BodyObservationStatus::Ready;
            y = observation.position.y;
        }
        world.Shutdown();
        return std::pair<bool, float>(ready, y);
    };
    const auto blocked = simulateDrop(true);
    const auto through = simulateDrop(false);
    pass &= Check(blocked.first && blocked.second > 0.2f && blocked.second < 1.5f,
                  "Default box lands on a World floor when the pair is on (y="
                      + std::to_string(blocked.second) + ")");
    pass &= Check(through.first && through.second < 0.0f,
                  "Default box falls through a World floor when the pair is off (y="
                      + std::to_string(through.second) + ")");

    {
        Physics::JoltPhysicsEngine world;
        world.Initialize();
        Physics::PhysicsLayerConfig layers = Physics::PhysicsLayerConfig::Seed();
        layers.SetCollides(Physics::kLayerDefault, Physics::kLayerWorld, false);
        world.BeginPlayWorld(layers);
        {
            Scene scene("RaycastLayers");
            Physics::JoltPhysicsSystem system(world);
            Entity worldBox = scene.CreateEntity("WorldBox");
            worldBox.GetComponent<TransformComponent>().position = Vec3(0.0f, 0.0f, 2.0f);
            auto& worldBody = worldBox.AddComponent<RigidBodyComponent>();
            worldBody.SetMotion(RigidBodyMotion::Static);
            worldBody.useGravity = false;
            worldBody.layer = Physics::kLayerWorld;
            worldBox.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(1.0f, 1.0f, 1.0f)));
            Entity defaultBox = scene.CreateEntity("DefaultBox");
            defaultBox.GetComponent<TransformComponent>().position = Vec3(0.0f, 0.0f, 6.0f);
            auto& defaultBody = defaultBox.AddComponent<RigidBodyComponent>();
            defaultBody.SetMotion(RigidBodyMotion::Static);
            defaultBody.useGravity = false;
            defaultBody.layer = Physics::kLayerDefault;
            defaultBox.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(1.0f, 1.0f, 1.0f)));
            system.Update(scene, 1.0f / 60.0f);

            Physics::RaycastHit hit;
            const bool unfiltered = system.Raycast(
                entt::null, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 1.0f), 20.0f, hit);
            pass &= Check(unfiltered && hit.hit && hit.entity == static_cast<entt::entity>(worldBox),
                          "unfiltered ray hits the closer World box");

            const std::uint16_t defaultLayer = Physics::kLayerDefault;
            const bool filtered = system.Raycast(
                entt::null, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 1.0f), 20.0f, hit, &defaultLayer);
            pass &= Check(filtered && hit.hit && hit.entity == static_cast<entt::entity>(defaultBox),
                          "Default-layer ray skips the World box when that pair is off");
        }
        world.Shutdown();
    }

    {
        Physics::JoltPhysicsEngine world;
        world.Initialize();
        world.BeginPlayWorld(Physics::PhysicsLayerConfig::Compatibility());
        {
            Scene scene("FirstOnUpdate");
            Physics::JoltPhysicsSystem system(world);
            Entity box = scene.CreateEntity("Existing");
            box.GetComponent<TransformComponent>().position = Vec3(0.0f, 3.0f, 0.0f);
            auto& body = box.AddComponent<RigidBodyComponent>();
            body.mass = 1.0f;
            box.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            const entt::entity boxId = static_cast<entt::entity>(box);

            system.PrepareSceneBodies(scene);
            Physics::BodyObservation before{};
            const bool ready = system.ObserveBody(boxId, before) == Physics::BodyObservationStatus::Ready;
            pass &= Check(ready && !system.GetBodyID(boxId).IsInvalid(),
                          "existing dynamic body is usable during the first onUpdate");

            system.Update(scene, 0.001f);
            Physics::BodyObservation held{};
            const bool still = system.ObserveBody(boxId, held) == Physics::BodyObservationStatus::Ready;
            pass &= Check(system.LastSubstepCount() == 0 && still && held.position.y > 2.99f && held.position.y < 3.01f,
                          "accumulator below fixedDt creates no substep and does not move the body");

            Entity spawned = scene.CreateEntity("Spawned");
            spawned.GetComponent<TransformComponent>().position = Vec3(2.0f, 3.0f, 0.0f);
            auto& spawnedBody = spawned.AddComponent<RigidBodyComponent>();
            spawnedBody.mass = 1.0f;
            spawned.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            const entt::entity spawnedId = static_cast<entt::entity>(spawned);
            system.ClearAccumulator();
            system.Update(scene, 0.001f);
            pass &= Check(system.LastSubstepCount() == 0 && !system.GetBodyID(spawnedId).IsInvalid(),
                          "a body spawned by onUpdate exists after a zero-substep structural pass");

            system.ClearAccumulator();
            system.Update(scene, Physics::kPhysicsFixedDt);
            Physics::BodyObservation stepped{};
            const bool steppedReady = system.ObserveBody(boxId, stepped) == Physics::BodyObservationStatus::Ready;
            pass &= Check(system.LastSubstepCount() == 1 && steppedReady && stepped.position.y < before.position.y,
                          "one fixed step moves the dynamic body");

            system.ClearAccumulator();
            system.Update(scene, 0.1f);
            pass &= Check(system.LastSubstepCount() == 4 && system.StalledStepCount() == 1 &&
                              system.Accumulator() == 0.0f,
                          "a 100ms frame takes four steps and discards the remainder");
        }
        world.Shutdown();
    }

    {
        Physics::JoltPhysicsEngine world;
        world.Initialize();
        world.BeginPlayWorld(Physics::PhysicsLayerConfig::Compatibility());
        {
            Scene scene("Hierarchy");
            Physics::JoltPhysicsSystem system(world);
            Entity parent = scene.CreateEntity("Parent");
            parent.GetComponent<TransformComponent>().position = Vec3(0.0f, 5.0f, 0.0f);
            Entity staticChild = scene.CreateEntity("StaticChild");
            staticChild.GetComponent<TransformComponent>().position = Vec3(0.0f, 1.0f, 0.0f);
            auto& staticBody = staticChild.AddComponent<RigidBodyComponent>();
            staticBody.SetMotion(RigidBodyMotion::Static);
            staticBody.mass = 0.0f;
            staticBody.useGravity = false;
            staticChild.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            scene.SetParent(staticChild, parent, false);

            Entity dynamicChild = scene.CreateEntity("DynamicChild");
            dynamicChild.GetComponent<TransformComponent>().position = Vec3(0.0f, 3.0f, 0.0f);
            auto& dynamicBody = dynamicChild.AddComponent<RigidBodyComponent>();
            dynamicBody.mass = 1.0f;
            dynamicBody.useGravity = false;
            dynamicChild.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            scene.SetParent(dynamicChild, parent, false);

            system.PrepareSceneBodies(scene);
            Physics::BodyObservation staticObservation{};
            const bool staticReady = system.ObserveBody(static_cast<entt::entity>(staticChild), staticObservation)
                == Physics::BodyObservationStatus::Ready;
            pass &= Check(staticReady && staticObservation.position.y > 5.9f && staticObservation.position.y < 6.1f,
                          "static child body uses the parented world position");

            parent.GetComponent<TransformComponent>().position = Vec3(0.0f, 8.0f, 0.0f);
            system.ClearAccumulator();
            system.Update(scene, Physics::kPhysicsFixedDt);
            const bool staticMoved = system.ObserveBody(static_cast<entt::entity>(staticChild), staticObservation)
                == Physics::BodyObservationStatus::Ready;
            pass &= Check(staticMoved && staticObservation.position.y > 8.9f && staticObservation.position.y < 9.1f,
                          "static child follows a moved parent");
            pass &= Check(staticChild.GetComponent<TransformComponent>().position.y > 0.9f
                              && staticChild.GetComponent<TransformComponent>().position.y < 1.1f,
                          "static child local position stays authored");

            Physics::BodyObservation dynamicObservation{};
            const bool dynamicReady = system.ObserveBody(static_cast<entt::entity>(dynamicChild), dynamicObservation)
                == Physics::BodyObservationStatus::Ready;
            pass &= Check(dynamicReady && dynamicObservation.position.x > -0.1f && dynamicObservation.position.x < 0.1f,
                          "dynamic child is not dragged when the parent moves");
            const Mat4 dynamicWorld = scene.GetWorldTransform(dynamicChild);
            pass &= Check(std::abs(dynamicWorld.m[12] - dynamicObservation.position.x) < 0.05f
                              && std::abs(dynamicWorld.m[13] - dynamicObservation.position.y) < 0.05f,
                          "dynamic child world transform matches the simulated body");

            Entity movedParent = scene.CreateEntity("MovedParent");
            movedParent.GetComponent<TransformComponent>().position = Vec3(10.0f, 0.0f, 0.0f);
            scene.SetParent(dynamicChild, movedParent, false);
            system.ClearAccumulator();
            system.Update(scene, Physics::kPhysicsFixedDt);
            const bool reparented = system.ObserveBody(static_cast<entt::entity>(dynamicChild), dynamicObservation)
                == Physics::BodyObservationStatus::Ready;
            pass &= Check(reparented && dynamicObservation.position.x > 9.5f && dynamicObservation.position.x < 10.5f,
                          "reparenting a dynamic body teleports it to the new world pose");

            Entity sheared = scene.CreateEntity("Sheared");
            sheared.GetComponent<TransformComponent>().scale = Vec3(2.0f, 1.0f, 2.0f);
            Entity rejected = scene.CreateEntity("Rejected");
            auto& rejectedBody = rejected.AddComponent<RigidBodyComponent>();
            rejectedBody.mass = 1.0f;
            rejected.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            scene.SetParent(rejected, sheared, false);
            const auto logBefore = Hephaestus::EngineLog::Snapshot().nextCursor;
            system.PrepareSceneBodies(scene);
            pass &= Check(system.GetBodyID(static_cast<entt::entity>(rejected)).IsInvalid(),
                          "non-uniform ancestor scale does not create a body");
            bool logged = false;
            for (const auto& entry : Hephaestus::EngineLog::Snapshot(logBefore).entries) {
                if (entry.category == "Physics" && entry.message.find("non-uniform") != std::string::npos) {
                    logged = true;
                }
            }
            pass &= Check(logged, "non-uniform ancestor scale logs a physics error");
        }
        world.Shutdown();
    }

    auto runKinematic = [](bool singleWideFrame) {
        Physics::JoltPhysicsEngine world;
        world.Initialize();
        world.BeginPlayWorld(Physics::PhysicsLayerConfig::Compatibility());
        float endX = 0.0f;
        float endSpeed = 0.0f;
        float midX = 0.0f;
        std::string motion;
        {
            Scene scene("Kinematic");
            Physics::JoltPhysicsSystem system(world);
            Entity platform = scene.CreateEntity("Platform");
            auto& body = platform.AddComponent<RigidBodyComponent>();
            body = RigidBodyComponent::CreateKinematic();
            platform.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(1.0f, 0.25f, 1.0f)));
            system.PrepareSceneBodies(scene);
            const entt::entity id = static_cast<entt::entity>(platform);
            if (singleWideFrame) {
                platform.GetComponent<TransformComponent>().position.x = 1.0f;
                system.Update(scene, Physics::kPhysicsFixedDt * 2.0f);
            } else {
                platform.GetComponent<TransformComponent>().position.x = 0.5f;
                system.Update(scene, Physics::kPhysicsFixedDt);
                Physics::BodyObservation mid{};
                system.ObserveBody(id, mid);
                midX = mid.position.x;
                platform.GetComponent<TransformComponent>().position.x = 1.0f;
                system.Update(scene, Physics::kPhysicsFixedDt);
            }
            Physics::BodyObservation observation{};
            system.ObserveBody(id, observation);
            endX = observation.position.x;
            endSpeed = observation.linearVelocity.x;
            motion = observation.motionType;
        }
        world.Shutdown();
        return std::tuple<float, float, float, std::string>(endX, endSpeed, midX, motion);
    };
    const auto wide = runKinematic(true);
    const auto stepped = runKinematic(false);
    pass &= Check(std::get<3>(wide) == "kinematic" && std::abs(std::get<0>(wide) - 1.0f) < 1.0e-3f,
                  "one 1/30 frame moves a kinematic body to the authored end");
    pass &= Check(std::abs(std::get<1>(wide) - 30.0f) < 1.0f,
                  "a 1 m move over two substeps has about 30 m/s, not 60");
    pass &= Check(std::abs(std::get<2>(stepped) - 0.5f) < 1.0e-3f && std::abs(std::get<0>(stepped) - 1.0f) < 1.0e-3f,
                  "two 1/60 frames reach the same kinematic positions");
    pass &= Check(std::abs(std::get<1>(stepped) - 30.0f) < 1.0f,
                  "each 1/60 kinematic slice is about 30 m/s");

    {
        Physics::JoltPhysicsEngine world;
        world.Initialize();
        world.BeginPlayWorld(Physics::PhysicsLayerConfig::Compatibility());
        {
            Scene scene("GroundSensor");
            Physics::JoltPhysicsSystem system(world);
            Entity character = scene.CreateEntity("Character");
            character.GetComponent<TransformComponent>().position = Vec3(0.0f, 1.0f, 0.0f);
            auto& characterBody = character.AddComponent<RigidBodyComponent>();
            characterBody.mass = 1.0f;
            characterBody.useGravity = false;
            character.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.4f, 0.4f, 0.4f)));
            Entity sensor = scene.CreateEntity("Sensor");
            sensor.GetComponent<TransformComponent>().position = Vec3(0.0f, 0.0f, 0.0f);
            auto& sensorBody = sensor.AddComponent<RigidBodyComponent>();
            sensorBody.SetMotion(RigidBodyMotion::Static);
            sensorBody.isTrigger = true;
            sensorBody.mass = 0.0f;
            sensorBody.useGravity = false;
            sensor.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(2.0f, 0.5f, 2.0f)));
            system.PrepareSceneBodies(scene);
            const bool sensorGround = Physics::CharacterMotor::IsGrounded(
                world, system, static_cast<entt::entity>(character), 2.0f);
            pass &= Check(!sensorGround, "character grounding ignores a sensor");

            Entity floor = scene.CreateEntity("Floor");
            floor.GetComponent<TransformComponent>().position = Vec3(0.0f, -1.0f, 0.0f);
            auto& floorBody = floor.AddComponent<RigidBodyComponent>();
            floorBody.SetMotion(RigidBodyMotion::Static);
            floorBody.mass = 0.0f;
            floorBody.useGravity = false;
            floor.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(2.0f, 0.5f, 2.0f)));
            system.PrepareSceneBodies(scene);
            const bool solidGround = Physics::CharacterMotor::IsGrounded(
                world, system, static_cast<entt::entity>(character), 3.0f);
            pass &= Check(solidGround, "character grounding still hits a solid floor below a sensor");
        }
        world.Shutdown();
    }

    {
        Physics::JoltPhysicsEngine world;
        world.Initialize();
        world.BeginPlayWorld(Physics::PhysicsLayerConfig::Seed());
        {
            Scene scene("Contacts");
            Physics::JoltPhysicsSystem system(world);
            std::vector<Physics::PhysicsContactEvent> events;
            system.SetContactEventSink([&](const Physics::PhysicsContactEvent& event) { events.push_back(event); });
            auto countKind = [&](Physics::PhysicsEventKind kind) {
                int count = 0;
                for (const Physics::PhysicsContactEvent& event : events) {
                    if (event.kind == kind) {
                        ++count;
                    }
                }
                return count;
            };

            Entity trigger = scene.CreateEntity("Trigger");
            trigger.GetComponent<TransformComponent>().position = Vec3(0.0f, 1.0f, 0.0f);
            auto& triggerBody = trigger.AddComponent<RigidBodyComponent>();
            triggerBody.SetMotion(RigidBodyMotion::Static);
            triggerBody.isTrigger = true;
            triggerBody.mass = 0.0f;
            triggerBody.useGravity = false;
            trigger.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));

            Entity dynamic = scene.CreateEntity("Dynamic");
            dynamic.GetComponent<TransformComponent>().position = Vec3(0.0f, 1.0f, 0.0f);
            auto& dynamicBody = dynamic.AddComponent<RigidBodyComponent>();
            dynamicBody.SetMotion(RigidBodyMotion::Dynamic);
            dynamicBody.mass = 1.0f;
            dynamicBody.useGravity = false;
            dynamic.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));

            system.PrepareSceneBodies(scene);
            Physics::BodyObservation triggerObservation;
            pass &= Check(system.ObserveBody(static_cast<entt::entity>(trigger), triggerObservation)
                              == Physics::BodyObservationStatus::Ready
                          && triggerObservation.motionType == "kinematic" && triggerObservation.active,
                          "a static trigger is an active kinematic sensor");
            const JPH::BodyID firstDynamicId = system.GetBodyID(static_cast<entt::entity>(dynamic));

            events.clear();
            system.Update(scene, Physics::kPhysicsFixedDt);
            pass &= Check(countKind(Physics::PhysicsEventKind::TriggerEnter) >= 1,
                          "an overlapping dynamic body emits TriggerEnter");
            events.clear();
            system.Update(scene, Physics::kPhysicsFixedDt);
            pass &= Check(countKind(Physics::PhysicsEventKind::TriggerStay) >= 1
                          && countKind(Physics::PhysicsEventKind::TriggerEnter) == 0,
                          "the next fixed step emits TriggerStay");

            world.GetBodyInterface().SetPosition(system.GetBodyID(static_cast<entt::entity>(dynamic)),
                                                 JPH::RVec3(8.0, 1.0, 0.0), JPH::EActivation::Activate);
            events.clear();
            system.Update(scene, Physics::kPhysicsFixedDt);
            pass &= Check(countKind(Physics::PhysicsEventKind::TriggerExit) >= 1,
                          "separating the dynamic body emits TriggerExit");

            world.GetBodyInterface().SetPosition(system.GetBodyID(static_cast<entt::entity>(dynamic)),
                                                 JPH::RVec3(0.0, 1.0, 0.0), JPH::EActivation::Activate);
            events.clear();
            system.Update(scene, Physics::kPhysicsFixedDt);
            pass &= Check(countKind(Physics::PhysicsEventKind::TriggerEnter) >= 1, "re-entry emits TriggerEnter");
            events.clear();
            scene.DestroyEntity(trigger);
            system.Update(scene, Physics::kPhysicsFixedDt);
            bool destroyedExit = false;
            for (const Physics::PhysicsContactEvent& event : events) {
                if (event.kind == Physics::PhysicsEventKind::TriggerExit && !event.otherAlive
                    && event.receiver == static_cast<std::uint32_t>(static_cast<entt::entity>(dynamic))) {
                    destroyedExit = true;
                }
            }
            pass &= Check(destroyedExit, "destroying the trigger exits the live body with no other entity");

            Entity staticBox = scene.CreateEntity("StaticBox");
            staticBox.GetComponent<TransformComponent>().position = Vec3(20.0f, 1.0f, 0.0f);
            auto& staticBody = staticBox.AddComponent<RigidBodyComponent>();
            staticBody.SetMotion(RigidBodyMotion::Static);
            staticBody.mass = 0.0f;
            staticBody.useGravity = false;
            staticBox.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            Entity kinematic = scene.CreateEntity("Kinematic");
            kinematic.GetComponent<TransformComponent>().position = Vec3(20.0f, 1.0f, 0.0f);
            kinematic.AddComponent<RigidBodyComponent>(RigidBodyComponent::CreateKinematic());
            kinematic.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            events.clear();
            system.Update(scene, Physics::kPhysicsFixedDt);
            pass &= Check(countKind(Physics::PhysicsEventKind::CollisionEnter) == 0
                          && countKind(Physics::PhysicsEventKind::TriggerEnter) == 0,
                          "kinematic versus static stays silent while kinematic-versus-static collision is off");

            Entity triggerA = scene.CreateEntity("TriggerA");
            triggerA.GetComponent<TransformComponent>().position = Vec3(30.0f, 1.0f, 0.0f);
            auto& triggerABody = triggerA.AddComponent<RigidBodyComponent>();
            triggerABody.SetMotion(RigidBodyMotion::Static);
            triggerABody.isTrigger = true;
            triggerABody.mass = 0.0f;
            triggerABody.useGravity = false;
            triggerABody.layer = Physics::kLayerTrigger;
            triggerA.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            Entity triggerB = scene.CreateEntity("TriggerB");
            triggerB.GetComponent<TransformComponent>().position = Vec3(30.0f, 1.0f, 0.0f);
            auto& triggerBBody = triggerB.AddComponent<RigidBodyComponent>();
            triggerBBody.SetMotion(RigidBodyMotion::Static);
            triggerBBody.isTrigger = true;
            triggerBBody.mass = 0.0f;
            triggerBBody.useGravity = false;
            triggerBBody.layer = Physics::kLayerTrigger;
            triggerB.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            events.clear();
            system.Update(scene, Physics::kPhysicsFixedDt);
            pass &= Check(events.empty(), "trigger versus trigger stays silent when the matrix bit is off");

            Entity floor = scene.CreateEntity("SolidFloor");
            floor.GetComponent<TransformComponent>().position = Vec3(40.0f, 1.0f, 0.0f);
            auto& floorBody = floor.AddComponent<RigidBodyComponent>();
            floorBody.SetMotion(RigidBodyMotion::Static);
            floorBody.mass = 0.0f;
            floorBody.useGravity = false;
            floor.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            Entity solid = scene.CreateEntity("SolidDynamic");
            solid.GetComponent<TransformComponent>().position = Vec3(40.0f, 1.0f, 0.0f);
            auto& solidBody = solid.AddComponent<RigidBodyComponent>();
            solidBody.SetMotion(RigidBodyMotion::Dynamic);
            solidBody.mass = 1.0f;
            solidBody.useGravity = false;
            solid.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            events.clear();
            system.Update(scene, Physics::kPhysicsFixedDt);
            pass &= Check(countKind(Physics::PhysicsEventKind::CollisionEnter) >= 1,
                          "an overlapping solid pair emits CollisionEnter");
            world.GetBodyInterface().DeactivateBody(system.GetBodyID(static_cast<entt::entity>(solid)));
            events.clear();
            system.Update(scene, Physics::kPhysicsFixedDt);
            pass &= Check(countKind(Physics::PhysicsEventKind::CollisionExit) >= 1,
                          "sleeping a solid body emits CollisionExit");

            scene.DestroyEntity(dynamic);
            system.PrepareSceneBodies(scene);
            pass &= Check(system.GetEntityForBody(firstDynamicId) == entt::null,
                          "a destroyed body id no longer resolves");
            Entity replacement = scene.CreateEntity("Replacement");
            replacement.GetComponent<TransformComponent>().position = Vec3(50.0f, 1.0f, 0.0f);
            auto& replacementBody = replacement.AddComponent<RigidBodyComponent>();
            replacementBody.SetMotion(RigidBodyMotion::Static);
            replacementBody.mass = 0.0f;
            replacementBody.useGravity = false;
            replacement.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            system.PrepareSceneBodies(scene);
            pass &= Check(system.GetEntityForBody(firstDynamicId) == entt::null
                          && system.GetEntityForBody(system.GetBodyID(static_cast<entt::entity>(replacement)))
                              == static_cast<entt::entity>(replacement),
                          "a recycled body index still rejects the old sequence");
        }
        world.Shutdown();
    }

    {
        Physics::JoltPhysicsEngine world;
        world.Initialize();
        world.BeginPlayWorld(Physics::PhysicsLayerConfig::Seed());
        {
            Scene scene("RenderInterpolation");
            Physics::JoltPhysicsSystem system(world);
            Entity box = scene.CreateEntity("FallingBox");
            box.GetComponent<TransformComponent>().position = Vec3(0.0f, 10.0f, 0.0f);
            auto& body = box.AddComponent<RigidBodyComponent>();
            body.mass = 1.0f;
            box.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
            auto& transform = box.GetComponent<TransformComponent>();

            for (int frame = 0; frame < 10; ++frame) {
                system.Update(scene, Physics::kPhysicsFixedDt);
            }
            const float previousY = transform.position.y;
            system.Update(scene, Physics::kPhysicsFixedDt * 1.25f);
            const float currentY = transform.position.y;
            pass &= Check(system.LastSubstepCount() == 1 && currentY < previousY,
                          "the box falls one fixed step and keeps a quarter step in the accumulator");

            system.ApplyRenderInterpolation(scene);
            const float renderedY = transform.position.y;
            const float expectedY = previousY + (currentY - previousY) * 0.25f;
            pass &= Check(std::abs(renderedY - expectedY) < 1.0e-4f,
                          "render interpolation places the box a quarter of the way through the last step");
            system.RestoreSimulationPoses(scene);
            pass &= Check(transform.position.y == currentY,
                          "restoring puts back the stepped simulation pose");

            transform.position = Vec3(5.0f, 20.0f, 0.0f);
            system.ApplyRenderInterpolation(scene);
            pass &= Check(transform.position.x == 5.0f && transform.position.y == 20.0f,
                          "a pose moved after the step is rendered where it was moved, not interpolated");
            system.RestoreSimulationPoses(scene);
            pass &= Check(transform.position.x == 5.0f && transform.position.y == 20.0f,
                          "restoring leaves a pose that was not interpolated alone");
        }
        world.Shutdown();
    }

    if (!pass) {
        std::cerr << "[FAIL] JoltPhysicsIntegrationTests\n";
        return 1;
    }
    std::cout << "[PASS] JoltPhysicsIntegrationTests\n";
    return 0;
}
