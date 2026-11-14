// Default material surface; a compiled material graph replaces this file with the same functions (MaterialShaderCompiler.cpp).

Surface evaluateSurfaceCoverage(vec3 N) {
    Surface surface;
    surface.texCoord = fragTexCoord;
    if (material.useWorldAlignedTexture != 0u) {
        surface.texCoord = getWorldAlignedUV(fragWorldPos, N, material.worldTextureScale);
    }
    surface.albedo = material.albedo * fragColor;
    surface.textureAlpha = 1.0;
    if (material.useAlbedoTexture != 0u) {
        uint index = material.albedoTextureIndex % MAX_TEXTURE_SLOTS;
        vec4 texColor = texture(materialTextures[index], surface.texCoord);
        surface.albedo = texColor.rgb * material.albedo * fragColor;
        surface.textureAlpha = texColor.a;
    }
    surface.opacity = material.opacity;
    surface.clipThreshold = material.opacityMask;
    surface.emissive = material.emissive * material.emissiveStrength;
    surface.normal = N;
    surface.roughness = material.roughness;
    surface.metallic = material.metallic;
    surface.ao = material.ao;
    surface.specular = material.specular;
    return surface;
}

void evaluateSurfaceShading(inout Surface surface) {
    vec2 texCoord = surface.texCoord;
    if (material.useNormalTexture != 0u) {
        uint index = material.normalTextureIndex % MAX_TEXTURE_SLOTS;
        vec3 tangentNormal = texture(materialTextures[index], texCoord).rgb * 2.0 - 1.0;
        surface.normal = perturbNormal(surface.normal, fragWorldPos, texCoord, tangentNormal);
    }
    if (material.useRoughnessTexture != 0u) {
        uint index = material.roughnessTextureIndex % MAX_TEXTURE_SLOTS;
        surface.roughness *= texture(materialTextures[index], texCoord)[material.textureChannels & 3u];
    }
    if (material.useAoTexture != 0u) {
        uint index = material.aoTextureIndex % MAX_TEXTURE_SLOTS;
        surface.ao *= texture(materialTextures[index], texCoord)[(material.textureChannels >> 4) & 3u];
    }
    if (material.useMetallicTexture != 0u) {
        uint index = material.metallicTextureIndex % MAX_TEXTURE_SLOTS;
        surface.metallic *= texture(materialTextures[index], texCoord)[(material.textureChannels >> 2) & 3u];
    }
}
