#version 450
#extension GL_GOOGLE_include_directive : require

#define GI_SET 1
#include "probe_common.glsl"

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    float shadingMode;
} ubo;

layout(push_constant) uniform DebugPush {
    float radius;
} pc;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) flat out int fragProbe;

const int SEGMENTS = 12;
const int RINGS = 8;
const float PI = 3.14159265359;

vec3 spherePoint(int segment, int ring) {
    float theta = float(ring) / float(RINGS) * PI;
    float phi = float(segment) / float(SEGMENTS) * 2.0 * PI;
    return vec3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
}

void main() {
    // SEGMENTS * RINGS quads of two triangles each, generated from gl_VertexIndex.
    const int corners[6] = int[](0, 1, 2, 2, 1, 3);
    int quad = gl_VertexIndex / 6;
    int corner = corners[gl_VertexIndex % 6];
    int segment = quad % SEGMENTS + (corner & 1);
    int ring = quad / SEGMENTS + (corner >> 1);
    vec3 n = spherePoint(segment, ring);
    fragNormal = n;
    fragProbe = gl_InstanceIndex;
    gl_Position = ubo.proj * ubo.view * vec4(probePosition(gl_InstanceIndex) + n * pc.radius, 1.0);
}
