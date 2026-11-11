/**
 * @file AnimationClip.cpp
 * @brief Clip sampling, TRS blend, and playback commit/advance.
 */

#include "AnimationClip.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Hephaestus {
namespace {

Vec4 NormalizeQuat(Vec4 q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len <= 1e-8f) {
        return Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    const float inv = 1.0f / len;
    return Vec4(q.x * inv, q.y * inv, q.z * inv, q.w * inv);
}

Mat4 Mat4FromQuat(Vec4 q) {
    q = NormalizeQuat(q);
    const float x = q.x;
    const float y = q.y;
    const float z = q.z;
    const float w = q.w;
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    Mat4 r = Mat4::Identity();
    r.m[0] = 1.0f - 2.0f * (yy + zz);
    r.m[1] = 2.0f * (xy + wz);
    r.m[2] = 2.0f * (xz - wy);
    r.m[4] = 2.0f * (xy - wz);
    r.m[5] = 1.0f - 2.0f * (xx + zz);
    r.m[6] = 2.0f * (yz + wx);
    r.m[8] = 2.0f * (xz + wy);
    r.m[9] = 2.0f * (yz - wx);
    r.m[10] = 1.0f - 2.0f * (xx + yy);
    return r;
}

Vec3 LerpVec3(const Vec3& a, const Vec3& b, float t) {
    return Vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

Vec4 SlerpQuat(Vec4 a, Vec4 b, float t) {
    a = NormalizeQuat(a);
    b = NormalizeQuat(b);
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        b = Vec4(-b.x, -b.y, -b.z, -b.w);
        dot = -dot;
    }
    if (dot > 0.9995f) {
        return NormalizeQuat(Vec4(a.x + (b.x - a.x) * t,
                                  a.y + (b.y - a.y) * t,
                                  a.z + (b.z - a.z) * t,
                                  a.w + (b.w - a.w) * t));
    }
    const float theta = std::acos(std::max(-1.0f, std::min(1.0f, dot)));
    const float sinTheta = std::sin(theta);
    const float w0 = std::sin((1.0f - t) * theta) / sinTheta;
    const float w1 = std::sin(t * theta) / sinTheta;
    return NormalizeQuat(Vec4(a.x * w0 + b.x * w1,
                              a.y * w0 + b.y * w1,
                              a.z * w0 + b.z * w1,
                              a.w * w0 + b.w * w1));
}

int FindKey(const std::vector<float>& times, float time) {
    if (times.empty()) {
        return -1;
    }
    if (time <= times.front()) {
        return 0;
    }
    if (time >= times.back()) {
        return static_cast<int>(times.size()) - 1;
    }
    auto it = std::upper_bound(times.begin(), times.end(), time);
    return static_cast<int>(std::distance(times.begin(), it)) - 1;
}

float KeyAlpha(const std::vector<float>& times, int index, float time,
               AnimationInterpolation interpolation) {
    if (interpolation == AnimationInterpolation::Step) {
        return 0.0f;
    }
    if (index < 0 || index + 1 >= static_cast<int>(times.size())) {
        return 0.0f;
    }
    const float t0 = times[static_cast<size_t>(index)];
    const float t1 = times[static_cast<size_t>(index + 1)];
    const float dt = t1 - t0;
    if (dt <= 1e-8f) {
        return 0.0f;
    }
    return std::max(0.0f, std::min(1.0f, (time - t0) / dt));
}

} // namespace

Mat4 ComposeBoneLocal(const BoneLocalPose& pose) {
    const Mat4 t = Mat4::Translation(pose.translation);
    const Mat4 r = Mat4FromQuat(pose.rotation);
    const Mat4 s = Mat4::Scale(pose.scale);
    return Mat4::Multiply(t, Mat4::Multiply(r, s));
}

BoneLocalPose RestPoseForBone(const SkeletonBone& bone) {
    BoneLocalPose pose;
    pose.translation = bone.restTranslation;
    pose.rotation = bone.restRotation;
    pose.scale = bone.restScale;
    return pose;
}

void SampleClip(const AnimationClip& clip,
                const Skeleton& skeleton,
                float time,
                std::vector<BoneLocalPose>& outPose) {
    outPose.resize(static_cast<size_t>(skeleton.BoneCount()));
    for (int i = 0; i < skeleton.BoneCount(); ++i) {
        outPose[static_cast<size_t>(i)] = RestPoseForBone(skeleton.bones[static_cast<size_t>(i)]);
    }

    for (const AnimationChannel& channel : clip.channels) {
        if (channel.evalBone < 0 || channel.evalBone >= skeleton.BoneCount() ||
            channel.times.empty()) {
            continue;
        }
        BoneLocalPose& pose = outPose[static_cast<size_t>(channel.evalBone)];
        const int index = FindKey(channel.times, time);
        if (index < 0) {
            continue;
        }
        const float alpha = KeyAlpha(channel.times, index, time, channel.interpolation);
        const size_t i0 = static_cast<size_t>(index);
        const size_t i1 = (index + 1 < static_cast<int>(channel.times.size()))
                              ? static_cast<size_t>(index + 1)
                              : i0;

        if (channel.path == AnimationPath::Rotation) {
            if (i0 >= channel.rotations.size()) {
                continue;
            }
            const Vec4 a = channel.rotations[i0];
            const Vec4 b = (i1 < channel.rotations.size()) ? channel.rotations[i1] : a;
            pose.rotation = SlerpQuat(a, b, alpha);
        } else {
            if (i0 >= channel.vectors.size()) {
                continue;
            }
            const Vec3 a = channel.vectors[i0];
            const Vec3 b = (i1 < channel.vectors.size()) ? channel.vectors[i1] : a;
            const Vec3 value = LerpVec3(a, b, alpha);
            if (channel.path == AnimationPath::Translation) {
                pose.translation = value;
            } else {
                pose.scale = value;
            }
        }
    }
}

BoneLocalPose BlendBonePose(const BoneLocalPose& from, const BoneLocalPose& to, float alpha) {
    const float t = std::max(0.0f, std::min(1.0f, alpha));
    BoneLocalPose out;
    out.translation = LerpVec3(from.translation, to.translation, t);
    out.rotation = SlerpQuat(from.rotation, to.rotation, t);
    out.scale = LerpVec3(from.scale, to.scale, t);
    return out;
}

void BlendLocalPoses(const std::vector<BoneLocalPose>& from,
                     const std::vector<BoneLocalPose>& to,
                     float alpha,
                     std::vector<BoneLocalPose>& outPose) {
    const size_t count = std::min(from.size(), to.size());
    outPose.resize(count);
    for (size_t i = 0; i < count; ++i) {
        outPose[i] = BlendBonePose(from[i], to[i], alpha);
    }
}

void LocalPosesToMatrices(const Skeleton& skeleton,
                          const std::vector<BoneLocalPose>& poses,
                          std::vector<Mat4>& outLocal) {
    outLocal.resize(static_cast<size_t>(skeleton.BoneCount()));
    for (int i = 0; i < skeleton.BoneCount(); ++i) {
        const SkeletonBone& bone = skeleton.bones[static_cast<size_t>(i)];
        if (i < static_cast<int>(poses.size())) {
            outLocal[static_cast<size_t>(i)] =
                Mat4::Multiply(bone.restPrefix, ComposeBoneLocal(poses[static_cast<size_t>(i)]));
        } else {
            outLocal[static_cast<size_t>(i)] = bone.localRest;
        }
    }
}

const AnimationClip* FindClip(const std::vector<AnimationClip>& clips,
                              const std::string& name,
                              std::string& outError) {
    outError.clear();
    if (name.empty()) {
        outError = "Animation name is empty.";
        return nullptr;
    }

    const AnimationClip* exactId = nullptr;
    const AnimationClip* exactName = nullptr;
    std::vector<const AnimationClip*> suffixMatches;
    for (const AnimationClip& clip : clips) {
        if (clip.id == name) {
            exactId = &clip;
        }
        if (clip.name == name || clip.sourceClip == name) {
            exactName = &clip;
        }
        if (clip.id.size() > name.size() + 1) {
            const size_t slash = clip.id.rfind('/');
            if (slash != std::string::npos && clip.id.substr(slash + 1) == name) {
                suffixMatches.push_back(&clip);
            }
        }
    }
    if (exactId) {
        return exactId;
    }
    if (exactName) {
        return exactName;
    }
    if (suffixMatches.size() == 1) {
        return suffixMatches.front();
    }
    if (suffixMatches.size() > 1) {
        outError = "Animation name '" + name +
                   "' is ambiguous; use a canonical id such as Folder/Name.";
        return nullptr;
    }
    outError = "Animation '" + name + "' was not found.";
    return nullptr;
}

float ClipRangeStart(const AnimationClip& clip) {
    return clip.startTime;
}

float ClipRangeEnd(const AnimationClip& clip) {
    if (clip.endTime > clip.startTime + 1e-6f) {
        return clip.endTime;
    }
    return std::max(clip.startTime, clip.duration);
}

std::string FormatSkeletonIncompatibility(const SkeletonCompatibilityReport& report) {
    std::ostringstream out;
    out << "Animation cannot be imported.\n\nSkeleton incompatibility detected:\n";
    if (!report.missingBones.empty()) {
        out << "\nMissing bones:\n";
        for (const std::string& name : report.missingBones) {
            out << "- " << name << "\n";
        }
    }
    if (!report.extraBones.empty()) {
        out << "\nUnused bones:\n";
        for (const std::string& name : report.extraBones) {
            out << "- " << name << "\n";
        }
    }
    if (!report.hierarchyMismatches.empty()) {
        out << "\nHierarchy mismatch:\n";
        for (const std::string& line : report.hierarchyMismatches) {
            out << "- " << line << "\n";
        }
    }
    if (report.restPoseMismatch) {
        out << "\nRest-pose mismatch: clip skeleton rest pose does not match the character.\n";
    }
    return out.str();
}

void AnimationPlayer::RequestPlay(const std::string& clipId) {
    pendingClip = clipId;
    hasPendingPlay = true;
    hasPendingFade = false;
}

void AnimationPlayer::RequestCrossFade(const std::string& clipId, float duration) {
    if (duration <= 1e-4f) {
        RequestPlay(clipId);
        return;
    }
    pendingFadeClip = clipId;
    pendingFadeDuration = duration;
    hasPendingFade = true;
    hasPendingPlay = false;
}

void AnimationPlayer::Pause() {
    paused = true;
}

void AnimationPlayer::Resume() {
    if (!currentClip.empty()) {
        paused = false;
        playing = true;
    }
}

void AnimationPlayer::Stop(float resetTime) {
    hasPendingPlay = false;
    hasPendingFade = false;
    fading = false;
    playing = false;
    paused = false;
    committedThisFrame = false;
    finishedThisFrame = false;
    time = resetTime;
}

void AnimationPlayer::SetSpeed(float value) {
    speed = value;
}

void AnimationPlayer::CommitPending() {
    committedThisFrame = false;
    if (hasPendingPlay) {
        fadeFromClip.clear();
        fading = false;
        currentClip = pendingClip;
        pendingClip.clear();
        hasPendingPlay = false;
        time = 0.0f;
        playing = true;
        paused = false;
        finishedThisFrame = false;
        committedThisFrame = true;
    } else if (hasPendingFade) {
        fadeFromClip = currentClip;
        fadeFromTime = time;
        fadeDuration = pendingFadeDuration;
        fadeElapsed = 0.0f;
        fading = !fadeFromClip.empty() && fadeFromClip != pendingFadeClip;
        currentClip = pendingFadeClip;
        pendingFadeClip.clear();
        hasPendingFade = false;
        time = 0.0f;
        playing = true;
        paused = false;
        finishedThisFrame = false;
        committedThisFrame = true;
        if (!fading) {
            fadeFromClip.clear();
        }
    }
}

} // namespace Hephaestus
