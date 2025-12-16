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
    uint useAoTexture;        // offset 84
    uint aoTextureIndex;      // offset 88
    uint paddingTail0;        // offset 92
    uint paddingTail1;        // offset 96
    uint paddingTail2;        // offset 100
    uint paddingTail3;        // offset 104
    uint paddingTail4;        // offset 108
    uint paddingTail5;        // offset 112
} material;

layout(set = 0, binding = 3) uniform sampler2D materialTextures[MAX_TEXTURE_SLOTS];

// Lighting data
layout(std140, set = 0, binding = 2) uniform LightingUBO {
    mat4 dirLightViewProj;
    vec4 dirShadowParams;          // x = bias, y = strength, z = texel size, w = enabled
    vec4 dirLightDirIntensity;      // xyz = direction, w = intensity
    vec4 dirLightColor;             // rgb = color, w = unused
    vec4 ambientColorIntensity;     // rgb = ambient color, w = ambient intensity
    vec4 pointLightPositions[16];
    vec4 pointLightColors[16];
    vec4 pointLightAttenuation[16];
    vec4 spotLightPositions[16];
    vec4 spotLightDirections[16];
    vec4 spotLightColors[16];
    vec4 spotLightAttenuation[16];
    int pointLightCount;
    int spotLightCount;
    float padding5;
    float padding6;
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

float sampleDirectionalShadow(vec4 lightSpacePos, vec3 normal, vec3 lightDir) {
    if (lighting.dirShadowParams.w < 0.5) {
        return 1.0;
    }

    float invW = 1.0 / max(lightSpacePos.w, 0.00001);
    vec3 projCoords = lightSpacePos.xyz * invW;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.z > 1.0) {
        return 1.0;
    }

    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0;
    }

    float bias = lighting.dirShadowParams.x * (1.0 - max(dot(normal, lightDir), 0.0));
    bias = max(bias, 0.00025);
    float texelSize = lighting.dirShadowParams.z;

    float shadow = 0.0;
    float samples = 0.0;
    const int pcfRadius = 1;
    for (int x = -pcfRadius; x <= pcfRadius; ++x) {
        for (int y = -pcfRadius; y <= pcfRadius; ++y) {
            vec2 offset = projCoords.xy + vec2(x, y) * texelSize;
            if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0) {
                continue;
            }
            float closestDepth = texture(shadowMap, offset).r;
            shadow += projCoords.z - bias > closestDepth ? 1.0 : 0.0;
            samples += 1.0;
        }
    }

    if (samples > 0.0) {
        shadow /= samples;
    }

    float strength = lighting.dirShadowParams.y;
    return 1.0 - shadow * strength;
}

// ═══════════════════════════════════════════════════════════════
// MAIN LIGHTING CALCULATION
// ═══════════════════════════════════════════════════════════════

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.viewPos - fragWorldPos);
    
    // ── Material property sampling ─────────────────────────────────
    // Albedo (diffuse) texture
    vec3 albedo = material.albedo * fragColor;
    if (material.useAlbedoTexture != 0u) {
        uint index = material.albedoTextureIndex % MAX_TEXTURE_SLOTS;
        vec4 texColor = texture(materialTextures[index], fragTexCoord);
        albedo = texColor.rgb * material.albedo * fragColor;
    }
    
    // Normal map (world space - for now, not using tangent space)
    // A better implementation would transform from tangent space
    vec3 normalMapped = N;
    if (material.useNormalTexture != 0u) {
        uint index = material.normalTextureIndex % MAX_TEXTURE_SLOTS;
        vec3 normalTex = texture(materialTextures[index], fragTexCoord).rgb;
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
        float texRoughness = texture(materialTextures[index], fragTexCoord).r;
        roughness = texRoughness * material.roughness; // Blend with base value
    }
    roughness = max(roughness, 0.04); // Prevent division by zero
    
    // Ambient occlusion texture
    float ao = material.ao;
    if (material.useAoTexture != 0u) {
        uint index = material.aoTextureIndex % MAX_TEXTURE_SLOTS;
        // AO is typically stored in R or G channel
        float texAo = texture(materialTextures[index], fragTexCoord).r;
        ao = texAo * material.ao; // Blend with base value
    }
    
    float metallic = material.metallic;
    
    // Calculate F0 (base reflectivity)
    // Dielectrics have F0 around 0.04, metals use albedo as F0
    vec3 F0 = vec3(0.04);
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
    
    // Add emissive
    color += material.emissive * material.emissiveStrength;
    
    // HDR tonemapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}
