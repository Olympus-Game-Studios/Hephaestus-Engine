#include "Assets/AssetOperationService.h"
#include "Assets/AssetPathPolicy.h"
#include "Assets/AssetReferenceIndex.h"
#include "Assets/CharacterAsset.h"
#include "Assets/OpenDocumentRegistry.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using Hephaestus::AssetOperationService;
using Hephaestus::AssetPathPolicy;
using Hephaestus::AssetReferenceIndex;
using Hephaestus::DocumentKind;
using Hephaestus::OpenDocumentRegistry;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool TestRenameRewritesReferrers(const fs::path& project) {
    bool ok = true;

    WriteText(project / "assets" / "meshes" / "Cube.glb", "mesh-bytes");
    WriteText(project / "assets" / "meshes" / "Cube.glb.hpmesh",
              R"({ "appliedMaterialPaths": ["assets/materials/Paint.hematerial"] })");
    WriteText(project / "assets" / "materials" / "Paint.hematerial", "{}");
    WriteText(project / "assets" / "scenes" / "Demo.hpscene", R"({
  "entities": [
    {
      "components": {
        "MeshRenderer": {
          "meshSource": "assets/meshes/Cube.glb"
        }
      }
    }
  ]
})");
    WriteText(project / "project.heproject", R"({ "name": "OpTest" })");

    AssetReferenceIndex index;
    index.ScanProject(project);

    OpenDocumentRegistry docs;
    docs.SetProjectRoot(project);
    docs.Register(DocumentKind::Scene, "assets/scenes/Demo.hpscene", true);

    AssetOperationService ops;
    ops.SetProjectRoot(project);
    ops.SetReferenceIndex(&index);
    ops.SetDocumentRegistry(&docs);

    const auto preflight =
        ops.Preflight("assets/meshes/Cube.glb", "assets/meshes/CubeRenamed.glb");
    ok &= Check(preflight.ok, "preflight ok");
    ok &= Check(!preflight.referrers.empty(), "preflight lists referrers");

    const auto result =
        ops.RenameAsset("assets/meshes/Cube.glb", "assets/meshes/CubeRenamed.glb");
    ok &= Check(result.success, "rename success: " + result.error);
    ok &= Check(fs::exists(project / "assets" / "meshes" / "CubeRenamed.glb"),
                "renamed file exists");
    ok &= Check(!fs::exists(project / "assets" / "meshes" / "Cube.glb"),
                "old file gone");
    ok &= Check(fs::exists(project / "assets" / "meshes" / "CubeRenamed.glb.hpmesh") &&
                    !fs::exists(project / "assets" / "meshes" / "Cube.glb.hpmesh"),
                "the model's settings file moves with it");
    const auto paintReferrers = index.GetReferrers("assets/materials/Paint.hematerial");
    ok &= Check(paintReferrers.size() == 1 &&
                    paintReferrers.front().documentPath == "assets/meshes/CubeRenamed.glb.hpmesh",
                "a model's default materials count as references, from its new settings file only");

    const std::string sceneText = ReadText(project / "assets" / "scenes" / "Demo.hpscene");
    ok &= Check(sceneText.find("assets/meshes/CubeRenamed.glb") != std::string::npos,
                "scene rewritten to new mesh path");
    ok &= Check(sceneText.find("assets/meshes/Cube.glb") == std::string::npos,
                "scene no longer references old mesh path");

    const auto newReferrers = index.GetReferrers("assets/meshes/CubeRenamed.glb");
    ok &= Check(!newReferrers.empty(), "index referrers updated to new path");
    ok &= Check(index.GetReferrers("assets/meshes/Cube.glb").empty(),
                "old path has no referrers");

    return ok;
}

bool TestTrashRestoreRoundTrip(const fs::path& project) {
    bool ok = true;

    WriteText(project / "assets" / "textures" / "Albedo.png", "png-bytes");
    WriteText(project / "assets" / "materials" / "Default.hematerial", R"({
  "albedoTexturePath": "assets/textures/Albedo.png"
})");
    WriteText(project / "project.heproject", R"({ "name": "TrashTest" })");

    AssetReferenceIndex index;
    index.ScanProject(project);

    AssetOperationService ops;
    ops.SetProjectRoot(project);
    ops.SetReferenceIndex(&index);

    const auto trash = ops.MoveToTrash("assets/textures/Albedo.png");
    ok &= Check(trash.success, "trash success: " + trash.error);
    ok &= Check(!trash.opId.empty(), "trash opId set");
    ok &= Check(!fs::exists(project / "assets" / "textures" / "Albedo.png"),
                "asset removed from project tree");
    ok &= Check(fs::exists(AssetOperationService::TrashRoot(project) / trash.opId / "manifest.json"),
                "trash manifest exists");

    const auto listed = ops.ListTrash();
    bool found = false;
    for (const auto& entry : listed) {
        if (entry.opId == trash.opId) {
            found = true;
            ok &= Check(entry.referrerCount >= 1, "trash lists referrer count");
            ok &= Check(!entry.originalPortablePaths.empty(), "trash lists original paths");
        }
    }
    ok &= Check(found, "ListTrash contains opId");

    // Refuse external absolute trash
    const fs::path external = project.parent_path() / "external_source.bin";
    WriteText(external, "external");
    const auto externalTrash = ops.MoveToTrash(external.string());
    ok &= Check(!externalTrash.success, "refuse external trash");
    ok &= Check(fs::exists(external), "external file still present");

    const auto restore = ops.RestoreFromTrash(trash.opId);
    ok &= Check(restore.success, "restore success: " + restore.error);
    ok &= Check(fs::exists(project / "assets" / "textures" / "Albedo.png"),
                "asset restored");
    ok &= Check(ops.ListTrash().empty() ||
                    std::none_of(ops.ListTrash().begin(), ops.ListTrash().end(),
                                 [&](const auto& e) { return e.opId == trash.opId; }),
                "trash entry cleared after restore");

    // Permanent delete path
    const auto trash2 = ops.MoveToTrash("assets/textures/Albedo.png");
    ok &= Check(trash2.success, "second trash");
    const auto purged = ops.PermanentDeleteTrash(trash2.opId);
    ok &= Check(purged.success, "permanent delete trash");
    ok &= Check(!fs::exists(project / "assets" / "textures" / "Albedo.png"),
                "asset stays deleted after permanent trash purge");
    ok &= Check(!fs::exists(AssetOperationService::TrashRoot(project) / trash2.opId),
                "trash folder removed");

    return ok;
}

bool TestOpenDocumentRetarget(const fs::path& project) {
    bool ok = true;
    WriteText(project / "assets" / "ui" / "Menu.hui", R"({ "root": {} })");

    OpenDocumentRegistry docs;
    docs.SetProjectRoot(project);
    ok &= Check(docs.Register(DocumentKind::HUI, "assets/ui/Menu.hui", true), "register hui");
    ok &= Check(docs.ListDirty().size() == 1, "one dirty doc");

    bool notified = false;
    docs.SetRetargetNotificationCallback(
        [&](const Hephaestus::OpenDocumentInfo& before, const Hephaestus::OpenDocumentInfo& after) {
            notified = AssetPathPolicy::KeysEqual(before.portablePath, "assets/ui/Menu.hui") &&
                       AssetPathPolicy::KeysEqual(after.portablePath, "assets/ui/MainMenu.hui");
        });

    ok &= Check(docs.RetargetPath("assets/ui/Menu.hui", "assets/ui/MainMenu.hui"), "retarget");
    ok &= Check(notified, "retarget callback fired");
    ok &= Check(docs.FindByPath("assets/ui/MainMenu.hui").has_value(), "find by new path");
    ok &= Check(!docs.FindByPath("assets/ui/Menu.hui").has_value(), "old path unregistered");

    return ok;
}

bool TestCharacterCompanionRenameAndTrash(const fs::path& project) {
    bool ok = true;
    WriteText(project / "project.heproject", R"({ "name": "CharOps" })");
    const fs::path character = project / "assets" / "characters" / "Cultist.hpcharacter";
    std::string error;
    ok &= Check(Hephaestus::CharacterSerializer::CreateNew(character, project, error),
                "CreateNew character: " + error);
    WriteText(project / "assets" / "characters" / ".source" / "Cultist" / "mesh.glb", "mesh");

    AssetReferenceIndex index;
    index.ScanProject(project);

    AssetOperationService ops;
    ops.SetProjectRoot(project);
    ops.SetReferenceIndex(&index);

    const auto renamed = ops.RenameAsset("assets/characters/Cultist.hpcharacter",
                                         "assets/characters/Warrior.hpcharacter");
    ok &= Check(renamed.success, "rename character: " + renamed.error);
    ok &= Check(fs::exists(project / "assets" / "characters" / "Warrior.hpcharacter"),
                "renamed json exists");
    ok &= Check(!fs::exists(project / "assets" / "characters" / ".source" / "Cultist"),
                "old owned dir removed");
    ok &= Check(fs::exists(project / "assets" / "characters" / ".source" / "Warrior" / "mesh.glb"),
                "owned dir followed stem");
    ok &= Check(fs::exists(project / "assets" / "characters" / ".source" / "Warrior" /
                           "WarriorCharacter.lua"),
                "controller lua renamed");

    Hephaestus::CharacterAsset loaded;
    ok &= Check(Hephaestus::CharacterSerializer::Deserialize(
                    loaded, project / "assets" / "characters" / "Warrior.hpcharacter", error),
                "load renamed character: " + error);
    ok &= Check(loaded.controller.scriptPath.find("WarriorCharacter.lua") != std::string::npos,
                "controller path rewritten: " + loaded.controller.scriptPath);

    const auto trashed = ops.MoveToTrash("assets/characters/Warrior.hpcharacter");
    ok &= Check(trashed.success, "trash character: " + trashed.error);
    ok &= Check(!fs::exists(project / "assets" / "characters" / "Warrior.hpcharacter"),
                "trashed json gone");
    ok &= Check(!fs::exists(project / "assets" / "characters" / ".source" / "Warrior"),
                "trashed owned dir gone");

    const auto restored = ops.RestoreFromTrash(trashed.opId);
    ok &= Check(restored.success, "restore character: " + restored.error);
    ok &= Check(fs::exists(project / "assets" / "characters" / "Warrior.hpcharacter"),
                "restored json");
    ok &= Check(fs::exists(project / "assets" / "characters" / ".source" / "Warrior" / "mesh.glb"),
                "restored owned mesh");
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: AssetOperationTransactionTests <repo_root>\n";
        return 2;
    }

    const fs::path root = fs::path(argv[1]);
    const fs::path outDir = root / "build" / "asset_operation_transaction_test_out";
    fs::create_directories(outDir);
    fs::remove_all(outDir / "scratch");
    const fs::path scratch = outDir / "scratch";
    fs::create_directories(scratch);

    bool ok = true;
    ok &= TestRenameRewritesReferrers(scratch / "RenameProj");
    ok &= TestTrashRestoreRoundTrip(scratch / "TrashProj");
    ok &= TestOpenDocumentRetarget(scratch / "DocsProj");
    ok &= TestCharacterCompanionRenameAndTrash(scratch / "CharProj");

    if (!ok) {
        return 1;
    }

    std::cout << "AssetOperationTransaction tests passed.\n";
    return 0;
}
