#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

layout(location = 0) out vec3 fragColor;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    fragColor = inColor;
}
