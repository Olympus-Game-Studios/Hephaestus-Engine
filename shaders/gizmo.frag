#version 450

// Gizmo fragment shader - simple shading with edge highlighting

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
} camera;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    // Normalize the normal
    vec3 N = normalize(fragNormal);
    
    // Simple directional lighting
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.5));
    float diff = max(dot(N, lightDir), 0.0);
    
    // Add ambient + diffuse
    vec3 ambient = fragColor * 0.4;
    vec3 diffuse = fragColor * diff * 0.6;
    
    // Bright, saturated colors for gizmo
    vec3 finalColor = ambient + diffuse;
    
    outColor = vec4(finalColor, 1.0);
}
