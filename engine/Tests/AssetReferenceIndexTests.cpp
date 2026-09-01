#include "Assets/AssetReferenceIndex.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using Hephaestus::AssetReferenceFieldKind;
using Hephaestus::AssetReferenceIndex;
using Hephaestus::AssetPathPolicy;

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

bool ContainsRef(const std::vector<Hephaestus::AssetReference>& refs,
                 AssetReferenceFieldKind kind,
                 const std::string& referencedPath) {
    for (const auto& ref : refs) {
        if (ref.fieldKind == kind && AssetPathPolicy::KeysEqual(ref.referencedPath, referencedPath)) {
            return true;
        }
    }
    return false;
}

bool TestSceneAndPrefabIndex(const fs::path& project) {
    bool ok = true;

    WriteText(project / "assets" / "scenes" / "Demo.hpscene", R"({
  "entities": [
    {
      "id": 1,
      "components": {
        "MeshRenderer": {
          "meshSource": "assets/meshes/Cube.glb",
          "appliedMaterialPaths": ["assets/materials/Default.hematerial"]
        },
        "LuaScript": {
          "scriptPath": "assets/scripts/Demo.lua"
        },
        "AudioSource": {
          "clipPath": "assets/audio/Click.wav"
        },
        "Environment": {
          "hdriPath": "assets/textures/env.hdr"
        }
      }
    }
  ],
  "prefabInstances": [
    { "prefabPath": "assets/prefabs/Lamp.hpprefab" }
  ]
})");

    WriteText(project / "assets" / "prefabs" / "Lamp.hpprefab", R"({
  "nodes": [
    {
      "components": [
        {
          "type": "MeshRenderer",
          "data": {
            "meshSource": "assets/meshes/cube.mesh",
            "appliedMaterialPaths": ["assets/materials/default.hematerial"],
            "note": "assets/textures/extra.png"
          }
        }
      ]
    }
  ]
})");

    WriteText(project / "assets" / "materials" / "Default.hematerial", R"({
  "albedoTexturePath": "assets/textures/albedo.png",
  "normalTexturePath": "",
  "graph": {
    "nodes": [
      { "id": 1, "resourcePath": "assets/textures/graph.png" }
    ]
  }
})");

    WriteText(project / "assets" / "ui" / "Menu.hui", R"({
  "controller": "assets/scripts/Menu.lua",
  "root": {
    "style": {
      "backgroundImageId": "assets/textures/bg.png"
    },
    "properties": {
      "imageId": "assets/textures/icon.png"
    }
  }
})");

    WriteText(project / "project.heproject", R"({
  "name": "RefIndexTest",
  "defaultScene": "assets/scenes/Demo.hpscene",
  "lastScene": "assets/scenes/Demo.hpscene"
})");

    // Non-indexed Lua body must not create references.
    WriteText(project / "assets" / "scripts" / "Demo.lua",
              "local p = \"assets/textures/should_not_index.png\"\n");

    AssetReferenceIndex index;
    index.ScanProject(project);

    const auto sceneDeps = index.GetDependencies("assets/scenes/Demo.hpscene");
    ok &= Check(ContainsRef(sceneDeps, AssetReferenceFieldKind::MeshSource, "assets/meshes/Cube.glb"),
                "scene meshSource");
    ok &= Check(ContainsRef(sceneDeps, AssetReferenceFieldKind::AppliedMaterialPath,
                            "assets/materials/Default.hematerial"),
                "scene appliedMaterialPaths");
    ok &= Check(ContainsRef(sceneDeps, AssetReferenceFieldKind::ScriptPath, "assets/scripts/Demo.lua"),
                "scene scriptPath");
    ok &= Check(ContainsRef(sceneDeps, AssetReferenceFieldKind::ClipPath, "assets/audio/Click.wav"),
                "scene clipPath");
    ok &= Check(ContainsRef(sceneDeps, AssetReferenceFieldKind::HdriPath, "assets/textures/env.hdr"),
                "scene hdriPath");
    ok &= Check(ContainsRef(sceneDeps, AssetReferenceFieldKind::PrefabPath, "assets/prefabs/Lamp.hpprefab"),
                "scene prefabPath");

    const auto meshReferrers = index.GetReferrers("assets/meshes/Cube.glb");
    ok &= Check(!meshReferrers.empty(), "mesh has referrers");
    ok &= Check(AssetPathPolicy::KeysEqual(meshReferrers.front().documentPath, "assets/scenes/Demo.hpscene"),
                "mesh referrer is scene");

    const auto prefabDeps = index.GetDependencies("assets/prefabs/Lamp.hpprefab");
    ok &= Check(ContainsRef(prefabDeps, AssetReferenceFieldKind::MeshSource, "assets/meshes/cube.mesh"),
                "prefab meshSource");
    bool foundAdvisory = false;
    for (const auto& ref : prefabDeps) {
        if (ref.fieldKind == AssetReferenceFieldKind::NestedAssetPath &&
            AssetPathPolicy::KeysEqual(ref.referencedPath, "assets/textures/extra.png") &&
            ref.isAdvisory) {
            foundAdvisory = true;
        }
    }
    ok &= Check(foundAdvisory, "prefab advisory nested assets/ string");

    const auto matDeps = index.GetDependencies("assets/materials/Default.hematerial");
    ok &= Check(ContainsRef(matDeps, AssetReferenceFieldKind::TexturePath, "assets/textures/albedo.png"),
                "material albedoTexturePath");
    ok &= Check(ContainsRef(matDeps, AssetReferenceFieldKind::ResourcePath, "assets/textures/graph.png"),
                "material graph resourcePath");

    const auto huiDeps = index.GetDependencies("assets/ui/Menu.hui");
    ok &= Check(ContainsRef(huiDeps, AssetReferenceFieldKind::Controller, "assets/scripts/Menu.lua"),
                "hui controller");
    ok &= Check(ContainsRef(huiDeps, AssetReferenceFieldKind::ImageId, "assets/textures/icon.png"),
                "hui imageId");
    ok &= Check(ContainsRef(huiDeps, AssetReferenceFieldKind::BackgroundImageId, "assets/textures/bg.png"),
                "hui backgroundImageId");

    const auto projectDeps = index.GetDependencies("project.heproject");
    ok &= Check(ContainsRef(projectDeps, AssetReferenceFieldKind::DefaultScene, "assets/scenes/Demo.hpscene"),
                "heproject defaultScene");

    // Lua file content must not appear as a referrer source for the fake texture.
    const auto luaNoise = index.GetReferrers("assets/textures/should_not_index.png");
    ok &= Check(luaNoise.empty(), "do not parse Lua string literals as references");

    return ok;
}

bool TestRewriteAndMalformed(const fs::path& project) {
    bool ok = true;

    const fs::path scene = project / "assets" / "scenes" / "Rewrite.hpscene";
    WriteText(scene, R"({
  "entities": [
    {
      "components": {
        "MeshRenderer": {
          "meshSource": "assets/meshes/Old.glb",
          "appliedMaterialPaths": ["assets/materials/Old.hematerial"]
        }
      }
    }
  ]
})");

    AssetReferenceIndex index;
    index.ScanProject(project);

    auto result = index.RewriteReferences("assets/meshes/Old.glb", "assets/meshes/New.glb");
    ok &= Check(result.success, "rewrite success");
    ok &= Check(result.documentsTouched >= 1, "rewrite touched documents");
    ok &= Check(result.referencesRewritten >= 1, "rewrite counted refs");

    const auto after = index.GetReferrers("assets/meshes/New.glb");
    ok &= Check(!after.empty(), "new mesh has referrers after rewrite");
    ok &= Check(index.GetReferrers("assets/meshes/Old.glb").empty(), "old mesh referrers cleared");

    // Malformed scene: fallback scan should still find meshSource.
    WriteText(project / "assets" / "scenes" / "Broken.hpscene",
              "{ this is not json \"meshSource\": \"assets/meshes/Broken.glb\" trailing");
    index.ScanDocument("assets/scenes/Broken.hpscene");
    const auto brokenDeps = index.GetDependencies("assets/scenes/Broken.hpscene");
    ok &= Check(ContainsRef(brokenDeps, AssetReferenceFieldKind::MeshSource, "assets/meshes/Broken.glb"),
                "malformed scene fallback finds meshSource");
    ok &= Check(!index.GetDiagnostics().empty(), "malformed JSON produces diagnostics");

    // Direct RewritePathInDocument helper
    int count = 0;
    std::string error;
    const bool rewritten = AssetReferenceIndex::RewritePathInDocument(
        scene, project, "assets/materials/Old.hematerial", "assets/materials/New.hematerial", &count,
        &error);
    ok &= Check(rewritten && count >= 1, "RewritePathInDocument updates appliedMaterialPaths");

    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: AssetReferenceIndexTests <repo_root>\n";
        return 2;
    }

    const fs::path root = fs::path(argv[1]);
    const fs::path outDir = root / "build" / "asset_reference_index_test_out";
    fs::create_directories(outDir);
    fs::remove_all(outDir / "scratch");
    const fs::path scratch = outDir / "scratch";
    fs::create_directories(scratch);

    bool ok = true;
    ok &= TestSceneAndPrefabIndex(scratch / "ProjA");
    ok &= TestRewriteAndMalformed(scratch / "ProjB");

    if (!ok) {
        return 1;
    }

    std::cout << "AssetReferenceIndex tests passed.\n";
    return 0;
}
