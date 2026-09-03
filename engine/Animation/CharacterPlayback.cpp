/**
 * @file CharacterPlayback.cpp
 * @brief Same-frame clip commit + pose evaluation for CharacterComponent.
 */

#include "CharacterPlayback.h"

#include "../Scene/CharacterComponent.h"
#include "../Scene/Scene.h"

#include <algorithm>

namespace Hephaestus {
namespace {

const AnimationClip* CurrentClip(const CharacterComponent& character) {
    std::string error;
    if (character.player.currentClip.empty()) {
        return nullptr;
    }
    return FindClip(character.clips, character.player.currentClip, error);
}

void EvaluatePose(CharacterComponent& character) {
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
    character.player.RequestPlay(clip->id.empty() ? clip->name : clip->id);
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
    character.player.RequestCrossFade(clip->id.empty() ? clip->name : clip->id, duration);
    return true;
}

void CharacterAnimationSystem::Pause(CharacterComponent& character) {
    character.player.Pause();
}

void CharacterAnimationSystem::Resume(CharacterComponent& character) {
    character.player.Resume();
}

void CharacterAnimationSystem::Stop(CharacterComponent& character) {
    character.player.Stop(0.0f);
    if (character.runtimeReady) {
        EvaluatePose(character);
    }
}

void CharacterAnimationSystem::SetSpeed(CharacterComponent& character, float speed) {
    character.player.SetSpeed(speed);
}

void CharacterAnimationSystem::TickEntity(CharacterComponent& character, float deltaTime) {
    if (!character.runtimeReady) {
        return;
    }
    if (!character.player.playing && !character.player.hasPendingPlay &&
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

} // namespace Hephaestus
