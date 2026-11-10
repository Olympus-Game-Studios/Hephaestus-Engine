#include "ActiveCameraSync.h"

#include "Components.h"
#include "Entity.h"
#include "Scene.h"

#include <cmath>

namespace Hephaestus {
namespace {

Vec3 RenderForward(float yawRadians, float pitchRadians)
{
    const float cosPitch = std::cos(pitchRadians);
    return Vec3(
        std::cos(yawRadians) * cosPitch,
        std::sin(pitchRadians),
        std::sin(yawRadians) * cosPitch);
}

Vec3 RotateDirection(const Mat4& world, const Vec3& direction)
{
    const Vec4 rotated = Mat4::TransformVec4(
        world, Vec4(direction.x, direction.y, direction.z, 0.0f));
    return vnorm(Vec3(rotated.x, rotated.y, rotated.z));
}

Entity FindActiveCameraEntity(Scene& scene)
{
    Entity active;
    Entity primary;
    auto cameraView = scene.GetEntitiesWith<CameraComponent, TransformComponent>();
    for (auto entityHandle : cameraView) {
        Entity entity(entityHandle, &scene);
        const CameraComponent& camera = entity.GetComponent<CameraComponent>();
        if (camera.isActive) {
            active = entity;
            break;
        }
        if (!primary && camera.isPrimary) {
            primary = entity;
        }
    }
    return active ? active : primary;
}

void AssignAnglesFromForward(const Vec3& forward, ActiveCameraView& out)
{
    const float clampedY = std::max(-1.0f, std::min(1.0f, forward.y));
    out.pitchRadians = std::asin(clampedY);
    out.yawRadians = std::atan2(forward.z, forward.x);
}

} // namespace

bool TryMakeCameraView(Scene& scene, Entity camera, ActiveCameraView& out)
{
    if (!camera ||
        !camera.HasComponent<CameraComponent>() ||
        !camera.HasComponent<TransformComponent>()) {
        return false;
    }

    const CameraComponent& cameraComponent = camera.GetComponent<CameraComponent>();
    const TransformComponent& local = camera.GetComponent<TransformComponent>();
    const Mat4 world = scene.GetWorldTransform(camera);

    Vec3 forward = RenderForward(local.rotation.y, local.rotation.x);
    if (local.parent != entt::null) {
        Entity parent{local.parent, &scene};
        if (parent && parent.HasComponent<TransformComponent>()) {
            forward = RotateDirection(scene.GetWorldTransform(parent), forward);
        }
    }
    if (vlen(forward) <= 1.0e-6f) {
        forward = Vec3(1.0f, 0.0f, 0.0f);
    }

    out.entity = static_cast<entt::entity>(camera);
    out.position = Vec3(world.m[12], world.m[13], world.m[14]);
    AssignAnglesFromForward(forward, out);
    out.rollRadians = local.rotation.z;
    out.fovDegrees = cameraComponent.fov;
    out.nearPlane = cameraComponent.nearPlane;
    out.farPlane = cameraComponent.farPlane;
    out.viewModelFovDegrees = cameraComponent.viewModelFov;
    return true;
}

Mat4 MakeCameraViewFrame(const Vec3& position, float yawRadians, float pitchRadians, float rollRadians)
{
    const Vec3 forward = RenderForward(yawRadians, pitchRadians);
    const Vec3 level(-std::sin(yawRadians), 0.0f, std::cos(yawRadians));
    const Vec3 levelUp = vcross(level, forward);
    const float c = std::cos(rollRadians);
    const float s = std::sin(rollRadians);
    const Vec3 up = vsub(vscale(levelUp, c), vscale(level, s));
    const Vec3 right = vadd(vscale(level, c), vscale(levelUp, s));
    Mat4 frame = Mat4::Identity();
    frame.m[0] = forward.x; frame.m[1] = forward.y; frame.m[2] = forward.z;
    frame.m[4] = up.x;      frame.m[5] = up.y;      frame.m[6] = up.z;
    frame.m[8] = right.x;   frame.m[9] = right.y;   frame.m[10] = right.z;
    frame.m[12] = position.x; frame.m[13] = position.y; frame.m[14] = position.z;
    return frame;
}

Mat4 MakeViewModelFovAdjust(const Mat4& view, float worldFovDegrees, float viewModelFovDegrees)
{
    if (viewModelFovDegrees <= 0.0f || worldFovDegrees <= 0.0f || viewModelFovDegrees == worldFovDegrees) {
        return Mat4::Identity();
    }
    // Projected x and y are the focal length times view-space x and y, so scaling them by the focal ratio
    // is the same as projecting with the view model's field of view.
    constexpr float kDegToRad = 3.1415926535f / 180.0f;
    const float k = std::tan(worldFovDegrees * 0.5f * kDegToRad) / std::tan(viewModelFovDegrees * 0.5f * kDegToRad);
    return Mat4::Multiply(Mat4::Invert(view), Mat4::Multiply(Mat4::Scale(Vec3(k, k, 1.0f)), view));
}

std::optional<ActiveCameraView> TryReadActiveCameraView(Scene& scene)
{
    Entity camera = FindActiveCameraEntity(scene);
    if (!camera) {
        return std::nullopt;
    }
    ActiveCameraView view;
    if (!TryMakeCameraView(scene, camera, view)) {
        return std::nullopt;
    }
    return view;
}

bool WriteActiveCameraLocalPosition(Scene& scene, const Vec3& localPosition)
{
    Entity camera = FindActiveCameraEntity(scene);
    if (!camera || !camera.HasComponent<TransformComponent>()) {
        return false;
    }
    camera.GetComponent<TransformComponent>().position = localPosition;
    return true;
}

bool WriteActiveCameraLocalRotation(Scene& scene, const Vec3& eulerRadians)
{
    Entity camera = FindActiveCameraEntity(scene);
    if (!camera || !camera.HasComponent<TransformComponent>()) {
        return false;
    }
    camera.GetComponent<TransformComponent>().rotation = eulerRadians;
    return true;
}

bool TranslateActiveCameraLocal(Scene& scene, const Vec3& localDelta)
{
    Entity camera = FindActiveCameraEntity(scene);
    if (!camera || !camera.HasComponent<TransformComponent>()) {
        return false;
    }
    TransformComponent& transform = camera.GetComponent<TransformComponent>();
    transform.position = vadd(transform.position, localDelta);
    return true;
}

} // namespace Hephaestus
