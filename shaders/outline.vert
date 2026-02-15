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

    // Transform normal to world space
    mat3 normalMatrix = mat3(transpose(inverse(mat3(pc.model))));
    vec3 worldNormal = normalize(normalMatrix * inNormal);

    // Distance-based scale (grows slower with distance)
    float distanceToCamera = length(worldPos.xyz - ubo.viewPos);
    float outlineScale = 0.02 * sqrt(max(distanceToCamera, 0.001) / 10.0);
    outlineScale = clamp(outlineScale, 0.01, 0.08);

    // Expand along normal direction
    worldPos.xyz += worldNormal * outlineScale;

    gl_Position = ubo.proj * ubo.view * worldPos;
}
