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

// World-aligned UVs: metres on the surface's own plane, anchored to the world so textures line up across faces.
// Mostly horizontal faces use X and Z; others run U along the horizontal line of the face and V up its slope.
vec2 getWorldAlignedUV(vec3 worldPos, vec3 normal, float scale) {
    vec3 n = normalize(normal);
    if (abs(n.y) > 0.7) {
        return worldPos.xz * scale;
    }
    vec3 tangent = normalize(vec3(n.z, 0.0, -n.x));
    // Faces turned either way along an axis share one U direction, so a wall's two sides match.
    if (tangent.x + tangent.z < 0.0) {
        tangent = -tangent;
    }
    vec3 bitangent = normalize(cross(n, tangent));
    if (bitangent.y < 0.0) {
        bitangent = -bitangent;
    }
    return vec2(dot(worldPos, tangent), dot(worldPos, bitangent)) * scale;
}
