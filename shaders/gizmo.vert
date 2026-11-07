#version 450

// Gizmo vertex shader - renders transform manipulation handles

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
} camera;

layout(push_constant) uniform GizmoPushConstants {
    mat4 model;      // Transform to world space
    vec4 state;      // x = hovered axis, y = active axis, z = gizmo scale, w = rotate arc sweep
} gizmo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in float inHandle;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) flat out vec4 gizmoState;
layout(location = 4) out vec3 fragLocalPos;
layout(location = 5) flat out int fragHandle;
layout(location = 6) flat out vec3 toCamera;

void main() {
    vec3 origin = gizmo.model[3].xyz;
    bool ortho = abs(camera.proj[3][3] - 1.0) < 1e-3;
    vec3 toEye = camera.viewPos - origin;
    vec3 forward = vec3(camera.view[0][2], camera.view[1][2], camera.view[2][2]);
    toCamera = (ortho || dot(toEye, toEye) < 1e-8) ? -normalize(forward) : normalize(toEye);

    int handle = int(round(inHandle));
    vec3 pos = inPosition;
    if (handle >= 4 && handle <= 6) {
        // Plane handles sit in the quadrant that faces the camera.
        vec3 localToCamera = transpose(mat3(gizmo.model)) * toCamera;
        vec3 s = vec3(localToCamera.x >= 0.0 ? 1.0 : -1.0,
                      localToCamera.y >= 0.0 ? 1.0 : -1.0,
                      localToCamera.z >= 0.0 ? 1.0 : -1.0);
        if (handle == 4) pos.xy *= s.xy;
        else if (handle == 5) pos.xz *= s.xz;
        else pos.yz *= s.yz;
    }

    vec4 worldPos = gizmo.model * vec4(pos, 1.0);
    gl_Position = camera.proj * camera.view * worldPos;

    fragColor = inColor;
    fragNormal = mat3(transpose(inverse(gizmo.model))) * inNormal;
    fragWorldPos = worldPos.xyz;
    gizmoState = gizmo.state;
    fragLocalPos = pos;
    fragHandle = handle;
}
