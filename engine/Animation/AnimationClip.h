/**
 * @file AnimationClip.h
 * @brief Imported clip curves, CPU sampler, and playback state (Stage D / D2).
 */

#pragma once

#include "Skeleton.h"

#include <string>
#include <vector>

namespace Hephaestus {

enum class AnimationPath {
    Translation,
    Rotation,
    Scale
};

enum class AnimationInterpolation {
    Linear,
    Step
};

struct BoneLocalPose {
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f}; ///< glTF xyzw
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct AnimationChannel {
    int evalBone = -1;
    AnimationPath path = AnimationPath::Translation;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    std::vector<float> times;
    std::vector<Vec3> vectors;   ///< translation or scale
    std::vector<Vec4> rotations; ///< xyzw
};

struct AnimationClip {
    std::string name;
    std::string id;
    std::string sourceClip;
    std::string skeletonSignature;
    float duration = 0.0f;
    float startTime = 0.0f;
    float endTime = 0.0f;
    bool loop = true;
    float speed = 1.0f;
    std::vector<AnimationChannel> channels;
};

struct AnimationPlayer {
    std::string currentClip;
    float time = 0.0f;
    float speed = 1.0f;
    bool playing = false;
    bool paused = false;

    std::string pendingClip;
    bool hasPendingPlay = false;
    bool committedThisFrame = false;

    bool fading = false;
    std::string fadeFromClip;
    float fadeFromTime = 0.0f;
    float fadeDuration = 0.0f;
    float fadeElapsed = 0.0f;
    bool hasPendingFade = false;
    std::string pendingFadeClip;
    float pendingFadeDuration = 0.0f;

    bool finishedThisFrame = false;
    std::string finishedClip;

    void RequestPlay(const std::string& clipId);
    void RequestCrossFade(const std::string& clipId, float duration);
    void Pause();
    void Resume();
    void Stop(float resetTime);
    void SetSpeed(float value);
    void CommitPending();
};

Mat4 ComposeBoneLocal(const BoneLocalPose& pose);
BoneLocalPose RestPoseForBone(const SkeletonBone& bone);
void SampleClip(const AnimationClip& clip,
                const Skeleton& skeleton,
                float time,
                std::vector<BoneLocalPose>& outPose);
void BlendLocalPoses(const std::vector<BoneLocalPose>& from,
                     const std::vector<BoneLocalPose>& to,
                     float alpha,
                     std::vector<BoneLocalPose>& outPose);
void LocalPosesToMatrices(const Skeleton& skeleton,
                          const std::vector<BoneLocalPose>& poses,
                          std::vector<Mat4>& outLocal);

const AnimationClip* FindClip(const std::vector<AnimationClip>& clips,
                              const std::string& name,
                              std::string& outError);

float ClipRangeStart(const AnimationClip& clip);
float ClipRangeEnd(const AnimationClip& clip);

std::string FormatSkeletonIncompatibility(const SkeletonCompatibilityReport& report);

} // namespace Hephaestus
