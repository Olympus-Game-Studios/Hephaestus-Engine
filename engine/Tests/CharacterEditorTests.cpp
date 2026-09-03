/**
 * @file CharacterEditorTests.cpp
 * @brief Stage F Character Editor document, import, playback, socket, and live-bind tests.
 */

#include "Animation/AnimationClip.h"
#include "Animation/CharacterPlayback.h"
#include "Animation/Skeleton.h"
#include "Assets/CharacterAsset.h"
#include "Assets/CharacterImport.h"
#include "Assets/CharacterRuntime.h"
#include "Scene/CharacterComponent.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "UI/CharacterDocument.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Hephaestus::AnimationChannel;
using Hephaestus::AnimationClip;
using Hephaestus::AnimationInterpolation;
using Hephaestus::AnimationPath;
using Hephaestus::CharacterAnimationSystem;
using Hephaestus::CharacterAsset;
using Hephaestus::CharacterClipEntry;
using Hephaestus::CharacterDocument;
using Hephaestus::CharacterImportRequest;
using Hephaestus::CharacterImporter;
using Hephaestus::CharacterRuntime;
using Hephaestus::CharacterSerializer;
using Hephaestus::CharacterSocket;
using Hephaestus::Skeleton;
using Hephaestus::SkeletonBone;
using Hephaestus::SkeletonBuilder;

namespace Hephaestus {

bool CharacterRuntime::UploadVisual(Entity, const std::filesystem::path&, EngineFrameRenderer*,
                                    std::string& outError, bool) {
    outError.clear();
    return true;
}

bool CharacterRuntime::BindScene(Scene&, const std::filesystem::path&, EngineFrameRenderer*) {
    return true;
}

} // namespace Hephaestus

namespace {

constexpr float kSqrtHalf = 0.7071067811865476f;

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

bool AlmostEqual(float a, float b, float eps = 1e-4f) {
    return std::abs(a - b) <= eps;
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

bool ImportHero(const fs::path& project, std::string& portable, std::string& error) {
    const fs::path glb = project / "source.glb";
    if (!WriteSkinnedGlb(glb, "Child", "Idle")) {
        error = "write source glb";
        return false;
    }
    CharacterImportRequest request;
    request.sourceGlb = glb;
    request.projectRoot = project;
    request.destinationFolderAbs = project / "assets" / "characters";
    request.characterName = "Hero";
    request.createController = false;
    return CharacterImporter::Import(request, portable, error);
}

bool TestDocumentDirtySave(const fs::path& project) {
    bool ok = true;
    std::string portable;
    std::string error;
    ok &= Check(ImportHero(project, portable, error), "import hero: " + error);
    const fs::path assetPath = project / "assets" / "characters" / "Hero.hpcharacter";

    Scene scene("CharacterEditor");
    CharacterDocument doc(&scene);
    doc.SetProjectRoot(project);
    ok &= Check(doc.OnOpen(assetPath, error), "open document: " + error);
    ok &= Check(!doc.IsDirty(), "fresh document is clean");

    const CharacterClipEntry* idle = doc.FindClipById("Idle");
    ok &= Check(idle != nullptr, "imported Idle clip exists");
    const std::string originalId = idle ? idle->id : "";
    ok &= Check(doc.SetClipDisplayName("Idle", "Idle Loop", error), "rename display: " + error);
    ok &= Check(doc.SetClipFolder("Idle", "Locomotion", error), "set folder: " + error);
    ok &= Check(doc.FindClipById("Idle") && doc.FindClipById("Idle")->id == originalId,
                "clip.id is unchanged after rename/folder");
    ok &= Check(doc.IsDirty(), "rename/folder dirties document");
    ok &= Check(doc.OnSave(error), "save document: " + error);
    ok &= Check(!doc.IsDirty(), "saved document is clean");

    CharacterAsset reloaded;
    ok &= Check(CharacterSerializer::Deserialize(reloaded, assetPath, error), "reload: " + error);
    bool found = false;
    for (const auto& clip : reloaded.clips) {
        if (clip.id == "Idle") {
            found = true;
            ok &= Check(clip.displayName == "Idle Loop", "persisted display name");
            ok &= Check(clip.folder == "Locomotion", "persisted folder");
        }
    }
    ok &= Check(found, "Idle clip persisted");
    return ok;
}

bool TestImportUndoLeavesGlb(const fs::path& project) {
    bool ok = true;
    std::string portable;
    std::string error;
    ok &= Check(ImportHero(project, portable, error), "import hero: " + error);
    const fs::path assetPath = project / "assets" / "characters" / "Hero.hpcharacter";

    Scene scene("ImportUndo");
    CharacterDocument doc(&scene);
    doc.SetProjectRoot(project);
    ok &= Check(doc.OnOpen(assetPath, error), "open: " + error);

    const fs::path walkGlb = project / "walk.glb";
    ok &= Check(WriteSkinnedGlb(walkGlb, "Child", "Walk"), "write walk glb");
    doc.BeginAuthoredEdit();
    std::vector<std::string> added;
    ok &= Check(CharacterImporter::ImportAnimationClips(project, doc.GetAsset(), portable, walkGlb,
                                                        {"Walk"}, "Locomotion", added, error),
                "import compatible Walk: " + error);
    doc.MarkAuthoredChanged();
    ok &= Check(!added.empty() && doc.FindClipById(added.front()) != nullptr, "Walk clip metadata added");
    const fs::path ownedWalk = project / "assets" / "characters" / ".source" / "Hero" / "walk.glb";
    ok &= Check(fs::exists(ownedWalk), "owned walk.glb copied");

    const fs::path badGlb = project / "bad.glb";
    ok &= Check(WriteSkinnedGlb(badGlb, "Leg", "Run"), "write incompatible glb");
    const size_t clipCount = doc.GetAsset().clips.size();
    std::vector<std::string> refused;
    ok &= Check(!CharacterImporter::ImportAnimationClips(project, doc.GetAsset(), portable, badGlb,
                                                         {"Run"}, {}, refused, error),
                "incompatible import refused");
    ok &= Check(doc.GetAsset().clips.size() == clipCount, "refused import does not add clips");

    ok &= Check(doc.Undo(error), "undo import metadata: " + error);
    ok &= Check(doc.FindClipById("Walk") == nullptr, "undo removes clip row");
    ok &= Check(fs::exists(ownedWalk), "undo leaves owned GLB on disk");
    return ok;
}

bool TestSampleAtTimeClamp() {
    bool ok = true;
    Skeleton skeleton;
    std::string error;
    SkeletonBone root;
    root.name = "Root";
    root.parentEval = -1;
    root.sourceJoint = 0;
    root.localRest = Mat4::Identity();
    root.inverseBind = Mat4::Identity();
    root.restTranslation = Vec3(0, 0, 0);
    root.restRotation = Vec4(0, 0, 0, 1);
    root.restScale = Vec3(1, 1, 1);
    skeleton.bones = {root};
    ok &= Check(SkeletonBuilder::Finalize(skeleton, error), "finalize skeleton: " + error);

    AnimationClip clip;
    clip.name = "Idle";
    clip.id = "Idle";
    clip.sourceClip = "Idle";
    clip.skeletonSignature = skeleton.signature;
    clip.duration = 1.0f;
    clip.startTime = 0.25f;
    clip.endTime = 0.75f;
    clip.loop = true;

    Scene scene("Sample");
    Entity entity = scene.CreateEntity("Preview");
    auto& character = entity.AddComponent<CharacterComponent>();
    ok &= Check(CharacterAnimationSystem::BindRuntime(character, skeleton, {clip}, error),
                "bind runtime: " + error);
    ok &= Check(CharacterAnimationSystem::SampleAtTime(character, "Idle", -1.0f, error),
                "sample below trim: " + error);
    ok &= Check(AlmostEqual(character.player.time, 0.0f), "below trim clamps to start");
    ok &= Check(CharacterAnimationSystem::SampleAtTime(character, "Idle", 4.0f, error),
                "sample above trim: " + error);
    ok &= Check(AlmostEqual(character.player.time, 0.5f), "above trim clamps to end-start");
    return ok;
}

bool TestSocketParentKeepsLocal(const fs::path& project) {
    bool ok = true;
    std::string portable;
    std::string error;
    ok &= Check(ImportHero(project, portable, error), "import hero: " + error);
    const fs::path assetPath = project / "assets" / "characters" / "Hero.hpcharacter";

    Scene scene("Sockets");
    CharacterDocument doc(&scene);
    doc.SetProjectRoot(project);
    ok &= Check(doc.OnOpen(assetPath, error), "open: " + error);
    ok &= Check(doc.CreateSocket("Weapon_R", "Child", error), "create socket: " + error);
    const float pos[3] = {0.0f, 0.1f, 0.0f};
    const float rot[3] = {0.1f, 0.2f, 0.3f};
    ok &= Check(doc.SetSocketTransform("Weapon_R", pos, rot, error), "set transform: " + error);
    ok &= Check(doc.SetSocketBone("Weapon_R", "Root", error), "reparent: " + error);
    const CharacterSocket* socket = doc.FindSocketByName("Weapon_R");
    ok &= Check(socket != nullptr, "socket exists");
    if (socket) {
        ok &= Check(socket->bone == "Root", "parent bone updated");
        ok &= Check(AlmostEqual(socket->position[1], 0.1f), "local position preserved");
        ok &= Check(AlmostEqual(socket->rotation[2], 0.3f), "local rotation preserved");
    }
    return ok;
}

bool TestReimportRefusal(const fs::path& project) {
    bool ok = true;
    std::string portable;
    std::string error;
    ok &= Check(ImportHero(project, portable, error), "import hero: " + error);
    const fs::path bad = project / "incompatible.glb";
    ok &= Check(WriteSkinnedGlb(bad, "Leg", "Idle"), "write incompatible");
    ok &= Check(!CharacterImporter::Reimport(project, portable, bad, error),
                "incompatible reimport refused");
    ok &= Check(!error.empty(), "reimport error is set");
    return ok;
}

bool TestBindEntityDoesNotClobber(const fs::path& project) {
    bool ok = true;
    std::string portable;
    std::string error;
    ok &= Check(ImportHero(project, portable, error), "import hero: " + error);

    Scene scene("Live");
    Entity entity = scene.CreateEntity("Instance");
    auto& character = entity.AddComponent<CharacterComponent>();
    character.characterPath = portable;
    ok &= Check(CharacterRuntime::BindEntity(entity, project, true, error), "first bind: " + error);
    character.defaultAnimation = "Idle";
    character.playbackSpeed = 2.5f;
    character.runController = false;
    character.controllerScriptPath = "assets/scripts/Instance.lua";
    character.materialOverrides = {"assets/materials/Override.hematerial"};

    CharacterAsset asset;
    const fs::path assetPath = project / "assets" / "characters" / "Hero.hpcharacter";
    ok &= Check(CharacterSerializer::Deserialize(asset, assetPath, error), "load asset: " + error);
    asset.defaults.animation.clear();
    asset.defaults.playbackSpeed = 9.0f;
    asset.controller.enabled = true;
    asset.controller.scriptPath = "assets/scripts/Asset.lua";
    ok &= Check(CharacterSerializer::Serialize(asset, assetPath, error), "write new defaults: " + error);
    CharacterRuntime::Invalidate(portable);
    ok &= Check(CharacterRuntime::BindEntity(entity, project, false, error), "rebind no defaults: " + error);
    ok &= Check(character.defaultAnimation == "Idle", "instance defaultAnimation kept");
    ok &= Check(AlmostEqual(character.playbackSpeed, 2.5f), "instance playbackSpeed kept");
    ok &= Check(character.runController == false, "instance controller enabled kept");
    ok &= Check(character.controllerScriptPath == "assets/scripts/Instance.lua",
                "instance controller path kept");
    ok &= Check(!character.materialOverrides.empty() &&
                    character.materialOverrides[0] == "assets/materials/Override.hematerial",
                "instance material override kept");
    return ok;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "hephaestus_character_editor_tests";
    fs::remove_all(root);
    fs::create_directories(root);

    bool ok = true;
    ok &= TestDocumentDirtySave(root / "Doc");
    ok &= TestImportUndoLeavesGlb(root / "ImportUndo");
    ok &= TestSampleAtTimeClamp();
    ok &= TestSocketParentKeepsLocal(root / "Sockets");
    ok &= TestReimportRefusal(root / "Reimport");
    ok &= TestBindEntityDoesNotClobber(root / "LiveBind");

    CharacterRuntime::InvalidateAll();
    if (!ok) {
        std::cerr << "Character editor tests failed\n";
        return 1;
    }
    std::cout << "Character editor tests passed\n";
    return 0;
}
