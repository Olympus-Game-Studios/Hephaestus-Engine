#version 450

// Push constants for billboard position and size
layout(push_constant) uniform PushConstants {
    vec3 position;  // Light position in world space
    float size;     // Half-extent as a fraction of the viewport half-height
    vec4 tint;      // Glyph colour, used by billboard.frag
} pc;

// Output to fragment shader
layout(location = 0) out vec2 outTexCoord;

// Uniforms
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
} ubo;

void main() {
    // Generate quad vertices (4 vertices per billboard instance)
    // Use gl_VertexIndex to determine which corner of the quad we're rendering
    vec2 quadCorners[4] = vec2[](
        vec2(-1.0, -1.0),  // Bottom-left
        vec2( 1.0, -1.0),  // Bottom-right
        vec2(-1.0,  1.0),  // Top-left
        vec2( 1.0,  1.0)   // Top-right
    );
    
    vec2 texCoords[4] = vec2[](
        vec2(0.0, 1.0),  // Bottom-left
        vec2(1.0, 1.0),  // Bottom-right
        vec2(0.0, 0.0),  // Top-left
        vec2(1.0, 0.0)   // Top-right
    );
    
    int cornerIndex = gl_VertexIndex % 4;
    vec2 corner = quadCorners[cornerIndex];
    outTexCoord = texCoords[cornerIndex];
    
    // Calculate camera-facing billboard orientation
    vec3 cameraUp = vec3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);
    vec3 cameraRight = vec3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);

    // Constant on-screen size: half-extent is a fraction of the viewport half-height.
    vec3 forward = vec3(ubo.view[0][2], ubo.view[1][2], ubo.view[2][2]);
    bool ortho = abs(ubo.proj[3][3] - 1.0) < 1e-3;
    float depth = ortho ? 1.0 : max(dot(pc.position - ubo.viewPos, forward), 1e-3);
    float halfExtent = pc.size * depth / abs(ubo.proj[1][1]);
    
    // Build the billboard quad in world space using push constant position
    vec3 worldPos = pc.position 
        + cameraRight * corner.x * halfExtent
        + cameraUp * corner.y * halfExtent;
    
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}
