#version 450
layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform ShadowPush {
    mat4 lightViewProj;
    mat4 model;
} pc;

void main() {
    gl_Position = pc.lightViewProj * pc.model * vec4(inPosition, 1.0);
}
