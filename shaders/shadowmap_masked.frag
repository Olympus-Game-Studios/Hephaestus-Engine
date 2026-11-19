#version 450
#extension GL_GOOGLE_include_directive : require

#define MAX_TEXTURE_SLOTS 512 // engine/Gfx/MaterialTextureSlots.h

layout(push_constant) uniform MaskedShadowPush {
    mat4 lightModelViewProj;
    vec4 modelRows[3];
    uint albedoTextureIndex;
    float opacity;
    float clipThreshold;
    float worldTextureScale;
} pc;

layout(set = 0, binding = 3) uniform sampler2D materialTextures[MAX_TEXTURE_SLOTS];

#include "material_common.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec3 fragNormal;

void main() {
    vec2 texCoord = pc.worldTextureScale > 0.0
        ? getWorldAlignedUV(fragWorldPos, normalize(fragNormal), pc.worldTextureScale)
        : fragTexCoord;
    float alpha = texture(materialTextures[pc.albedoTextureIndex % MAX_TEXTURE_SLOTS], texCoord).a;
    if (clamp(pc.opacity, 0.0, 1.0) * alpha < clamp(pc.clipThreshold, 0.0, 1.0)) {
        discard;
    }
}
