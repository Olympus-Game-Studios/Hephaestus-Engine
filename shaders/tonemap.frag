#version 450

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput sceneColor;
layout(set = 0, binding = 1) uniform sampler2D bloom;

layout(push_constant) uniform ToneMapPush {
    uint outputIsSrgb; // 1 when the swapchain format applies the sRGB encode in hardware
    float exposure;    // stops
    float contrast;
    float saturation;
    vec4 colorFilter;  // rgb multiplier
    vec2 viewportSize;
    float vignette;
    float grain;
    float scanlines;
    float time;
    float bloomIntensity;
} pc;

layout(location = 0) out vec4 outColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = gl_FragCoord.xy / max(pc.viewportSize, vec2(1.0));
    vec3 color = max(subpassLoad(sceneColor).rgb, vec3(0.0));
    if (pc.bloomIntensity > 0.0) {
        color += texture(bloom, uv).rgb * pc.bloomIntensity;
    }
    color *= exp2(pc.exposure) * pc.colorFilter.rgb;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, pc.saturation);
    color = clamp((color - 0.5) * pc.contrast + 0.5, 0.0, 1.0);

    vec2 centred = (uv - 0.5) * vec2(pc.viewportSize.x / max(pc.viewportSize.y, 1.0), 1.0);
    color *= 1.0 - pc.vignette * smoothstep(0.35, 1.05, length(centred));
    color *= 1.0 - pc.scanlines * step(mod(floor(gl_FragCoord.y), 3.0), 0.5);
    color += pc.grain * (hash(gl_FragCoord.xy + fract(pc.time) * 97.0) - 0.5) * 0.12;
    color = clamp(color, 0.0, 1.0);

    if (pc.outputIsSrgb != 0u) {
        color = srgbToLinear(color);
    }
    outColor = vec4(color, 1.0);
}
