#include "Assets/AssetTypes.h"
#include "Core/SimulationStep.h"
#include "Physics/PhysicsContactEvents.h"
#include "Plugins/PluginContent.h"
#include "Plugins/PluginManifest.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scripting/LuaScriptComponent.h"
#include "Scripting/LuaScriptEngine.h"
#include "Scripting/PluginScriptHost.h"
#include "Scripting/ScriptLanguageRegistry.h"
#include "ScriptLanguageTestPlugin.h"

#include <chrono>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

extern "C" const HPScriptLanguage* const* HP_GetScriptLanguages(uint32_t hostApiVersion, uint32_t* count);

namespace {

namespace fs = std::filesystem;

bool Check(bool condition, const std::string& message)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << "\n";
    return condition;
}

void WriteFile(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::trunc) << text;
}

bool Near(float a, float b)
{
    return std::fabs(a - b) < 0.0001f;
}

bool RejectsLanguage(const HPScriptLanguage& language)
{
    std::string error;
    return !ScriptLanguageRegistry::Instance().Register(&language, error) && !error.empty();
}

} // namespace

int main(int argc, char** argv)
{
    bool pass = true;
    auto& registry = ScriptLanguageRegistry::Instance();
    uint32_t count = 0;
    const HPScriptLanguage* language = HP_GetScriptLanguages(HP_SCRIPT_LANGUAGE_API_VERSION, &count)[0];
    TestStats& stats = *TestLanguage_Stats();

    // ── Registration rules ──────────────────────────────────────────────────
    HPScriptLanguage broken = *language;
    broken.apiVersion = HP_SCRIPT_LANGUAGE_API_VERSION + 1;
    pass &= Check(RejectsLanguage(broken), "a language built for another API version is rejected");
    broken = *language;
    broken.createInstance = nullptr;
    pass &= Check(RejectsLanguage(broken), "a language missing a required callback is rejected");
    broken = *language;
    broken.id = "lua";
    pass &= Check(RejectsLanguage(broken), "a plugin cannot replace built-in Lua");
    broken = *language;
    broken.extensions = ".lua";
    pass &= Check(RejectsLanguage(broken), "a plugin cannot claim .lua files");

    std::string error;
    HPScriptLanguage firstRelease = *language;
    firstRelease.id = "firstrelease";
    firstRelease.extensions = ".v1script";
    firstRelease.structSize = static_cast<uint32_t>(offsetof(HPScriptLanguage, syntax));
    pass &= Check(registry.Register(&firstRelease, error) && registry.FindSyntaxForPath("a.v1script") == nullptr,
                  "a language built before syntax tables existed still registers, as plain text");
    registry.Clear();
    pass &= Check(registry.Register(language, error), "the C test language registers");
    pass &= Check(RejectsLanguage(*language), "the same language id cannot register twice");
    broken = *language;
    broken.id = "other";
    pass &= Check(RejectsLanguage(broken), "two languages cannot claim the same extension");

    // ── Routing by extension ────────────────────────────────────────────────
    pass &= Check(registry.FindForPath("assets/scripts/mover.tscript") == language &&
                  registry.FindForPath("MOVER.TSCRIPT") == language &&
                  registry.FindForPath("ui.tsx") == language,
                  "script paths route to the language by extension, ignoring case");
    pass &= Check(registry.FindForPath("player.lua") == nullptr && registry.FindForPath("noext") == nullptr,
                  "Lua and extensionless paths stay with built-in Lua");
    pass &= Check(registry.IsScriptPath("a.lua") && registry.IsScriptPath("a.tscript") &&
                  !registry.IsScriptPath("a.txt"),
                  "IsScriptPath accepts Lua and plugin languages only");
    const HPSyntax* luaSyntax = registry.FindSyntaxForPath("scripts/door.LUA");
    pass &= Check(luaSyntax && luaSyntax->ruleCount > 0 && luaSyntax->blockCount > 0,
                  "built-in Lua supplies its highlighting through the same syntax table");
    pass &= Check(registry.FindSyntaxForPath("a.tscript") == language->syntax &&
                  registry.FindSyntaxForPath("a.txt") == nullptr,
                  "a plugin language supplies highlighting for its extensions");
    pass &= Check(AssetTypeFromExtension(".tsx") == AssetType::Script,
                  "plugin language files show up as Script assets");

    const fs::path root = fs::temp_directory_path() / "hephaestus_script_language_tests";
    fs::remove_all(root);
    const fs::path moverPath = root / "mover.tscript";
    WriteFile(moverPath, "ok");
    WriteFile(root / "broken.tscript", "fail");

    LuaExposeScanResult fields;
    pass &= Check(ScriptLanguageRegistry::DescribeFields(*language, moverPath, fields) && fields.valid &&
                  fields.fields.size() == 2 && fields.fields[0].name == "speed" &&
                  fields.fields[0].type == LuaExposeType::Number &&
                  fields.fields[1].type == LuaExposeType::String && !fields.contentHash.empty(),
                  "a language describes inspector fields without a runtime");

    // ── Lifecycle inside the simulation step ────────────────────────────────
    {
        Scene scene("ScriptLanguages");
        Entity mover = scene.CreateEntity("Mover");
        auto& moverScript = mover.AddComponent<LuaScriptComponent>(moverPath.string());
        moverScript.variables["speed"] = PropertyValue(2.0);
        Entity target = scene.CreateEntity("LuaTarget");
        target.AddComponent<LuaScriptComponent>().scriptSource = R"lua(
function onPing(n) _G.pinged = n end
function onUpdate(dt)
    if not _G.bumped then
        local movers = Scene.findAllWithPrefix("Mover")
        _G.bumped = movers[1] ~= nil and movers[1]:Call("bump", 2)
    end
end
)lua";
        scene.CreateEntity("Broken").AddComponent<LuaScriptComponent>((root / "broken.tscript").string());

        LuaScriptEngine lua;
        PluginScriptHost host;
        lua.Initialize(&scene, nullptr);
        lua.SetPluginScriptHost(&host);
        host.SetLuaEngine(&lua);
        lua.OnSimulationStarted();

        Hephaestus::SimulationStepContext step;
        step.scene = &scene;
        step.lua = &lua;
        step.pluginScripts = &host;
        Hephaestus::RunSimulationStep(step, 0.5f);
        Hephaestus::RunSimulationStep(step, 0.5f);

        pass &= Check(stats.runtimesCreated == 1, "one runtime starts for the language");
        pass &= Check(stats.instancesCreated == 1 && stats.awakes == 1,
                      "the valid script loads and wakes once");
        pass &= Check(stats.updates == 2 && stats.lateUpdates == 2,
                      "update and late update run every frame");
        const Vec3 position = mover.GetComponent<TransformComponent>().position;
        pass &= Check(Near(position.x, 2.0f), "the script reads its speed variable and moves through the host API");
        pass &= Check(lua.ExecuteString("assert(_G.pinged == 7)"), "a plugin script calls a Lua function with arguments");
        pass &= Check(lua.ExecuteString("assert(_G.bumped == true)") && Near(position.y, 2.0f),
                      "a Lua script calls a plugin script through entity:Call");
        pass &= Check(!lua.CallEntityFunction("bump", mover, {PropertyValue(1.0)}),
                      "Lua does not run scripts a plugin language claims");

        Hephaestus::RunSimulationStep(step, 0.5f);
        pass &= Check(stats.createAttempts == 2, "a script that failed to load is not retried until it changes");

        Physics::PhysicsContactEvent contact;
        contact.kind = Physics::PhysicsEventKind::CollisionEnter;
        contact.receiver = static_cast<uint32_t>(static_cast<entt::entity>(mover));
        host.DispatchContactEvents(scene, {contact});
        pass &= Check(stats.contacts == 1 && stats.lastContactKind == HP_CONTACT_COLLISION_ENTER,
                      "contact events reach plugin scripts");

        fs::last_write_time(moverPath, fs::last_write_time(moverPath) + std::chrono::seconds(2));
        Hephaestus::RunSimulationStep(step, 0.5f);
        pass &= Check(stats.instancesDestroyed == 1 && stats.instancesCreated == 2 && stats.awakes == 2,
                      "editing the script file reloads it");

        mover.GetComponent<LuaScriptComponent>().enabled = false;
        Hephaestus::RunSimulationStep(step, 0.5f);
        pass &= Check(stats.instancesDestroyed == 2 && host.InstanceCount() == 0,
                      "disabling the component destroys its script");

        mover.GetComponent<LuaScriptComponent>().enabled = true;
        Hephaestus::RunSimulationStep(step, 0.5f);
        host.OnSimulationStopped();
        pass &= Check(stats.instancesDestroyed == 3 && stats.runtimesDestroyed == 1 && host.InstanceCount() == 0,
                      "stopping play destroys every script and the runtime");
        lua.OnSimulationStopped();
        lua.Shutdown();
    }

    // ── Loading a language module from disk ─────────────────────────────────
    if (argc > 1) {
        registry.Clear();
        const fs::path module = argv[1];
        error.clear();
        const bool loaded = registry.LoadModule(module, error);
        if (!loaded) std::cerr << error << "\n";
        pass &= Check(loaded && registry.FindForPath("x.tscript") != nullptr &&
                      registry.FindForPath("x.tscript") != language,
                      "a shared language module loads and registers its languages");
        pass &= Check(registry.LoadModule(module, error), "loading the same module again is harmless");
        pass &= Check(!registry.LoadModule(root / "missing.dll", error), "a missing module reports an error");
    } else {
        pass &= Check(false, "the test language module path was passed on the command line");
    }

    // ── Manifests and packaged games ────────────────────────────────────────
    WriteFile(root / "Lang" / "Lang.hpplugin", R"({"version":1,"id":"com.example.lang","name":"Lang",
        "pluginVersion":"1.0.0","engineVersion":"*","type":"runtime","runtimeEntry":"bin/Lang.dll"})");
    const auto runtimeOnly = Hephaestus::Plugins::ParsePluginManifest(root / "Lang" / "Lang.hpplugin");
    pass &= Check(runtimeOnly.success && runtimeOnly.manifest.entry.empty() &&
                  runtimeOnly.manifest.runtimeEntry == fs::path("bin/Lang.dll"),
                  "a runtime-only plugin needs no editor entry");
    WriteFile(root / "Bad" / "Bad.hpplugin", R"({"version":1,"id":"com.example.bad","name":"Bad",
        "pluginVersion":"1.0.0","engineVersion":"*","runtimeEntry":"../escape.dll"})");
    pass &= Check(!Hephaestus::Plugins::ParsePluginManifest(root / "Bad" / "Bad.hpplugin").success,
                  "runtimeEntry cannot leave the plugin folder");

    const fs::path package = root / "Package";
    pass &= Check(Hephaestus::Plugins::WriteStagedPluginMounts(package, {
                      {"com.example.lang", {}, "Plugins/com.example.lang/bin/Lang.dll"},
                      {"com.example.content", "Plugins/com.example.content/Content", {}}}, error),
                  "staged plugin mounts are written");
    const auto modules = Hephaestus::Plugins::StagedRuntimeModules(package);
    pass &= Check(modules.size() == 1 && modules[0] == package / "Plugins/com.example.lang/bin/Lang.dll",
                  "the packaged game finds staged runtime modules");

    fs::remove_all(root);
    std::cout << (pass ? "[PASS] ScriptLanguagePluginTests\n" : "[FAIL] ScriptLanguagePluginTests\n");
    return pass ? 0 : 1;
}
