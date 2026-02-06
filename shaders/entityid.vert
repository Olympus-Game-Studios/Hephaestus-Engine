#version 450

// Entity ID picking vertex shader - render entities with their ID encoded in color

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float padding;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint entityId;
    uint padding1;
    uint padding2;
    uint padding3;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

void main() {
    gl_Position = camera.projection * camera.view * pc.model * vec4(inPosition, 1.0);
}
