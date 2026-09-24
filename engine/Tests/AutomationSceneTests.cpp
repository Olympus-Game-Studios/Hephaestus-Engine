#include "Automation/AutomationService.h"
#include "Automation/SceneAutomation.h"
#include "Scene/Scene.h"

#include <cmath>
#include <iostream>
#include <string>

namespace Automation = Hephaestus::Automation;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

class SceneBackend final : public Automation::AutomationBackend {
public:
    Scene scene{"Automation Test Scene"};
    int prepared = 0;
    int completed = 0;
    int structural = 0;
    int aborted = 0;
    int saves = 0;

    Automation::HostMask Host() const noexcept override { return Automation::HostMask::Editor; }
    Automation::SceneAccess ActiveScene() noexcept override { return {&scene}; }
    Hephaestus::PlayState GetPlayState() const noexcept override
    {
        return Hephaestus::PlayState::Editor;
    }
    Automation::Json GetProjectInfo() const override
    {
        return {{"name", "AutomationFixture"}, {"path", "fixture"}};
    }
    std::string ActiveScenePath() const override { return "assets/scenes/Automation.hpscene"; }
    bool SaveActiveScene(std::string&) override
    {
        ++saves;
        return true;
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
    void AbortEditorMutation() override { ++aborted; }
};

Automation::ToolResult Call(Automation::AutomationService& service, std::string name,
                            Automation::Json arguments = Automation::Json::object())
{
    auto call = service.Post({std::move(name), std::move(arguments)});
    service.Tick();
    return call.result.get();
}

} // namespace

int main()
{
    bool ok = true;
    SceneBackend backend;
    Automation::AutomationService service(backend);
    Automation::RegisterSceneAutomation(service);

    auto project = Call(service, "project.get_info");
    ok &= Check(project.Ok() && project.value["name"] == "AutomationFixture",
                "Project info comes from the host backend");

    auto current = Call(service, "scene.get_current");
    ok &= Check(current.Ok() && current.value["name"] == "Automation Test Scene",
                "Current scene inspects the live Scene");

    auto first = Call(service, "entity.create", {{"name", "DuplicateName"}});
    auto second = Call(service, "entity.create", {{"name", "DuplicateName"}});
    ok &= Check(first.Ok() && second.Ok(), "Entities are created through Scene");
    const std::string firstRef = first.value["entity"].get<std::string>();
    const std::string secondRef = second.value["entity"].get<std::string>();
    ok &= Check(firstRef != secondRef && firstRef.starts_with("entity-"),
                "External entity references are opaque and distinct");

    auto found = Call(service, "entity.find", {{"name", "DuplicateName"}});
    ok &= Check(found.Ok() && found.value["entities"].size() == 2,
                "Entity name lookup returns every ambiguous match");

    auto transformed = Call(service, "entity.set_transform",
                            {{"entity", firstRef},
                             {"position", Automation::Json::array({1.0, 2.0, 3.0})},
                             {"rotation", Automation::Json::array({0.1, 0.2, 0.3})},
                             {"scale", Automation::Json::array({2.0, 2.0, 2.0})}});
    ok &= Check(transformed.Ok() && transformed.value["transform"]["position"][1] == 2.0,
                "Transform mutation updates the live component");

    auto addLight = Call(service, "component.add", {{"entity", firstRef}, {"type", "Light"}});
    ok &= Check(addLight.Ok(), "Generic component IO adds a Light component");
    const int preparedBeforeUnknown = backend.prepared;
    auto unknown = Call(service, "component.add", {{"entity", firstRef}, {"type", "Unknown"}});
    ok &= Check(!unknown.Ok() && unknown.error->code == Automation::ErrorCode::UnknownComponent &&
                    backend.prepared == preparedBeforeUnknown,
                "Unknown components fail before opening an editor mutation");
    auto components = Call(service, "component.list", {{"entity", firstRef}});
    ok &= Check(components.Ok() && components.value["components"].size() == 2,
                "Component listing reflects the live entity");
    auto light = Call(service, "component.get", {{"entity", firstRef}, {"type", "Light"}});
    ok &= Check(light.Ok() && light.value["data"].is_object(),
                "Generic component IO captures live component data");
    auto removeLight = Call(service, "component.remove", {{"entity", firstRef}, {"type", "Light"}});
    ok &= Check(removeLight.Ok(), "Generic component IO removes a Light component");

    auto listed = Call(service, "scene.list_entities");
    ok &= Check(listed.Ok() && listed.value["entities"].size() == 2,
                "Scene listing returns live entities");

    auto saved = Call(service, "scene.save");
    ok &= Check(saved.Ok() && backend.saves == 1, "Scene save delegates to the host subsystem");

    auto prefabInfo = Call(service, "prefab.get_info", {{"entity", secondRef}});
    ok &= Check(prefabInfo.Ok() && prefabInfo.value["isPrefabNode"] == false &&
                    prefabInfo.value["isPrefabInstance"] == false,
                "prefab.get_info reports plain entities as non-prefab");
    auto prefabRevert = Call(service, "prefab.revert", {{"entity", secondRef}});
    ok &= Check(!prefabRevert.Ok() &&
                    prefabRevert.error->code == Automation::ErrorCode::ToolUnavailable,
                "prefab.revert rejects non-prefab entities");
    auto prefabOverrides = Call(service, "prefab.list_overrides", {{"entity", secondRef}});
    ok &= Check(!prefabOverrides.Ok() &&
                    prefabOverrides.error->code == Automation::ErrorCode::ToolUnavailable,
                "prefab.list_overrides rejects non-prefab entities");

    auto destroyed = Call(service, "entity.destroy", {{"entity", firstRef}});
    ok &= Check(destroyed.Ok(), "Entity destruction uses Scene");
    auto stale = Call(service, "entity.get", {{"entity", firstRef}});
    ok &= Check(!stale.Ok() && stale.error->code == Automation::ErrorCode::EntityRefExpired,
                "Destroyed entity references expire");
    auto survivor = Call(service, "entity.get", {{"entity", secondRef}});
    ok &= Check(survivor.Ok(), "Unrelated entity reference remains valid");

    ok &= Check(backend.prepared == backend.completed,
                "Every successful mutation completes the host mutation hook");
    ok &= Check(backend.aborted == 0, "No successful test mutation is aborted");
    ok &= Check(backend.structural == 5,
                "Create, add/remove component refresh policy, and destroy are tracked");

    service.Shutdown();
    if (ok) {
        std::cout << "AutomationSceneTests passed\n";
        return 0;
    }
    return 1;
}
