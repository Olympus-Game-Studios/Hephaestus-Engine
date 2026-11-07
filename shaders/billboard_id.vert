#version 450

// Billboard entity ID picking vertex shader
// Generates a camera-facing quad from push constants (no vertex input)

layout(push_constant) uniform PushConstants {
    vec3 position;  // Billboard world position
    float size;     // Half-extent as a fraction of the viewport half-height
    uint entityId;  // Entity ID for picking
    uint padding1;
    uint padding2;
    uint padding3;
} pc;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    float padding;
} camera;

void main() {
    // Generate quad corners from gl_VertexIndex (triangle strip: 4 verts)
    vec2 quadCorners[4] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0),
        vec2( 1.0,  1.0)
    );

    int cornerIndex = gl_VertexIndex % 4;
    vec2 corner = quadCorners[cornerIndex];

    // Camera-facing billboard using view matrix axes
    vec3 cameraUp    = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);
    vec3 cameraRight = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);

    // Constant on-screen size: half-extent is a fraction of the viewport half-height.
    vec3 forward = vec3(camera.view[0][2], camera.view[1][2], camera.view[2][2]);
    bool ortho = abs(camera.proj[3][3] - 1.0) < 1e-3;
    float depth = ortho ? 1.0 : max(dot(pc.position - camera.viewPos, forward), 1e-3);
    float halfExtent = pc.size * depth / abs(camera.proj[1][1]);

    vec3 worldPos = pc.position
        + cameraRight * corner.x * halfExtent
        + cameraUp    * corner.y * halfExtent;

    gl_Position = camera.proj * camera.view * vec4(worldPos, 1.0);
}
