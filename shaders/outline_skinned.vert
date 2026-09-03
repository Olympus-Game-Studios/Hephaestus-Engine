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
    float expand;
} pc;

layout(std140, set = 1, binding = 0) uniform BoneUBO {
    mat4 boneMats[128];
} bones;

void main() {
    mat4 skin = inWeights.x * bones.boneMats[inJoints.x] +
                inWeights.y * bones.boneMats[inJoints.y] +
                inWeights.z * bones.boneMats[inJoints.z] +
                inWeights.w * bones.boneMats[inJoints.w];
    vec4 worldPos = pc.model * (skin * vec4(inPosition, 1.0));

    mat3 skinnedNormal = mat3(pc.model) * mat3(skin);
    vec3 worldNormal = normalize(skinnedNormal * inNormal);

    float distanceToCamera = length(worldPos.xyz - ubo.viewPos);
    float outlineScale = 0.02 * sqrt(max(distanceToCamera, 0.001) / 10.0);
    outlineScale = clamp(outlineScale, 0.01, 0.08);

    worldPos.xyz += worldNormal * outlineScale * pc.expand;

    gl_Position = ubo.proj * ubo.view * worldPos;
}
