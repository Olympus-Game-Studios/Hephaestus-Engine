#version 450

#define MAX_TEXTURE_SLOTS 128
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in vec4 fragLightSpacePos;

layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    float padding1;
} ubo;

// Material properties (set via push constants or additional descriptor sets)
layout(std140, set = 0, binding = 1) uniform MaterialUBO {
    vec3 albedo;              // offset 0, size 12 (padded to 16 in std140)
    float albedoPadding;      // offset 12, explicit padding
    float metallic;           // offset 16
    float metallicPadding0;   // offset 20
    float metallicPadding1;   // offset 24
    float metallicPadding2;   // offset 28
    vec3 emissive;            // offset 32, size 12 (padded to 16 in std140)
    float emissivePadding;    // offset 44, explicit padding
    float roughness;          // offset 48
    float ao;                 // offset 52
    float emissiveStrength;   // offset 56
    uint useAlbedoTexture;    // offset 60
    uint albedoTextureIndex;  // offset 64
    uint useNormalTexture;    // offset 68
    uint normalTextureIndex;  // offset 72
    uint useRoughnessTexture; // offset 76
    uint roughnessTextureIndex; // offset 80
    uint useMetallicTexture;  // offset 84
    uint metallicTextureIndex; // offset 88
    uint useAoTexture;        // offset 92
    uint aoTextureIndex;      // offset 96
    uint useWorldAlignedTexture; // offset 100
    float worldTextureScale;  // offset 104
    float reflectionStrength; // offset 108
    float specular;           // offset 112
    float opacity;            // offset 116
    float opacityMask;        // offset 120
    float refraction;         // offset 124 (reserved)
    uint paddingTail1;        // offset 128
    uint paddingTail2;        // offset 132
} material;

layout(set = 0, binding = 3) uniform sampler2D materialTextures[MAX_TEXTURE_SLOTS];
layout(set = 0, binding = 5) uniform sampler2D environmentMap;

// Lighting data
layout(std140, set = 0, binding = 2) uniform LightingUBO {
    mat4 dirLightViewProj;
    vec4 dirShadowParams;          // x = bias, y = strength, z = texel size, w = enabled
    vec4 dirLightDirIntensity;      // xyz = direction, w = intensity
    vec4 dirLightColor;             // rgb = color, w = unused
    vec4 ambientColorIntensity;     // rgb = ambient color, w = ambient intensity
    vec4 environmentReflectionParams; // x = intensity, y = enabled, zw = unused
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
} lighting;

layout(set = 0, binding = 4) uniform sampler2D shadowMap;

const float PI = 3.14159265359;

// ═══════════════════════════════════════════════════════════════
// PBR FUNCTIONS
// ═══════════════════════════════════════════════════════════════

// Normal Distribution Function (GGX/Trowbridge-Reitz)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return nom / denom;
}

// Geometry Function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return nom / denom;
}

// Smith's method for geometry obstruction
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

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

vec3 sampleEnvironmentReflection(vec3 direction) {
    vec3 dir = normalize(direction);
    vec2 uv;
    uv.x = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;
    uv.y = asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5;
    return texture(environmentMap, uv).rgb;
}

float sampleDirectionalShadow(vec4 lightSpacePos, vec3 normal, vec3 lightDir) {
    if (lighting.dirShadowParams.w < 0.5) {
        return 1.0;
    }

    // Compute view-space depth of this fragment for cascade selection
    // Engine LookAt maps forward to +Z, so view-space Z is positive for objects in front
    float viewDepth = (ubo.view * vec4(fragWorldPos, 1.0)).z;
    
    // Select cascade based on view depth vs split distances
    int cascade = 0;
    if      (viewDepth < lighting.cascadeSplits.x) cascade = 0;
    else if (viewDepth < lighting.cascadeSplits.y) cascade = 1;
    else if (viewDepth < lighting.cascadeSplits.z) cascade = 2;
    else                                           cascade = 3;
    
    // Transform world position by this cascade's VP matrix
    vec4 cascadeLightSpace = lighting.cascadeViewProj[cascade] * vec4(fragWorldPos, 1.0);
    vec3 projCoords = cascadeLightSpace.xyz / cascadeLightSpace.w;
    
    // Transform from NDC [-1,1] to texture coordinates [0,1]
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
    // Fragment is outside the shadow map frustum - no shadow
    if (projCoords.z > 1.0 || projCoords.z < 0.0) {
        return 1.0;
    }
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0;
    }
    
    // Map UV to the correct 2x2 atlas quadrant
    // Cascade 0: top-left, 1: top-right, 2: bottom-left, 3: bottom-right
    vec2 atlasOffset;
    if      (cascade == 0) atlasOffset = vec2(0.0, 0.0);
    else if (cascade == 1) atlasOffset = vec2(0.5, 0.0);
    else if (cascade == 2) atlasOffset = vec2(0.0, 0.5);
    else                   atlasOffset = vec2(0.5, 0.5);
    
    vec2 atlasUV = projCoords.xy * 0.5 + atlasOffset;

    // Calculate slope-scaled bias — scale with cascade index for stability
    float baseBias = lighting.dirShadowParams.x;
    float NdotL = max(dot(normal, lightDir), 0.0);
    float slopeBias = baseBias * tan(acos(clamp(NdotL, 0.001, 1.0)));
    slopeBias = clamp(slopeBias, 0.0, baseBias * 5.0);
    float cascadeBiasScale = 1.0 + float(cascade) * 0.75;
    float bias = (baseBias + slopeBias) * cascadeBiasScale;
    
    // Texel size in full-atlas UV space (1 / full resolution)
    float texelSize = lighting.dirShadowParams.z;

    // 16-sample Poisson disk for smooth soft shadows
    const vec2 poissonDisk[16] = vec2[](
        vec2(-0.9420, -0.3990), vec2( 0.9456, -0.7689),
        vec2(-0.0942, -0.9293), vec2( 0.3448,  0.2930),
        vec2(-0.9159,  0.4572), vec2(-0.8154, -0.8790),
        vec2(-0.3826,  0.2762), vec2( 0.9744,  0.7564),
        vec2( 0.4430, -0.1675), vec2( 0.1600,  0.8520),
        vec2(-0.5268, -0.6275), vec2( 0.7936,  0.1592),
        vec2(-0.2415,  0.9525), vec2( 0.5375, -0.8607),
        vec2(-0.6168,  0.7338), vec2( 0.1120, -0.3690)
    );

    // Per-fragment rotation angle from world position to break up banding
    float rotAngle = fract(sin(dot(fragWorldPos.xz, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    float cosA = cos(rotAngle);
    float sinA = sin(rotAngle);

    // Spread radius in atlas texels — wider for far cascades
    float spreadTexels = 2.5 + float(cascade) * 1.0;

    float shadow = 0.0;
    for (int i = 0; i < 16; i++) {
        // Rotate sample around origin
        vec2 offset = vec2(
            poissonDisk[i].x * cosA - poissonDisk[i].y * sinA,
            poissonDisk[i].x * sinA + poissonDisk[i].y * cosA
        );
        vec2 sampleCoord = atlasUV + offset * spreadTexels * texelSize;
        float closestDepth = texture(shadowMap, sampleCoord).r;
        shadow += (projCoords.z - bias > closestDepth) ? 1.0 : 0.0;
    }
    shadow /= 16.0;

    float strength = lighting.dirShadowParams.y;
    return 1.0 - shadow * strength;
}

// ═══════════════════════════════════════════════════════════════
// MAIN LIGHTING CALCULATION
// ═══════════════════════════════════════════════════════════════

// Calculate world-aligned UV coordinates using triplanar projection
vec2 getWorldAlignedUV(vec3 worldPos, vec3 normal, float scale) {
    // Use triplanar mapping - blend between XY, XZ, and YZ planes based on normal
    vec3 blendWeights = abs(normal);
    blendWeights = blendWeights / (blendWeights.x + blendWeights.y + blendWeights.z);
    
    // Sample coordinates for each plane
    vec2 uvXY = worldPos.xy * scale;
    vec2 uvXZ = worldPos.xz * scale;
    vec2 uvYZ = worldPos.yz * scale;
    
    // For mostly horizontal surfaces (floors/ceilings), use XZ plane
    // For vertical surfaces, blend between XY and YZ based on normal direction
    // This gives the most intuitive result for ground textures
    if (blendWeights.y > 0.7) {
        return uvXZ;  // Mostly horizontal - use top-down projection
    }
    // Blend between the three projections for angled surfaces
    return uvXY * blendWeights.z + uvXZ * blendWeights.y + uvYZ * blendWeights.x;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.viewPos - fragWorldPos);
    
    // Calculate texture coordinates - either from UV or world position
    vec2 texCoord = fragTexCoord;
    if (material.useWorldAlignedTexture != 0u) {
        texCoord = getWorldAlignedUV(fragWorldPos, N, material.worldTextureScale);
    }
    
    // ── Material property sampling ─────────────────────────────────
    // Albedo (diffuse) texture
    vec3 albedo = material.albedo * fragColor;
    if (material.useAlbedoTexture != 0u) {
        uint index = material.albedoTextureIndex % MAX_TEXTURE_SLOTS;
        vec4 texColor = texture(materialTextures[index], texCoord);
        albedo = texColor.rgb * material.albedo * fragColor;
    }
    
    // Normal map (world space - for now, not using tangent space)
    // A better implementation would transform from tangent space
    vec3 normalMapped = N;
    if (material.useNormalTexture != 0u) {
        uint index = material.normalTextureIndex % MAX_TEXTURE_SLOTS;
        vec3 normalTex = texture(materialTextures[index], texCoord).rgb;
        // Unpack normal from [0,1] to [-1,1]
        normalMapped = normalize(normalTex * 2.0 - 1.0);
        // For a world-space approximation, blend with original normal
        // This is a simplified approach; full implementation would need tangent space
        normalMapped = normalize(mix(N, normalMapped, 0.7));
    }
    N = normalMapped;
    
    // Roughness texture
    float roughness = material.roughness;
    if (material.useRoughnessTexture != 0u) {
        uint index = material.roughnessTextureIndex % MAX_TEXTURE_SLOTS;
        // Roughness is typically in the R or G channel
        float texRoughness = texture(materialTextures[index], texCoord).r;
        roughness = texRoughness * material.roughness; // Blend with base value
    }
    roughness = max(roughness, 0.04); // Prevent division by zero
    
    // Ambient occlusion texture
    float ao = material.ao;
    if (material.useAoTexture != 0u) {
        uint index = material.aoTextureIndex % MAX_TEXTURE_SLOTS;
        // AO is typically stored in R or G channel
        float texAo = texture(materialTextures[index], texCoord).r;
        ao = texAo * material.ao; // Blend with base value
    }
    
    // Metallic texture
    float metallic = material.metallic;
    if (material.useMetallicTexture != 0u) {
        uint index = material.metallicTextureIndex % MAX_TEXTURE_SLOTS;
        // Metallic is typically in the B channel of ORM maps or R channel of standalone
        float texMetallic = texture(materialTextures[index], texCoord).r;
        metallic = texMetallic * material.metallic; // Blend with base value
    }
    metallic = clamp(metallic, 0.0, 1.0);
    float opacity = clamp(material.opacity, 0.0, 1.0);
    if (opacity < clamp(material.opacityMask, 0.0, 1.0)) {
        discard;
    }
    
    // Calculate F0 (base reflectivity)
    // Dielectrics have F0 around 0.04, metals use albedo as F0
    vec3 F0 = vec3(0.08 * clamp(material.specular, 0.0, 1.0));
    F0 = mix(F0, albedo, metallic);
    
    // Reflectance equation
    vec3 Lo = vec3(0.0);
    
    // ── Directional Light ────────────────────────────────────
    if (lighting.dirLightDirIntensity.w > 0.0) {
        vec3 L = normalize(-lighting.dirLightDirIntensity.xyz);
        vec3 H = normalize(V + L);
        vec3 radiance = lighting.dirLightColor.rgb * lighting.dirLightDirIntensity.w;
        float shadowFactor = sampleDirectionalShadow(fragLightSpacePos, N, L);

        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        
        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;
        
        // Energy conservation
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;
        
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL * shadowFactor;
    }
    
    // ── Point Lights ─────────────────────────────────────────
    for (int i = 0; i < lighting.pointLightCount && i < 16; ++i) {
        vec3 lightPos = lighting.pointLightPositions[i].xyz;
        float range = lighting.pointLightPositions[i].w;
        vec3 lightColor = lighting.pointLightColors[i].rgb;
        float intensity = lighting.pointLightColors[i].w;
        vec3 attenuation = lighting.pointLightAttenuation[i].xyz;
        
        vec3 L = lightPos - fragWorldPos;
        float distance = length(L);
        
        // Skip if out of range
        if (distance > range) continue;
        
        L = L / distance; // Normalize
        vec3 H = normalize(V + L);
        
        float atten = calculateAttenuation(distance, attenuation);
        vec3 radiance = lightColor * intensity * atten;
        
        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        
        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;
        
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;
        
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }
    
    // ── Spot Lights ──────────────────────────────────────────
    for (int i = 0; i < lighting.spotLightCount && i < 16; ++i) {
        vec3 lightPos = lighting.spotLightPositions[i].xyz;
        float range = lighting.spotLightPositions[i].w;
        vec3 lightDir = lighting.spotLightDirections[i].xyz;
        float innerAngle = lighting.spotLightDirections[i].w;
        vec3 lightColor = lighting.spotLightColors[i].rgb;
        float intensity = lighting.spotLightColors[i].w;
        vec3 attenuation = lighting.spotLightAttenuation[i].xyz;
        float outerAngle = lighting.spotLightAttenuation[i].w;
        
        vec3 L = lightPos - fragWorldPos;
        float distance = length(L);
        
        // Skip if out of range
        if (distance > range) continue;
        
        L = L / distance; // Normalize (points from fragment to light)
        
        // Calculate spotlight cone falloff (need direction from light to fragment)
        vec3 lightToFrag = -L; // Invert to get direction from light to fragment
        float spotFalloff = calculateSpotlightFalloff(lightDir, lightToFrag, innerAngle, outerAngle);
        if (spotFalloff <= 0.0) continue;
        
        vec3 H = normalize(V + L);
        
        float atten = calculateAttenuation(distance, attenuation);
        vec3 radiance = lightColor * intensity * atten * spotFalloff;
        
        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        
        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;
        
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;
        
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }
    
    // ── Ambient Lighting ─────────────────────────────────────
    vec3 ambient = lighting.ambientColorIntensity.rgb * lighting.ambientColorIntensity.w * albedo * ao;
    vec3 color = ambient + Lo;

    // ── Environment Reflections ──────────────────────────────
    // Basic IBL foundation: reflect the active HDRI only. Nearby objects are not reflected yet.
    if (lighting.environmentReflectionParams.y > 0.5 && material.reflectionStrength > 0.0) {
        vec3 R = reflect(-V, N);
        vec3 envReflection = sampleEnvironmentReflection(R);
        float roughnessFactor = 1.0 - clamp(roughness, 0.0, 1.0);
        roughnessFactor *= roughnessFactor;
        vec3 reflectionFresnel = fresnelSchlick(max(dot(N, V), 0.0), F0);
        vec3 environmentSpecular = envReflection * reflectionFresnel * roughnessFactor *
                                   lighting.environmentReflectionParams.x *
                                   material.reflectionStrength * ao;
        color += environmentSpecular;
    }
    
    // Add emissive
    color += material.emissive * material.emissiveStrength;
    
    // HDR tonemapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, opacity);
}
