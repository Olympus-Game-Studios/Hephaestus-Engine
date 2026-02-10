#version 450

// Billboard entity ID picking fragment shader
// Outputs the entity ID encoded in RGBA channels

layout(push_constant) uniform PushConstants {
    vec3 position;
    float size;
    uint entityId;
    uint padding1;
    uint padding2;
    uint padding3;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    uint id = pc.entityId;
    float r = float((id >>  0) & 0xFFu) / 255.0;
    float g = float((id >>  8) & 0xFFu) / 255.0;
    float b = float((id >> 16) & 0xFFu) / 255.0;
    float a = float((id >> 24) & 0xFFu) / 255.0;
    outColor = vec4(r, g, b, a);
}
