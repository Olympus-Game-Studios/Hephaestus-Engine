#version 450
#extension GL_GOOGLE_include_directive : require

#define GI_SET 1
#include "probe_common.glsl"
#define CASCADE_SET 1
#define CASCADE_SAMPLING
#include "cascade_common.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) flat in ivec3 fragSlot;
layout(location = 2) flat in float fragOff;
layout(location = 0) out vec4 outColor;

void main() {
    if (fragOff > 0.5) {
        outColor = vec4(0.8, 0.05, 0.05, 1.0);  // switched off: stuck inside geometry
        return;
    }
    vec2 size = vec2(textureSize(cascadeIrradiance, 0));
    vec3 n = normalize(fragNormal);
    outColor = vec4(texture(cascadeIrradiance,
                            cascadeAtlasUV(cascadeTile(0, fragSlot), n, IRRADIANCE_TILE, IRRADIANCE_INNER, size)).rgb, 1.0);
}
