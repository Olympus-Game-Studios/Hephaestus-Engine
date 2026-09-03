/**
 * @file CharacterPlaybackTests.cpp
 * @brief Stage D clip sampler, same-frame Play, loop/stop, serialize, CrossFade.
 */

#include "Animation/AnimationClip.h"
#include "Animation/CharacterPlayback.h"
#include "Animation/Skeleton.h"
#include "Assets/CharacterAsset.h"
#include "Assets/SkeletalModelImporter.h"
#include "Scene/CharacterComponent.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/PrefabComponentIO.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

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
using Hephaestus::BoneLocalPose;
using Hephaestus::CharacterAnimationSystem;
using Hephaestus::CharacterSocket;
using Hephaestus::SkeletalImportResult;
using Hephaestus::SkeletalModelImporter;
using Hephaestus::Skeleton;
using Hephaestus::SkeletonBone;
using Hephaestus::SkeletonBuilder;

namespace {

constexpr float kSqrtHalf = 0.7071067811865476f;

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

bool AlmostEqual(float a, float b, float eps = 1e-3f) {
    return std::abs(a - b) <= eps;
}

bool MatricesClose(const Mat4& a, const Mat4& b, float eps = 1e-3f) {
    for (int i = 0; i < 16; ++i) {
        if (!AlmostEqual(a.m[i], b.m[i], eps)) {
            return false;
        }
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

bool MakeTwoBoneSkeleton(Skeleton& skeleton, std::string& error) {
    skeleton = {};
    SkeletonBone root;
    root.name = "Root";
    root.parentEval = -1;
    root.sourceJoint = 0;
    root.localRest = Mat4::Identity();
    root.inverseBind = Mat4::Identity();
    root.restTranslation = Vec3(0.0f, 0.0f, 0.0f);
    root.restRotation = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    root.restScale = Vec3(1.0f, 1.0f, 1.0f);
    SkeletonBone child;
    child.name = "Child";
    child.parentEval = 0;
    child.sourceJoint = 1;
    child.localRest = Mat4::Translation(Vec3(0.0f, 1.0f, 0.0f));
    child.inverseBind = Mat4::Translation(Vec3(0.0f, -1.0f, 0.0f));
    child.restTranslation = Vec3(0.0f, 1.0f, 0.0f);
    child.restRotation = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    child.restScale = Vec3(1.0f, 1.0f, 1.0f);
    skeleton.bones = {root, child};
    return SkeletonBuilder::Finalize(skeleton, error);
}

AnimationClip MakeIdle(const Skeleton& skeleton) {
    AnimationClip clip;
    clip.name = "Idle";
    clip.id = "Idle";
    clip.sourceClip = "Idle";
    clip.skeletonSignature = skeleton.signature;
    clip.duration = 1.0f;
    clip.endTime = 1.0f;
    clip.loop = true;
    return clip;
}

AnimationClip MakeRun(const Skeleton& skeleton) {
    AnimationClip clip;
    clip.name = "Run";
    clip.id = "Run";
    clip.sourceClip = "Run";
    clip.skeletonSignature = skeleton.signature;
    clip.duration = 1.0f;
    clip.endTime = 1.0f;
    clip.loop = true;
    AnimationChannel channel;
    channel.evalBone = skeleton.FindBone("Child")->evalIndex;
    channel.path = AnimationPath::Translation;
    channel.interpolation = AnimationInterpolation::Linear;
    channel.times = {0.0f, 1.0f};
    channel.vectors = {Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 2.0f, 0.0f)};
    clip.channels.push_back(channel);
    return clip;
}

AnimationClip MakeDeath(const Skeleton& skeleton) {
    AnimationClip clip;
    clip.name = "Death";
    clip.id = "Death";
    clip.sourceClip = "Death";
    clip.skeletonSignature = skeleton.signature;
    clip.duration = 0.5f;
    clip.endTime = 0.5f;
    clip.loop = false;
    return clip;
}

Vec3 ChildWorldTranslation(const CharacterComponent& character) {
    const SkeletonBone* child = character.skeleton.FindBone("Child");
    if (!child || child->evalIndex < 0 ||
        child->evalIndex >= static_cast<int>(character.worldPose.size())) {
        return Vec3(0, 0, 0);
    }
    const Mat4& world = character.worldPose[static_cast<size_t>(child->evalIndex)];
    return Vec3(world.m[12], world.m[13], world.m[14]);
}

bool WriteAnimatedGlb(const fs::path& path) {
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
        "{\"name\":\"Child\",\"translation\":[0,1,0],\"rotation\":[0,0," +
        std::to_string(kSqrtHalf) + "," + std::to_string(kSqrtHalf) + "]},"
        "{\"translation\":[0,0,0],\"mesh\":0,\"skin\":0}"
        "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
        "\"JOINTS_0\":2,\"WEIGHTS_0\":3},\"indices\":4}]}],"
        "\"skins\":[{\"name\":\"Armature\",\"joints\":[0,1],\"inverseBindMatrices\":5}],"
        "\"animations\":[{\"name\":\"Idle\",\"samplers\":[{\"input\":6,\"output\":7,"
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

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(file.data()),
              static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(out);
}

bool TestImportSampleTimes(const fs::path& scratch) {
    bool ok = true;
    const fs::path path = scratch / "animated.glb";
    if (!WriteAnimatedGlb(path)) {
        return Check(false, "write animated glb");
    }
    SkeletalImportResult result;
    std::string error;
    ok &= Check(SkeletalModelImporter::Load(path, {}, result, error), "load animated: " + error);
    ok &= Check(result.clips.size() == 1, "imported one clip");
    if (result.clips.empty()) {
        return false;
    }
    const AnimationClip& clip = result.clips[0];
    ok &= Check(clip.name == "Idle", "clip name Idle");
    ok &= Check(AlmostEqual(clip.duration, 1.0f), "duration 1");
    std::vector<BoneLocalPose> pose0;
    std::vector<BoneLocalPose> pose1;
    SampleClip(clip, result.skeleton, 0.0f, pose0);
    SampleClip(clip, result.skeleton, 1.0f, pose1);
    const int child = result.skeleton.FindBone("Child")->evalIndex;
    ok &= Check(AlmostEqual(pose0[static_cast<size_t>(child)].rotation.z, kSqrtHalf) &&
                    AlmostEqual(pose0[static_cast<size_t>(child)].rotation.w, kSqrtHalf),
                "t=0 child rest RotZ(90)");
    ok &= Check(AlmostEqual(pose1[static_cast<size_t>(child)].rotation.x, 0.0f) &&
                    AlmostEqual(pose1[static_cast<size_t>(child)].rotation.y, 0.0f) &&
                    AlmostEqual(pose1[static_cast<size_t>(child)].rotation.z, 0.0f) &&
                    AlmostEqual(pose1[static_cast<size_t>(child)].rotation.w, 1.0f),
                "t=end child identity rotation");
    return ok;
}

bool TestSameFramePlayAndLoopStop() {
    bool ok = true;
    Skeleton skeleton;
    std::string error;
    ok &= Check(MakeTwoBoneSkeleton(skeleton, error), "skeleton: " + error);
    CharacterComponent character;
    ok &= Check(CharacterAnimationSystem::BindRuntime(
                    character, skeleton, {MakeIdle(skeleton), MakeRun(skeleton)}, error),
                "bind: " + error);

    ok &= Check(CharacterAnimationSystem::Play(character, "Run", error), "play Run: " + error);
    CharacterAnimationSystem::TickEntity(character, 0.16f);
    Vec3 runT0 = ChildWorldTranslation(character);
    ok &= Check(AlmostEqual(runT0.y, 1.0f), "same-frame Play samples Run at t=0 (y=1)");
    ok &= Check(character.player.currentClip == "Run", "current clip is Run");
    ok &= Check(character.player.playing, "playing after Play");

    CharacterAnimationSystem::TickEntity(character, 0.999f);
    Vec3 runT1 = ChildWorldTranslation(character);
    ok &= Check(AlmostEqual(runT1.y, 2.0f, 0.02f), "Run near clip end (y~2)");

    CharacterAnimationSystem::TickEntity(character, 0.25f);
    ok &= Check(character.player.playing, "loop keeps playing");
    ok &= Check(character.player.time < 1.0f, "loop wrapped below duration");

    CharacterAnimationSystem::Stop(character);
    ok &= Check(!character.player.playing, "Stop clears playing");
    ok &= Check(AlmostEqual(character.player.time, 0.0f), "Stop resets time");
    Vec3 stopped = ChildWorldTranslation(character);
    ok &= Check(AlmostEqual(stopped.y, 1.0f), "Stop samples clip start (y=1)");
    return ok;
}

bool TestIncompatibleClipRefused() {
    bool ok = true;
    Skeleton skeleton;
    std::string error;
    ok &= Check(MakeTwoBoneSkeleton(skeleton, error), "skeleton");
    CharacterComponent character;
    AnimationClip bad = MakeRun(skeleton);
    bad.skeletonSignature = "deadbeefdeadbeef";
    ok &= Check(CharacterAnimationSystem::BindRuntime(character, skeleton, {MakeIdle(skeleton), bad},
                                                      error),
                "bind");
    ok &= Check(!CharacterAnimationSystem::Play(character, "Run", error),
                "incompatible Run is refused");
    ok &= Check(!error.empty(), "incompatible error message");
    ok &= Check(character.player.currentClip.empty(), "current clip unchanged");
    return ok;
}

bool TestCrossFadeMidpoint() {
    bool ok = true;
    Skeleton skeleton;
    std::string error;
    ok &= Check(MakeTwoBoneSkeleton(skeleton, error), "skeleton");
    CharacterComponent character;
    ok &= Check(CharacterAnimationSystem::BindRuntime(
                    character, skeleton, {MakeIdle(skeleton), MakeRun(skeleton)}, error),
                "bind");
    ok &= Check(CharacterAnimationSystem::Play(character, "Run", error), "play Run");
    CharacterAnimationSystem::TickEntity(character, 0.0f);
    CharacterAnimationSystem::TickEntity(character, 0.999f);
    Vec3 runEnd = ChildWorldTranslation(character);
    ok &= Check(AlmostEqual(runEnd.y, 2.0f), "Run at end before fade (y=2)");
    ok &= Check(CharacterAnimationSystem::CrossFade(character, "Idle", 0.2f, error),
                "crossfade Idle");
    CharacterAnimationSystem::TickEntity(character, 0.0f);
    Vec3 atCommit = ChildWorldTranslation(character);
    ok &= Check(AlmostEqual(atCommit.y, 2.0f), "CrossFade commit frame still source pose");
    CharacterAnimationSystem::TickEntity(character, 0.1f);
    Vec3 mid = ChildWorldTranslation(character);
    ok &= Check(mid.y > 1.05f && mid.y < 1.95f, "CrossFade midpoint blends Child Y");
    return ok;
}

bool TestSceneSerializeCharacter() {
    bool ok = true;
    Scene scene("CharacterSerialize");
    Entity entity = scene.CreateEntity("Cultist");
    auto& character = entity.AddComponent<CharacterComponent>();
    character.characterPath = "assets/characters/Cultist.hpcharacter";
    character.defaultAnimation = "Idle";
    character.playbackSpeed = 1.25f;
    character.runController = false;
    character.controllerScriptPath = "assets/characters/.source/Cultist/CultistCharacter.lua";
    character.controllerVariables["WalkSpeedThreshold"] = PropertyValue(0.1);
    character.authoredControllerVariables.insert("WalkSpeedThreshold");
    character.player.time = 3.5f;
    character.player.playing = true;

    SceneSerializer serializer(scene);
    const std::string snapshot = serializer.SerializeToString();
    ok &= Check(snapshot.find("\"Character\"") != std::string::npos, "scene JSON has Character");
    ok &= Check(snapshot.find("3.5") == std::string::npos, "playback time is not serialized");

    Scene loaded("Loaded");
    ok &= Check(SceneSerializer::DeserializeFromString(loaded, snapshot), "deserialize snapshot");
    Entity found = loaded.FindEntityByName("Cultist");
    ok &= Check(static_cast<bool>(found) && found.HasComponent<CharacterComponent>(),
                "loaded Cultist has Character");
    if (!found || !found.HasComponent<CharacterComponent>()) {
        return false;
    }
    const auto& loadedCharacter = found.GetComponent<CharacterComponent>();
    ok &= Check(loadedCharacter.characterPath == character.characterPath, "characterPath");
    ok &= Check(loadedCharacter.defaultAnimation == "Idle", "defaultAnimation");
    ok &= Check(AlmostEqual(loadedCharacter.playbackSpeed, 1.25f), "playbackSpeed");
    ok &= Check(loadedCharacter.runController == false, "runController");
    ok &= Check(loadedCharacter.controllerScriptPath == character.controllerScriptPath,
                "controllerScriptPath");
    ok &= Check(loadedCharacter.authoredControllerVariables.count("WalkSpeedThreshold") == 1,
                "authored controller variable");
    ok &= Check(AlmostEqual(loadedCharacter.player.time, 0.0f), "runtime time not restored");

    PrefabComponentEntry entry;
    ok &= Check(PrefabComponentIO::CaptureComponent(entity, "Character", entry), "prefab capture");
    Scene other("Other");
    Entity copy = other.CreateEntity("Copy");
    ok &= Check(PrefabComponentIO::ApplyEntry(copy, entry), "prefab apply");
    ok &= Check(copy.HasComponent<CharacterComponent>(), "applied Character");
    ok &= Check(copy.GetComponent<CharacterComponent>().defaultAnimation == "Idle",
                "applied defaultAnimation");
    return ok;
}

bool TestRepeatedPlayDoesNotRestart() {
    bool ok = true;
    Skeleton skeleton;
    std::string error;
    ok &= Check(MakeTwoBoneSkeleton(skeleton, error), "skeleton");
    CharacterComponent character;
    ok &= Check(CharacterAnimationSystem::BindRuntime(character, skeleton, {MakeIdle(skeleton)}, error),
                "bind");
    ok &= Check(AlmostEqual(character.player.speed, 1.0f), "runtime speed defaults to 1");
    ok &= Check(CharacterAnimationSystem::Play(character, "Idle", error), "play Idle");
    CharacterAnimationSystem::TickEntity(character, 0.0f);
    CharacterAnimationSystem::TickEntity(character, 0.25f);
    const float timeAfterAdvance = character.player.time;
    ok &= Check(timeAfterAdvance > 0.1f, "time advanced");
    ok &= Check(CharacterAnimationSystem::Play(character, "Idle", error), "repeated Play Idle");
    ok &= Check(!character.player.hasPendingPlay, "repeated Play is a no-op");
    CharacterAnimationSystem::TickEntity(character, 0.0f);
    ok &= Check(AlmostEqual(character.player.time, timeAfterAdvance), "repeated Play did not restart");
    return ok;
}

bool TestRepeatedCrossFadeDoesNotRestart() {
    bool ok = true;
    Skeleton skeleton;
    std::string error;
    ok &= Check(MakeTwoBoneSkeleton(skeleton, error), "skeleton");
    CharacterComponent character;
    ok &= Check(CharacterAnimationSystem::BindRuntime(
                    character, skeleton, {MakeIdle(skeleton), MakeRun(skeleton)}, error),
                "bind");
    ok &= Check(CharacterAnimationSystem::Play(character, "Run", error), "play Run");
    CharacterAnimationSystem::TickEntity(character, 0.0f);
    ok &= Check(CharacterAnimationSystem::CrossFade(character, "Idle", 0.4f, error), "crossfade Idle");
    CharacterAnimationSystem::TickEntity(character, 0.0f);
    ok &= Check(character.player.currentClip == "Idle", "fade destination is current clip");
    ok &= Check(character.player.fading, "fading after CrossFade");
    ok &= Check(CharacterAnimationSystem::CrossFade(character, "Idle", 0.4f, error),
                "repeated CrossFade Idle");
    ok &= Check(!character.player.hasPendingFade, "repeated CrossFade is a no-op");
    CharacterAnimationSystem::TickEntity(character, 0.1f);
    ok &= Check(character.player.fading, "still fading");
    ok &= Check(character.player.fadeElapsed > 0.05f, "fade progress was not reset");
    return ok;
}

bool TestSetSpeedLockdown() {
    bool ok = true;
    Skeleton skeleton;
    std::string error;
    ok &= Check(MakeTwoBoneSkeleton(skeleton, error), "skeleton");
    CharacterComponent character;
    character.playbackSpeed = 2.0f;
    AnimationClip idle = MakeIdle(skeleton);
    idle.speed = 1.0f;
    ok &= Check(CharacterAnimationSystem::BindRuntime(character, skeleton, {idle}, error), "bind");
    ok &= Check(AlmostEqual(character.player.speed, 1.0f), "bind does not copy playbackSpeed into player.speed");
    ok &= Check(CharacterAnimationSystem::Play(character, "Idle", error), "play");
    CharacterAnimationSystem::TickEntity(character, 0.0f);
    CharacterAnimationSystem::TickEntity(character, 0.1f);
    ok &= Check(AlmostEqual(character.player.time, 0.2f, 0.02f), "rate = dt * 1 * playbackSpeed * clip.speed");
    ok &= Check(CharacterAnimationSystem::SetSpeed(character, 1.5f), "SetSpeed 1.5");
    ok &= Check(AlmostEqual(character.player.speed, 1.5f), "runtime speed is 1.5");
    CharacterAnimationSystem::Stop(character);
    ok &= Check(AlmostEqual(character.player.speed, 1.5f), "Stop preserves runtime speed");
    ok &= Check(CharacterAnimationSystem::Play(character, "Idle", error), "play after stop");
    ok &= Check(AlmostEqual(character.player.speed, 1.5f), "Play preserves runtime speed");
    ok &= Check(!CharacterAnimationSystem::SetSpeed(character, 0.0f), "SetSpeed 0 is rejected");
    ok &= Check(!CharacterAnimationSystem::SetSpeed(character, -1.0f), "SetSpeed negative is rejected");
    ok &= Check(AlmostEqual(character.player.speed, 1.5f), "rejected SetSpeed leaves previous value");
    ok &= Check(CharacterAnimationSystem::BindRuntime(character, skeleton, {idle}, error), "rebind");
    ok &= Check(AlmostEqual(character.player.speed, 1.0f), "BindRuntime resets runtime speed");
    return ok;
}

bool TestCompletionFlags() {
    bool ok = true;
    Skeleton skeleton;
    std::string error;
    ok &= Check(MakeTwoBoneSkeleton(skeleton, error), "skeleton");
    CharacterComponent character;
    ok &= Check(CharacterAnimationSystem::BindRuntime(
                    character, skeleton, {MakeIdle(skeleton), MakeDeath(skeleton)}, error),
                "bind");
    ok &= Check(CharacterAnimationSystem::Play(character, "Death", error), "play Death");
    CharacterAnimationSystem::TickEntity(character, 0.0f);
    CharacterAnimationSystem::TickEntity(character, 0.5f);
    ok &= Check(character.player.finishedThisFrame, "non-looping clip finished this frame");
    ok &= Check(character.player.finishedClip == "Death", "finished clip id");
    ok &= Check(!character.player.playing, "not playing after finish");
    CharacterAnimationSystem::TickEntity(character, 0.016f);
    ok &= Check(!character.player.finishedThisFrame, "finished flag does not repeat");
    ok &= Check(character.player.currentClip == "Death", "finished clip remains current");

    ok &= Check(CharacterAnimationSystem::Play(character, "Idle", error), "play looping Idle");
    CharacterAnimationSystem::TickEntity(character, 0.0f);
    CharacterAnimationSystem::TickEntity(character, 1.5f);
    ok &= Check(!character.player.finishedThisFrame, "looping clip does not finish");
    ok &= Check(character.player.playing, "looping clip still playing");

    ok &= Check(CharacterAnimationSystem::Play(character, "Death", error), "replay Death");
    CharacterAnimationSystem::TickEntity(character, 0.0f);
    ok &= Check(character.player.playing, "Play of finished clip restarts");
    character.playbackSpeed = 50.0f;
    CharacterAnimationSystem::TickEntity(character, 0.1f);
    ok &= Check(character.player.finishedThisFrame, "high speed still finishes once");
    return ok;
}

bool TestEvaluateSocketModelSpace() {
    bool ok = true;
    Skeleton skeleton;
    std::string error;
    ok &= Check(MakeTwoBoneSkeleton(skeleton, error), "skeleton");
    CharacterComponent character;
    ok &= Check(CharacterAnimationSystem::BindRuntime(
                    character, skeleton, {MakeIdle(skeleton)}, error),
                "bind");
    CharacterSocket socket;
    socket.name = "Weapon_R";
    socket.bone = "Child";
    socket.position[1] = 1.0f;
    Mat4 model{};
    ok &= Check(Hephaestus::EvaluateSocketWorld(character, socket, model, error),
                "evaluate socket: " + error);
    ok &= Check(AlmostEqual(model.m[12], 0.0f) && AlmostEqual(model.m[13], 2.0f) &&
                    AlmostEqual(model.m[14], 0.0f),
                "socket model space is bone world times local offset, not the authored (0,1,0)");
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    const fs::path scratch = fs::temp_directory_path() / "hephaestus_character_playback";
    fs::create_directories(scratch);
    ok &= TestImportSampleTimes(scratch);
    ok &= TestSameFramePlayAndLoopStop();
    ok &= TestIncompatibleClipRefused();
    ok &= TestCrossFadeMidpoint();
    ok &= TestSceneSerializeCharacter();
    ok &= TestRepeatedPlayDoesNotRestart();
    ok &= TestRepeatedCrossFadeDoesNotRestart();
    ok &= TestSetSpeedLockdown();
    ok &= TestCompletionFlags();
    ok &= TestEvaluateSocketModelSpace();
    if (ok) {
        std::cout << "Character playback tests passed.\n";
        return 0;
    }
    return 1;
}
