#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    float padding1;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

void main() {
    // Transform to world space
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);

    // Approximate world-space pivot using model translation (column 3)
    vec3 worldPivot = pc.model[3].xyz;

    // Direction from pivot to vertex (handles rotations/scales reasonably well)
    vec3 offsetDir = worldPos.xyz - worldPivot;
    float dirLen = length(offsetDir);
    if (dirLen < 1e-4) {
        mat3 normalMatrix = mat3(transpose(inverse(mat3(pc.model))));
        offsetDir = normalize(normalMatrix * inNormal);
    } else {
        offsetDir /= dirLen;
    }

    // Distance-based scale (grows slower with distance)
    float distanceToCamera = length(worldPos.xyz - ubo.viewPos);
    float outlineScale = 0.015 * sqrt(max(distanceToCamera, 0.001) / 10.0);
    outlineScale = clamp(outlineScale, 0.008, 0.05);

    // Expand away from pivot
    worldPos.xyz += offsetDir * outlineScale;

    gl_Position = ubo.proj * ubo.view * worldPos;
}
