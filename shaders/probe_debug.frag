#version 450
#extension GL_GOOGLE_include_directive : require

#define GI_SET 1
#include "probe_common.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) flat in int fragProbe;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 size = vec2(textureSize(irradianceAtlas, 0));
    vec3 n = normalize(fragNormal);
    outColor = vec4(texture(irradianceAtlas, probeAtlasUV(fragProbe, n, IRRADIANCE_TILE, IRRADIANCE_INNER, size)).rgb, 1.0);
}
