// Lighting UBO and light helpers shared by triangle.frag and probe_update.comp.
// Define LIGHTING_SET and LIGHTING_BINDING before including; layout must match EngineFrameRenderer.cpp:LightingUBO.

layout(std140, set = LIGHTING_SET, binding = LIGHTING_BINDING) uniform LightingUBO {
    mat4 dirLightViewProj;
    vec4 dirShadowParams;          // x = bias, y = strength, z = texel size, w = enabled
    vec4 dirLightDirIntensity;      // xyz = direction, w = intensity
    vec4 dirLightColor;             // rgb = color, w = unused
    vec4 ambientColorIntensity;     // rgb = ambient color, w = ambient intensity
    vec4 environmentReflectionParams; // x = intensity, y = enabled, z = sky irradiance enabled, w = sky intensity
    vec4 pointLightPositions[16];
    vec4 pointLightColors[16];
    vec4 pointLightAttenuation[16];
    vec4 spotLightPositions[16];
    vec4 spotLightDirections[16];
    vec4 spotLightColors[16];
    vec4 spotLightAttenuation[16];
    int pointLightCount;
    int spotLightCount;
    int cascadeCount;
    float csmPadding1;
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;
    vec4 skyIrradianceSH[9]; // see engine/Gfx/SkyIrradiance.cpp
} lighting;

// Calculate light attenuation for point lights
float calculateAttenuation(float distance, vec3 attenuation) {
    return 1.0 / (attenuation.x + attenuation.y * distance + attenuation.z * distance * distance);
}

// Calculate spotlight cone falloff (smooth transition between inner and outer cone)
float calculateSpotlightFalloff(vec3 lightDir, vec3 lightToFrag, float innerAngle, float outerAngle) {
    // Calculate cosine of angle between light direction and direction to fragment
    float cosTheta = dot(normalize(lightDir), normalize(lightToFrag));
    
    // Cone angles are stored as full cone aperture, so we need half-angles for comparison
    float halfInner = innerAngle * 0.5;
    float halfOuter = outerAngle * 0.5;
    
    // Pre-calculate cosines of cone half-angles for comparison
    float cosOuter = cos(halfOuter);
    float cosInner = cos(halfInner);
    
    // If outside outer cone, no light
    // Note: cosine decreases as angle increases, so we check if cosTheta < cosOuter
    if (cosTheta < cosOuter) {
        return 0.0;
    }
    
    // If inside inner cone, full intensity
    if (cosTheta >= cosInner) {
        return 1.0;
    }
    
    // Smooth falloff between inner and outer cone
    // Map from [cosOuter, cosInner] to [0, 1]
    float falloff = (cosTheta - cosOuter) / (cosInner - cosOuter);
    return smoothstep(0.0, 1.0, falloff);
}

vec3 evaluateSkyIrradiance(vec3 n) {
    vec3 e = lighting.skyIrradianceSH[0].rgb * 0.282095;
    e += lighting.skyIrradianceSH[1].rgb * (0.488603 * n.y);
    e += lighting.skyIrradianceSH[2].rgb * (0.488603 * n.z);
    e += lighting.skyIrradianceSH[3].rgb * (0.488603 * n.x);
    e += lighting.skyIrradianceSH[4].rgb * (1.092548 * n.x * n.y);
    e += lighting.skyIrradianceSH[5].rgb * (1.092548 * n.y * n.z);
    e += lighting.skyIrradianceSH[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0));
    e += lighting.skyIrradianceSH[7].rgb * (1.092548 * n.x * n.z);
    e += lighting.skyIrradianceSH[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
    return max(e, vec3(0.0));
}
