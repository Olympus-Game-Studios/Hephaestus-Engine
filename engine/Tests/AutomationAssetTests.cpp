// Automation asset-tool tests: list/find/get_info plus transactional
// rename/move/trash/restore and deferred import/reimport through the real
// AssetManager, AssetOperationService, EditorAssetPipeline, and
// EditorAssetImportService.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Automation/AssetAutomation.h"
#include "Automation/AutomationService.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetOperationService.h"
#include "Assets/AssetReferenceIndex.h"
#include "Assets/EditorAssetImportService.h"
#include "Assets/EditorAssetPipeline.h"
#include "Assets/OpenDocumentRegistry.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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

class AssetBackend final : public Hephaestus::Automation::AutomationBackend {
public:
    AssetBackend(Hephaestus::AssetOperationService& operations,
                 Hephaestus::AssetReferenceIndex& references,
                 Hephaestus::Automation::IAssetImportService* import,
                 AssetDatabase* database)
        : m_operations(&operations), m_references(&references), m_import(import),
          m_database(database)
    {
    }

    Hephaestus::Automation::HostMask Host() const noexcept override
    {
        return Hephaestus::Automation::HostMask::Editor;
    }
    Hephaestus::Automation::SceneAccess ActiveScene() noexcept override { return {}; }
    Hephaestus::PlayState GetPlayState() const noexcept override { return m_playState; }
    Hephaestus::AssetManager* Assets() noexcept override
    {
        return &Hephaestus::AssetManager::GetInstance();
    }
    Hephaestus::AssetOperationService* AssetOperations() noexcept override { return m_operations; }
    Hephaestus::AssetReferenceIndex* AssetReferences() noexcept override { return m_references; }
    Hephaestus::Automation::IAssetImportService* AssetImport() noexcept override { return m_import; }
    AssetDatabase* ImportDatabase() noexcept override { return m_database; }
    void NotifyAssetChanged() override { ++m_notifyCount; }
    bool PrepareEditorMutation(std::string_view, std::string&) override { return true; }

    void SetPlayState(Hephaestus::PlayState state) { m_playState = state; }
    int NotifyCount() const { return m_notifyCount; }

private:
    Hephaestus::AssetOperationService* m_operations = nullptr;
    Hephaestus::AssetReferenceIndex* m_references = nullptr;
    Hephaestus::Automation::IAssetImportService* m_import = nullptr;
    AssetDatabase* m_database = nullptr;
    Hephaestus::PlayState m_playState = Hephaestus::PlayState::Editor;
    int m_notifyCount = 0;
};

Hephaestus::Automation::ToolResult Call(Hephaestus::Automation::AutomationService& service,
                                        const std::string& name,
                                        Hephaestus::Automation::Json arguments =
                                            Hephaestus::Automation::Json::object())
{
    auto posted = service.Post({name, std::move(arguments)});
    service.Tick();
    return posted.result.get();
}

Hephaestus::Automation::ToolResult CallDeferred(Hephaestus::Automation::AutomationService& service,
                                                const std::string& name,
                                                Hephaestus::Automation::Json arguments,
                                                std::chrono::milliseconds timeout)
{
    auto posted = service.Post({name, std::move(arguments)});
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        service.Tick();
        if (posted.result.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            return posted.result.get();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return posted.result.get();
}

const Hephaestus::Automation::Json* FindAsset(const Hephaestus::Automation::Json& assets,
                                              const std::string& path)
{
    for (const auto& asset : assets) {
        if (asset.value("path", std::string()) == path) return &asset;
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    bool ok = true;
    const std::string repoRoot = argc > 1 ? argv[1] : ".";
    const fs::path project = fs::temp_directory_path() / "heph_automation_asset_test";
    std::error_code ec;
    fs::remove_all(project, ec);
    fs::create_directories(project / "assets" / "materials");
    fs::create_directories(project / "assets" / "scripts");

    WriteText(project / "assets" / "materials" / "Test.hematerial",
              "{ \"name\": \"Test\", \"metallic\": 0.0, \"roughness\": 0.5 }\n");
    WriteText(project / "assets" / "materials" / "Second.hematerial",
              "{ \"name\": \"Second\" }\n");
    WriteText(project / "assets" / "scripts" / "Foo.lua",
              "-- automation fixture script\nreturn {}\n");

    auto& assetManager = Hephaestus::AssetManager::GetInstance();
    assetManager.Clear();
    assetManager.SetProjectRoot(project.string());
    assetManager.SetAssetRoot((project / "assets").string());
    assetManager.Refresh();

    Hephaestus::AssetReferenceIndex references;
    references.ScanProject(project);
    Hephaestus::OpenDocumentRegistry documents;
    Hephaestus::AssetOperationService operations;
    operations.SetProjectRoot(project);
    operations.SetReferenceIndex(&references);
    operations.SetDocumentRegistry(&documents);

    EditorAssetPipeline pipeline;
    if (!pipeline.Initialize(project)) {
        std::cerr << "[FAIL] pipeline initialization\n";
        return 1;
    }
    EditorAssetImportService importService(pipeline);
    AssetDatabase* database = &pipeline.GetDatabase();

    AssetBackend backend(operations, references, &importService, database);
    Hephaestus::Automation::AutomationService service(backend);
    Hephaestus::Automation::RegisterAssetAutomation(service);

    // ── list ───────────────────────────────────────────────────────────────
    auto listed = Call(service, "asset.list", {{"recursive", true}});
    ok &= Check(listed.Ok() && listed.value.contains("assets"),
                "asset.list returns an assets array");
    ok &= Check(FindAsset(listed.value["assets"], "assets/materials/Test.hematerial") != nullptr,
                "asset.list finds the fixture material");

    auto typed = Call(service, "asset.list", {{"recursive", true}, {"type", "Material"}});
    ok &= Check(typed.Ok() && typed.value["assets"].size() == 2,
                "asset.list type filter returns only materials");

    auto recursive = Call(service, "asset.list", {{"recursive", true}, {"name", "Second"}});
    ok &= Check(recursive.Ok() && recursive.value["assets"].size() == 1 &&
                    recursive.value["assets"][0]["path"] == "assets/materials/Second.hematerial",
                "asset.list recursive + name filter");

    // ── find ───────────────────────────────────────────────────────────────
    auto found = Call(service, "asset.find", {{"query", "Test"}, {"type", "material"}});
    ok &= Check(found.Ok() && found.value["total"] == 1 &&
                    found.value["matches"][0]["path"] == "assets/materials/Test.hematerial",
                "asset.find locates by query and type");

    auto missingQuery = Call(service, "asset.find", Hephaestus::Automation::Json::object());
    ok &= Check(!missingQuery.Ok() &&
                    missingQuery.error->code == Hephaestus::Automation::ErrorCode::InvalidArguments,
                "asset.find requires a query");

    // ── get_info ───────────────────────────────────────────────────────────
    auto info = Call(service, "asset.get_info", {{"path", "assets/materials/Test.hematerial"}});
    ok &= Check(info.Ok() && info.value["asset"]["type"] == "Material" &&
                    info.value["portableKey"] == "assets/materials/Test.hematerial",
                "asset.get_info reports portable key and type");

    auto relativeInfo = Call(service, "asset.get_info", {{"path", "materials/Test.hematerial"}});
    ok &= Check(relativeInfo.Ok() &&
                    relativeInfo.value["portableKey"] == "assets/materials/Test.hematerial",
                "asset.get_info accepts asset-root-relative paths");

    auto missing = Call(service, "asset.get_info", {{"path", "assets/materials/Nope.hematerial"}});
    ok &= Check(!missing.Ok() &&
                    missing.error->code == Hephaestus::Automation::ErrorCode::InvalidArguments,
                "asset.get_info rejects unknown assets");

    // ── rename / move ──────────────────────────────────────────────────────
    auto renamed = Call(service, "asset.rename",
                        {{"path", "assets/materials/Second.hematerial"},
                         {"newName", "Renamed.hematerial"}});
    ok &= Check(renamed.Ok() &&
                    fs::exists(project / "assets" / "materials" / "Renamed.hematerial") &&
                    !fs::exists(project / "assets" / "materials" / "Second.hematerial"),
                "asset.rename moves the file through AssetOperationService");

    auto moved = Call(service, "asset.move",
                      {{"path", "assets/materials/Renamed.hematerial"}, {"destination", "scripts"}});
    ok &= Check(moved.Ok() && fs::exists(project / "assets" / "scripts" / "Renamed.hematerial") &&
                    !fs::exists(project / "assets" / "materials" / "Renamed.hematerial"),
                "asset.move relocates the asset");

    // ── destination escape rejection ───────────────────────────────────────
    const fs::path pngSource = fs::path(repoRoot) / "assets" / "textures" / "light_bulb.png";
    auto escape = Call(service, "asset.import",
                       {{"source", pngSource.string()}, {"destination", "../../escape"}});
    ok &= Check(!escape.Ok() &&
                    escape.error->code == Hephaestus::Automation::ErrorCode::InvalidArguments,
                "asset.import rejects destination escapes");

    // ── trash / restore ────────────────────────────────────────────────────
    auto trashed = Call(service, "asset.trash", {{"path", "assets/scripts/Renamed.hematerial"}});
    const std::string opId = trashed.Ok() ? trashed.value.value("opId", std::string()) : "";
    ok &= Check(trashed.Ok() && !opId.empty() &&
                    !fs::exists(project / "assets" / "scripts" / "Renamed.hematerial") &&
                    fs::exists(project / ".hephaestus" / "trash" / opId),
                "asset.trash moves the asset into project trash");

    auto restored = Call(service, "asset.restore", {{"opId", opId}});
    ok &= Check(restored.Ok() &&
                    fs::exists(project / "assets" / "scripts" / "Renamed.hematerial"),
                "asset.restore returns the asset from trash");

    // ── import / reimport (deferred) ───────────────────────────────────────
    auto imported = CallDeferred(service, "asset.import",
                                 {{"source", pngSource.string()}, {"destination", "textures"}},
                                 std::chrono::seconds(30));
    ok &= Check(imported.Ok() && imported.value["imported"] == true &&
                    imported.value["asset"]["path"] == "assets/textures/light_bulb.png" &&
                    fs::exists(project / "assets" / "textures" / "light_bulb.png"),
                "asset.import copies and imports a texture through the pipeline");

    auto reimported = CallDeferred(service, "asset.reimport",
                                   {{"path", "assets/textures/light_bulb.png"}},
                                   std::chrono::seconds(30));
    ok &= Check(reimported.Ok() && reimported.value["success"] == true,
                "asset.reimport completes through ReimportManager");

    const auto importedInfo =
        Call(service, "asset.get_info", {{"path", "assets/textures/light_bulb.png"}});
    ok &= Check(importedInfo.Ok() && importedInfo.value.contains("import") &&
                    importedInfo.value["import"]["imported"] == true,
                "asset.get_info reports import status from the database");

    ok &= Check(backend.NotifyCount() > 0, "asset mutations notify the editor host");

    // ── Play Mode mutation gating ──────────────────────────────────────────
    backend.SetPlayState(Hephaestus::PlayState::Playing);
    auto blockedImport = Call(service, "asset.import",
                              {{"source", pngSource.string()}, {"destination", "textures"},
                               {"overwrite", true}});
    ok &= Check(!blockedImport.Ok() &&
                    blockedImport.error->code == Hephaestus::Automation::ErrorCode::PlayModeActive,
                "asset.import is blocked while Play Mode is active");
    auto readDuringPlay = Call(service, "asset.list");
    ok &= Check(readDuringPlay.Ok(), "asset.list remains available during Play Mode");
    backend.SetPlayState(Hephaestus::PlayState::Editor);

    service.Shutdown();
    assetManager.Clear();
    fs::remove_all(project, ec);

    if (ok) std::cout << "[PASS] AutomationAssetTests\n";
    return ok ? 0 : 1;
}
