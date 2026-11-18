#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;

// Layout must match EngineFrameRenderer.cpp:MaskedShadowPushConstants.
layout(push_constant) uniform MaskedShadowPush {
    mat4 lightModelViewProj;
    vec4 modelRows[3];
    uint albedoTextureIndex;
    float opacity;
    float clipThreshold;
    float worldTextureScale;
} pc;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec3 fragNormal;

void main() {
    vec4 position = vec4(inPosition, 1.0);
    fragWorldPos = vec3(dot(pc.modelRows[0], position), dot(pc.modelRows[1], position), dot(pc.modelRows[2], position));
    fragNormal = normalize(inverse(mat3(pc.modelRows[0].xyz, pc.modelRows[1].xyz, pc.modelRows[2].xyz)) * inNormal);
    fragTexCoord = inTexCoord;
    gl_Position = pc.lightModelViewProj * position;
}
