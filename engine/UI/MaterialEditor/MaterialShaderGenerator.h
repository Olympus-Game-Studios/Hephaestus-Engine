#pragma once

#include "MaterialGraph.h"

#include <string>
#include <vector>

namespace MaterialEditor {

/// Textures a graph samples, read through MaterialUBO::graphTextureSlots (shaders/triangle.frag).
inline constexpr size_t kMaxGraphTextures = 16;

struct GeneratedSamplerBinding {
    std::string name;
    std::string resourcePath;
    bool linear = false; ///< sampled without sRGB decoding: typed data samples, or a texture node marked Linear
};

struct MaterialShaderGenerationResult {
    bool ok = true;
    std::string declarations;
    std::string body;
    std::vector<GeneratedSamplerBinding> samplers;
    std::vector<std::string> diagnostics;
};

/// Statements filling `surface` from the graph, run inside evaluateSurfaceCoverage (shaders/material_surface.glsl).
MaterialShaderGenerationResult GenerateMaterialShader(const Graph& graph);

/// material_graph.glsl for a compiled material: the generated body wrapped in the surface functions.
std::string BuildMaterialGraphSource(const MaterialShaderGenerationResult& generated);

} // namespace MaterialEditor
