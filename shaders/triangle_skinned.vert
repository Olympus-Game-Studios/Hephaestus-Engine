#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    float padding1;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

layout(std140, set = 0, binding = 2) uniform LightingUBO {
    mat4 dirLightViewProj;
    vec4 dirShadowParams;
    vec4 dirLightDirIntensity;
    vec4 dirLightColor;
    vec4 ambientColorIntensity;
    vec4 environmentReflectionParams;
    vec4 pointLightPositions[16];
    vec4 pointLightColors[16];
    vec4 pointLightAttenuation[16];
    vec4 spotLightPositions[16];
    vec4 spotLightDirections[16];
    vec4 spotLightColors[16];
    vec4 spotLightAttenuation[16];
    int pointLightCount;
    int spotLightCount;
    int cascadeCount;
    float csmPadding1;
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;
} lighting;

layout(std140, set = 1, binding = 0) uniform BoneUBO {
    mat4 boneMats[128];
} bones;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragTexCoord;
layout(location = 4) out vec4 fragLightSpacePos;
layout(location = 5) flat out vec3 fragObjectPos;
layout(location = 6) out vec2 fragLightmapUV;

mat4 skinMatrix() {
    return inWeights.x * bones.boneMats[inJoints.x] +
           inWeights.y * bones.boneMats[inJoints.y] +
           inWeights.z * bones.boneMats[inJoints.z] +
           inWeights.w * bones.boneMats[inJoints.w];
}

void main() {
    mat4 skin = skinMatrix();
    vec4 localPos = skin * vec4(inPosition, 1.0);
    vec4 worldPos = pc.model * localPos;
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragWorldPos = worldPos.xyz;
    fragLightSpacePos = lighting.dirLightViewProj * worldPos;
    fragObjectPos = pc.model[3].xyz;

    mat3 skinnedNormal = mat3(pc.model) * mat3(skin);
    fragNormal = normalize(skinnedNormal * inNormal);

    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragLightmapUV = vec2(0.0);
}
