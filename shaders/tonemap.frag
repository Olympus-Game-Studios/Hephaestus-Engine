#version 450

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput sceneColor;
layout(set = 0, binding = 1) uniform sampler2D bloom;
layout(set = 0, binding = 2) uniform sampler2D sceneTexture; // the same HDR image, for the scope lens's offset reads

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
    float lensRadius;     // scope lens radius as a fraction of the viewport height; 0 = no scope
    float lensDistortion; // barrel distortion at the lens edge
    float lensAberration; // colour fringing at the lens edge, as a fraction of the lens radius
    float lensEdge;       // how dark the lens gets at its edge, 0 to 1
    float lensRing;       // width of the dark tube around the lens, as a fraction of the lens radius
    float lensOpacity;    // fades the whole scope in and out
    float lensReticle;    // 1 draws the reticle
} pc;

layout(location = 0) out vec4 outColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// Tone maps and grades one HDR colour (display-referred result, before the screen effects).
vec3 grade(vec3 color, vec2 uv) {
    color = max(color, vec3(0.0));
    if (pc.bloomIntensity > 0.0) {
        color += texture(bloom, uv).rgb * pc.bloomIntensity;
    }
    color *= exp2(pc.exposure) * pc.colorFilter.rgb;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, pc.saturation);
    return clamp((color - 0.5) * pc.contrast + 0.5, 0.0, 1.0);
}

// Line coverage for a line `halfWidth` pixels wide at `distance` pixels.
float line(float distance, float halfWidth) {
    return 1.0 - smoothstep(halfWidth - 0.5, halfWidth + 0.5, abs(distance));
}

// The scope: the scene through a barrel-distorted lens with colour fringing and darkened edges, a dark tube
// around it, black outside, and a mil-dot reticle with a red centre dot. `p` is in pixels from the screen centre.
vec3 scope(vec3 plain, vec2 uv, vec2 p) {
    float radiusPx = pc.lensRadius * pc.viewportSize.y;
    float r = length(p) / radiusPx;
    float ringOuter = 1.0 + pc.lensRing;
    vec3 color = vec3(0.0);
    if (r < 1.0) {
        vec2 dir = p / max(length(p), 1e-4);
        float scale = 1.0 + pc.lensDistortion * r * r;
        vec2 shift = dir * pc.lensAberration * radiusPx * r * r;
        vec2 toUv = 1.0 / pc.viewportSize;
        vec2 centre = pc.viewportSize * 0.5;
        vec2 sampleR = (centre + p * scale + shift) * toUv;
        vec2 sampleG = (centre + p * scale) * toUv;
        vec2 sampleB = (centre + p * scale - shift) * toUv;
        vec3 hdr = vec3(texture(sceneTexture, sampleR).r, texture(sceneTexture, sampleG).g,
                        texture(sceneTexture, sampleB).b);
        color = grade(hdr, sampleG);
        color *= 1.0 - pc.lensEdge * smoothstep(0.55, 1.0, r);
        if (pc.lensReticle > 0.5) {
            // Sizes follow the lens, so the reticle reads the same at any resolution.
            float fine = max(0.6, radiusPx * 0.0018);
            float thick = max(1.8, radiusPx * 0.0065);
            float ink = max(line(p.y, fine), line(p.x, fine));
            // Thicker posts from the edge in to half the radius on the left, right and bottom; the centre stays fine.
            float posts = max(line(p.y, thick) * step(0.5, abs(p.x) / radiusPx),
                              line(p.x, thick) * step(0.5, p.y / radiusPx));
            ink = max(ink, posts);
            float spacing = radiusPx * 0.09;
            float dotRadius = max(1.4, radiusPx * 0.0065);
            for (int i = 1; i <= 5; ++i) {
                float d = spacing * float(i);
                ink = max(ink, 1.0 - smoothstep(dotRadius - 0.5, dotRadius + 0.5, length(vec2(abs(p.x) - d, p.y))));
                ink = max(ink, 1.0 - smoothstep(dotRadius - 0.5, dotRadius + 0.5, length(vec2(p.x, abs(p.y) - d))));
            }
            color = mix(color, vec3(0.015), ink * 0.94);
            float centreRadius = max(1.8, radiusPx * 0.008);
            float centreDot = 1.0 - smoothstep(centreRadius - 0.6, centreRadius + 0.6, length(p));
            color = mix(color, vec3(0.95, 0.08, 0.05), centreDot);
        }
    }
    // The tube: a bright bevel where it meets the glass, dark machined metal with two grooves, lit from the top left,
    // darkening into the black outside.
    float tube = smoothstep(0.995, 1.005, r) * (1.0 - smoothstep(ringOuter - 0.01, ringOuter + 0.01, r));
    if (tube > 0.0) {
        float t = clamp((r - 1.0) / max(pc.lensRing, 1e-3), 0.0, 1.0);
        float light = 0.5 + 0.5 * dot(normalize(p + 1e-4), normalize(vec2(-0.6, -0.8)));
        float bevel = exp(-t * 22.0) * (0.25 + 0.45 * light);
        float grooves = 1.0 - 0.45 * exp(-pow((t - 0.35) * 40.0, 2.0)) - 0.45 * exp(-pow((t - 0.62) * 40.0, 2.0));
        float metal = (0.07 + 0.06 * light) * (1.0 - 0.85 * smoothstep(0.55, 1.0, t)) * grooves;
        color = mix(color, vec3(metal) * vec3(1.0, 1.0, 1.06) + vec3(bevel), tube);
    }
    return mix(plain, color, clamp(pc.lensOpacity, 0.0, 1.0));
}

void main() {
    vec2 uv = gl_FragCoord.xy / max(pc.viewportSize, vec2(1.0));
    vec3 color = grade(subpassLoad(sceneColor).rgb, uv);

    if (pc.lensRadius > 0.0 && pc.lensOpacity > 0.0) {
        color = scope(color, uv, gl_FragCoord.xy - pc.viewportSize * 0.5);
    }

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
