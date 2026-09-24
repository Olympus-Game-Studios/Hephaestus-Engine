#version 450

// HephaestusUI Vulkan overlay - fragment shader
//
// Premultiplied alpha output: outColor.rgb is premultiplied by outColor.a, and
// the UI pipeline uses (ONE, ONE_MINUS_SRC_ALPHA) blending so text edges,
// PNG alpha and semi-transparent panels composite correctly.

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec4 vRect;
layout(location = 3) in vec2 vParams;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    float srgbOutput;
    float pad;
} pc;

// Signed distance to a rounded box centred at the origin with half-extent `b`
// and corner radius `r`.
float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

void main() {
    float rounded = 1.0;
    if (vParams.x > 0.0) {
        vec2 center = (vRect.xy + vRect.zw) * 0.5;
        vec2 halfSize = (vRect.zw - vRect.xy) * 0.5;
        float r = min(vParams.x, min(halfSize.x, halfSize.y));
        float d = sdRoundedBox(gl_FragCoord.xy - center, halfSize - vec2(r), r);
        rounded = clamp(0.5 - d, 0.0, 1.0);
    }

    vec4 texColor = texture(uTexture, vUV);

    // Vertex colours are authored in sRGB (matching the Qt editor). When the
    // swapchain attachment is sRGB the hardware re-encodes linear output, so
    // convert tint to linear first to avoid double-encoding.
    vec3 tint = vColor.rgb;
    if (pc.srgbOutput > 0.5) {
        tint = tint * tint;
    }

    float alpha = texColor.a * vColor.a * rounded;
    vec3 rgb = texColor.rgb * tint * alpha;
    outColor = vec4(rgb, alpha);
}