#version 450
#extension GL_GOOGLE_include_directive : require

// One sphere per probe of the finest camera-following grid (editor viewport "GI Probes" overlay).
#define GI_SET 1
#include "probe_common.glsl"
#define CASCADE_SET 1
#include "cascade_common.glsl"

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
layout(location = 1) flat out ivec3 fragSlot;
layout(location = 2) flat out float fragOff;

const int SEGMENTS = 12;
const int RINGS = 8;
const float PI = 3.14159265359;

vec3 spherePoint(int segment, int ring) {
    float theta = float(ring) / float(RINGS) * PI;
    float phi = float(segment) / float(SEGMENTS) * 2.0 * PI;
    return vec3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
}

void main() {
    const int corners[6] = int[](0, 1, 2, 2, 1, 3);
    int quad = gl_VertexIndex / 6;
    int corner = corners[gl_VertexIndex % 6];
    vec3 n = spherePoint(quad % SEGMENTS + (corner & 1), quad / SEGMENTS + (corner >> 1));
    ivec3 slot = cascadeSlotCoord(gl_InstanceIndex);
    ivec3 cell = cascadeCellOfSlot(0, slot);
    int index = cascadeStateIndex(0, slot);
    fragNormal = n;
    fragSlot = slot;
    fragOff = probeOffset[index].w;
    if (probeState[index].xyz != cell || probeState[index].w <= 0) {
        gl_Position = vec4(0.0, 0.0, -1.0, 1.0);  // not traced yet: collapse the sphere
        return;
    }
    vec3 center = cascadeProbePosition(0, cell) + probeOffset[index].xyz * cascades.spacing[0];
    gl_Position = ubo.proj * ubo.view * vec4(center + n * pc.radius, 1.0);
}
