#pragma once

#include "../Core/Math.h"

#include <entt/entt.hpp>
#include <optional>

class Entity;
class Scene;

namespace Hephaestus {

/**
 * World pose of the active gameplay camera, in the render-camera convention.
 *
 * Locked convention:
 * - Position is the camera entity's world translation.
 * - The camera's local view forward is the render-camera basis of its local
 *   Euler angles (pitch = rotation.x, yaw = rotation.y, radians):
 *     forward = (cos(yaw) * cos(pitch), sin(pitch), sin(yaw) * cos(pitch))
 * - That direction is rotated by the parent world matrix. Translation is not
 *   applied to the direction. The result is normalized.
 * - Render yaw/pitch invert that formula:
 *     pitch = asin(forward.y)
 *     yaw   = atan2(forward.z, forward.x)
 * - A root camera with pitch inside (-90, 90) degrees therefore round-trips
 *   local pitch and yaw. Camera.setRotation(pitch, yaw, roll) stays aligned
 *   with the rendered view for an unparented camera.
 * - Parent yaw θ (rotation.y only) and child pitch φ (rotation.x, local yaw 0)
 *   produce render yaw = -θ and render pitch = φ.
 * - fov, nearPlane, and farPlane come from CameraComponent.
 * - rollRadians is the camera entity's local rotation.z. Negative roll tilts the
 *   view's up towards its right (a head tilted to the right).
 */
struct ActiveCameraView {
    entt::entity entity = entt::null;
    Vec3 position{0.0f, 0.0f, 0.0f};
    float yawRadians = 0.0f;
    float pitchRadians = 0.0f;
    float rollRadians = 0.0f;
    float fovDegrees = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float viewModelFovDegrees = 0.0f; ///< 0 when view models use fovDegrees
};

bool TryMakeCameraView(Scene& scene, Entity camera, ActiveCameraView& out);

/// World matrix of the rendered view: columns are forward (+X), up (+Y) and right (+Z) at the view position.
/// Identity for yaw 0 and pitch 0, so camera-relative offsets read as (forward, up, right).
Mat4 MakeCameraViewFrame(const Vec3& position, float yawRadians, float pitchRadians, float rollRadians = 0.0f);

/// World-space transform that makes geometry drawn with a perspective projection at worldFovDegrees appear as
/// if projected at viewModelFovDegrees (both vertical). Identity when viewModelFovDegrees is 0 or equal.
Mat4 MakeViewModelFovAdjust(const Mat4& view, float worldFovDegrees, float viewModelFovDegrees);

/// First CameraComponent with isActive, otherwise the first with isPrimary.
std::optional<ActiveCameraView> TryReadActiveCameraView(Scene& scene);

bool WriteActiveCameraLocalPosition(Scene& scene, const Vec3& localPosition);
bool WriteActiveCameraLocalRotation(Scene& scene, const Vec3& eulerRadians);
bool TranslateActiveCameraLocal(Scene& scene, const Vec3& localDelta);

} // namespace Hephaestus
