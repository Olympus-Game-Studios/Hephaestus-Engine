#include "Assets/CharacterRuntime.h"
#include "Physics/JoltPhysicsEngine.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Physics/PhysicsLayerConfig.h"
#include "Physics/PhysicsStepClock.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scripting/LuaScriptComponent.h"
#include "Scripting/LuaScriptEngine.h"
#include "Scripting/ScriptPhysicsBindings.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace Hephaestus {

bool CharacterRuntime::UploadVisual(Entity, const std::filesystem::path&, EngineFrameRenderer*,
                                    std::string& outError, bool) {
    outError.clear();
    return true;
}

bool CharacterRuntime::BindScene(Scene&, const std::filesystem::path&, EngineFrameRenderer*) {
    return true;
}

} // namespace Hephaestus

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

Entity AddBox(Scene& scene, const char* name, Vec3 position, bool isStatic, bool sensor)
{
    Entity entity = scene.CreateEntity(name);
    entity.GetComponent<TransformComponent>().position = position;
    auto& body = entity.AddComponent<RigidBodyComponent>();
    body.isTrigger = sensor;
    if (isStatic) {
        body.SetMotion(RigidBodyMotion::Static);
    } else {
        body.mass = 1.0f;
        body.SetMotion(RigidBodyMotion::Dynamic);
    }
    body.useGravity = false;
    entity.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
    return entity;
}

} // namespace

int main()
{
    bool pass = true;
    Physics::JoltPhysicsEngine world;
    world.Initialize();
    world.BeginPlayWorld(Physics::PhysicsLayerConfig::Seed());
    {
        Scene scene("PhysicsLua");
        Physics::JoltPhysicsSystem physics(world);
        Entity trigger = AddBox(scene, "Trigger", Vec3(0.0f, 1.0f, 0.0f), true, true);
        Entity dynamic = AddBox(scene, "Dynamic", Vec3(0.0f, 1.0f, 0.0f), false, false);
        auto& script = dynamic.AddComponent<LuaScriptComponent>();
        script.scriptSource = R"lua(
function onTriggerEnter(other, contact)
    _G.enter = true
    _G.enterOther = other ~= nil
    _G.enterContact = contact ~= nil and contact.penetration ~= nil
end
function onTriggerStay(other, contact)
    _G.stay = (_G.stay or 0) + 1
end
function onTriggerExit(other, contact)
    _G.exit = true
    _G.exitOther = other ~= nil
    _G.exitContact = contact ~= nil
end
function onCollisionEnter(other, contact)
    _G.collision = contact ~= nil
end
)lua";

        LuaScriptEngine lua;
        lua.Initialize(&scene, nullptr);
        lua.OnSimulationStarted();
        lua.Update(scene, Physics::kPhysicsFixedDt);

        physics.Update(scene, Physics::kPhysicsFixedDt);
        lua.DispatchContactEvents(scene, physics.FrameContactEvents());
        pass &= Check(lua.ExecuteString("assert(_G.enter == true and _G.enterOther == true and _G.enterContact == true)"),
                      "onTriggerEnter receives the other entity and a contact");

        physics.Update(scene, Physics::kPhysicsFixedDt);
        lua.DispatchContactEvents(scene, physics.FrameContactEvents());
        pass &= Check(lua.ExecuteString("assert((_G.stay or 0) >= 1)"), "onTriggerStay runs on the next fixed step");

        scene.DestroyEntity(trigger);
        physics.Update(scene, Physics::kPhysicsFixedDt);
        lua.DispatchContactEvents(scene, physics.FrameContactEvents());
        pass &= Check(lua.ExecuteString("assert(_G.exit == true and _G.exitOther == false and _G.exitContact == false)"),
                      "onTriggerExit has a nil other entity and no contact");

        Entity floor = AddBox(scene, "Floor", Vec3(12.0f, 1.0f, 0.0f), true, false);
        Entity solid = AddBox(scene, "Solid", Vec3(12.0f, 1.0f, 0.0f), false, false);
        auto& solidScript = solid.AddComponent<LuaScriptComponent>();
        solidScript.scriptSource = R"lua(
function onCollisionEnter(other, contact)
    _G.collision = contact ~= nil and contact.normal ~= nil
end
)lua";
        lua.Update(scene, Physics::kPhysicsFixedDt);
        physics.Update(scene, Physics::kPhysicsFixedDt);
        lua.DispatchContactEvents(scene, physics.FrameContactEvents());
        pass &= Check(lua.ExecuteString("assert(_G.collision == true)"),
                      "onCollisionEnter receives a contact");
        (void)floor;

        bool physicsAvailable = true;
        const ScriptPhysicsAccessor access = [&]() {
            return physicsAvailable ? ScriptPhysicsAccess{&scene, &world, &physics, nullptr} : ScriptPhysicsAccess{};
        };
        lua.SetPhysicsBinding(MakeScriptPhysicsBinding(access));
        lua.SetCharacterBinding(MakeScriptCharacterBinding(access));
        Entity probe = AddBox(scene, "Probe", Vec3(30.0f, 1.0f, 0.0f), false, false);
        AddBox(scene, "ProbeFloor", Vec3(30.0f, -3.0f, 0.0f), true, false);
        auto& probeScript = probe.AddComponent<LuaScriptComponent>();
        probeScript.scriptSource = R"lua(
function onUpdate(dt)
    _G.probeHasBody = Physics.hasBody()
    _G.probeSet = Physics.setLinearVelocity(0, 2, 0, true)
    local v = Physics.getLinearVelocity()
    _G.probeVy = v and v.y or nil
    local hit = Physics.raycast(30, 0.4, 0, 0, -1, 0, 10)
    _G.probeRayDistance = hit and hit.hit and hit.distance or nil
    _G.probeCharacterMove = Character.move(1, 0)
end
)lua";
        physics.Update(scene, Physics::kPhysicsFixedDt);
        lua.Update(scene, Physics::kPhysicsFixedDt);
        pass &= Check(lua.ExecuteString("assert(_G.probeHasBody == true and _G.probeSet == true)"),
                      "shared physics binding finds and drives the script's body");
        pass &= Check(lua.ExecuteString("assert(math.abs(_G.probeVy - 2) < 1e-4)"),
                      "shared physics binding reads back the velocity it set");
        pass &= Check(lua.ExecuteString("assert(_G.probeRayDistance ~= nil and math.abs(_G.probeRayDistance - 2.9) < 0.05)"),
                      "shared physics binding raycasts against the scene");
        pass &= Check(lua.ExecuteString("assert(_G.probeCharacterMove == false)"),
                      "shared character binding reports no controller system");

        Entity seeker = AddBox(scene, "Seeker", Vec3(50.0f, 5.0f, 0.0f), false, false);
        AddBox(scene, "SeekSensor", Vec3(50.0f, 2.0f, 0.0f), true, true);
        AddBox(scene, "SeekSolid", Vec3(50.0f, -1.0f, 0.0f), true, false);
        auto& seekerScript = seeker.AddComponent<LuaScriptComponent>();
        seekerScript.scriptSource = R"lua(
function onUpdate(dt)
    local sensor = Scene.findAllWithPrefix("SeekSensor")[1]
    local first = Physics.raycast(50, 3.8, 0, 0, -1, 0, 10)
    _G.seekFirstSensor = first ~= nil and first.sensor == true and math.abs(first.distance - 1.3) < 0.05
    _G.seekSameId = first ~= nil and first.entity ~= nil and first.entity:GetId() == sensor:GetId()
    _G.seekIdIsInteger = math.type(this:GetId()) == "integer" and this:GetId() ~= sensor:GetId()
    local past = Physics.raycast(50, 3.8, 0, 0, -1, 0, 10, nil, false, { this, sensor })
    _G.seekPastSensor = past ~= nil and past.sensor == false and math.abs(past.distance - 4.3) < 0.05
end
)lua";
        physics.Update(scene, Physics::kPhysicsFixedDt);
        lua.Update(scene, Physics::kPhysicsFixedDt);
        pass &= Check(lua.ExecuteString("assert(_G.seekFirstSensor == true)"),
                      "raycast reports that the first hit is a sensor");
        pass &= Check(lua.ExecuteString("assert(_G.seekSameId == true and _G.seekIdIsInteger == true)"),
                      "entity GetId is a stable integer shared by every reference to one entity");
        pass &= Check(lua.ExecuteString("assert(_G.seekPastSensor == true)"),
                      "raycast ignore list skips the listed sensor and reaches the solid behind it");

        Entity plate = AddBox(scene, "ThinPlate", Vec3(70.0f, 1.0f, 0.0f), true, false);
        plate.GetComponent<TransformComponent>().scale = Vec3(0.03f, 1.0f, 1.0f);
        physics.Update(scene, Physics::kPhysicsFixedDt);
        Physics::RaycastHit plateHit{};
        pass &= Check(physics.Raycast(entt::null, Vec3(68.0f, 1.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), 5.0f, plateHit) &&
                          plateHit.hit && plateHit.entity == static_cast<entt::entity>(plate) &&
                          std::abs(plateHit.distance - 1.985f) < 0.01f,
                      "a box thinner than the default convex radius keeps its real size");

        physicsAvailable = false;
        lua.Update(scene, Physics::kPhysicsFixedDt);
        pass &= Check(lua.ExecuteString("assert(_G.probeHasBody == false and _G.probeSet == false and _G.probeRayDistance == nil)"),
                      "shared physics binding reports unavailable physics without touching it");

        lua.OnSimulationStopped();
        lua.Shutdown();
    }
    world.Shutdown();

    if (!pass) {
        std::cerr << "[FAIL] PhysicsLuaTests\n";
        return 1;
    }
    std::cout << "[PASS] PhysicsLuaTests\n";
    return 0;
}
