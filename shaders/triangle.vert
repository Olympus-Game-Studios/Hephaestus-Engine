#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
    vec3 viewPos;      // Camera position in world space
    float padding1;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

layout(std140, set = 0, binding = 2) uniform LightingUBO {
    mat4 dirLightViewProj;
    vec4 dirShadowParams;
    vec4 dirLightDirIntensity;      // xyz = direction, w = intensity
    vec4 dirLightColor;             // rgb = color, w = unused
    vec4 ambientColorIntensity;     // rgb = ambient color, w = ambient intensity
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

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragTexCoord;
layout(location = 4) out vec4 fragLightSpacePos;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    
    fragWorldPos = worldPos.xyz;
    fragLightSpacePos = lighting.dirLightViewProj * worldPos;
    
    // Transform normal to world space
    mat3 normalMatrix = mat3(transpose(inverse(mat3(pc.model))));
    fragNormal = normalize(normalMatrix * inNormal);
    
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
