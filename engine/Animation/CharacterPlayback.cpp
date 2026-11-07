/**
 * @file CharacterPlayback.cpp
 * @brief Same-frame clip commit + pose evaluation for CharacterComponent.
 */

#include "CharacterPlayback.h"

#include "../Scene/CharacterComponent.h"
#include "../Scene/Components.h"
#include "../Scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace Hephaestus {
namespace {

const AnimationClip* CurrentClip(const CharacterComponent& character) {
    std::string error;
    if (character.player.currentClip.empty()) {
        return nullptr;
    }
    return FindClip(character.clips, character.player.currentClip, error);
}

void AdvancePlayer(CharacterComponent& character, float deltaTime) {
    AnimationPlayer& player = character.player;
    player.finishedThisFrame = false;
    if (player.paused || !player.playing) {
        return;
    }

    const AnimationClip* clip = CurrentClip(character);
    if (!clip) {
        return;
    }

    const float start = ClipRangeStart(*clip);
    const float end = ClipRangeEnd(*clip);
    const float range = std::max(0.0f, end - start);
    const float rate = deltaTime * player.speed * character.playbackSpeed * clip->speed;

    if (player.committedThisFrame) {
        player.committedThisFrame = false;
        player.time = 0.0f;
        return;
    }

    if (player.fading) {
        player.fadeElapsed += rate;
        player.fadeFromTime += rate;
        if (player.fadeElapsed >= player.fadeDuration) {
            player.fading = false;
            player.fadeFromClip.clear();
        }
    }

    player.time += rate;
    if (range <= 1e-8f) {
        player.time = 0.0f;
        return;
    }

    if (clip->loop) {
        while (player.time >= range) {
            player.time -= range;
        }
        while (player.time < 0.0f) {
            player.time += range;
        }
    } else if (player.time >= range) {
        player.time = range;
        player.playing = false;
        player.finishedThisFrame = true;
        player.finishedClip = clip->id.empty() ? clip->name : clip->id;
    }
}

bool EnsureCompatible(CharacterComponent& character, const AnimationClip& clip, std::string& outError) {
    if (clip.skeletonSignature.empty() || character.skeleton.signature.empty()) {
        return true;
    }
    if (clip.skeletonSignature == character.skeleton.signature) {
        return true;
    }
    outError = "Incompatible animation '" + (clip.id.empty() ? clip.name : clip.id) +
               "': skeleton signature does not match the character.";
    character.lastError = outError;
    return false;
}

} // namespace

bool CharacterAnimationSystem::BindRuntime(CharacterComponent& character,
                                           Skeleton skeleton,
                                           std::vector<AnimationClip> clips,
                                           std::string& outError) {
    outError.clear();
    if (skeleton.BoneCount() <= 0) {
        outError = "Character has no skeleton.";
        return false;
    }
    character.skeleton = std::move(skeleton);
    character.clips = std::move(clips);
    character.player = AnimationPlayer{};
    character.runtimeReady = true;
    character.lastError.clear();
    EvaluatePose(character);
    return true;
}

bool CharacterAnimationSystem::Play(CharacterComponent& character,
                                    const std::string& name,
                                    std::string& outError) {
    if (!character.runtimeReady) {
        outError = "Character runtime is not bound.";
        return false;
    }
    const AnimationClip* clip = FindClip(character.clips, name, outError);
    if (!clip) {
        character.lastError = outError;
        return false;
    }
    if (!EnsureCompatible(character, *clip, outError)) {
        return false;
    }
    const std::string resolvedId = clip->id.empty() ? clip->name : clip->id;
    AnimationPlayer& player = character.player;
    if (player.hasPendingPlay && player.pendingClip == resolvedId) {
        return true;
    }
    if (player.currentClip == resolvedId && player.playing && !player.paused && !player.fading &&
        !player.hasPendingPlay && !player.hasPendingFade) {
        return true;
    }
    character.player.RequestPlay(resolvedId);
    return true;
}

bool CharacterAnimationSystem::CrossFade(CharacterComponent& character,
                                         const std::string& name,
                                         float duration,
                                         std::string& outError) {
    if (!character.runtimeReady) {
        outError = "Character runtime is not bound.";
        return false;
    }
    const AnimationClip* clip = FindClip(character.clips, name, outError);
    if (!clip) {
        character.lastError = outError;
        return false;
    }
    if (!EnsureCompatible(character, *clip, outError)) {
        return false;
    }
    const std::string resolvedId = clip->id.empty() ? clip->name : clip->id;
    if (duration < 0.0f) {
        duration = 0.0f;
    }
    if (duration <= 1e-4f) {
        return Play(character, name, outError);
    }
    AnimationPlayer& player = character.player;
    if (player.hasPendingFade && player.pendingFadeClip == resolvedId) {
        return true;
    }
    if (player.currentClip == resolvedId && player.playing && !player.paused &&
        !player.hasPendingPlay) {
        return true;
    }
    character.player.RequestCrossFade(resolvedId, duration);
    return true;
}

void CharacterAnimationSystem::Pause(CharacterComponent& character) {
    character.player.Pause();
}

void CharacterAnimationSystem::Resume(CharacterComponent& character) {
    character.player.Resume();
}

bool CharacterAnimationSystem::SetSpeed(CharacterComponent& character, float speed) {
    if (!(speed > 0.0f) || !std::isfinite(speed)) {
        return false;
    }
    character.player.SetSpeed(speed);
    return true;
}

bool CharacterAnimationSystem::IsPlaying(const CharacterComponent& character, const char* clipId) {
    const bool playing = character.player.playing && !character.player.paused;
    if (!clipId) {
        return playing;
    }
    std::string error;
    const AnimationClip* clip = FindClip(character.clips, clipId, error);
    const std::string resolved = clip ? (clip->id.empty() ? clip->name : clip->id) : std::string();
    return playing && !resolved.empty() && character.player.currentClip == resolved;
}

float CharacterAnimationSystem::ClipDuration(const CharacterComponent& character, const char* clipId) {
    std::string error;
    const AnimationClip* clip = nullptr;
    if (clipId) {
        clip = FindClip(character.clips, clipId, error);
    } else if (!character.player.currentClip.empty()) {
        clip = FindClip(character.clips, character.player.currentClip, error);
    }
    return clip ? (std::max)(0.0f, ClipRangeEnd(*clip) - ClipRangeStart(*clip)) : 0.0f;
}

void CharacterAnimationSystem::Stop(CharacterComponent& character) {
    character.player.Stop(0.0f);
    if (character.runtimeReady) {
        EvaluatePose(character);
    }
}

void CharacterAnimationSystem::EvaluatePose(CharacterComponent& character) {
    if (!character.runtimeReady) {
        return;
    }

    std::vector<BoneLocalPose> pose;
    const AnimationClip* clip = CurrentClip(character);
    if (clip) {
        const float sampleTime = ClipRangeStart(*clip) + character.player.time;
        SampleClip(*clip, character.skeleton, sampleTime, pose);
        if (character.player.fading) {
            std::string error;
            const AnimationClip* from = FindClip(character.clips, character.player.fadeFromClip, error);
            if (from && character.player.fadeDuration > 1e-6f) {
                std::vector<BoneLocalPose> fromPose;
                SampleClip(*from, character.skeleton,
                           ClipRangeStart(*from) + character.player.fadeFromTime, fromPose);
                const float alpha = character.player.fadeElapsed / character.player.fadeDuration;
                BlendLocalPoses(fromPose, pose, alpha, pose);
            }
        }
    } else {
        pose.resize(static_cast<size_t>(character.skeleton.BoneCount()));
        for (int i = 0; i < character.skeleton.BoneCount(); ++i) {
            pose[static_cast<size_t>(i)] =
                RestPoseForBone(character.skeleton.bones[static_cast<size_t>(i)]);
        }
    }

    character.localPoses = std::move(pose);
    LocalPosesToMatrices(character.skeleton, character.localPoses, character.localMatrices);
    SkeletonBuilder::EvaluateWorld(character.skeleton, character.localMatrices, character.worldPose);
    SkeletonBuilder::FillBonePalette(character.skeleton, character.worldPose, character.bonePalette);
}

bool CharacterAnimationSystem::SampleAtTime(CharacterComponent& character,
                                            const std::string& clipId,
                                            float time,
                                            std::string& outError) {
    outError.clear();
    if (!character.runtimeReady) {
        outError = "Character runtime is not bound.";
        return false;
    }
    const AnimationClip* clip = FindClip(character.clips, clipId, outError);
    if (!clip) {
        character.lastError = outError;
        return false;
    }
    if (!EnsureCompatible(character, *clip, outError)) {
        return false;
    }

    const float start = ClipRangeStart(*clip);
    const float end = ClipRangeEnd(*clip);
    const float clamped = std::max(start, std::min(end, time));
    character.player.currentClip = clip->id.empty() ? clip->name : clip->id;
    character.player.pendingClip.clear();
    character.player.hasPendingPlay = false;
    character.player.hasPendingFade = false;
    character.player.committedThisFrame = false;
    character.player.fading = false;
    character.player.fadeFromClip.clear();
    character.player.time = clamped - start;
    EvaluatePose(character);
    return true;
}

void CharacterAnimationSystem::TickEntity(CharacterComponent& character, float deltaTime) {
    if (!character.runtimeReady) {
        return;
    }
    if (!character.suppressAutoPlay && !character.player.playing && !character.player.hasPendingPlay &&
        !character.player.hasPendingFade && character.player.currentClip.empty() &&
        !character.defaultAnimation.empty()) {
        std::string error;
        Play(character, character.defaultAnimation, error);
    }
    character.player.CommitPending();
    if (character.player.committedThisFrame) {
        const AnimationClip* clip = CurrentClip(character);
        if (clip) {
            character.player.time = 0.0f;
        }
    }
    AdvancePlayer(character, deltaTime);
    EvaluatePose(character);
}

void CharacterAnimationSystem::Tick(Scene& scene, float deltaTime) {
    auto view = scene.GetEntitiesWith<CharacterComponent>();
    for (auto handle : view) {
        TickEntity(view.get<CharacterComponent>(handle), deltaTime);
    }
}

Mat4 ComposeSocketLocal(const CharacterSocket& socket) {
    TransformComponent transform;
    transform.position = Vec3(socket.position[0], socket.position[1], socket.position[2]);
    transform.rotation = Vec3(socket.rotation[0], socket.rotation[1], socket.rotation[2]);
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);
    return transform.GetLocalMatrix();
}

void DecomposeSocketLocal(const Mat4& local, CharacterSocket& socket) {
    TransformComponent transform;
    transform.SetFromMatrix(local);
    socket.position[0] = transform.position.x;
    socket.position[1] = transform.position.y;
    socket.position[2] = transform.position.z;
    socket.rotation[0] = transform.rotation.x;
    socket.rotation[1] = transform.rotation.y;
    socket.rotation[2] = transform.rotation.z;
}

bool EvaluateSocketWorld(const CharacterComponent& character,
                         const CharacterSocket& socket,
                         Mat4& outWorld,
                         std::string& outError) {
    outError.clear();
    if (socket.bone.empty()) {
        outError = "Socket '" + socket.name + "' has no parent bone.";
        return false;
    }
    const SkeletonBone* bone = character.skeleton.FindBone(socket.bone);
    if (!bone) {
        outError = "Socket '" + socket.name + "' parent bone '" + socket.bone + "' was not found.";
        return false;
    }
    if (bone->evalIndex < 0 ||
        bone->evalIndex >= static_cast<int>(character.worldPose.size())) {
        outError = "Socket '" + socket.name + "' parent bone has no evaluated pose.";
        return false;
    }
    const Mat4& boneWorld = character.worldPose[static_cast<size_t>(bone->evalIndex)];
    outWorld = Mat4::Multiply(boneWorld, ComposeSocketLocal(socket));
    return true;
}

void FillSkeletonDebugOverlay(const CharacterComponent& character,
                              const std::string& selectedBone,
                              std::vector<EditorDebugLine>& outLines,
                              std::vector<EditorDebugPoint>& outPoints) {
    outLines.clear();
    outPoints.clear();
    if (character.worldPose.size() != static_cast<size_t>(character.skeleton.BoneCount())) {
        return;
    }

    auto translation = [](const Mat4& m) {
        return Vec3(m.m[12], m.m[13], m.m[14]);
    };

    for (const SkeletonBone& bone : character.skeleton.bones) {
        const Vec3 joint = translation(character.worldPose[static_cast<size_t>(bone.evalIndex)]);
        const bool selected = !selectedBone.empty() && bone.name == selectedBone;
        EditorDebugPoint point;
        point.p = joint;
        point.color = selected ? Vec3(1.0f, 0.85f, 0.2f) : Vec3(0.35f, 0.85f, 1.0f);
        point.size = selected ? 0.04f : 0.025f;
        outPoints.push_back(point);

        if (bone.parentEval >= 0 &&
            bone.parentEval < static_cast<int>(character.worldPose.size())) {
            EditorDebugLine line;
            line.a = translation(character.worldPose[static_cast<size_t>(bone.parentEval)]);
            line.b = joint;
            line.color = selected ? Vec3(1.0f, 0.7f, 0.15f) : Vec3(0.2f, 0.65f, 0.95f);
            outLines.push_back(line);
        }
    }
}

} // namespace Hephaestus
