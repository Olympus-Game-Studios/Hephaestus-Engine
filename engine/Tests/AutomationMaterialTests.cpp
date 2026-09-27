// Automation material-tool tests: create/get/set/assign through the real
// MaterialAssetSerializer and scene component paths, including undo/dirty hooks.

#include "Automation/AutomationService.h"
#include "Automation/MaterialAutomation.h"
#include "Automation/SceneAutomation.h"
#include "Assets/AssetManager.h"
#include "Scene/Scene.h"

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

class MaterialBackend final : public Hephaestus::Automation::AutomationBackend {
public:
    Scene scene{"Material Automation Scene"};
    int prepared = 0;
    int completed = 0;
    int structural = 0;
    int aborted = 0;

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
    void AbortEditorMutation() override { ++aborted; }
};

Hephaestus::Automation::ToolResult Call(Hephaestus::Automation::AutomationService& service,
                                        const std::string& name,
                                        Hephaestus::Automation::Json arguments)
{
    auto posted = service.Post({name, std::move(arguments)});
    service.Tick();
    return posted.result.get();
}

} // namespace

int main()
{
    bool ok = true;
    const fs::path project = fs::temp_directory_path() / "heph_automation_material_test";
    std::error_code ec;
    fs::remove_all(project, ec);
    fs::create_directories(project / "assets" / "materials");

    auto& assetManager = Hephaestus::AssetManager::GetInstance();
    assetManager.Clear();
    assetManager.SetProjectRoot(project.string());
    assetManager.SetAssetRoot((project / "assets").string());
    assetManager.Refresh();

    MaterialBackend backend;
    Hephaestus::Automation::AutomationService service(backend);
    Hephaestus::Automation::RegisterMaterialAutomation(service);
    Hephaestus::Automation::RegisterSceneAutomation(service);

    // ── create ─────────────────────────────────────────────────────────────
    auto created = Call(service, "material.create", {{"name", "AutomationMat"}});
    ok &= Check(created.Ok() &&
                    created.value["material"]["path"] == "assets/materials/AutomationMat.hematerial" &&
                    fs::exists(project / "assets" / "materials" / "AutomationMat.hematerial"),
                "material.create writes a valid material asset");
    ok &= Check(created.Ok() && created.value["properties"]["metallic"] == 0.0,
                "material.create returns default PBR properties");

    auto duplicate = Call(service, "material.create", {{"name", "AutomationMat"}});
    ok &= Check(!duplicate.Ok() &&
                    duplicate.error->code == Hephaestus::Automation::ErrorCode::InvalidArguments,
                "material.create rejects duplicate names");

    // ── get ────────────────────────────────────────────────────────────────
    auto fetched = Call(service, "material.get", {{"path", "assets/materials/AutomationMat.hematerial"}});
    ok &= Check(fetched.Ok() &&
                    fetched.value["material"]["displayName"] == "AutomationMat" &&
                    fetched.value["properties"]["roughness"] == 0.5,
                "material.get reads stored PBR properties");

    auto missing = Call(service, "material.get", {{"path", "assets/materials/Missing.hematerial"}});
    ok &= Check(!missing.Ok() &&
                    missing.error->code == Hephaestus::Automation::ErrorCode::InvalidArguments,
                "material.get rejects unknown assets");

    // ── set ────────────────────────────────────────────────────────────────
    auto set = Call(service, "material.set",
                    {{"path", "assets/materials/AutomationMat.hematerial"},
                     {"properties", {{"metallic", 0.9},
                                     {"roughness", 0.1},
                                     {"albedo", {0.2, 0.4, 0.6}},
                                     {"emissiveStrength", 2.0},
                                     {"useWorldAlignedTexture", true}}}});
    ok &= Check(set.Ok() && set.value["saved"] == true &&
                    std::abs(set.value["properties"]["metallic"].get<double>() - 0.9) < 1e-5 &&
                    std::abs(set.value["properties"]["albedo"][1].get<double>() - 0.4) < 1e-5,
                "material.set applies supported properties");

    auto reread = Call(service, "material.get", {{"path", "assets/materials/AutomationMat.hematerial"}});
    ok &= Check(reread.Ok() &&
                    std::abs(reread.value["properties"]["roughness"].get<double>() - 0.1) < 1e-5,
                "material.set persists through the serializer");

    auto unknown = Call(service, "material.set",
                        {{"path", "assets/materials/AutomationMat.hematerial"},
                         {"properties", {{"nonsense", 1.0}}}});
    ok &= Check(!unknown.Ok() &&
                    unknown.error->code == Hephaestus::Automation::ErrorCode::UnknownProperty,
                "material.set rejects unknown properties");

    auto outOfRange = Call(service, "material.set",
                           {{"path", "assets/materials/AutomationMat.hematerial"},
                            {"properties", {{"metallic", 5.0}}}});
    ok &= Check(!outOfRange.Ok() &&
                    outOfRange.error->code == Hephaestus::Automation::ErrorCode::InvalidArguments,
                "material.set rejects out-of-range values");

    auto badType = Call(service, "material.set",
                        {{"path", "assets/materials/AutomationMat.hematerial"},
                         {"properties", {{"roughness", "smooth"}}}});
    ok &= Check(!badType.Ok() &&
                    badType.error->code == Hephaestus::Automation::ErrorCode::InvalidArguments,
                "material.set rejects wrong value types");

    // ── assign ─────────────────────────────────────────────────────────────
    auto entity = Call(service, "entity.create", {{"name", "MaterialTarget"}});
    const std::string entityRef = entity.Ok() ? entity.value["entity"].get<std::string>() : "";
    ok &= Check(entity.Ok(), "entity.create provides an entity reference");

    auto added = Call(service,
                      "component.add",
                      {{"entity", entityRef}, {"type", "MeshRenderer"}});
    ok &= Check(added.Ok(), "component.add adds a MeshRenderer");

    const int preparedBeforeAssign = backend.prepared;
    const int completedBeforeAssign = backend.completed;
    auto assigned = Call(service, "material.assign",
                         {{"entity", entityRef}, {"path", "assets/materials/AutomationMat.hematerial"}});
    ok &= Check(assigned.Ok() && assigned.value["slot"] == 0 &&
                    assigned.value["material"]["path"] == "assets/materials/AutomationMat.hematerial",
                "material.assign reports the assigned slot");
    ok &= Check(backend.prepared == preparedBeforeAssign + 1 &&
                    backend.completed == completedBeforeAssign + 1,
                "material.assign participates in undo/dirty hooks");

    const auto& registry = backend.scene.GetRegistry();
    auto view = registry.view<MeshRendererComponent>();
    bool slotAssigned = false;
    for (entt::entity target : view) {
        const auto& renderer = view.get<MeshRendererComponent>(target);
        if (!renderer.appliedMaterialPaths.empty() &&
            renderer.appliedMaterialPaths[0] == "assets/materials/AutomationMat.hematerial") {
            slotAssigned = true;
        }
    }
    ok &= Check(slotAssigned, "material.assign writes the MeshRenderer material slot");

    auto badSlot = Call(service, "material.assign",
                        {{"entity", entityRef},
                         {"path", "assets/materials/AutomationMat.hematerial"},
                         {"slot", 9}});
    ok &= Check(!badSlot.Ok() &&
                    badSlot.error->code == Hephaestus::Automation::ErrorCode::PropertyOutOfRange,
                "material.assign rejects out-of-range slots");

    auto missingMaterial = Call(service, "material.assign",
                                {{"entity", entityRef},
                                 {"path", "assets/materials/Nope.hematerial"}});
    ok &= Check(!missingMaterial.Ok() &&
                    missingMaterial.error->code == Hephaestus::Automation::ErrorCode::InvalidArguments,
                "material.assign rejects unknown material assets");

    service.Shutdown();
    assetManager.Clear();
    fs::remove_all(project, ec);

    if (ok) std::cout << "[PASS] AutomationMaterialTests\n";
    return ok ? 0 : 1;
}
