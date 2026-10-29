#version 450

layout(set = 0, binding = 0) uniform sampler2D sceneColor;  // the temporal result (engine/Gfx/TemporalResolve.h)

layout(push_constant) uniform ToneMapPush {
    uint outputIsSrgb; // 1 when the swapchain format applies the sRGB encode in hardware
} pc;

layout(location = 0) out vec4 outColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main() {
    vec4 hdr = texelFetch(sceneColor, ivec2(gl_FragCoord.xy), 0);
    vec3 color = max(hdr.rgb, vec3(0.0));
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    if (pc.outputIsSrgb != 0u) {
        color = srgbToLinear(color);
    }
    outColor = vec4(color, 1.0);
}
