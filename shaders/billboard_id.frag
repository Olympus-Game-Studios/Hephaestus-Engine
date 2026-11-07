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

layout(location = 0) in vec2 inQuadPos;

layout(location = 0) out vec4 outColor;

// Tile shape from billboard.frag, so only the drawn tile picks the light.
const float kTileHalf = 0.5 - 6.0 / 128.0;
const float kTileRadius = 26.0 / 128.0;

float roundedBox(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
    if (roundedBox(inQuadPos, vec2(kTileHalf), kTileRadius) > 0.0) {
        discard;
    }

    uint id = pc.entityId;
    float r = float((id >>  0) & 0xFFu) / 255.0;
    float g = float((id >>  8) & 0xFFu) / 255.0;
    float b = float((id >> 16) & 0xFFu) / 255.0;
    float a = float((id >> 24) & 0xFFu) / 255.0;
    outColor = vec4(r, g, b, a);
}
