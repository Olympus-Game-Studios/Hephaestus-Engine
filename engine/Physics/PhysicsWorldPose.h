#pragma once

#include "Core/Math.h"

#include <cmath>
#include <cstddef>

namespace Physics {

inline constexpr float kUniformScaleEpsilon = 1.0e-4f;
inline constexpr float kPoseChangeEpsilon = 1.0e-6f;

struct PoseQuat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct RigidTransformSample {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct WorldPose {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 rotationEuler{0.0f, 0.0f, 0.0f};
    PoseQuat rotation{};
    Vec3 shapeScale{1.0f, 1.0f, 1.0f};
    Vec3 ancestorScale{1.0f, 1.0f, 1.0f};
};

inline PoseQuat PoseQuatMultiply(const PoseQuat& lhs, const PoseQuat& rhs)
{
    PoseQuat out;
    out.x = lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y;
    out.y = lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x;
    out.z = lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w;
    out.w = lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z;
    return out;
}

inline PoseQuat PoseQuatConjugate(const PoseQuat& quat)
{
    return PoseQuat{-quat.x, -quat.y, -quat.z, quat.w};
}

inline PoseQuat PoseQuatFromEulerXYZ(const Vec3& euler)
{
    const float hx = euler.x * 0.5f;
    const float hy = euler.y * 0.5f;
    const float hz = euler.z * 0.5f;
    const float cx = std::cos(hx);
    const float sx = std::sin(hx);
    const float cy = std::cos(hy);
    const float sy = std::sin(hy);
    const float cz = std::cos(hz);
    const float sz = std::sin(hz);
    const PoseQuat qx{sx, 0.0f, 0.0f, cx};
    const PoseQuat qy{0.0f, sy, 0.0f, cy};
    const PoseQuat qz{0.0f, 0.0f, sz, cz};
    return PoseQuatMultiply(PoseQuatMultiply(qx, qy), qz);
}

inline Vec3 PoseQuatToEulerXYZ(const PoseQuat& quat)
{
    Vec3 euler{};
    const float sinr = 2.0f * (quat.w * quat.x + quat.y * quat.z);
    const float cosr = 1.0f - 2.0f * (quat.x * quat.x + quat.y * quat.y);
    euler.x = std::atan2(sinr, cosr);

    float sinp = 2.0f * (quat.w * quat.y - quat.z * quat.x);
    sinp = std::clamp(sinp, -1.0f, 1.0f);
    euler.y = std::asin(sinp);

    const float siny = 2.0f * (quat.w * quat.z + quat.x * quat.y);
    const float cosy = 1.0f - 2.0f * (quat.y * quat.y + quat.z * quat.z);
    euler.z = std::atan2(siny, cosy);
    return euler;
}

inline Vec3 RotateByPoseQuat(const PoseQuat& quat, const Vec3& vector)
{
    const PoseQuat point{vector.x, vector.y, vector.z, 0.0f};
    const PoseQuat rotated = PoseQuatMultiply(PoseQuatMultiply(quat, point), PoseQuatConjugate(quat));
    return Vec3(rotated.x, rotated.y, rotated.z);
}

inline bool ScaleIsUniform(const Vec3& scale)
{
    const float ax = std::abs(scale.x);
    const float ay = std::abs(scale.y);
    const float az = std::abs(scale.z);
    return std::abs(ax - ay) <= kUniformScaleEpsilon
        && std::abs(ay - az) <= kUniformScaleEpsilon
        && std::abs(ax - az) <= kUniformScaleEpsilon;
}

inline Vec3 AbsScale(const Vec3& scale)
{
    return Vec3(std::abs(scale.x), std::abs(scale.y), std::abs(scale.z));
}

inline Mat4 RigidSampleMatrix(const RigidTransformSample& sample)
{
    const Mat4 translation = Mat4::Translation(sample.position);
    const Mat4 rotationX = Mat4::RotationX(sample.rotation.x);
    const Mat4 rotationY = Mat4::RotationY(sample.rotation.y);
    const Mat4 rotationZ = Mat4::RotationZ(sample.rotation.z);
    const Mat4 scale = Mat4::Scale(sample.scale);
    Mat4 result = Mat4::Multiply(rotationZ, scale);
    result = Mat4::Multiply(rotationY, result);
    result = Mat4::Multiply(rotationX, result);
    return Mat4::Multiply(translation, result);
}

// chainFromRoot includes the entity as the last sample. A non-uniform ancestor fails.
inline bool TryComposeWorldPose(const RigidTransformSample* chainFromRoot, std::size_t count, WorldPose& out)
{
    if (!chainFromRoot || count == 0) {
        return false;
    }

    Mat4 world = Mat4::Identity();
    PoseQuat rotation{};
    Vec3 ancestorScale(1.0f, 1.0f, 1.0f);
    for (std::size_t index = 0; index < count; ++index) {
        const bool isSelf = index + 1 == count;
        if (!isSelf && !ScaleIsUniform(chainFromRoot[index].scale)) {
            return false;
        }
        if (!isSelf) {
            const Vec3 absolute = AbsScale(chainFromRoot[index].scale);
            ancestorScale = Vec3(ancestorScale.x * absolute.x,
                                  ancestorScale.y * absolute.y,
                                  ancestorScale.z * absolute.z);
        }
        world = Mat4::Multiply(world, RigidSampleMatrix(chainFromRoot[index]));
        rotation = PoseQuatMultiply(rotation, PoseQuatFromEulerXYZ(chainFromRoot[index].rotation));
    }

    const Vec3 localScale = AbsScale(chainFromRoot[count - 1].scale);
    out.position = Vec3(world.m[12], world.m[13], world.m[14]);
    out.rotation = rotation;
    out.rotationEuler = PoseQuatToEulerXYZ(rotation);
    out.ancestorScale = ancestorScale;
    out.shapeScale = Vec3(localScale.x * ancestorScale.x,
                           localScale.y * ancestorScale.y,
                           localScale.z * ancestorScale.z);
    return true;
}

inline Vec3 WorldPointToLocal(const Vec3& parentPosition, const PoseQuat& parentRotation,
                              const Vec3& ancestorScale, const Vec3& worldPoint)
{
    const Vec3 delta = vsub(worldPoint, parentPosition);
    const Vec3 unrotated = RotateByPoseQuat(PoseQuatConjugate(parentRotation), delta);
    auto safeDivide = [](float value, float scale) {
        return std::abs(scale) > 1.0e-8f ? value / scale : value;
    };
    return Vec3(safeDivide(unrotated.x, ancestorScale.x),
                safeDivide(unrotated.y, ancestorScale.y),
                safeDivide(unrotated.z, ancestorScale.z));
}

inline PoseQuat WorldRotationToLocal(const PoseQuat& parentRotation, const PoseQuat& worldRotation)
{
    return PoseQuatMultiply(PoseQuatConjugate(parentRotation), worldRotation);
}

inline bool PoseNearlyEqual(const Vec3& a, const Vec3& b, float epsilon = kPoseChangeEpsilon)
{
    return std::abs(a.x - b.x) <= epsilon
        && std::abs(a.y - b.y) <= epsilon
        && std::abs(a.z - b.z) <= epsilon;
}

} // namespace Physics
