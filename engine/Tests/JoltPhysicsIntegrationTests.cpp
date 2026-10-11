#include "Physics/JoltPhysicsEngine.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Physics/PhysicsLayerConfig.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <Jolt/Core/Factory.h>

#include <iostream>
#include <string>
#include <utility>

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
            floorBody.isStatic = true;
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
            worldBody.isStatic = true;
            worldBody.useGravity = false;
            worldBody.layer = Physics::kLayerWorld;
            worldBox.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(1.0f, 1.0f, 1.0f)));
            Entity defaultBox = scene.CreateEntity("DefaultBox");
            defaultBox.GetComponent<TransformComponent>().position = Vec3(0.0f, 0.0f, 6.0f);
            auto& defaultBody = defaultBox.AddComponent<RigidBodyComponent>();
            defaultBody.isStatic = true;
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

    if (!pass) {
        std::cerr << "[FAIL] JoltPhysicsIntegrationTests\n";
        return 1;
    }
    std::cout << "[PASS] JoltPhysicsIntegrationTests\n";
    return 0;
}
