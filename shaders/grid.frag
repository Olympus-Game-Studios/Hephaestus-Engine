#version 450

// Grid fragment shader - procedurally renders an infinite grid on the XZ plane

layout(location = 0) in vec3 nearPoint;
layout(location = 1) in vec3 farPoint;
layout(location = 2) in mat4 fragView;
layout(location = 6) in mat4 fragProj;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform GridPushConstants {
    float gridSize;      // Size of major grid lines spacing
    float cellSize;      // Size of minor grid cells
    float fadeDistance;  // Distance at which grid fades out
    float lineWidth;     // Line width factor
} grid;

vec4 gridColor(vec3 fragPos3D, float scale, bool isAxisLine) {
    vec2 coord = fragPos3D.xz * scale;
    vec2 derivative = fwidth(coord);
    vec2 gridLine = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line = min(gridLine.x, gridLine.y);
    float minimumz = min(derivative.y, 1.0);
    float minimumx = min(derivative.x, 1.0);
    
    vec4 color = vec4(0.2, 0.2, 0.2, 1.0 - min(line, 1.0));
    
    // Highlight X axis (red) - using a small epsilon for the check
    if (fragPos3D.z > -0.5 * minimumz && fragPos3D.z < 0.5 * minimumz) {
        color = vec4(0.8, 0.2, 0.2, 1.0);
    }
    // Highlight Z axis (blue)
    if (fragPos3D.x > -0.5 * minimumx && fragPos3D.x < 0.5 * minimumx) {
        color = vec4(0.2, 0.2, 0.8, 1.0);
    }
    
    return color;
}

float computeDepth(vec3 pos) {
    vec4 clipSpacePos = fragProj * fragView * vec4(pos, 1.0);
    // Vulkan depth range is [0, 1]
    return clipSpacePos.z / clipSpacePos.w;
}

float computeLinearDepth(vec3 pos, float near, float far) {
    vec4 clipSpacePos = fragProj * fragView * vec4(pos, 1.0);
    float clipSpaceDepth = clipSpacePos.z / clipSpacePos.w;
    // For Vulkan [0,1] depth range
    float linearDepth = (2.0 * near * far) / (far + near - clipSpaceDepth * (far - near));
    return linearDepth / far; // Normalize to [0, 1]
}

void main() {
    // Compute where the ray intersects the XZ plane (Y=0)
    float t = -nearPoint.y / (farPoint.y - nearPoint.y);
    
    // Discard if plane is behind camera or too far
    if (t < 0.0 || t > 1.0) {
        discard;
    }
    
    vec3 fragPos3D = nearPoint + t * (farPoint - nearPoint);
    
    // Compute depth for depth buffer write
    gl_FragDepth = computeDepth(fragPos3D);
    
    // Distance-based fade
    float distFromCamera = length(fragPos3D - nearPoint);
    float fade = max(0.0, 1.0 - (distFromCamera / grid.fadeDistance));
    fade = fade * fade; // Quadratic falloff
    
    // Small grid (minor lines)
    vec4 smallGrid = gridColor(fragPos3D, 1.0 / grid.cellSize, false);
    // Large grid (major lines)  
    vec4 largeGrid = gridColor(fragPos3D, 1.0 / grid.gridSize, true);
    
    // Blend grids - major lines are more visible at distance
    float distanceFactor = min(1.0, distFromCamera / (grid.fadeDistance * 0.3));
    vec4 gridResult = mix(smallGrid, largeGrid, distanceFactor);
    
    // Apply fade
    gridResult.a *= fade;
    
    // Discard fully transparent pixels
    if (gridResult.a < 0.01) {
        discard;
    }
    
    outColor = gridResult;
}
