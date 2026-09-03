#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float padding;
} camera;

layout(std140, set = 1, binding = 0) uniform BoneUBO {
    mat4 boneMats[128];
} bones;

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint entityId;
    uint padding1;
    uint padding2;
    uint padding3;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;

void main() {
    mat4 skin = inWeights.x * bones.boneMats[inJoints.x] +
                inWeights.y * bones.boneMats[inJoints.y] +
                inWeights.z * bones.boneMats[inJoints.z] +
                inWeights.w * bones.boneMats[inJoints.w];
    gl_Position = camera.projection * camera.view * pc.model * skin * vec4(inPosition, 1.0);
}
