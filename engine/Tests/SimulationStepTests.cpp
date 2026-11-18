#include "Core/SimulationStep.h"
#include "Physics/JoltPhysicsEngine.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Physics/PhysicsLayerConfig.h"
#include "Physics/PhysicsStepClock.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scripting/LuaScriptComponent.h"
#include "Scripting/LuaScriptEngine.h"
#include "Scripting/ScriptBase.h"
#include "Scripting/ScriptEngine.h"

#include <iostream>
#include <memory>
#include <string>
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

class RecordingScript : public ScriptBase {
public:
    explicit RecordingScript(std::vector<std::string>& log) : m_log(log) {}
    void OnUpdate(float) override { m_log.push_back("cpp-update"); }
    void OnLateUpdate(float) override { m_log.push_back("cpp-late"); }

private:
    std::vector<std::string>& m_log;
};

std::string Join(const std::vector<std::string>& log)
{
    std::string out;
    for (const std::string& entry : log) {
        if (!out.empty()) out += ",";
        out += entry;
    }
    return out;
}

} // namespace

int main()
{
    bool pass = true;
    Physics::JoltPhysicsEngine world;
    world.Initialize();
    world.BeginPlayWorld(Physics::PhysicsLayerConfig::Seed());
    {
        Scene scene("SimulationStep");
        Physics::JoltPhysicsSystem physics(world);

        Entity box = scene.CreateEntity("Box");
        box.GetComponent<TransformComponent>().position = Vec3(0.0f, 5.0f, 0.0f);
        auto& body = box.AddComponent<RigidBodyComponent>();
        body.mass = 1.0f;
        body.SetMotion(RigidBodyMotion::Dynamic);
        body.useGravity = true;
        box.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f)));
        auto& script = box.AddComponent<LuaScriptComponent>();
        script.scriptSource = R"lua(
function onUpdate(dt) table.insert(_G.stepLog, "lua-update") end
function onLateUpdate(dt) table.insert(_G.stepLog, "lua-late") end
)lua";

        std::vector<std::string> log;
        LuaScriptEngine lua;
        lua.Initialize(&scene, nullptr);
        lua.ExecuteString("_G.stepLog = {}");
        lua.OnSimulationStarted();
        ScriptEngine cppScripts;
        cppScripts.Initialize(&scene);
        cppScripts.AttachScript(box, std::make_shared<RecordingScript>(log), "Recording");
        cppScripts.AwakeScripts();

        // Merge the Lua log into the C++ log at each safe point so one list shows the order.
        const auto drainLua = [&]() {
            lua.ExecuteString(R"lua(
_G.stepDrained = table.concat(_G.stepLog, ",")
_G.stepLog = {}
)lua");
        };
        int safePoints = 0;
        bool reloadAtFirstSafePoint = false;
        Hephaestus::SimulationStepContext step;
        step.scene = &scene;
        step.physics = &physics;
        step.lua = &lua;
        step.cppScripts = &cppScripts;
        step.reloadAtSafePoint = [&]() {
            ++safePoints;
            log.push_back("safe");
            return reloadAtFirstSafePoint;
        };

        bool completed = Hephaestus::RunSimulationStep(step, Physics::kPhysicsFixedDt);
        drainLua();
        pass &= Check(completed && safePoints == 4, "a full frame passes four reload safe points");
        pass &= Check(Join(log) == "cpp-update,safe,safe,safe,cpp-late,safe",
                      "C++ scripts update before the first safe point and late-update after the third");
        pass &= Check(lua.ExecuteString("assert(_G.stepDrained == 'lua-update,lua-late')"),
                      "Lua onUpdate and onLateUpdate both run in a full frame");
        const float yAfterFirstFrame = scene.GetWorldTransform(box).m[13];
        pass &= Check(yAfterFirstFrame < 5.0f, "physics steps after scripts in a full frame");

        log.clear();
        safePoints = 0;
        reloadAtFirstSafePoint = true;
        completed = Hephaestus::RunSimulationStep(step, Physics::kPhysicsFixedDt);
        drainLua();
        pass &= Check(!completed && safePoints == 1, "a reload at the first safe point ends the frame");
        pass &= Check(Join(log) == "cpp-update,safe", "no late update runs after an early reload");
        pass &= Check(lua.ExecuteString("assert(_G.stepDrained == 'lua-update')"),
                      "Lua onLateUpdate is skipped after an early reload");
        pass &= Check(scene.GetWorldTransform(box).m[13] == yAfterFirstFrame,
                      "physics does not step after an early reload");

        // Time.setScale(0) freezes gameplay: physics does not move while scripts still run with dt 0.
        reloadAtFirstSafePoint = false;
        lua.ExecuteString("Time.setScale(0); function _G.recordDt(dt) _G.lastDt = dt end");
        const float yBeforePause = scene.GetWorldTransform(box).m[13];
        for (int i = 0; i < 5; ++i) {
            Hephaestus::RunSimulationStep(step, Physics::kPhysicsFixedDt);
        }
        pass &= Check(scene.GetWorldTransform(box).m[13] == yBeforePause, "a time scale of 0 freezes physics");
        pass &= Check(lua.ExecuteString("assert(Time.deltaTime() == 0 and math.abs(Time.unscaledDeltaTime() - "
                                        + std::to_string(Physics::kPhysicsFixedDt) + ") < 1e-6)"),
                      "paused scripts see dt 0 and the real frame time through Time.unscaledDeltaTime");
        lua.ExecuteString("Time.setScale(1)");
        Hephaestus::RunSimulationStep(step, Physics::kPhysicsFixedDt);
        pass &= Check(scene.GetWorldTransform(box).m[13] < yBeforePause, "physics resumes at a time scale of 1");

        cppScripts.Shutdown();
        lua.OnSimulationStopped();
        lua.Shutdown();
    }
    world.Shutdown();

    if (!pass) {
        std::cerr << "[FAIL] SimulationStepTests\n";
        return 1;
    }
    std::cout << "[PASS] SimulationStepTests\n";
    return 0;
}
