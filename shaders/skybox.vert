#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 fragTexCoord;

void main() {
    // Remove translation from view matrix for skybox
    mat4 viewNoTranslation = mat4(mat3(ubo.view));
    vec4 pos = ubo.proj * viewNoTranslation * vec4(inPosition, 1.0);
    
    // Force skybox to be at max depth
    gl_Position = pos.xyww;
    
    // Use position as texture coordinate for cubemap sampling
    fragTexCoord = inPosition;
}
