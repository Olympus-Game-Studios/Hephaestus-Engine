// Material sampling helpers shared by triangle.frag and shadowmap_masked.frag.

// What a material gives the lighting in triangle.frag; emissive is already scaled by its strength.
struct Surface {
    vec2 texCoord;
    vec3 albedo;
    float opacity;
    float textureAlpha;
    float clipThreshold;
    vec3 normal;
    float roughness;
    float metallic;
    float ao;
    vec3 emissive;
    float specular;
};

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
