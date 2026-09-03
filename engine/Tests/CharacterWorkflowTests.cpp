/**
 * @file CharacterWorkflowTests.cpp
 * @brief Stage E import/bind/serialize/reimport/hide-.source workflow tests.
 */

#include "Animation/Skeleton.h"
#include "Assets/AssetManager.h"
#include "Assets/CharacterAsset.h"
#include "Assets/CharacterImport.h"
#include "Assets/CharacterRuntime.h"
#include "Scene/CharacterComponent.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/PrefabComponentIO.h"
#include "Scene/PrefabOverrideSystem.h"
#include "Scene/PrefabSerializer.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scripting/LuaScriptComponent.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Hephaestus::AssetManager;
using Hephaestus::CharacterAsset;
using Hephaestus::CharacterImporter;
using Hephaestus::CharacterImportRequest;
using Hephaestus::CharacterInspectResult;
using Hephaestus::CharacterOwnedSource;
using Hephaestus::CharacterRuntime;
using Hephaestus::CharacterRuntimeData;
using Hephaestus::CharacterSerializer;
using Hephaestus::SkeletalImportResult;
using Hephaestus::SkeletalModelImporter;

namespace {

constexpr float kSqrtHalf = 0.7071067811865476f;

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

void AppendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void PadTo4(std::vector<uint8_t>& out, uint8_t pad) {
    while (out.size() % 4 != 0) {
        out.push_back(pad);
    }
}

bool WriteSkinnedGlb(const fs::path& path, const std::string& childName, const std::string& clipName) {
    const float positions[] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const float normals[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    const uint16_t joints0[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const float weights0[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const uint16_t indices[] = {0, 1, 2};
    Mat4 ibmRoot = Mat4::Identity();
    Mat4 ibmChild = Mat4::Translation(Vec3(0.0f, -1.0f, 0.0f));
    const float times[] = {0.0f, 1.0f};
    const float rotations[] = {
        0.0f, 0.0f, kSqrtHalf, kSqrtHalf,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    std::vector<uint8_t> bin;
    AppendBytes(bin, positions, sizeof(positions));
    const uint32_t nrmOff = static_cast<uint32_t>(bin.size());
    AppendBytes(bin, normals, sizeof(normals));
    const uint32_t j0Off = static_cast<uint32_t>(bin.size());
    AppendBytes(bin, joints0, sizeof(joints0));
    const uint32_t w0Off = static_cast<uint32_t>(bin.size());
    AppendBytes(bin, weights0, sizeof(weights0));
    const uint32_t idxOff = static_cast<uint32_t>(bin.size());
    AppendBytes(bin, indices, sizeof(indices));
    PadTo4(bin, 0);
    const uint32_t ibmOff = static_cast<uint32_t>(bin.size());
    AppendBytes(bin, ibmRoot.m, sizeof(ibmRoot.m));
    AppendBytes(bin, ibmChild.m, sizeof(ibmChild.m));
    const uint32_t timeOff = static_cast<uint32_t>(bin.size());
    AppendBytes(bin, times, sizeof(times));
    const uint32_t rotOff = static_cast<uint32_t>(bin.size());
    AppendBytes(bin, rotations, sizeof(rotations));
    PadTo4(bin, 0);

    const std::string json =
        std::string("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,") +
        "\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":["
        "{\"name\":\"Root\",\"children\":[1]},"
        "{\"name\":\"" + childName + "\",\"translation\":[0,1,0],\"rotation\":[0,0," +
        std::to_string(kSqrtHalf) + "," + std::to_string(kSqrtHalf) + "]},"
        "{\"translation\":[0,0,0],\"mesh\":0,\"skin\":0}"
        "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
        "\"JOINTS_0\":2,\"WEIGHTS_0\":3},\"indices\":4}]}],"
        "\"skins\":[{\"name\":\"Armature\",\"joints\":[0,1],\"inverseBindMatrices\":5}],"
        "\"animations\":[{\"name\":\"" + clipName + "\",\"samplers\":[{\"input\":6,\"output\":7,"
        "\"interpolation\":\"LINEAR\"}],\"channels\":[{\"sampler\":0,\"target\":{"
        "\"node\":1,\"path\":\"rotation\"}}]}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":4,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},"
        "{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":7,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"}"
        "],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" + std::to_string(nrmOff) + ",\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" + std::to_string(j0Off) + ",\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":" + std::to_string(w0Off) + ",\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":" + std::to_string(idxOff) + ",\"byteLength\":6},"
        "{\"buffer\":0,\"byteOffset\":" + std::to_string(ibmOff) + ",\"byteLength\":128},"
        "{\"buffer\":0,\"byteOffset\":" + std::to_string(timeOff) + ",\"byteLength\":8},"
        "{\"buffer\":0,\"byteOffset\":" + std::to_string(rotOff) + ",\"byteLength\":32}"
        "],"
        "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}]}";

    std::vector<uint8_t> jsonBytes(json.begin(), json.end());
    PadTo4(jsonBytes, static_cast<uint8_t>(' '));
    std::vector<uint8_t> file;
    const uint32_t magic = 0x46546C67;
    const uint32_t version = 2;
    const uint32_t jsonLen = static_cast<uint32_t>(jsonBytes.size());
    const uint32_t jsonType = 0x4E4F534A;
    const uint32_t binLen = static_cast<uint32_t>(bin.size());
    const uint32_t binType = 0x004E4942;
    const uint32_t length = 12 + 8 + jsonLen + 8 + binLen;
    AppendBytes(file, &magic, 4);
    AppendBytes(file, &version, 4);
    AppendBytes(file, &length, 4);
    AppendBytes(file, &jsonLen, 4);
    AppendBytes(file, &jsonType, 4);
    file.insert(file.end(), jsonBytes.begin(), jsonBytes.end());
    AppendBytes(file, &binLen, 4);
    AppendBytes(file, &binType, 4);
    file.insert(file.end(), bin.begin(), bin.end());

    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(file.data()),
              static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(out);
}

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool TestRefuseGltf(const fs::path& scratch) {
    const fs::path gltf = scratch / "raw.gltf";
    std::ofstream(gltf) << "{}";
    CharacterInspectResult inspect;
    std::string error;
    const bool okInspect = CharacterImporter::Inspect(gltf, inspect, error);
    bool ok = true;
    ok &= Check(!okInspect, "raw gltf inspect fails");
    ok &= Check(error.find("GLB") != std::string::npos, "gltf error mentions GLB");
    return ok;
}

bool TestImportBindSerialize(const fs::path& project) {
    bool ok = true;
    const fs::path glb = project / "source.glb";
    if (!WriteSkinnedGlb(glb, "Child", "Idle")) {
        return Check(false, "write source glb");
    }

    CharacterImportRequest request;
    request.sourceGlb = glb;
    request.projectRoot = project;
    request.destinationFolderAbs = project / "assets" / "characters";
    request.characterName = "Cultist";
    request.createController = true;

    std::string portable;
    std::string error;
    ok &= Check(CharacterImporter::Import(request, portable, error), "import: " + error);
    ok &= Check(portable == "assets/characters/Cultist.hpcharacter", "portable key: " + portable);
    ok &= Check(fs::exists(project / "assets" / "characters" / "Cultist.hpcharacter"),
                "hpcharacter exists");
    ok &= Check(fs::exists(project / "assets" / "characters" / ".source" / "Cultist" / "mesh.glb"),
                "owned mesh.glb exists");
    ok &= Check(fs::exists(project / "assets" / "characters" / ".source" / "Cultist" /
                           "CultistCharacter.lua"),
                "controller lua exists");

    CharacterAsset asset;
    ok &= Check(CharacterSerializer::Deserialize(
                    asset, project / "assets" / "characters" / "Cultist.hpcharacter", error),
                "deserialize imported: " + error);
    ok &= Check(asset.defaults.animation == "Idle", "default Idle: " + asset.defaults.animation);
    ok &= Check(!asset.skeleton.signature.empty(), "signature stored");

    CharacterRuntime::InvalidateAll();
    Scene scene("Bind");
    Entity entity = scene.CreateEntity("Cultist");
    auto& character = entity.AddComponent<CharacterComponent>();
    character.characterPath = portable;
    ok &= Check(CharacterRuntime::BindEntity(entity, project, true, error), "bind: " + error);
    ok &= Check(entity.HasComponent<MeshRendererComponent>(), "runtime MeshRenderer created");
    ok &= Check(IsRuntimeGeneratedVisual(entity.GetComponent<MeshRendererComponent>()),
                "MeshRenderer is runtime-generated");
    ok &= Check(character.defaultAnimation == "Idle", "defaults copied on first bind");
    ok &= Check(character.runtimeReady, "runtimeReady after bind");

    const std::string snapshot = SceneSerializer(scene).SerializeToString();
    ok &= Check(snapshot.find("\"Character\"") != std::string::npos, "scene has Character");
    ok &= Check(snapshot.find("MeshRenderer") == std::string::npos,
                "runtime MeshRenderer is not serialized");

    PrefabComponentEntry captured;
    ok &= Check(!PrefabComponentIO::CaptureComponent(entity, "MeshRenderer", captured),
                "prefab capture skips runtime MeshRenderer");

    std::string duplicateError;
    std::string duplicatePortable;
    ok &= Check(!CharacterImporter::Import(request, duplicatePortable, duplicateError),
                "duplicate import refused");
    return ok;
}

bool TestReimportCases(const fs::path& project) {
    bool ok = true;
    const fs::path glb = project / "source.glb";
    const fs::path incompatible = project / "incompatible.glb";
    if (!WriteSkinnedGlb(glb, "Child", "Idle") || !WriteSkinnedGlb(incompatible, "Leg", "Idle")) {
        return Check(false, "write reimport glbs");
    }

    CharacterImportRequest request;
    request.sourceGlb = glb;
    request.projectRoot = project;
    request.destinationFolderAbs = project / "assets" / "characters";
    request.characterName = "Soldier";
    request.createController = false;
    std::string portable;
    std::string error;
    ok &= Check(CharacterImporter::Import(request, portable, error), "import soldier: " + error);

    const fs::path characterAbs = project / "assets" / "characters" / "Soldier.hpcharacter";
    const std::string before = ReadText(characterAbs);

    ok &= Check(!CharacterImporter::Reimport(project, portable, incompatible, error),
                "case A incompatible fails");
    ok &= Check(error.find("has not been modified") != std::string::npos, "case A message");
    ok &= Check(ReadText(characterAbs) == before, "case A did not write JSON");
    ok &= Check(fs::file_size(project / "assets" / "characters" / ".source" / "Soldier" / "mesh.glb") ==
                    fs::file_size(glb),
                "case A did not overwrite owned GLB");

    const fs::path owned = project / "assets" / "characters" / ".source" / "Soldier" / "mesh.glb";
    fs::copy_file(incompatible, owned, fs::copy_options::overwrite_existing);
    const std::string afterOverwrite = ReadText(characterAbs);
    std::string caseBError;
    ok &= Check(!CharacterImporter::Reimport(project, portable, owned, caseBError),
                "case B overwritten incompatible fails");
    ok &= Check(caseBError.find("Stored signature") != std::string::npos, "case B signature dialog");
    ok &= Check(ReadText(characterAbs) == afterOverwrite, "case B did not write JSON");

    fs::copy_file(glb, owned, fs::copy_options::overwrite_existing);
    ok &= Check(CharacterImporter::Reimport(project, portable, glb, error),
                "compatible reimport: " + error);
    return ok;
}

bool TestHideSourceListing(const fs::path& project) {
    bool ok = true;
    fs::create_directories(project / "assets" / "characters" / ".source" / "Hidden");
    std::ofstream(project / "assets" / "characters" / "Hero.hpcharacter") << "{}";
    std::ofstream(project / "assets" / "characters" / ".source" / "Hidden" / "mesh.glb") << "x";

    auto& assets = AssetManager::GetInstance();
    assets.Clear();
    assets.SetProjectRoot(project.string());
    assets.SetAssetRoot((project / "assets").string());
    assets.Refresh();

    const auto root = assets.ListDirectory("characters");
    bool sawSource = false;
    bool sawHero = false;
    for (const auto& entry : root) {
        if (entry.name == ".source") {
            sawSource = true;
        }
        if (entry.name == "Hero.hpcharacter") {
            sawHero = true;
        }
    }
    ok &= Check(!sawSource, ".source hidden from ListDirectory");
    ok &= Check(sawHero, "character json still listed");

    const auto all = assets.GetAllAssets();
    bool searchSeesSource = false;
    for (const auto& info : all) {
        if (info.relativePath.find(".source") != std::string::npos) {
            searchSeesSource = true;
        }
    }
    ok &= Check(!searchSeesSource, ".source hidden from GetAllAssets search");

    assets.Clear();
    return ok;
}

bool TestPrefabCharacterLuaCoexistence(const fs::path& outDir) {
    bool ok = true;
    Scene scene("CharPrefab");
    Entity entity = scene.CreateEntity("Cultist");
    auto& character = entity.AddComponent<CharacterComponent>();
    character.characterPath = "assets/characters/Cultist.hpcharacter";
    character.defaultAnimation = "Idle";
    character.playbackSpeed = 1.1f;
    character.runController = true;
    character.controllerScriptPath = "assets/scripts/CultistCharacter.lua";
    character.controllerVariables["WalkThreshold"] = PropertyValue(0.1);
    character.authoredControllerVariables.insert("WalkThreshold");
    character.authoredControllerVariablesInitialized = true;
    auto& lua = entity.AddComponent<LuaScriptComponent>();
    lua.scriptPath = "assets/scripts/EnemyAI.lua";
    lua.variables["health"] = PropertyValue(10.0);
    lua.authoredVariables.insert("health");
    lua.authoredVariablesInitialized = true;

    auto& runtimeMesh = entity.AddComponent<MeshRendererComponent>();
    runtimeMesh.runtimeGenerated = true;
    runtimeMesh.meshAssetPath = character.characterPath;

    const fs::path prefabPath = outDir / "Cultist.hpprefab";
    Entity instance;
    std::string error;
    ok &= Check(PrefabOverrideSystem::CreateFromSelection(scene, entity, prefabPath, &instance, error),
                "create prefab from character: " + error);
    if (!instance) {
        return false;
    }
    ok &= Check(instance.HasComponent<CharacterComponent>(), "prefab instance has Character");
    ok &= Check(instance.HasComponent<LuaScriptComponent>(), "prefab instance has LuaScript");
    if (instance.HasComponent<CharacterComponent>()) {
        const auto& captured = instance.GetComponent<CharacterComponent>();
        ok &= Check(captured.runController, "prefab keeps runController");
        ok &= Check(captured.controllerScriptPath == "assets/scripts/CultistCharacter.lua",
                    "prefab keeps controllerScriptPath");
        ok &= Check(captured.controllerVariables.count("WalkThreshold") == 1,
                    "prefab keeps controllerVariables");
    }
    ok &= Check(!instance.HasComponent<MeshRendererComponent>() ||
                    IsRuntimeGeneratedVisual(instance.GetComponent<MeshRendererComponent>()),
                "prefab capture did not author a designer MeshRenderer");

    PrefabAsset asset;
    ok &= Check(PrefabSerializer::Deserialize(asset, prefabPath, error), "load prefab: " + error);
    bool capturedMesh = false;
    bool capturedCharacter = false;
    bool capturedLua = false;
    for (const auto& node : asset.nodes) {
        for (const auto& component : node.components) {
            capturedMesh = capturedMesh || component.type == "MeshRenderer";
            capturedCharacter = capturedCharacter || component.type == "Character";
            capturedLua = capturedLua || component.type == "LuaScript";
        }
    }
    ok &= Check(!capturedMesh, "prefab asset omits runtime MeshRenderer");
    ok &= Check(capturedCharacter, "prefab asset has Character");
    ok &= Check(capturedLua, "prefab asset has LuaScript");

    instance.GetComponent<CharacterComponent>().defaultAnimation = "Run";
    PrefabOverrideSystem::SyncEntityOverrides(scene, instance);
    ok &= Check(PrefabOverrideSystem::ApplyPropertyToPrefab(
                    scene, instance, "Character", "defaultAnimation", error),
                "apply character clip: " + error);

    Entity afterApply = scene.FindEntityByName("Cultist");
    if (!afterApply) {
        auto view = scene.GetEntitiesWith<PrefabInstanceComponent>();
        for (auto handle : view) {
            afterApply = Entity(handle, &scene);
            break;
        }
    }
    ok &= Check(static_cast<bool>(afterApply) && afterApply.HasComponent<LuaScriptComponent>(),
                "instance still has LuaScript after Character apply");
    if (afterApply && afterApply.HasComponent<LuaScriptComponent>()) {
        ok &= Check(afterApply.GetComponent<LuaScriptComponent>().scriptPath ==
                        "assets/scripts/EnemyAI.lua",
                    "lua path not clobbered by character apply");
    }
    if (afterApply && afterApply.HasComponent<CharacterComponent>()) {
        const auto& applied = afterApply.GetComponent<CharacterComponent>();
        ok &= Check(applied.defaultAnimation == "Run", "applied defaultAnimation is Run");
        ok &= Check(applied.controllerScriptPath == "assets/scripts/CultistCharacter.lua",
                    "controller path not clobbered by Character apply");
        ok &= Check(applied.runController, "runController not clobbered by Character apply");
    }
    return ok;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "hephaestus_character_workflow_tests";
    fs::remove_all(root);
    fs::create_directories(root);

    bool ok = true;
    ok &= TestRefuseGltf(root / "Gltf");
    ok &= TestImportBindSerialize(root / "ImportProj");
    ok &= TestReimportCases(root / "ReimportProj");
    ok &= TestHideSourceListing(root / "HideProj");
    ok &= TestPrefabCharacterLuaCoexistence(root / "PrefabProj");

    CharacterRuntime::InvalidateAll();
    if (!ok) {
        std::cerr << "Character workflow tests failed\n";
        return 1;
    }
    std::cout << "Character workflow tests passed\n";
    return 0;
}
