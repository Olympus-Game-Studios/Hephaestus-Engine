#include "Animation/CharacterPlayback.h"
#include "Automation/EntityRef.h"
#include "Core/PlayModeManager.h"
#include "Scene/ActiveCameraSync.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scripting/LuaScriptEngine.h"
#include "Scripting/ScriptEngine.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace Automation = Hephaestus::Automation;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

bool Reconstruct(Scene& scene, const fs::path& source, std::string& error)
{
    Scene staged;
    if (!SceneSerializer::Deserialize(staged, source, error)) return false;
    return SceneSerializer::Deserialize(scene, source, error);
}

std::size_t EntityCount(Scene& scene)
{
    return scene.GetEntitiesWith<TagComponent>().size();
}

} // namespace

// PlayModeManager's behavior is the unit under test. Host systems are faked at
// their narrow lifecycle boundaries so this test stays independent of Vulkan,
// Qt, Jolt, and the Lua shared library.
void LuaScriptEngine::OnSimulationStarted() {}
void LuaScriptEngine::OnSimulationStopped() {}
void LuaScriptEngine::Update(Scene&, float) {}
void LuaScriptEngine::LateUpdate(Scene&, float) {}
void LuaScriptEngine::DispatchAnimationFinished(Scene&) {}
void LuaScriptEngine::DispatchContactEvents(
    Scene&, const std::vector<Physics::PhysicsContactEvent>&) {}
void ScriptEngine::AwakeScripts() {}
void ScriptEngine::DestroyScripts() {}
void ScriptEngine::UpdateScripts(float) {}
void ScriptEngine::LateUpdateScripts(float) {}
void Hephaestus::CharacterAnimationSystem::Tick(Scene&, float) {}

int main()
{
    bool ok = true;
    const fs::path root = fs::temp_directory_path() / "hephaestus_runtime_scene_lifecycle_tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const fs::path source = root / "Restart.hpscene";

    Scene authored("Restart Test");
    Entity player = authored.CreateEntity("Player");
    player.GetComponent<TransformComponent>().position = Vec3(1.0f, 2.0f, 3.0f);
    Entity camera = authored.CreateEntity("Camera");
    camera.GetComponent<TransformComponent>().position = Vec3(10.0f, 4.0f, -2.0f);
    camera.AddComponent<CameraComponent>().isActive = true;
    std::string error;
    ok &= Check(SceneSerializer(authored).Serialize(source, error),
                "write authored test scene: " + error);

    Scene scene;
    ok &= Check(SceneSerializer::Deserialize(scene, source, error),
                "load authored scene: " + error);
    ok &= Check(scene.IsReloadable() && scene.GetSourceAssetPath() ==
                    fs::weakly_canonical(source).generic_string(),
                "loaded scene retains canonical authored source identity");

    Hephaestus::PlayModeManager play;
    play.SetScene(&scene);
    bool nestedReloadAccepted = true;
    play.SetSceneReloadCallback([&](std::string& outError) {
        nestedReloadAccepted = play.RequestSceneReload();
        return Reconstruct(scene, source, outError);
    });
    ok &= Check(play.StartPlayMode(), "start Play Mode");
    play.SetRealtime(false);

    const entt::entity oldPlayer = static_cast<entt::entity>(scene.FindEntityByName("Player"));
    Automation::EntityRefTable refs;
    const Automation::EntityRef oldRef = refs.Bind(scene, oldPlayer);
    scene.FindEntityByName("Player").GetComponent<TransformComponent>().position =
        Vec3(99.0f, 88.0f, 77.0f);
    const std::uint64_t generation = scene.GetRuntimeGeneration();
    ok &= Check(play.RequestSceneReload() && play.HasPendingSceneReload(),
                "reload request is accepted and remains pending until the host boundary");
    ok &= Check(scene.FindEntityByName("Player").GetComponent<TransformComponent>().position.x == 99.0f,
                "requesting reload does not destroy entities inside the caller stack");
    play.AdvanceFrameTime(1.0f / 60.0f);
    ok &= Check(play.ProcessSceneReload(), "process reload at the host boundary");
    ok &= Check(!nestedReloadAccepted && !play.HasPendingSceneReload(),
                "teardown cannot queue a nested scene reload");
    ok &= Check(scene.GetRuntimeGeneration() > generation && EntityCount(scene) == 2,
                "reconstruction advances generation without duplicating entities");
    ok &= Check(scene.FindEntityByName("Player").GetComponent<TransformComponent>().position.x == 1.0f,
                "authored player transform is restored");
    ok &= Check(refs.Resolve(oldRef, &scene).status == Automation::EntityResolutionStatus::Destroyed &&
                    !scene.GetRegistry().valid(oldPlayer),
                "prior-generation automation and EnTT entity references stay invalid");
    ok &= Check(!play.IsRealtime(), "manual execution mode survives scene reconstruction");
    ok &= Check(play.GetRuntimeCamera().posX == 10.0f && play.GetRuntimeCamera().posY == 4.0f,
                "runtime camera pose is reselected from the reconstructed scene");

    for (int reload = 0; reload < 3; ++reload) {
        scene.FindEntityByName("Player").GetComponent<TransformComponent>().position.x =
            200.0f + static_cast<float>(reload);
        ok &= Check(play.RequestSceneReload(), "repeat reload request accepted");
        ok &= Check(play.ProcessSceneReload(), "repeat reload completes");
        ok &= Check(EntityCount(scene) == 2 &&
                        scene.FindEntityByName("Player").GetComponent<TransformComponent>().position.x == 1.0f,
                    "repeated reload restores authored state with stable entity count");
    }

    play.StopPlayMode();
    ok &= Check(play.IsRealtime(), "Editor Stop restores realtime execution semantics");

    Scene inMemory("Unsaved");
    Hephaestus::PlayModeManager unsavedPlay;
    unsavedPlay.SetScene(&inMemory);
    unsavedPlay.SetSceneReloadCallback([](std::string&) { return true; });
    ok &= Check(unsavedPlay.StartPlayMode() && !unsavedPlay.RequestSceneReload(),
                "in-memory scene rejects reload without an authored source");
    unsavedPlay.StopPlayMode();

    Scene missingSource;
    ok &= Check(SceneSerializer::Deserialize(missingSource, source, error),
                "load scene before testing a removed source");
    Hephaestus::PlayModeManager missingPlay;
    missingPlay.SetScene(&missingSource);
    missingPlay.SetSceneReloadCallback([&](std::string& outError) {
        return Reconstruct(missingSource, source, outError);
    });
    ok &= Check(missingPlay.StartPlayMode(), "start Play Mode for missing-source check");
    fs::remove(source, ec);
    ok &= Check(missingPlay.RequestSceneReload() && missingPlay.ProcessSceneReload() &&
                    missingPlay.IsPaused() && EntityCount(missingSource) == 2,
                "missing source fails before teardown and freezes gameplay safely");
    missingPlay.StopPlayMode();

    fs::remove_all(root, ec);
    if (ok) {
        std::cout << "RuntimeSceneLifecycleTests passed\n";
        return 0;
    }
    return 1;
}
