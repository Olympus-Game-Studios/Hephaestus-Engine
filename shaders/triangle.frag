#version 460
#extension GL_GOOGLE_include_directive : require
#ifdef RAY_TRACED_SHADOWS
#extension GL_EXT_ray_query : require
#endif

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
    float shadingMode; // 0 = Lit, 1 = Unlit (Wireframe uses polygon mode)
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
    uint blendMode;           // offset 128: 0 opaque, 1 alpha blend, 2 additive
    uint paddingTail2;        // offset 132
} material;

layout(set = 0, binding = 3) uniform sampler2D materialTextures[MAX_TEXTURE_SLOTS];
layout(set = 0, binding = 5) uniform sampler2D environmentMap;

// Lighting data
#define LIGHTING_SET 0
#define LIGHTING_BINDING 2
#include "lighting_common.glsl"
#define GI_SET 2
#include "probe_common.glsl"

layout(set = 0, binding = 4) uniform sampler2D shadowMap;
layout(set = 0, binding = 6) uniform sampler2D localShadowAtlas;  // 8 x 4 tiles of 512 (EngineFrameRenderer::kLocalShadowTile)
#ifdef RAY_TRACED_SHADOWS
layout(set = GI_SET, binding = 11) uniform accelerationStructureEXT sceneStructure;  // engine/Gfx/ProbeGI.cpp kScene
#endif

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

// Rougher surfaces read a blurrier (smaller) mip of the environment.
vec3 sampleEnvironmentReflection(vec3 direction, float roughness) {
    vec3 dir = normalize(direction);
    vec2 uv;
    uv.x = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;
    uv.y = asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5;
    float lod = clamp(roughness, 0.0, 1.0) * float(textureQueryLevels(environmentMap) - 1);
    return textureLod(environmentMap, uv, lod).rgb;
}

// Point/spot shadow from one perspective tile, compared in linear depth so the bias is in metres.
float localShadow(int tile, vec3 lightPos, vec4 params) {
    const float TILE = 512.0;
    const float nearPlane = 0.05;  // kLocalShadowNear in engine/Gfx/EngineFrameRenderer.cpp
    float farPlane = params.z;
    vec3 geometricNormal = normalize(fragNormal);
    vec3 toLight = lightPos - fragWorldPos;
    float dist = length(toLight);
    float NdotL = clamp(dot(geometricNormal, toLight / max(dist, 1e-4)), 0.0, 1.0);
    float texelWorld = 2.0 * dist * params.w / TILE;
    vec3 shadowPos = fragWorldPos + geometricNormal * texelWorld * (0.5 + 1.5 * (1.0 - NdotL));
    vec4 clip = lighting.localShadowViewProj[tile] * vec4(shadowPos, 1.0);
    if (clip.w <= nearPlane) {
        return 1.0;
    }
    vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return 1.0;
    }
    vec2 tileOrigin = vec2(float(tile % 8), float(tile / 8));
    float receiver = clip.w - texelWorld * 1.5;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 local = clamp(uv + vec2(x, y) / TILE, vec2(0.5 / TILE), vec2(1.0 - 0.5 / TILE));
            float depth = texture(localShadowAtlas, (tileOrigin + local) / vec2(8.0, 4.0)).r;
            float blocker = nearPlane * farPlane / (farPlane - depth * (farPlane - nearPlane));
            lit += receiver > blocker ? 0.0 : 1.0;
        }
    }
    return mix(1.0, lit / 9.0, params.y);
}

#ifdef RAY_TRACED_SHADOWS
bool rayOccluded(vec3 origin, vec3 dir, float maxDistance) {
    rayQueryEXT query;
    rayQueryInitializeEXT(query, sceneStructure, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0xFF,
                          origin, 0.0, dir, maxDistance);
    while (rayQueryProceedEXT(query)) {}
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

vec3 rayOrigin() {
    return fragWorldPos + normalize(fragNormal) * 0.01;
}

// Fraction of the sun's disc hidden from this fragment, from 8 rays spread over its angular size.
float tracedSunOcclusion(vec3 toSun) {
    float tanHalfSun = lighting.shadowBiasWorld.z;
    vec3 origin = rayOrigin();
    if (tanHalfSun <= 0.0) {
        return rayOccluded(origin, toSun, 10000.0) ? 1.0 : 0.0;
    }
    const int RAYS = 8;
    vec3 tangent = normalize(cross(abs(toSun.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), toSun));
    vec3 bitangent = cross(toSun, tangent);
    float rotation = fract(sin(dot(fragWorldPos.xz, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    float hidden = 0.0;
    for (int i = 0; i < RAYS; ++i) {
        float radius = sqrt((float(i) + 0.5) / float(RAYS)) * tanHalfSun;
        float angle = rotation + float(i) * 2.39996323;  // golden angle spiral over the disc
        vec3 dir = normalize(toSun + (tangent * cos(angle) + bitangent * sin(angle)) * radius);
        hidden += rayOccluded(origin, dir, 10000.0) ? 1.0 : 0.0;
    }
    return hidden / float(RAYS);
}

float tracedLocalShadow(vec3 lightPos, float strength) {
    vec3 origin = rayOrigin();
    vec3 toLight = lightPos - origin;
    float dist = length(toLight);
    bool hidden = dist > 0.02 && rayOccluded(origin, toLight / dist, dist - 0.01);
    return hidden ? 1.0 - strength : 1.0;
}
#endif

float pointShadow(int light, vec3 lightPos) {
    vec4 params = lighting.pointShadowParams[light];
#ifdef RAY_TRACED_SHADOWS
    if (params.x < -1.5) {
        return tracedLocalShadow(lightPos, params.y);
    }
#endif
    if (params.x < 0.0) {
        return 1.0;
    }
    vec3 d = fragWorldPos - lightPos;
    vec3 a = abs(d);
    int face = a.x >= a.y && a.x >= a.z ? (d.x >= 0.0 ? 0 : 1)
             : a.y >= a.z ? (d.y >= 0.0 ? 2 : 3)
             : (d.z >= 0.0 ? 4 : 5);
    return localShadow(int(params.x) + face, lightPos, params);
}

float spotShadow(int light, vec3 lightPos) {
    vec4 params = lighting.spotShadowParams[light];
#ifdef RAY_TRACED_SHADOWS
    if (params.x < -1.5) {
        return tracedLocalShadow(lightPos, params.y);
    }
#endif
    return params.x < 0.0 ? 1.0 : localShadow(int(params.x), lightPos, params);
}

// Fraction of the 16 Poisson taps in shadow for one cascade.
float cascadeOcclusion(int cascade, vec3 lightDir) {
    // Biases are sized from this cascade's texel footprint, so they stay a few centimetres near the camera
    // instead of a fixed fraction of the cascade's whole depth range (which leaked light at contact points).
    vec3 geometricNormal = normalize(fragNormal);
    float NdotL = clamp(dot(geometricNormal, lightDir), 0.0, 1.0);
    float texelWorld = lighting.cascadeTexelWorld[cascade];
    float normalOffset = texelWorld * (0.5 + 1.5 * (1.0 - NdotL)) + lighting.shadowBiasWorld.y;
    vec3 shadowPos = fragWorldPos + geometricNormal * normalOffset;
    float bias = (texelWorld + lighting.shadowBiasWorld.x) / lighting.cascadeDepthRange[cascade];

    vec4 cascadeLightSpace = lighting.cascadeViewProj[cascade] * vec4(shadowPos, 1.0);
    vec3 projCoords = cascadeLightSpace.xyz / cascadeLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        any(lessThan(projCoords.xy, vec2(0.0))) || any(greaterThan(projCoords.xy, vec2(1.0)))) {
        return 0.0;
    }

    // Cascade 0: top-left, 1: top-right, 2: bottom-left, 3: bottom-right of the 2x2 atlas
    vec2 atlasOffset = vec2(float(cascade % 2), float(cascade / 2)) * 0.5;
    vec2 atlasUV = projCoords.xy * 0.5 + atlasOffset;
    float texelSize = lighting.dirShadowParams.z;
    // Keep filter taps inside this cascade's quadrant so they never read a neighbouring cascade.
    vec2 quadrantMin = atlasOffset + vec2(0.5 * texelSize);
    vec2 quadrantMax = atlasOffset + vec2(0.5 - 0.5 * texelSize);

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
    mat2 rotation = mat2(cos(rotAngle), sin(rotAngle), -sin(rotAngle), cos(rotAngle));

    // Soft shadows (PCSS): the penumbra grows with the distance from the blocker to the receiver
    // times the sun's angular size, so contact shadows stay sharp and long shadows soften.
    const float MAX_TEXELS = 24.0;
    const float BLOCKER_SEARCH_METRES = 4.0;  // occluders further than this from the receiver soften no more
    float depthRange = lighting.cascadeDepthRange[cascade];
    float tanHalfSun = lighting.shadowBiasWorld.z;
    float receiver = projCoords.z - bias;
    float searchTexels = clamp(tanHalfSun * BLOCKER_SEARCH_METRES / texelWorld, 1.5, MAX_TEXELS);
    float blockerSum = 0.0;
    float blockers = 0.0;
    for (int i = 0; i < 16; i++) {
        vec2 sampleCoord = clamp(atlasUV + rotation * poissonDisk[i] * searchTexels * texelSize, quadrantMin, quadrantMax);
        float depth = texture(shadowMap, sampleCoord).r;
        if (depth < receiver) {
            blockerSum += depth;
            blockers += 1.0;
        }
    }
    if (blockers == 0.0) {
        return 0.0;
    }
    float blockerDistance = (receiver - blockerSum / blockers) * depthRange;
    float filterTexels = clamp(blockerDistance * tanHalfSun / texelWorld, 1.5, MAX_TEXELS);

    float shadow = 0.0;
    for (int i = 0; i < 16; i++) {
        vec2 sampleCoord = clamp(atlasUV + rotation * poissonDisk[i] * filterTexels * texelSize, quadrantMin, quadrantMax);
        float closestDepth = texture(shadowMap, sampleCoord).r;
        shadow += receiver > closestDepth ? 1.0 : 0.0;
    }
    return shadow / 16.0;
}

float sampleDirectionalShadow(vec4 lightSpacePos, vec3 normal, vec3 lightDir) {
    if (lighting.dirShadowParams.w < 0.5) {
        return 1.0;
    }
#ifdef RAY_TRACED_SHADOWS
    if (lighting.shadowBiasWorld.w > 0.5) {
        return 1.0 - tracedSunOcclusion(lightDir) * lighting.dirShadowParams.y;
    }
#endif

    // Engine LookAt maps forward to +Z, so view-space Z is positive for objects in front
    float viewDepth = (ubo.view * vec4(fragWorldPos, 1.0)).z;
    vec4 splits = lighting.cascadeSplits;
    if (viewDepth >= splits.w) {
        return 1.0;
    }
    int cascade = viewDepth < splits.x ? 0 : viewDepth < splits.y ? 1 : viewDepth < splits.z ? 2 : 3;

    // The last 10% of each cascade cross-fades into the next one, and the last 10% of the final
    // cascade fades the shadow out, so neither the cascade seams nor the shadow distance show as a line.
    float cascadeStart = cascade == 0 ? 0.0 : splits[cascade - 1];
    float band = 0.1 * (splits[cascade] - cascadeStart);
    float t = clamp((viewDepth - (splits[cascade] - band)) / max(band, 1e-4), 0.0, 1.0);
    float occlusion = cascadeOcclusion(cascade, lightDir);
    if (t > 0.0) {
        occlusion = cascade < 3 ? mix(occlusion, cascadeOcclusion(cascade + 1, lightDir), t)
                                : occlusion * (1.0 - t);
    }

    return 1.0 - occlusion * lighting.dirShadowParams.y;
}

// ═══════════════════════════════════════════════════════════════
// MAIN LIGHTING CALCULATION
// ═══════════════════════════════════════════════════════════════

// Applies a tangent-space normal map. Meshes carry no tangents, so the frame comes from screen-space derivatives.
vec3 perturbNormal(vec3 n, vec3 worldPos, vec2 uv, vec3 tangentNormal) {
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float scale = max(dot(t, t), dot(b, b));
    if (scale < 1e-20) {
        return n;
    }
    mat3 tbn = mat3(t * inversesqrt(scale), b * inversesqrt(scale), n);
    return normalize(tbn * tangentNormal);
}

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
    float textureAlpha = 1.0;
    if (material.useAlbedoTexture != 0u) {
        uint index = material.albedoTextureIndex % MAX_TEXTURE_SLOTS;
        vec4 texColor = texture(materialTextures[index], texCoord);
        albedo = texColor.rgb * material.albedo * fragColor;
        textureAlpha = texColor.a;
    }

    float opacity = clamp(material.opacity, 0.0, 1.0);
    if (material.blendMode != 0u) {
        opacity *= textureAlpha;
        if (opacity <= 0.002) {
            discard;
        }
    } else if (opacity < clamp(material.opacityMask, 0.0, 1.0)) {
        discard;
    }
    if (material.blendMode == 2u) {
        outColor = vec4((albedo + material.emissive * material.emissiveStrength) * opacity, opacity);
        return;
    }

    // Unlit viewport mode: albedo (+ emissive), skip lighting
    if (ubo.shadingMode > 0.5) {
        vec3 unlit = albedo + material.emissive * material.emissiveStrength;
        outColor = vec4(unlit, opacity);
        return;
    }
    
    if (material.useNormalTexture != 0u) {
        uint index = material.normalTextureIndex % MAX_TEXTURE_SLOTS;
        vec3 tangentNormal = texture(materialTextures[index], texCoord).rgb * 2.0 - 1.0;
        N = perturbNormal(N, fragWorldPos, texCoord, tangentNormal);
    }
    
    // Roughness texture
    float roughness = material.roughness;
    if (material.useRoughnessTexture != 0u) {
        uint index = material.roughnessTextureIndex % MAX_TEXTURE_SLOTS;
        // Roughness is typically in the R or G channel
        float texRoughness = texture(materialTextures[index], texCoord).r;
        roughness = texRoughness * material.roughness; // Blend with base value
    }
    roughness = clamp(roughness, 0.04, 1.0); // a multiplier above 1 can roughen a map; 0.04 avoids division by zero
    
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
        vec3 radiance = lightColor * intensity * atten * pointShadow(i, lightPos);
        
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
        vec3 radiance = lightColor * intensity * atten * spotFalloff * spotShadow(i, lightPos);
        
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
    if (lighting.environmentReflectionParams.z > 0.5) {
        vec3 irradiance = evaluateSkyIrradiance(N) * lighting.environmentReflectionParams.w;
        ambient = irradiance / PI * albedo * (1.0 - metallic) * ao;
    }
    if (gi.probeCounts.w == 1) {
        // Fade from the probes to the sky/flat ambient over one probe spacing outside the volume.
        vec3 halfExtent = 0.5 * gi.probeSpacing.xyz * vec3(gi.probeCounts.xyz - 1);
        vec3 center = gi.firstProbe.xyz + halfExtent;
        float spacing = max(max(gi.probeSpacing.x, gi.probeSpacing.y), max(gi.probeSpacing.z, 0.5));
        vec3 outside = max(abs(fragWorldPos - center) - halfExtent, vec3(0.0));
        float weight = clamp(1.0 - length(outside) / spacing, 0.0, 1.0);
        if (weight > 0.0) {
            vec3 probeLight = sampleProbeIrradiance(fragWorldPos, N, V) * gi.firstProbe.w;
            ambient = mix(ambient, probeLight * albedo * (1.0 - metallic) * ao, weight);
        }
    }
    vec3 color = ambient + Lo;

    // ── Environment Reflections ──────────────────────────────
    // Basic IBL foundation: reflect the active HDRI only. Nearby objects are not reflected yet.
    if (lighting.environmentReflectionParams.y > 0.5 && material.reflectionStrength > 0.0) {
        vec3 R = reflect(-V, N);
        vec3 envReflection = sampleEnvironmentReflection(R, roughness);
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

    outColor = vec4(color, opacity);
}
