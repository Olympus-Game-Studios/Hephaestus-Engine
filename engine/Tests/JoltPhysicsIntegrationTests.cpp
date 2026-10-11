#include "Physics/JoltPhysicsEngine.h"
#include "Physics/PhysicsLayerConfig.h"

#include <Jolt/Core/Factory.h>

#include <iostream>
#include <string>

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

    if (!pass) {
        std::cerr << "[FAIL] JoltPhysicsIntegrationTests\n";
        return 1;
    }
    std::cout << "[PASS] JoltPhysicsIntegrationTests\n";
    return 0;
}
