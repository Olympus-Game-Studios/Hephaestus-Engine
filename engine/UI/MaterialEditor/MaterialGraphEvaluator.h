#pragma once

#include "MaterialGraph.h"
#include "../../Scene/Components.h"

#include <string>

namespace MaterialEditor {

bool EvaluateGraph(const Graph& graph, MaterialComponent& outMaterial, std::string& outErrorMessage);

/// True when evaluating the graph into the fixed material fields would change how it looks, so it must be drawn
/// with its own compiled shader (MaterialShaderCompiler.h).
bool GraphNeedsCompiledShader(const Graph& graph);

/// Names of the rendered fields that differ between two materials, space separated; empty when they render the same.
std::string DescribeRenderedDifferences(const MaterialComponent& expected, const MaterialComponent& actual);

} // namespace MaterialEditor
