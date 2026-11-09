#include "Scene/ActiveCameraSync.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <cmath>
#include <iostream>

namespace {

bool Check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

bool Near(float a, float b)
{
    return std::abs(a - b) < 0.001f;
}

bool NearVec(const Vec3& a, const Vec3& b)
{
    return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
}

Vec3 RenderForward(float yawRadians, float pitchRadians)
{
    const float cosPitch = std::cos(pitchRadians);
    return Vec3(
        std::cos(yawRadians) * cosPitch,
        std::sin(pitchRadians),
        std::sin(yawRadians) * cosPitch);
}

Entity AddCamera(Scene& scene, const char* name, bool active)
{
    Entity entity = scene.CreateEntity(name);
    CameraComponent camera = CameraComponent::CreateActive(60.0f, 0.1f, 1000.0f);
    camera.isActive = active;
    entity.AddComponent<CameraComponent>(camera);
    return entity;
}

bool TestRootCamera()
{
    Scene scene;
    Entity camera = AddCamera(scene, "RootCamera", true);
    TransformComponent& transform = camera.GetComponent<TransformComponent>();
    transform.position = Vec3(1.0f, 2.0f, 4.0f);
    transform.rotation = Vec3(0.35f, -0.8f, 0.1f);
    CameraComponent& component = camera.GetComponent<CameraComponent>();
    component.fov = 90.0f;
    component.nearPlane = 0.05f;
    component.farPlane = 750.0f;

    const std::optional<Hephaestus::ActiveCameraView> view =
        Hephaestus::TryReadActiveCameraView(scene);
    bool ok = Check(view.has_value(), "root camera view");
    if (!view) {
        return false;
    }
    ok &= Check(NearVec(view->position, Vec3(1.0f, 2.0f, 4.0f)), "root camera position");
    ok &= Check(Near(view->pitchRadians, 0.35f), "root camera pitch");
    ok &= Check(Near(view->yawRadians, -0.8f), "root camera yaw");
    ok &= Check(Near(view->rollRadians, 0.1f), "root camera roll");
    ok &= Check(Near(view->fovDegrees, 90.0f), "root camera fov");
    ok &= Check(Near(view->nearPlane, 0.05f), "root camera near");
    ok &= Check(Near(view->farPlane, 750.0f), "root camera far");
    ok &= Check(NearVec(RenderForward(view->yawRadians, view->pitchRadians),
                        RenderForward(-0.8f, 0.35f)),
                "root camera forward");
    return ok;
}

bool TestParentedCamera()
{
    Scene scene;
    Entity parent = scene.CreateEntity("Parent");
    parent.GetComponent<TransformComponent>().position = Vec3(8.0f, 1.0f, -3.0f);
    Entity camera = AddCamera(scene, "ChildCamera", true);
    camera.GetComponent<TransformComponent>().position = Vec3(0.0f, 1.62f, 0.0f);
    scene.SetParent(camera, parent);

    std::optional<Hephaestus::ActiveCameraView> view =
        Hephaestus::TryReadActiveCameraView(scene);
    bool ok = Check(view.has_value(), "parented camera view");
    if (!view) {
        return false;
    }
    ok &= Check(NearVec(view->position, Vec3(8.0f, 2.62f, -3.0f)),
                "parented camera position");
    ok &= Check(Near(view->yawRadians, 0.0f) && Near(view->pitchRadians, 0.0f),
                "parented identity orientation");

    parent.GetComponent<TransformComponent>().position = Vec3(8.0f, 4.0f, -3.0f);
    view = Hephaestus::TryReadActiveCameraView(scene);
    ok &= Check(view.has_value() && NearVec(view->position, Vec3(8.0f, 5.62f, -3.0f)),
                "parent movement");

    parent.GetComponent<TransformComponent>().position = Vec3(10.0f, 0.0f, 0.0f);
    parent.GetComponent<TransformComponent>().rotation = Vec3(0.0f, 1.5707963f, 0.0f);
    camera.GetComponent<TransformComponent>().position = Vec3(0.0f, 0.0f, 2.0f);
    view = Hephaestus::TryReadActiveCameraView(scene);
    ok &= Check(view.has_value() && NearVec(view->position, Vec3(12.0f, 0.0f, 0.0f)),
                "parent yaw moves child offset");
    return ok;
}

bool TestParentYawChildPitch()
{
    constexpr float kParentYaw = 0.5f;
    constexpr float kChildPitch = -0.25f;

    Scene scene;
    Entity parent = scene.CreateEntity("YawParent");
    parent.GetComponent<TransformComponent>().rotation = Vec3(0.0f, kParentYaw, 0.0f);
    parent.GetComponent<TransformComponent>().position = Vec3(1.0f, 0.0f, 2.0f);
    Entity camera = AddCamera(scene, "PitchChild", true);
    camera.GetComponent<TransformComponent>().rotation = Vec3(kChildPitch, 0.0f, 0.0f);
    camera.GetComponent<TransformComponent>().position = Vec3(0.0f, 1.62f, 0.0f);
    CameraComponent& component = camera.GetComponent<CameraComponent>();
    component.fov = 90.0f;
    component.nearPlane = 0.05f;
    component.farPlane = 1000.0f;
    scene.SetParent(camera, parent);

    const std::optional<Hephaestus::ActiveCameraView> view =
        Hephaestus::TryReadActiveCameraView(scene);
    bool ok = Check(view.has_value(), "yaw-pitch view");
    if (!view) {
        return false;
    }
    ok &= Check(Near(view->yawRadians, -kParentYaw), "parent yaw maps to render yaw");
    ok &= Check(Near(view->pitchRadians, kChildPitch), "child pitch maps to render pitch");
    ok &= Check(NearVec(view->position, Vec3(1.0f, 1.62f, 2.0f)), "yaw-pitch position");
    ok &= Check(Near(view->fovDegrees, 90.0f), "component fov");
    ok &= Check(Near(view->nearPlane, 0.05f), "component near");
    ok &= Check(Near(view->farPlane, 1000.0f), "component far");

    const Vec3 rendered = RenderForward(view->yawRadians, view->pitchRadians);
    const Vec3 expected = RenderForward(-kParentYaw, kChildPitch);
    ok &= Check(NearVec(rendered, expected), "rendered forward matches locked yaw/pitch");
    return ok;
}

bool TestSetRotationRoundTrip()
{
    Scene scene;
    Entity camera = AddCamera(scene, "Driven", true);
    const Vec3 written(0.2f, -1.1f, 0.05f);
    bool ok = Check(Hephaestus::WriteActiveCameraLocalRotation(scene, written),
                    "write rotation");
    ok &= Check(Hephaestus::WriteActiveCameraLocalPosition(scene, Vec3(3.0f, 4.0f, 5.0f)),
                "write position");
    const std::optional<Hephaestus::ActiveCameraView> view =
        Hephaestus::TryReadActiveCameraView(scene);
    if (!Check(view.has_value(), "driven view")) {
        return false;
    }
    ok &= Check(Near(view->pitchRadians, written.x), "setRotation pitch");
    ok &= Check(Near(view->yawRadians, written.y), "setRotation yaw");
    ok &= Check(Near(view->rollRadians, written.z), "setRotation roll");
    ok &= Check(NearVec(view->position, Vec3(3.0f, 4.0f, 5.0f)), "setPosition");

    Entity other = AddCamera(scene, "Inactive", false);
    other.GetComponent<TransformComponent>().position = Vec3(100.0f, 0.0f, 0.0f);
    const std::optional<Hephaestus::ActiveCameraView> still =
        Hephaestus::TryReadActiveCameraView(scene);
    ok &= Check(still.has_value() && still->entity == static_cast<entt::entity>(camera),
                "inactive camera is ignored");
    return ok;
}

bool TestViewFrameMatchesRenderView()
{
    bool ok = true;
    const float samples[][2] = {{0.0f, 0.0f}, {0.7f, 0.3f}, {-2.1f, -0.9f}, {3.0f, 1.2f}};
    for (const auto& sample : samples) {
        const float yaw = sample[0];
        const float pitch = sample[1];
        const Vec3 eye(1.0f, 2.0f, 3.0f);
        const Mat4 frame = Hephaestus::MakeCameraViewFrame(eye, yaw, pitch);
        const Mat4 view = Mat4::LookAt(eye, vadd(eye, RenderForward(yaw, pitch)), Vec3(0.0f, 1.0f, 0.0f));
        const Mat4 local = Mat4::Multiply(view, frame);
        ok &= Check(NearVec(Mat4::TransformPoint(local, Vec3(0.0f, 0.0f, 0.0f)), Vec3(0.0f, 0.0f, 0.0f)),
                    "view frame origin is the eye");
        ok &= Check(NearVec(Mat4::TransformDirection(local, Vec3(1.0f, 0.0f, 0.0f)), Vec3(0.0f, 0.0f, 1.0f)),
                    "view frame +X is view forward");
        ok &= Check(NearVec(Mat4::TransformDirection(local, Vec3(0.0f, 1.0f, 0.0f)), Vec3(0.0f, 1.0f, 0.0f)),
                    "view frame +Y is view up");
        ok &= Check(NearVec(Mat4::TransformDirection(local, Vec3(0.0f, 0.0f, 1.0f)), Vec3(1.0f, 0.0f, 0.0f)),
                    "view frame +Z is view right");
    }
    const Mat4 identity = Hephaestus::MakeCameraViewFrame(Vec3(0.0f, 0.0f, 0.0f), 0.0f, 0.0f);
    ok &= Check(NearVec(Mat4::TransformDirection(identity, Vec3(0.3f, 0.5f, 0.7f)), Vec3(0.3f, 0.5f, 0.7f)),
                "view frame is identity at yaw 0 pitch 0");
    return ok;
}

bool TestViewModelFovAdjust()
{
    const Vec3 eye(2.0f, 1.0f, -3.0f);
    const Mat4 view = Mat4::LookAt(eye, vadd(eye, RenderForward(0.6f, -0.2f)), Vec3(0.0f, 1.0f, 0.0f));
    const float aspect = 2.4f;
    const Mat4 worldProj = Mat4::Perspective(65.0f * 3.1415926535f / 180.0f, aspect, 0.02f, 1000.0f);
    const Mat4 viewModelProj = Mat4::Perspective(55.0f * 3.1415926535f / 180.0f, aspect, 0.02f, 1000.0f);
    const Mat4 adjust = Hephaestus::MakeViewModelFovAdjust(view, 65.0f, 55.0f);
    bool ok = true;
    for (const Vec3& offset : {Vec3(0.4f, -0.2f, 0.1f), Vec3(0.3f, 0.05f, -0.15f), Vec3(1.0f, 0.3f, 0.4f)}) {
        const Vec3 p = vadd(eye, Mat4::TransformDirection(Mat4::Invert(view), offset));
        const Vec4 viaAdjust = Mat4::TransformVec4(Mat4::Multiply(worldProj, Mat4::Multiply(view, adjust)),
                                                   Vec4(p.x, p.y, p.z, 1.0f));
        const Vec4 direct = Mat4::TransformVec4(Mat4::Multiply(viewModelProj, view), Vec4(p.x, p.y, p.z, 1.0f));
        ok &= Check(Near(viaAdjust.x / viaAdjust.w, direct.x / direct.w) &&
                        Near(viaAdjust.y / viaAdjust.w, direct.y / direct.w) &&
                        Near(viaAdjust.z / viaAdjust.w, direct.z / direct.w),
                    "view model FOV adjust projects like a camera at the view model FOV");
    }
    const Mat4 none = Hephaestus::MakeViewModelFovAdjust(view, 65.0f, 0.0f);
    ok &= Check(NearVec(Mat4::TransformPoint(none, Vec3(1.0f, 2.0f, 3.0f)), Vec3(1.0f, 2.0f, 3.0f)),
                "a view model FOV of 0 leaves view models alone");
    return ok;
}

} // namespace

int main()
{
    if (!TestViewModelFovAdjust()) {
        return 1;
    }
    bool ok = TestRootCamera();
    ok &= TestViewFrameMatchesRenderView();
    ok &= TestParentedCamera();
    ok &= TestParentYawChildPitch();
    ok &= TestSetRotationRoundTrip();
    if (ok) {
        std::cout << "ActiveCameraSyncTests passed.\n";
    }
    return ok ? 0 : 1;
}
