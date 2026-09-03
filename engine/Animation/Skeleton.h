/**
 * @file Skeleton.h
 * @brief Runtime skeleton: identity order, topological evaluation order, signature.
 */

#pragma once

#include "../Core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Hephaestus {

constexpr int kMaxCharacterBones = 128;
constexpr int kMaxBoneInfluences = 4;

struct SkeletonBone {
    std::string name;
    int parentEval = -1;     ///< Parent in evaluation order, or -1
    int sourceJoint = -1;    ///< glTF skin joints[] index
    int identityIndex = -1;  ///< Sorted-name identity slot
    int evalIndex = -1;      ///< Parent-before-child evaluation / GPU palette index
    Mat4 localRest = Mat4::Identity();
    Mat4 inverseBind = Mat4::Identity();
};

struct SkeletonCompatibilityReport {
    bool compatible = false;
    bool restPoseMismatch = false;
    std::vector<std::string> missingBones;
    std::vector<std::string> extraBones;
    std::vector<std::string> hierarchyMismatches;
};

struct Skeleton {
    std::string skinName;
    std::string signature;
    std::vector<SkeletonBone> bones; ///< Evaluation order (parent before child)
    std::vector<int> sourceJointToEval;
    std::vector<int> sourceJointToIdentity;
    std::vector<int> identityToEval;

    int BoneCount() const { return static_cast<int>(bones.size()); }

    const SkeletonBone* FindBone(const std::string& name) const;
};

class SkeletonBuilder {
public:
    /// Incoming `parentEval` values are indices into `bones` as currently ordered
    /// (typically source-joint order). Finalize rewrites them to evaluation indices.
    static bool Finalize(Skeleton& skeleton, std::string& outError);
    static std::string ComputeSignature(const Skeleton& skeleton);
    static SkeletonCompatibilityReport Compare(const Skeleton& character,
                                               const Skeleton& other);
    static void EvaluateRestWorld(const Skeleton& skeleton, std::vector<Mat4>& outWorld);
    static void EvaluateWorld(const Skeleton& skeleton,
                              const std::vector<Mat4>& localPose,
                              std::vector<Mat4>& outWorld);
    /// True when each IBM equals inverse(restWorld) * meshNodeWorld (bind == rest).
    static bool RestMatchesBind(const Skeleton& skeleton,
                                const Mat4& meshNodeWorld,
                                float epsilon,
                                std::string& outError);
};

Mat4 ConvertSkeletalMatrix(const Mat4& gltfMatrix, float unitScale);
Vec3 ConvertSkeletalPoint(const Vec3& gltfPoint, float unitScale);

} // namespace Hephaestus
