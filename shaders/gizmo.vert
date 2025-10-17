#version 450

// Gizmo vertex shader - renders transform manipulation handles

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
} camera;

layout(push_constant) uniform GizmoPushConstants {
    mat4 model;      // Transform to world space
    vec4 state;      // x = hovered axis, y = active axis, z = gizmo scale
} gizmo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) flat out vec4 gizmoState;

void main() {
    vec4 worldPos = gizmo.model * vec4(inPosition, 1.0);
    gl_Position = camera.proj * camera.view * worldPos;
    
    fragColor = inColor; // Use vertex color
    fragNormal = mat3(transpose(inverse(gizmo.model))) * inNormal;
    fragWorldPos = worldPos.xyz;
    gizmoState = gizmo.state;
}
