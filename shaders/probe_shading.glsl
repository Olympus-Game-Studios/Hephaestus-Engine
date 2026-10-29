// Lighting at a ray's hit and octahedral texel directions, shared by the probe updates and the surface shader's
// traced reflections. Include after lighting_common.glsl and probe_common.glsl.

const float PI = 3.14159265359;

// Light arriving along dir from the sky (sun excluded), for rays that miss.
vec3 skyRadiance(vec3 dir) {
    if (lighting.environmentReflectionParams.z > 0.5) {
        return evaluateSkyRadiance(dir) * lighting.environmentReflectionParams.w;
    }
    return lighting.ambientColorIntensity.rgb * lighting.ambientColorIntensity.w;
}

// Sky light reaching a surface facing n, already integrated over the hemisphere.
vec3 skyIrradiance(vec3 n) {
    if (lighting.environmentReflectionParams.z > 0.5) {
        return evaluateSkyIrradiance(n) * lighting.environmentReflectionParams.w;
    }
    return lighting.ambientColorIntensity.rgb * lighting.ambientColorIntensity.w * PI;
}

vec3 directIrradiance(vec3 pos, vec3 n, float sunVisibility) {
    vec3 e = vec3(0.0);
    if (lighting.dirLightDirIntensity.w > 0.0) {
        vec3 l = normalize(-lighting.dirLightDirIntensity.xyz);
        e += lighting.dirLightColor.rgb * lighting.dirLightDirIntensity.w * max(dot(n, l), 0.0) * sunVisibility;
    }
    for (int i = 0; i < lighting.pointLightCount && i < 16; ++i) {
        vec3 toLight = lighting.pointLightPositions[i].xyz - pos;
        float dist = length(toLight);
        if (dist > lighting.pointLightPositions[i].w) continue;
        float atten = calculateAttenuation(dist, lighting.pointLightAttenuation[i].xyz);
        e += lighting.pointLightColors[i].rgb * lighting.pointLightColors[i].w * atten *
             max(dot(n, toLight / max(dist, 1e-4)), 0.0);
    }
    for (int i = 0; i < lighting.spotLightCount && i < 16; ++i) {
        vec3 toLight = lighting.spotLightPositions[i].xyz - pos;
        float dist = length(toLight);
        if (dist > lighting.spotLightPositions[i].w) continue;
        vec3 l = toLight / max(dist, 1e-4);
        float cone = calculateSpotlightFalloff(lighting.spotLightDirections[i].xyz, -l,
                                               lighting.spotLightDirections[i].w,
                                               lighting.spotLightAttenuation[i].w);
        if (cone <= 0.0) continue;
        float atten = calculateAttenuation(dist, lighting.spotLightAttenuation[i].xyz);
        e += lighting.spotLightColors[i].rgb * lighting.spotLightColors[i].w * atten * cone * max(dot(n, l), 0.0);
    }
    return e;
}

vec3 texelDirection(ivec2 local, int inner) {
    // Border texels (0 and inner + 1) take the direction just across the octahedral fold.
    vec2 uv = (vec2(local) - 0.5) / float(inner) * 2.0 - 1.0;
    if (abs(uv.x) > 1.0) {
        uv.x = sign(uv.x) * 2.0 - uv.x;
        uv.y = -uv.y;
    }
    if (abs(uv.y) > 1.0) {
        uv.y = sign(uv.y) * 2.0 - uv.y;
        uv.x = -uv.x;
    }
    return octDecode(uv);
}
