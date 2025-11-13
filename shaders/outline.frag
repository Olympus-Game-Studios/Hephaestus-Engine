#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 outlineColor; // We'll use part of the push constant for color
} pc;

void main() {
    // Solid color for the outline - bright orange/yellow to make it visible
    outColor = vec4(1.0, 0.6, 0.0, 1.0); // Orange outline
}
