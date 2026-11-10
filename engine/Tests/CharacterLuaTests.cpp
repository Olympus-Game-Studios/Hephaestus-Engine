/**
 * @file CharacterLuaTests.cpp
 * @brief Stage G Lua slot coexistence, Animator, callbacks, and CallController tests.
 */

#include "Animation/AnimationClip.h"
#include "Animation/CharacterPlayback.h"
#include "Animation/Skeleton.h"
#include "Assets/CharacterAsset.h"
#include "Assets/CharacterRuntime.h"
#include "Scene/CharacterComponent.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scripting/LuaScriptComponent.h"
#include "Scripting/LuaScriptEngine.h"
#include "UI/HephaestusUI/UIContext.h"
#include "UI/HephaestusUI/UISystem.h"
#include "UI/HephaestusUI/UIRenderer.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using Hephaestus::AnimationClip;
using Hephaestus::CharacterAnimationSystem;
using Hephaestus::CharacterRuntime;
using Hephaestus::CharacterSocket;
using Hephaestus::Skeleton;
using Hephaestus::SkeletonBone;
using Hephaestus::SkeletonBuilder;

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

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
    }
    return condition;
}

bool MakeTwoBoneSkeleton(Skeleton& skeleton, std::string& error) {
    skeleton = {};
    SkeletonBone root;
    root.name = "Root";
    root.parentEval = -1;
    root.sourceJoint = 0;
    root.localRest = Mat4::Identity();
    root.inverseBind = Mat4::Identity();
    root.restTranslation = Vec3(0.0f, 0.0f, 0.0f);
    root.restRotation = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    root.restScale = Vec3(1.0f, 1.0f, 1.0f);
    SkeletonBone child;
    child.name = "Child";
    child.parentEval = 0;
    child.sourceJoint = 1;
    child.localRest = Mat4::Translation(Vec3(0.0f, 1.0f, 0.0f));
    child.inverseBind = Mat4::Translation(Vec3(0.0f, -1.0f, 0.0f));
    child.restTranslation = Vec3(0.0f, 1.0f, 0.0f);
    child.restRotation = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    child.restScale = Vec3(1.0f, 1.0f, 1.0f);
    skeleton.bones = {root, child};
    return SkeletonBuilder::Finalize(skeleton, error);
}

AnimationClip MakeClip(const Skeleton& skeleton, const std::string& id, bool loop, float duration) {
    AnimationClip clip;
    clip.name = id;
    clip.id = id;
    clip.sourceClip = id;
    clip.skeletonSignature = skeleton.signature;
    clip.duration = duration;
    clip.endTime = duration;
    clip.loop = loop;
    return clip;
}

bool WriteFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << contents;
    return static_cast<bool>(out);
}

bool TestSlotsAndAnimator(const fs::path& root) {
    bool ok = true;
    const fs::path aiPath = root / "assets" / "scripts" / "EnemyAI.lua";
    const fs::path controllerPath = root / "assets" / "scripts" / "CharacterController.lua";
    ok &= Check(WriteFile(aiPath, R"(
Expose.number("health", 10)
function onAwake()
    _G.gameplayAwake = true
end
function onUpdate(dt)
    _G.callHitGameplay = this:Call("BeginShoot")
    this:CallController("BeginShoot")
end
function onAnimationFinished(name)
    _G.gameplayFinished = name
end
)"), "write AI");
    ok &= Check(WriteFile(controllerPath, R"(
Expose.number("WalkThreshold", 0.1)
function onAwake()
    _G.controllerAwake = true
    _G.walkThreshold = WalkThreshold
    animator = this:GetAnimator()
    local character = this:GetCharacter()
    _G.gotCharacter = character ~= nil
    _G.missingSocket = (not character) or (character:GetSocket("Missing") == nil)
end
function BeginShoot()
    if _G.shootStarted then
        return
    end
    _G.shootStarted = true
    animator:Play("Shoot")
end
function onUpdate(dt)
    if not _G.shootStarted then
        animator:CrossFade("Idle", 0.1)
    end
end
function onAnimationFinished(name)
    _G.controllerFinished = name
end
)"), "write controller");

    Scene scene("CharacterLua");
    Entity entity = scene.CreateEntity("Cultist");
    auto& character = entity.AddComponent<CharacterComponent>();
    character.runController = true;
    character.controllerScriptPath = "assets/scripts/CharacterController.lua";
    character.controllerVariables["WalkThreshold"] = PropertyValue(0.25);
    Skeleton skeleton;
    std::string error;
    ok &= Check(MakeTwoBoneSkeleton(skeleton, error), "skeleton");
    ok &= Check(CharacterAnimationSystem::BindRuntime(
                    character, skeleton,
                    {MakeClip(skeleton, "Idle", true, 1.0f), MakeClip(skeleton, "Shoot", false, 0.2f)},
                    error),
                "bind runtime");
    auto& lua = entity.AddComponent<LuaScriptComponent>();
    lua.scriptPath = "assets/scripts/EnemyAI.lua";
    lua.variables["health"] = PropertyValue(7.0);

    LuaScriptEngine engine;
    engine.Initialize(&scene, nullptr);
    engine.SetProjectRoot(root);
    engine.OnSimulationStarted();
    engine.Update(scene, 0.016f);
    ok &= Check(character.player.hasPendingPlay && character.player.pendingClip == "Shoot",
                "CallController queues Play Shoot");
    CharacterAnimationSystem::Tick(scene, 0.016f);
    engine.DispatchAnimationFinished(scene);
    engine.LateUpdate(scene, 0.016f);

    ok &= Check(engine.ExecuteString("assert(_G.gameplayAwake == true)"), "gameplay onAwake");
    ok &= Check(engine.ExecuteString("assert(_G.controllerAwake == true)"), "controller onAwake");
    ok &= Check(engine.ExecuteString("assert(_G.shootStarted == true)"), "CallController BeginShoot");
    ok &= Check(engine.ExecuteString("assert(_G.callHitGameplay == false)"),
                "this:Call does not reach the controller");
    ok &= Check(engine.ExecuteString("assert(_G.walkThreshold == 0.25)"), "controller Expose seed");
    ok &= Check(engine.ExecuteString("assert(_G.gotCharacter == true)"), "GetCharacter present");
    ok &= Check(engine.ExecuteString("assert(_G.missingSocket == true)"), "GetSocket missing is nil");
    ok &= Check(character.player.currentClip == "Shoot", "Shoot committed same frame");
    ok &= Check(character.player.speed == 1.0f, "runtime speed default");

    engine.Update(scene, 0.016f);
    CharacterAnimationSystem::Tick(scene, 0.25f);
    engine.DispatchAnimationFinished(scene);
    engine.LateUpdate(scene, 0.016f);
    ok &= Check(engine.ExecuteString("assert(_G.controllerFinished == 'Shoot')"),
                "controller onAnimationFinished");
    ok &= Check(engine.ExecuteString("assert(_G.gameplayFinished == 'Shoot')"),
                "gameplay onAnimationFinished");

    Entity noCharacter = scene.CreateEntity("Prop");
    auto& propLua = noCharacter.AddComponent<LuaScriptComponent>();
    propLua.scriptSource = "function onUpdate(dt) _G.gotAnimator = this:GetAnimator() ~= nil end";
    engine.Update(scene, 0.016f);
    ok &= Check(engine.ExecuteString("assert(_G.gotAnimator == false)"), "GetAnimator nil without Character");

    engine.OnSimulationStopped();
    engine.Shutdown();
    return ok;
}

bool TestControllerDisabled(const fs::path& root) {
    bool ok = true;
    ok &= Check(WriteFile(root / "assets" / "scripts" / "IdleController.lua",
                          "function onUpdate(dt) this:GetAnimator():Play('Idle') end"),
                "write idle controller");
    ok &= Check(WriteFile(root / "assets" / "scripts" / "OnlyAI.lua",
                          "function onUpdate(dt) _G.aiRan = true end"),
                "write AI");

    Scene scene("DisabledController");
    Entity entity = scene.CreateEntity("Cultist");
    auto& character = entity.AddComponent<CharacterComponent>();
    character.runController = false;
    character.controllerScriptPath = "assets/scripts/IdleController.lua";
    Skeleton skeleton;
    std::string error;
    ok &= Check(MakeTwoBoneSkeleton(skeleton, error), "skeleton");
    ok &= Check(CharacterAnimationSystem::BindRuntime(
                    character, skeleton, {MakeClip(skeleton, "Idle", true, 1.0f)}, error),
                "bind");
    auto& lua = entity.AddComponent<LuaScriptComponent>();
    lua.scriptPath = "assets/scripts/OnlyAI.lua";

    LuaScriptEngine engine;
    engine.Initialize(&scene, nullptr);
    engine.SetProjectRoot(root);
    engine.OnSimulationStarted();
    engine.Update(scene, 0.016f);
    CharacterAnimationSystem::Tick(scene, 0.016f);
    engine.LateUpdate(scene, 0.016f);
    ok &= Check(engine.ExecuteString("assert(_G.aiRan == true)"), "gameplay still runs when controller is off");
    ok &= Check(character.player.currentClip.empty() || !character.player.hasPendingPlay,
                "disabled controller did not issue Play");
    engine.OnSimulationStopped();
    engine.Shutdown();
    return ok;
}

bool TestBrokenControllerIsolation(const fs::path& root) {
    bool ok = true;
    ok &= Check(WriteFile(root / "assets" / "scripts" / "BrokenController.lua", "this is not lua [[["),
                "write broken controller");
    ok &= Check(WriteFile(root / "assets" / "scripts" / "SurvivorAI.lua",
                          "function onUpdate(dt) _G.survivor = true end"),
                "write survivor AI");

    Scene scene("Broken");
    Entity entity = scene.CreateEntity("Cultist");
    auto& character = entity.AddComponent<CharacterComponent>();
    character.runController = true;
    character.controllerScriptPath = "assets/scripts/BrokenController.lua";
    Skeleton skeleton;
    std::string error;
    ok &= Check(MakeTwoBoneSkeleton(skeleton, error), "skeleton");
    ok &= Check(CharacterAnimationSystem::BindRuntime(
                    character, skeleton, {MakeClip(skeleton, "Idle", true, 1.0f)}, error),
                "bind");
    auto& lua = entity.AddComponent<LuaScriptComponent>();
    lua.scriptPath = "assets/scripts/SurvivorAI.lua";

    LuaScriptEngine engine;
    engine.Initialize(&scene, nullptr);
    engine.SetProjectRoot(root);
    engine.OnSimulationStarted();
    engine.Update(scene, 0.016f);
    CharacterAnimationSystem::Tick(scene, 0.016f);
    engine.LateUpdate(scene, 0.016f);
    ok &= Check(engine.ExecuteString("assert(_G.survivor == true)"),
                "broken controller does not stop gameplay Lua");
    ok &= Check(character.runtimeReady, "character runtime still bound");
    engine.OnSimulationStopped();
    engine.Shutdown();
    return ok;
}

bool TestSceneReloadRequestLifecycle() {
    bool ok = true;
    Scene scene("ReloadRequest");
    Entity entity = scene.CreateEntity("ReloadScript");
    entity.AddComponent<LuaScriptComponent>().scriptSource = R"(
local reloadProbeState = package.loaded.__runtimeSceneReload or {}
package.loaded.__runtimeSceneReload = reloadProbeState
function onAwake()
    _G.reloadAwakeCount = (_G.reloadAwakeCount or 0) + 1
    if _G.reloadAwakeCount == 1 then
        _G.oldReloadScreen = UI.createScreen("ReloadProbeHUD")
        reloadProbeState.oldEntity = this
    else
        _G.newReloadScreen = UI.createScreen("ReloadProbeHUD")
        reloadProbeState.newEntity = this
    end
end
function onDestroy()
    _G.reloadDestroyCount = (_G.reloadDestroyCount or 0) + 1
end
function onUpdate(dt)
    if _G.requestSceneReload then
        _G.reloadAccepted = Scene.reload()
        _G.requestSceneReload = false
    end
end
)";

    LuaScriptEngine engine;
    engine.Initialize(&scene, nullptr);
    Hephaestus::HephaestusUI::UISystem uiSystem;
    Hephaestus::HephaestusUI::UIContext uiContext;
    Hephaestus::HephaestusUI::UIRenderer uiRenderer;
    engine.SetUIBindings(&uiSystem, &uiContext, &uiRenderer);
    int acceptedRequests = 0;
    std::string requestedScene = "unset";
    engine.SetSceneReloadCallback([&acceptedRequests, &requestedScene](const std::string& scenePath) {
        ++acceptedRequests;
        requestedScene = scenePath;
        return true;
    });
    engine.OnSimulationStarted();
    engine.Update(scene, 1.0f / 60.0f);
    ok &= Check(engine.ExecuteString("assert(_G.reloadAwakeCount == 1)"),
                "authored entity script receives its first onAwake");

    engine.ExecuteString("_G.requestSceneReload = true");
    engine.Update(scene, 1.0f / 60.0f);
    ok &= Check(acceptedRequests == 1 && engine.ExecuteString("assert(_G.reloadAccepted == true)"),
                "Scene.reload accepts a gameplay-originated deferred request");
    ok &= Check(requestedScene.empty(), "Scene.reload asks for the current scene");
    ok &= Check(engine.ExecuteString("assert(Scene.load('assets/scenes/Other.hpscene') == true)") &&
                    requestedScene == "assets/scenes/Other.hpscene" && acceptedRequests == 2,
                "Scene.load asks the host for the named scene");

    ok &= Check(engine.ExecuteString("Time.setScale(0.25); assert(Time.getScale() == 0.25)") &&
                    engine.GetTimeScale() == 0.25f,
                "Time.setScale sets the gameplay time scale");
    ok &= Check(engine.ExecuteString("Time.setScale(-3); assert(Time.getScale() == 0)") &&
                    engine.ExecuteString("Time.setScale(99); assert(Time.getScale() == 10)"),
                "the time scale is clamped to 0..10");

    {
        const std::filesystem::path storage = std::filesystem::temp_directory_path() / "hephaestus_lua_storage_tests";
        std::error_code ec;
        std::filesystem::remove_all(storage, ec);
        engine.SetStorageRoot(storage);
        ok &= Check(engine.ExecuteString(R"lua(
assert(Storage.load("settings") == nil)
assert(Storage.save("settings", { sensitivity = 0.5, volumes = { master = 1, sfx = 0.25 }, name = "Andrew",
                                  bindings = { "W", "A" }, fullscreen = true }))
local s = Storage.load("settings")
assert(s.sensitivity == 0.5 and s.volumes.sfx == 0.25 and s.name == "Andrew" and s.bindings[2] == "A"
       and s.fullscreen == true)
local ok, err = Storage.save("../escape", { x = 1 })
assert(ok == false and err ~= nil)
assert(Storage.save("bad", { f = print }) == false)
)lua") && std::filesystem::exists(storage / "settings.json") && !std::filesystem::exists(storage.parent_path() / "escape.json"),
                    "Storage saves and loads tables per name and refuses unsafe names and values");
        std::filesystem::remove_all(storage, ec);
    }

    engine.OnSimulationStopped();
    ok &= Check(engine.GetTimeScale() == 1.0f, "stopping the simulation resets the time scale");
    ok &= Check(engine.ExecuteString("assert(_G.reloadDestroyCount == 1)"),
                "old entity script onDestroy runs before scene teardown");
    uiSystem.ClearScreens();
    ok &= Check(engine.ExecuteString("assert(UI.setScreenVisible(_G.oldReloadScreen, false) == false)"),
                "old scene screen handles are invalidated at teardown");
    scene.Clear();
    scene.MarkReconstructedFromSource();
    Entity reloaded = scene.CreateEntity("ReloadScript");
    reloaded.AddComponent<LuaScriptComponent>().scriptSource = R"(
local reloadProbeState = package.loaded.__runtimeSceneReload or {}
package.loaded.__runtimeSceneReload = reloadProbeState
function onAwake()
    _G.reloadAwakeCount = (_G.reloadAwakeCount or 0) + 1
    if _G.reloadAwakeCount == 1 then
        _G.oldReloadScreen = UI.createScreen("ReloadProbeHUD")
        reloadProbeState.oldEntity = this
    else
        _G.newReloadScreen = UI.createScreen("ReloadProbeHUD")
        reloadProbeState.newEntity = this
    end
end
function onDestroy()
    _G.reloadDestroyCount = (_G.reloadDestroyCount or 0) + 1
end
)";
    engine.OnSimulationStarted();
    engine.Update(scene, 1.0f / 60.0f);
    ok &= Check(engine.ExecuteString("assert(_G.reloadAwakeCount == 2)"),
                "reconstructed entity gets a fresh script instance and onAwake");
    ok &= Check(uiSystem.GetScreens().size() == 1 && engine.ExecuteString(
                    "assert(_G.newReloadScreen ~= _G.oldReloadScreen)"),
                "reloaded scene mounts one new screen with a non-recycled handle");
    ok &= Check(engine.ExecuteString("assert(package.loaded.__runtimeSceneReload.oldEntity:IsValid() == false and package.loaded.__runtimeSceneReload.newEntity:IsValid() == true)"),
                "Lua entity userdata from the prior scene generation expires");
    engine.OnSimulationStopped();
    engine.Shutdown();
    return ok;
}

} // namespace

int main() {
#if !LUA_AVAILABLE
    std::cout << "CharacterLuaTests skipped (LUA_AVAILABLE=0)\n";
    return 0;
#else
    bool ok = true;
    const fs::path root = fs::temp_directory_path() / "hephaestus_character_lua_tests";
    fs::remove_all(root);
    fs::create_directories(root / "assets" / "scripts");
    ok &= TestSlotsAndAnimator(root);
    ok &= TestControllerDisabled(root);
    ok &= TestBrokenControllerIsolation(root);
    ok &= TestSceneReloadRequestLifecycle();
    if (ok) {
        std::cout << "CharacterLuaTests passed\n";
        return 0;
    }
    return 1;
#endif
}
