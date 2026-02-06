#version 450

// Grid vertex shader - renders a fullscreen quad that we'll use to draw the grid procedurally
// Using the near-far plane technique for infinite grid rendering

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float padding;
} camera;

layout(push_constant) uniform GridPushConstants {
    float gridSize;      // Size of major grid lines (world units)
    float cellSize;      // Size of minor grid cells
    float fadeDistance;  // Distance at which grid fades out
    float lineWidth;     // Line width factor
} grid;

// Output to fragment shader
layout(location = 0) out vec3 nearPoint;
layout(location = 1) out vec3 farPoint;
layout(location = 2) out mat4 fragView;
layout(location = 6) out mat4 fragProj;

// Fullscreen quad positions (CCW)
vec3 gridPlane[6] = vec3[](
    vec3( 1,  1, 0), vec3(-1, -1, 0), vec3(-1,  1, 0),
    vec3(-1, -1, 0), vec3( 1,  1, 0), vec3( 1, -1, 0)
);

vec3 UnprojectPoint(float x, float y, float z, mat4 viewInv, mat4 projInv) {
    vec4 unprojectedPoint = viewInv * projInv * vec4(x, y, z, 1.0);
    return unprojectedPoint.xyz / unprojectedPoint.w;
}

void main() {
    vec3 p = gridPlane[gl_VertexIndex];
    
    mat4 viewInv = inverse(camera.view);
    mat4 projInv = inverse(camera.projection);
    
    nearPoint = UnprojectPoint(p.x, p.y, 0.0, viewInv, projInv);
    farPoint = UnprojectPoint(p.x, p.y, 1.0, viewInv, projInv);
    
    fragView = camera.view;
    fragProj = camera.projection;
    
    gl_Position = vec4(p, 1.0);
}
