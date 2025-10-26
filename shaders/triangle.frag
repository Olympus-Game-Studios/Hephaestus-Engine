#version 450

#define MAX_TEXTURE_SLOTS 128
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    float padding1;
} ubo;

// Material properties (set via push constants or additional descriptor sets)
layout(std140, set = 0, binding = 1) uniform MaterialUBO {
    vec3 albedo;
    float metallic;
    vec3 emissive;
    float roughness;
    float ao;
    float emissiveStrength;
    uint useAlbedoTexture;
    uint albedoTextureIndex;
} material;

layout(set = 0, binding = 3) uniform sampler2D materialTextures[MAX_TEXTURE_SLOTS];

// Lighting data
layout(std140, set = 0, binding = 2) uniform LightingUBO {
    // Directional light (sun)
    vec3 dirLightDir;
    float dirLightIntensity;
    vec3 dirLightColor;
    float padding4;
    
    // Ambient lighting
    vec3 ambientColor;
    float ambientIntensity;
    
    // Point lights (up to 4)
    vec4 pointLightPositions[4];  // w = range
    vec4 pointLightColors[4];     // w = intensity
    vec4 pointLightAttenuation[4]; // xyz = constant/linear/quadratic, w = unused
    int pointLightCount;
    float padding5;
    float padding6;
    float padding7;
} lighting;

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

// ═══════════════════════════════════════════════════════════════
// MAIN LIGHTING CALCULATION
// ═══════════════════════════════════════════════════════════════

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.viewPos - fragWorldPos);
    
    // Material properties
    vec3 albedo = material.albedo * fragColor;
    
    if (material.useAlbedoTexture != 0u) {
        uint index = material.albedoTextureIndex % MAX_TEXTURE_SLOTS;
        vec4 texColor = texture(materialTextures[index], fragTexCoord);
        albedo = texColor.rgb * fragColor;
    }
    
    float metallic = material.metallic;
    float roughness = max(material.roughness, 0.04); // Prevent division by zero
    float ao = material.ao;
    
    // Calculate F0 (base reflectivity)
    // Dielectrics have F0 around 0.04, metals use albedo as F0
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Reflectance equation
    vec3 Lo = vec3(0.0);
    
    // ── Directional Light ────────────────────────────────────
    if (lighting.dirLightIntensity > 0.0) {
        vec3 L = normalize(-lighting.dirLightDir);
        vec3 H = normalize(V + L);
        vec3 radiance = lighting.dirLightColor * lighting.dirLightIntensity;
        
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
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }
    
    // ── Point Lights ─────────────────────────────────────────
    for (int i = 0; i < lighting.pointLightCount && i < 4; ++i) {
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
    
    // ── Ambient Lighting ─────────────────────────────────────
    vec3 ambient = lighting.ambientColor * lighting.ambientIntensity * albedo * ao;
    vec3 color = ambient + Lo;
    
    // Add emissive
    color += material.emissive * material.emissiveStrength;
    
    // HDR tonemapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}
