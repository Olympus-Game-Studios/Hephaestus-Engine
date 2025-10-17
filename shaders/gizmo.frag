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
layout(location = 3) flat in vec4 gizmoState;

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

    // Determine which axis this vertex belongs to based on its color
    int axisId = -1;
    if (fragColor.r > fragColor.g && fragColor.r > fragColor.b) {
        axisId = 0;
    } else if (fragColor.g > fragColor.r && fragColor.g > fragColor.b) {
        axisId = 1;
    } else if (fragColor.b > fragColor.r && fragColor.b > fragColor.g) {
        axisId = 2;
    }

    int hoveredAxis = gizmoState.x > -0.5 ? int(gizmoState.x + 0.5) : -1;
    int activeAxis = gizmoState.y > -0.5 ? int(gizmoState.y + 0.5) : -1;

    float intensity = 1.0;
    if (axisId >= 0) {
        if (axisId == hoveredAxis && axisId != activeAxis) {
            intensity = 1.35;
        }
        if (axisId == activeAxis) {
            intensity = 1.8;
        }
    }

    vec3 finalColor = (ambient + diffuse) * intensity;
    outColor = vec4(finalColor, 1.0);
}
