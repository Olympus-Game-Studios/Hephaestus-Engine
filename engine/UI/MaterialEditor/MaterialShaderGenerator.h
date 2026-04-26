#pragma once

#include "MaterialGraph.h"

#include <string>
#include <vector>

namespace MaterialEditor {

struct GeneratedSamplerBinding {
    std::string name;
    std::string resourcePath;
};

struct GeneratedUniform {
    std::string name;
    ValueType type = ValueType::Float1;
};

struct MaterialShaderGenerationResult {
    bool ok = true;
    std::string declarations;
    std::string body;
    std::vector<GeneratedSamplerBinding> samplers;
    std::vector<GeneratedUniform> uniforms;
    std::vector<std::string> diagnostics;
};

MaterialShaderGenerationResult GenerateMaterialShader(const Graph& graph);

} // namespace MaterialEditor
