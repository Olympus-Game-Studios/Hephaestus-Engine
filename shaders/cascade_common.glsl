// Camera-following probe cascades (engine/Gfx/ProbeCascades.cpp). Define CASCADE_SET before including, after
// probe_common.glsl (octahedral helpers and tile sizes). Grid maths must match engine/Gfx/ProbeCascadeGrid.cpp.

const int CASCADES = 4;
const uint CASCADE_SLOT_BITS = 20u;  // ProbeCascadeScheduler::kSlotBits

layout(std140, set = CASCADE_SET, binding = 12) uniform CascadeParams {
    ivec4 counts;           // xyz = probes per axis, w = cascades in use (0 = off)
    ivec4 minCell[CASCADES];  // xyz = the grid's lowest world cell
    vec4 spacing;           // per cascade, metres
    vec4 settings;          // x = max hysteresis, y = max ray distance in cascade 0 spacings, z = job count
    mat4 rotation;          // applied to probeRayDirection, changes every frame
} cascades;

// probeOffset: xyz = move from the cell centre in probe spacings (|x| <= 0.45), w = 1 when the probe is switched
// off (stuck inside geometry). Both belong to the probe in probeState's cell; a new cell starts from zero.
#ifdef CASCADE_STATE_WRITE
layout(std430, set = CASCADE_SET, binding = 13) buffer CascadeState { ivec4 probeState[]; };
layout(std430, set = CASCADE_SET, binding = 19) buffer CascadeOffsets { vec4 probeOffset[]; };
#else
layout(std430, set = CASCADE_SET, binding = 13) readonly buffer CascadeState { ivec4 probeState[]; };
layout(std430, set = CASCADE_SET, binding = 19) readonly buffer CascadeOffsets { vec4 probeOffset[]; };
#endif

// GLSL leaves % undefined for negative operands, and world cells go negative.
int cascadeWrap(int v, int n) {
    return v - n * int(floor(float(v) / float(n)));
}

ivec3 cascadeSlotCoord(int slot) {
    ivec3 n = cascades.counts.xyz;
    return ivec3(slot % n.x, (slot / n.x) % n.y, slot / (n.x * n.y));
}

int cascadeSlotIndex(ivec3 s) {
    ivec3 n = cascades.counts.xyz;
    return s.x + s.y * n.x + s.z * n.x * n.y;
}

ivec3 cascadeCellOfSlot(int c, ivec3 s) {
    ivec3 lo = cascades.minCell[c].xyz;
    ivec3 n = cascades.counts.xyz;
    return lo + ivec3(cascadeWrap(s.x - lo.x, n.x), cascadeWrap(s.y - lo.y, n.y), cascadeWrap(s.z - lo.z, n.z));
}

ivec3 cascadeSlotOfCell(ivec3 cell) {
    ivec3 n = cascades.counts.xyz;
    return ivec3(cascadeWrap(cell.x, n.x), cascadeWrap(cell.y, n.y), cascadeWrap(cell.z, n.z));
}

int cascadeStateIndex(int c, ivec3 s) {
    return c * cascades.counts.x * cascades.counts.y * cascades.counts.z + cascadeSlotIndex(s);
}

// Probes sit at cell centres, so walls and floors on whole-metre coordinates fall between probes, not on them.
vec3 cascadeProbePosition(int c, ivec3 cell) {
    return (vec3(cell) + 0.5) * cascades.spacing[c];
}

// The probe's traced position: its cell centre plus its relocation, which only counts while it still holds that cell.
vec4 cascadeProbeOffset(int c, ivec3 slot, ivec3 cell) {
    int index = cascadeStateIndex(c, slot);
    return probeState[index].xyz == cell ? probeOffset[index] : vec4(0.0);
}

// Tiles: x = slot x + y * count x, y = slot z + cascade * count z.
ivec2 cascadeTile(int c, ivec3 s) {
    return ivec2(s.x + s.y * cascades.counts.x, s.z + c * cascades.counts.z);
}

#ifdef CASCADE_SAMPLING
layout(set = CASCADE_SET, binding = 14) uniform sampler2D cascadeIrradiance;
layout(set = CASCADE_SET, binding = 15) uniform sampler2D cascadeVisibility;

vec2 cascadeAtlasUV(ivec2 tile, vec3 dir, int tileSize, int inner, vec2 atlasSize) {
    vec2 origin = vec2(tile * tileSize + 1);
    vec2 local = (octEncode(dir) * 0.5 + 0.5) * float(inner);
    return (origin + local) / atlasSize;
}

// xyz = irradiance / pi from cascade c around n, w = 1 when any of its eight probes has been traced.
vec4 sampleCascade(int c, vec3 pos, vec3 n, vec3 viewDir) {
    float spacing = cascades.spacing[c];
    vec3 grid = pos / spacing - 0.5;
    ivec3 base = ivec3(floor(grid));
    vec3 alpha = grid - vec3(base);
    vec3 biasedPos = pos + (n * 0.2 + viewDir * 0.8) * (0.75 * spacing) * 0.3;
    vec2 irrSize = vec2(textureSize(cascadeIrradiance, 0));
    vec2 visSize = vec2(textureSize(cascadeVisibility, 0));

    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    for (int i = 0; i < 8; ++i) {
        ivec3 offset = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        ivec3 cell = base + offset;
        ivec3 slot = cascadeSlotOfCell(cell);
        ivec4 state = probeState[cascadeStateIndex(c, slot)];
        if (state.xyz != cell || state.w <= 0) continue;
        vec4 relocation = probeOffset[cascadeStateIndex(c, slot)];
        if (relocation.w > 0.5) continue;
        ivec2 tile = cascadeTile(c, slot);
        vec3 probePos = cascadeProbePosition(c, cell) + relocation.xyz * spacing;
        vec3 tri = mix(1.0 - alpha, alpha, vec3(offset));
        float weight = tri.x * tri.y * tri.z;

        vec3 toProbe = probePos - pos;
        float len = length(toProbe);
        vec3 dirToProbe = len > 1e-4 ? toProbe / len : n;
        float wrap = (dot(dirToProbe, n) + 1.0) * 0.5;
        weight *= wrap * wrap + 0.2;

        vec3 fromProbe = biasedPos - probePos;
        float dist = length(fromProbe);
        vec2 moments = texture(cascadeVisibility,
            cascadeAtlasUV(tile, dist > 1e-4 ? fromProbe / dist : -n, VISIBILITY_TILE, VISIBILITY_INNER, visSize)).rg;
        if (dist > moments.x) {
            float variance = abs(moments.x * moments.x - moments.y);
            float d = dist - moments.x;
            float chebyshev = variance / (variance + d * d);
            weight *= chebyshev * chebyshev * chebyshev;
        }
        // No floor for hidden probes, and tiny weights crushed further (RTXGI): a lit probe on the far side of a
        // wall must not tint a dark room through it.
        const float CRUSH = 0.2;
        if (weight < CRUSH) weight *= weight * weight / (CRUSH * CRUSH);
        weight = max(weight, 1e-6);
        sum += weight * texture(cascadeIrradiance, cascadeAtlasUV(tile, n, IRRADIANCE_TILE, IRRADIANCE_INNER, irrSize)).rgb;
        weightSum += weight;
    }
    return weightSum > 0.0 ? vec4(sum / weightSum, 1.0) : vec4(0.0);
}

// How far inside cascade c's grid pos is, in probes from the nearest face; negative outside.
float cascadeInset(int c, vec3 pos) {
    vec3 g = pos / cascades.spacing[c] - 0.5 - vec3(cascades.minCell[c].xyz);
    vec3 hi = vec3(cascades.counts.xyz - 1) - g;
    return min(min(min(g.x, g.y), g.z), min(min(hi.x, hi.y), hi.z));
}

// xyz = irradiance / pi, w = how much of it to use over the sky: 1 inside the grids, fading to 0 only over the
// outermost grid's last two probes. The finest grid that holds pos is used, cross-fading into the next one over
// its outer two probes. Probes not traced yet count as dark, never as sky: assuming sky there floods closed
// rooms with light that then takes many seconds to bounce away.
vec4 sampleCascades(vec3 pos, vec3 n, vec3 viewDir) {
    for (int c = 0; c < cascades.counts.w; ++c) {
        float inset = cascadeInset(c, pos);
        if (inset < 1.0) continue;
        vec4 here = sampleCascade(c, pos, n, viewDir);
        float keep = clamp((inset - 1.0) / 2.0, 0.0, 1.0);
        int o = c + 1;
        while (o < cascades.counts.w && cascadeInset(o, pos) < 1.0) ++o;
        bool hasOuter = o < cascades.counts.w;
        vec3 rgb = here.rgb;
        if (hasOuter && (keep < 1.0 || here.w == 0.0)) {
            vec4 outer = sampleCascade(o, pos, n, viewDir);
            if (outer.w > 0.0) rgb = here.w > 0.0 ? mix(outer.rgb, here.rgb, keep) : outer.rgb;
        }
        return vec4(rgb, hasOuter ? 1.0 : keep);
    }
    return vec4(0.0);
}
#endif
