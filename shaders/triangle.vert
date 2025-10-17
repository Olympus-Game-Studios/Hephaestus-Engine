#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
    vec3 viewPos;      // Camera position in world space
    float padding1;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space (inverse transpose for non-uniform scaling)
    // For uniform scaling, this simplifies to just the rotation part
    mat3 normalMatrix = mat3(transpose(inverse(mat3(pc.model))));
    fragNormal = normalize(normalMatrix * inNormal);
    
    fragColor = inColor;
}
