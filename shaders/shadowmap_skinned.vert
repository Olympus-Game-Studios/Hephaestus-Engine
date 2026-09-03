#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;

layout(std140, set = 0, binding = 0) uniform BoneUBO {
    mat4 boneMats[128];
} bones;

layout(push_constant) uniform ShadowPush {
    mat4 lightViewProj;
    mat4 model;
} pc;

void main() {
    mat4 skin = inWeights.x * bones.boneMats[inJoints.x] +
                inWeights.y * bones.boneMats[inJoints.y] +
                inWeights.z * bones.boneMats[inJoints.z] +
                inWeights.w * bones.boneMats[inJoints.w];
    gl_Position = pc.lightViewProj * pc.model * skin * vec4(inPosition, 1.0);
}
