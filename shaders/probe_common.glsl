// Shared probe GI declarations and sampling. Define GI_SET before including.
// Layout must match engine/Gfx/ProbeGI.cpp:ProbeGIParams and the atlas layout in ProbeGI::createAtlases.

layout(std140, set = GI_SET, binding = 0) uniform ProbeGIParams {
    vec4 firstProbe;    // xyz = grid position of probe (0,0,0), w = intensity
    vec4 probeSpacing;  // xyz = grid step (0 on single-probe axes), w = hysteresis
    ivec4 probeCounts;  // xyz = probes per axis, w = 1 when the volume is active
    mat4 sunViewProj;
    vec4 giParams;      // x = max ray distance, y = 1 when the sun map is valid, z = 1 to discard history
} gi;

layout(std430, set = GI_SET, binding = 3) readonly buffer ProbeOffsets { vec4 probeOffsets[]; };
layout(set = GI_SET, binding = 9) uniform sampler2D irradianceAtlas;
layout(set = GI_SET, binding = 10) uniform sampler2D visibilityAtlas;

const int IRRADIANCE_INNER = 8;
const int IRRADIANCE_TILE = IRRADIANCE_INNER + 2;
const int VISIBILITY_INNER = 16;
const int VISIBILITY_TILE = VISIBILITY_INNER + 2;

vec2 octEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 e = n.xz;
    if (n.y < 0.0) {
        e = (1.0 - abs(e.yx)) * vec2(e.x >= 0.0 ? 1.0 : -1.0, e.y >= 0.0 ? 1.0 : -1.0);
    }
    return e;
}

vec3 octDecode(vec2 e) {
    vec3 n = vec3(e.x, 1.0 - abs(e.x) - abs(e.y), e.y);
    if (n.y < 0.0) {
        n.xz = (1.0 - abs(n.zx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

ivec3 probeCoord(int p) {
    ivec3 c = gi.probeCounts.xyz;
    return ivec3(p % c.x, (p / c.x) % c.y, p / (c.x * c.y));
}

int probeIndex(ivec3 c) {
    return c.x + c.y * gi.probeCounts.x + c.z * gi.probeCounts.x * gi.probeCounts.y;
}

vec3 probePosition(int p) {
    return gi.firstProbe.xyz + gi.probeSpacing.xyz * vec3(probeCoord(p)) + probeOffsets[p].xyz;
}

ivec2 probeTile(int p) {
    int tilesX = gi.probeCounts.x * gi.probeCounts.y;
    return ivec2(p % tilesX, p / tilesX);
}

vec2 probeAtlasUV(int p, vec3 dir, int tile, int inner, vec2 atlasSize) {
    vec2 origin = vec2(probeTile(p) * tile + 1);
    vec2 local = (octEncode(dir) * 0.5 + 0.5) * float(inner);
    return (origin + local) / atlasSize;
}

// Cosine-weighted mean radiance around n at pos (irradiance / pi), DDGI-style weights.
vec3 sampleProbeIrradiance(vec3 pos, vec3 n) {
    ivec3 counts = gi.probeCounts.xyz;
    vec3 spacing = gi.probeSpacing.xyz;
    vec3 safeSpacing = max(spacing, vec3(1e-4));
    vec3 grid = (pos - gi.firstProbe.xyz) / safeSpacing;
    ivec3 base = clamp(ivec3(floor(grid)), ivec3(0), max(counts - 2, ivec3(0)));
    vec3 alpha = clamp(grid - vec3(base), 0.0, 1.0);
    alpha = mix(alpha, vec3(0.0), lessThanEqual(spacing, vec3(0.0)));

    vec2 irrSize = vec2(textureSize(irradianceAtlas, 0));
    vec2 visSize = vec2(textureSize(visibilityAtlas, 0));
    float bias = 0.05 * max(max(spacing.x, spacing.y), max(spacing.z, 0.5));
    vec3 biasedPos = pos + n * bias;

    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    for (int i = 0; i < 8; ++i) {
        ivec3 offset = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        ivec3 coord = min(base + offset, counts - 1);
        int p = probeIndex(coord);
        vec3 probePos = probePosition(p);
        vec3 tri = mix(1.0 - alpha, alpha, vec3(offset));
        float weight = tri.x * tri.y * tri.z;

        vec3 toProbe = probePos - pos;
        float len = length(toProbe);
        vec3 dirToProbe = len > 1e-4 ? toProbe / len : n;
        float wrap = (dot(dirToProbe, n) + 1.0) * 0.5;
        weight *= wrap * wrap + 0.2;

        vec3 fromProbe = biasedPos - probePos;
        float dist = length(fromProbe);
        vec2 moments = texture(visibilityAtlas,
            probeAtlasUV(p, dist > 1e-4 ? fromProbe / dist : -n, VISIBILITY_TILE, VISIBILITY_INNER, visSize)).rg;
        if (dist > moments.x) {
            float variance = abs(moments.x * moments.x - moments.y);
            float d = dist - moments.x;
            float chebyshev = variance / (variance + d * d);
            weight *= max(chebyshev * chebyshev * chebyshev, 0.05);
        }
        weight = max(weight, 1e-6);

        sum += weight * texture(irradianceAtlas, probeAtlasUV(p, n, IRRADIANCE_TILE, IRRADIANCE_INNER, irrSize)).rgb;
        weightSum += weight;
    }
    return sum / weightSum;
}
