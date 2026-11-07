#version 450

// Editor light icon: a rounded tile in the editor's panel colours with a tinted glyph on top.

layout(location = 0) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec3 position;
    float size;
    vec4 tint;
} pc;

// White glyph on a transparent background (editor/assets/billboards).
layout(binding = 1) uniform sampler2D texSampler;

const vec3 kTileFill = vec3(0x1C, 0x23, 0x30) / 255.0;    // theme surface-raised
const vec3 kTileBorder = vec3(0x31, 0x3C, 0x4F) / 255.0;  // theme border-default
const float kTileHalf = 0.5 - 6.0 / 128.0;
const float kTileRadius = 26.0 / 128.0;
const float kBorderWidth = 3.0 / 128.0;

float roundedBox(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
    float d = roundedBox(inTexCoord - 0.5, vec2(kTileHalf), kTileRadius);
    float aa = max(fwidth(d), 1e-4);
    float coverage = 1.0 - smoothstep(-aa, aa, d);
    if (coverage <= 0.0) {
        discard;
    }

    float border = smoothstep(-kBorderWidth - aa, -kBorderWidth + aa, d);
    vec3 tile = mix(kTileFill, kTileBorder, border);

    vec4 glyph = texture(texSampler, inTexCoord);
    vec3 color = mix(tile, glyph.rgb * pc.tint.rgb, glyph.a);
    outColor = vec4(color, coverage * mix(0.92, 1.0, glyph.a));
}
