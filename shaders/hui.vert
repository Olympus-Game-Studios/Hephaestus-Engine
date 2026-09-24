#version 450

// HephaestusUI Vulkan overlay - vertex shader
//
// Vertex layout (UIVertex):
//   location 0: vec2 aPos        - viewport pixel coords (top-left origin, y-down)
//   location 1: vec2 aUV         - texture atlas coordinates
//   location 2: vec4 aColor      - straight (non-premultiplied) RGBA
//   location 3: vec4 aRect       - rounded-rect bounds minX,minY,maxX,maxY (pixels)
//   location 4: vec2 aParams     - cornerRadius, unused

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aRect;
layout(location = 4) in vec2 aParams;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec4 vRect;
layout(location = 3) out vec2 vParams;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    float srgbOutput;
    float pad;
} pc;

void main() {
    vec2 ndc = vec2(
        aPos.x / pc.viewportSize.x * 2.0 - 1.0,
        1.0 - aPos.y / pc.viewportSize.y * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
    vRect = aRect;
    vParams = aParams;
}