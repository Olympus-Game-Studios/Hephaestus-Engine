#pragma once

#include "MaterialGraph.h"
#include "../../Assets/CompiledMaterialShader.h"

#include <filesystem>
#include <string>
#include <vector>

namespace MaterialEditor {

struct MaterialShaderCompileResult {
    bool ok = false;
    std::vector<std::string> diagnostics;
    Hephaestus::CompiledMaterialShader shader;
};

/// False when the editor was built without glslang (HEPHAESTUS_MATERIAL_SHADER_COMPILER).
bool MaterialShaderCompilerAvailable();

/// The folder holding triangle.frag and its includes that the build copies to `shaders/source`; empty if not found.
std::filesystem::path FindMaterialShaderSourceDirectory();

/// Compiles the graph into triangle.frag's raster and ray-traced-shadow builds.
MaterialShaderCompileResult CompileMaterialGraph(const Graph& graph, const std::filesystem::path& shaderSourceDirectory);

} // namespace MaterialEditor
