// Automation script-tool tests: attach/get/set_property/reload/remove through
// LuaScriptComponent, LuaExposeScanner, and LuaScriptVariableReconciler.

#include "Automation/AutomationService.h"
#include "Automation/SceneAutomation.h"
#include "Automation/ScriptAutomation.h"
#include "Assets/AssetManager.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Scripting/LuaScriptComponent.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

void WriteText(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

class ScriptBackend final : public Hephaestus::Automation::AutomationBackend {
public:
    Scene scene{"Script Automation Scene"};
    int prepared = 0;
    int completed = 0;
    int structural = 0;
    int reloadCalls = 0;

    Hephaestus::Automation::HostMask Host() const noexcept override
    {
        return Hephaestus::Automation::HostMask::Editor;
    }
    Hephaestus::Automation::SceneAccess ActiveScene() noexcept override { return {&scene}; }
    Hephaestus::PlayState GetPlayState() const noexcept override
    {
        return Hephaestus::PlayState::Editor;
    }
    Hephaestus::AssetManager* Assets() noexcept override
    {
        return &Hephaestus::AssetManager::GetInstance();
    }
    bool PrepareEditorMutation(std::string_view, std::string&) override
    {
        ++prepared;
        return true;
    }
    void CompleteEditorMutation(bool isStructural) override
    {
        ++completed;
        if (isStructural) ++structural;
    }
    bool RequestScriptReload(std::string& outError) override
    {
        ++reloadCalls;
        outError.clear();
        return true;
    }
};

Hephaestus::Automation::ToolResult Call(Hephaestus::Automation::AutomationService& service,
                                        const std::string& name,
                                        Hephaestus::Automation::Json arguments)
{
    auto posted = service.Post({name, std::move(arguments)});
    service.Tick();
    return posted.result.get();
}

const Hephaestus::Automation::Json* FindProperty(const Hephaestus::Automation::Json& properties,
                                                 const std::string& name)
{
    for (const auto& property : properties) {
        if (property.value("name", std::string()) == name) return &property;
    }
    return nullptr;
}

} // namespace

int main()
{
    bool ok = true;
    const fs::path project = fs::temp_directory_path() / "heph_automation_script_test";
    std::error_code ec;
    fs::remove_all(project, ec);
    WriteText(project / "assets" / "scripts" / "Demo.lua",
              "-- automation fixture\n"
              "Expose.number(\"speed\", 1.0)\n"
              "Expose.bool(\"enabled\", true)\n"
              "Expose.string(\"label\", \"hello\")\n"
              "Expose.vec3(\"offset\", 0.0, 1.0, 2.0)\n"
              "Expose.integer(\"count\", 3)\n"
              "Expose.color(\"tint\", 1.0, 1.0, 1.0, 1.0)\n");

    auto& assetManager = Hephaestus::AssetManager::GetInstance();
    assetManager.Clear();
    assetManager.SetProjectRoot(project.string());
    assetManager.SetAssetRoot((project / "assets").string());
    assetManager.Refresh();

    ScriptBackend backend;
    Hephaestus::Automation::AutomationService service(backend);
    Hephaestus::Automation::RegisterSceneAutomation(service);
    Hephaestus::Automation::RegisterScriptAutomation(service);

    auto entity = Call(service, "entity.create", {{"name", "ScriptTarget"}});
    const std::string entityRef = entity.Ok() ? entity.value["entity"].get<std::string>() : "";
    ok &= Check(entity.Ok(), "entity.create provides an entity reference");

    // ── get without a script ───────────────────────────────────────────────
    auto detached = Call(service, "script.get", {{"entity", entityRef}});
    ok &= Check(detached.Ok() && detached.value["attached"] == false,
                "script.get reports detached entities");

    // ── attach ─────────────────────────────────────────────────────────────
    const int preparedBeforeAttach = backend.prepared;
    auto attached = Call(service, "script.attach",
                         {{"entity", entityRef}, {"path", "assets/scripts/Demo.lua"}});
    ok &= Check(attached.Ok() && attached.value["attached"] == true &&
                    attached.value["script"]["path"] == "assets/scripts/Demo.lua",
                "script.attach sets the LuaScriptComponent path");
    ok &= Check(attached.Ok() && attached.value["properties"].size() == 6,
                "script.attach seeds exposed properties");
    ok &= Check(FindProperty(attached.value["properties"], "speed") != nullptr &&
                    (*FindProperty(attached.value["properties"], "speed"))["type"] == "number",
                "script.attach exposes scanner metadata");
    ok &= Check(backend.prepared == preparedBeforeAttach + 1 && backend.completed > 0,
                "script.attach participates in undo/dirty hooks");

    // ── get attached ───────────────────────────────────────────────────────
    auto inspected = Call(service, "script.get", {{"entity", entityRef}});
    ok &= Check(inspected.Ok() && inspected.value["attached"] == true &&
                    inspected.value["valid"] == true,
                "script.get reports attached script metadata");
    ok &= Check(FindProperty(inspected.value["properties"], "offset") != nullptr,
                "script.get lists vec3 exposed properties");

    // ── set_property ───────────────────────────────────────────────────────
    auto setNumber = Call(service, "script.set_property",
                          {{"entity", entityRef}, {"name", "speed"}, {"value", 2.5}});
    ok &= Check(setNumber.Ok() && setNumber.value["value"] == 2.5 &&
                    setNumber.value["authored"] == true,
                "script.set_property sets an authored number");

    auto setVec = Call(service, "script.set_property",
                       {{"entity", entityRef}, {"name", "offset"}, {"value", {3.0, 4.0, 5.0}}});
    ok &= Check(setVec.Ok() && setVec.value["value"][2] == 5.0,
                "script.set_property sets a vec3 value");

    auto setBool = Call(service, "script.set_property",
                        {{"entity", entityRef}, {"name", "enabled"}, {"value", false}});
    ok &= Check(setBool.Ok() && setBool.value["value"] == false,
                "script.set_property sets a bool value");

    auto unknownProperty = Call(service, "script.set_property",
                                {{"entity", entityRef}, {"name", "missing"}, {"value", 1}});
    ok &= Check(!unknownProperty.Ok() &&
                    unknownProperty.error->code == Hephaestus::Automation::ErrorCode::UnknownProperty,
                "script.set_property rejects unknown properties");

    auto wrongType = Call(service, "script.set_property",
                          {{"entity", entityRef}, {"name", "speed"}, {"value", "fast"}});
    ok &= Check(!wrongType.Ok() &&
                    wrongType.error->code == Hephaestus::Automation::ErrorCode::InvalidPropertyType,
                "script.set_property rejects incompatible types");

    auto wrongInteger = Call(service, "script.set_property",
                             {{"entity", entityRef}, {"name", "count"}, {"value", 2.5}});
    ok &= Check(!wrongInteger.Ok() &&
                    wrongInteger.error->code == Hephaestus::Automation::ErrorCode::InvalidPropertyType,
                "script.set_property enforces integer values");

    // ── reload ─────────────────────────────────────────────────────────────
    // Rewrite the script with an extra Expose field and reload.
    WriteText(project / "assets" / "scripts" / "Demo.lua",
              "-- automation fixture v2\n"
              "Expose.number(\"speed\", 1.0)\n"
              "Expose.bool(\"enabled\", true)\n"
              "Expose.string(\"label\", \"hello\")\n"
              "Expose.vec3(\"offset\", 0.0, 1.0, 2.0)\n"
              "Expose.integer(\"count\", 3)\n"
              "Expose.color(\"tint\", 1.0, 1.0, 1.0, 1.0)\n"
              "Expose.number(\"extra\", 9.0)\n");
    auto reloaded = Call(service, "script.reload", {{"entity", entityRef}});
    ok &= Check(reloaded.Ok() && reloaded.value["runtime"]["available"] == true &&
                    backend.reloadCalls == 1,
                "script.reload requests a runtime reload through the host");
    ok &= Check(reloaded.Ok() && !reloaded.value["updated"].empty() &&
                    reloaded.value["updated"][0]["changed"] == true,
                "script.reload reconciles newly declared Expose fields");

    auto afterReload = Call(service, "script.get", {{"entity", entityRef}});
    ok &= Check(FindProperty(afterReload.value["properties"], "extra") != nullptr,
                "script.reload adds the new exposed property");
    ok &= Check(FindProperty(afterReload.value["properties"], "speed") != nullptr &&
                    (*FindProperty(afterReload.value["properties"], "speed"))["authored"] == true,
                "script.reload preserves authored values");

    // ── properties survive in the component ────────────────────────────────
    auto& registry = backend.scene.GetRegistry();
    bool hasComponent = false;
    auto view = registry.view<LuaScriptComponent>();
    for (entt::entity handle : view) {
        const auto& component = view.get<LuaScriptComponent>(handle);
        if (component.variables.count("speed") != 0 &&
            component.variables.at("speed").IsNumber() &&
            *component.variables.at("speed").AsNumber() == 2.5 &&
            component.authoredVariables.count("speed") != 0) {
            hasComponent = true;
        }
    }
    ok &= Check(hasComponent, "script.set_property stores an authored component value");

    // ── remove ─────────────────────────────────────────────────────────────
    const int preparedBeforeRemove = backend.prepared;
    auto removed = Call(service, "script.remove", {{"entity", entityRef}});
    ok &= Check(removed.Ok() && removed.value["removed"] == true &&
                    backend.prepared == preparedBeforeRemove + 1,
                "script.remove participates in undo/dirty hooks");

    auto afterRemove = Call(service, "script.get", {{"entity", entityRef}});
    ok &= Check(afterRemove.Ok() && afterRemove.value["attached"] == false,
                "script.remove detaches the component");

    auto removeAgain = Call(service, "script.remove", {{"entity", entityRef}});
    ok &= Check(!removeAgain.Ok() &&
                    removeAgain.error->code == Hephaestus::Automation::ErrorCode::ComponentMissing,
                "script.remove reports a missing component");

    service.Shutdown();
    assetManager.Clear();
    fs::remove_all(project, ec);

    if (ok) std::cout << "[PASS] AutomationScriptTests\n";
    return ok ? 0 : 1;
}
