/**
 * @file CharacterAssetTests.cpp
 * @brief Headless .hpcharacter schema, owned-source, and reference-rewrite tests.
 */

#include "Assets/CharacterAsset.h"
#include "Assets/AssetPathPolicy.h"
#include "Assets/AssetReferenceIndex.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using Hephaestus::AssetPathPolicy;
using Hephaestus::AssetReferenceFieldKind;
using Hephaestus::AssetReferenceIndex;
using Hephaestus::CharacterAsset;
using Hephaestus::CharacterOwnedSource;
using Hephaestus::CharacterSerializer;

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

bool TestOwnedSourcePaths() {
    bool ok = true;
    const std::string character = "assets/characters/Cultist.hpcharacter";
    const std::string owned = CharacterOwnedSource::OwnedDirectoryPortableKey(character);
    ok &= Check(owned == "assets/characters/.source/Cultist",
                "owned directory portable key: got " + owned);
    ok &= Check(CharacterOwnedSource::ControllerScriptFileName("Cultist") ==
                    "CultistCharacter.lua",
                "controller file name");
    ok &= Check(CharacterOwnedSource::IsOwnedSourcePortableKey(
                    "assets/characters/.source/Cultist/mesh.glb"),
                "owned glb is owned source");
    ok &= Check(!CharacterOwnedSource::IsOwnedSourcePortableKey(character),
                "character json is not an owned source file");
    return ok;
}

bool TestRoundTripAndUnknownFields() {
    bool ok = true;
    CharacterAsset asset = CharacterSerializer::MakeEmpty("Cultist");
    asset.skeleton.source = "assets/characters/.source/Cultist/mesh.glb";
    asset.skeleton.skin = "Armature";
    asset.skeleton.signature = "abc123";
    asset.mesh.source = "assets/characters/.source/Cultist/mesh.glb";
    asset.mesh.submeshes.push_back({"Body", "assets/materials/CultistBody.hematerial"});
    asset.animationFolders.push_back("Locomotion");
    Hephaestus::CharacterClipEntry clip;
    clip.id = "Locomotion/Idle";
    clip.displayName = "Idle";
    clip.folder = "Locomotion";
    clip.source = "assets/characters/.source/Cultist/mesh.glb";
    clip.sourceClip = "Idle";
    clip.loop = true;
    clip.speed = 1.0f;
    clip.endTime = 2.5f;
    asset.clips.push_back(clip);
    asset.controller.scriptPath = "assets/characters/.source/Cultist/CultistCharacter.lua";
    asset.defaults.animation = "Idle";

    std::string error;
    CharacterAsset loaded;
    const std::string json = CharacterSerializer::SerializeToString(asset);
    ok &= Check(CharacterSerializer::DeserializeFromString(loaded, json, error),
                "deserialize round-trip: " + error);
    ok &= Check(loaded.name == "Cultist", "name round-trip");
    ok &= Check(loaded.skeleton.source == asset.skeleton.source, "skeleton source");
    ok &= Check(loaded.skeleton.skin == "Armature", "skeleton skin");
    ok &= Check(loaded.skeleton.signature == "abc123", "signature");
    ok &= Check(loaded.mesh.submeshes.size() == 1 &&
                    loaded.mesh.submeshes[0].material ==
                        "assets/materials/CultistBody.hematerial" &&
                    loaded.mesh.submeshes[0].meshPart.empty(),
                "submesh material");
    CharacterAsset multi = asset;
    multi.mesh.submeshes[0].meshPart = "Body";
    CharacterAsset loadedMulti;
    ok &= Check(CharacterSerializer::DeserializeFromString(
                    loadedMulti, CharacterSerializer::SerializeToString(multi), error),
                "meshPart round-trip");
    ok &= Check(loadedMulti.mesh.submeshes[0].meshPart == "Body", "meshPart persisted");
    const std::string legacy = R"({
  "schema": "HephaestusCharacter",
  "assetVersion": 1,
  "name": "Legacy",
  "skeleton": { "source": "a.glb", "skin": "Armature", "signature": "x" },
  "mesh": { "source": "a.glb", "submeshes": [{ "name": "Skin", "material": "m.hematerial" }] }
})";
    CharacterAsset legacyAsset;
    ok &= Check(CharacterSerializer::DeserializeFromString(legacyAsset, legacy, error),
                "legacy single-mesh json: " + error);
    ok &= Check(legacyAsset.mesh.submeshes.size() == 1 &&
                    legacyAsset.mesh.submeshes[0].name == "Skin" &&
                    legacyAsset.mesh.submeshes[0].meshPart.empty(),
                "legacy slot has empty meshPart");
    ok &= Check(loaded.clips.size() == 1 && loaded.clips[0].id == "Locomotion/Idle",
                "clip id");
    ok &= Check(loaded.controller.scriptPath == asset.controller.scriptPath,
                "controller scriptPath");
    ok &= Check(loaded.sharedRig.empty(), "sharedRig null");

    const std::string withExtra = R"({
  "schema": "HephaestusCharacter",
  "assetVersion": 1,
  "name": "Extra",
  "futureField": { "ignored": true },
  "skeleton": { "source": "", "skin": "", "signature": "" }
})";
    CharacterAsset extra;
    ok &= Check(CharacterSerializer::DeserializeFromString(extra, withExtra, error),
                "unknown fields ignored: " + error);
    ok &= Check(extra.name == "Extra", "unknown-field document still loads name");
    return ok;
}

bool TestSchemaAndVersionReject() {
    bool ok = true;
    std::string error;
    CharacterAsset asset;
    ok &= Check(!CharacterSerializer::DeserializeFromString(asset, R"({"name":"x"})", error),
                "missing schema rejected");
    ok &= Check(!CharacterSerializer::DeserializeFromString(
                    asset, R"({"schema":"Nope","assetVersion":1})", error),
                "wrong schema rejected");
    ok &= Check(!CharacterSerializer::DeserializeFromString(
                    asset,
                    R"({"schema":"HephaestusCharacter","assetVersion":99,"name":"x"})",
                    error),
                "future assetVersion rejected");
    return ok;
}

bool TestCreateNew(const fs::path& project) {
    bool ok = true;
    fs::create_directories(project / "assets" / "characters");
    const fs::path character = project / "assets" / "characters" / "Cultist.hpcharacter";
    std::string error;
    ok &= Check(CharacterSerializer::CreateNew(character, project, error),
                "CreateNew: " + error);

    CharacterAsset loaded;
    ok &= Check(CharacterSerializer::Deserialize(loaded, character, error),
                "load created character: " + error);
    ok &= Check(loaded.name == "Cultist", "created name");
    ok &= Check(loaded.controller.scriptPath ==
                    "assets/characters/.source/Cultist/CultistCharacter.lua",
                "created controller path: " + loaded.controller.scriptPath);

    const fs::path lua = project / "assets" / "characters" / ".source" / "Cultist" /
                         "CultistCharacter.lua";
    ok &= Check(fs::exists(lua), "owned controller lua exists");
    ok &= Check(fs::is_directory(project / "assets" / "characters" / ".source" / "Cultist"),
                "owned source directory exists");
    ok &= Check(AssetPathPolicy::IsPortableKey(loaded.controller.scriptPath),
                "controller path is portable");
    return ok;
}

bool ContainsRef(const std::vector<Hephaestus::AssetReference>& refs,
                 AssetReferenceFieldKind kind,
                 const std::string& referencedPath) {
    for (const auto& ref : refs) {
        if (ref.fieldKind == kind &&
            AssetPathPolicy::KeysEqual(ref.referencedPath, referencedPath)) {
            return true;
        }
    }
    return false;
}

bool TestIndexAndRewrite(const fs::path& project) {
    bool ok = true;
    const fs::path character = project / "assets" / "characters" / "Soldier.hpcharacter";
    CharacterAsset asset = CharacterSerializer::MakeEmpty("Soldier");
    asset.skeleton.source = "assets/characters/.source/Soldier/mesh.glb";
    asset.mesh.source = "assets/characters/.source/Soldier/mesh.glb";
    asset.mesh.submeshes.push_back({"Body", "assets/materials/Old.hematerial"});
    asset.controller.scriptPath = "assets/characters/.source/Soldier/SoldierCharacter.lua";
    std::string error;
    ok &= Check(CharacterSerializer::Serialize(asset, character, error),
                "serialize soldier: " + error);
    WriteText(project / "assets" / "characters" / ".source" / "Soldier" / "mesh.glb", "mesh");
    WriteText(project / "assets" / "materials" / "Old.hematerial", "{}");
    WriteText(project / "assets" / "materials" / "New.hematerial", "{}");

    AssetReferenceIndex index;
    index.ScanProject(project);
    const auto deps = index.GetDependencies("assets/characters/Soldier.hpcharacter");
    ok &= Check(ContainsRef(deps, AssetReferenceFieldKind::MeshSource,
                            "assets/characters/.source/Soldier/mesh.glb"),
                "indexes skeleton/mesh source");
    ok &= Check(ContainsRef(deps, AssetReferenceFieldKind::AppliedMaterialPath,
                            "assets/materials/Old.hematerial"),
                "indexes submesh material");
    ok &= Check(ContainsRef(deps, AssetReferenceFieldKind::ScriptPath,
                            "assets/characters/.source/Soldier/SoldierCharacter.lua"),
                "indexes controller scriptPath");

    auto result = index.RewriteReferences("assets/materials/Old.hematerial",
                                          "assets/materials/New.hematerial");
    ok &= Check(result.success && result.referencesRewritten >= 1, "rewrite material");

    CharacterAsset rewritten;
    ok &= Check(CharacterSerializer::Deserialize(rewritten, character, error),
                "reload after rewrite: " + error);
    ok &= Check(rewritten.mesh.submeshes.size() == 1 &&
                    rewritten.mesh.submeshes[0].material ==
                        "assets/materials/New.hematerial",
                "material path rewritten in .hpcharacter");
    return ok;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "hephaestus_character_asset_tests";
    fs::remove_all(root);
    fs::create_directories(root);

    bool ok = true;
    ok &= TestOwnedSourcePaths();
    ok &= TestRoundTripAndUnknownFields();
    ok &= TestSchemaAndVersionReject();
    ok &= TestCreateNew(root / "CreateProj");
    ok &= TestIndexAndRewrite(root / "IndexProj");

    fs::remove_all(root);
    if (ok) {
        std::cout << "Character asset tests passed.\n";
        return 0;
    }
    return 1;
}
