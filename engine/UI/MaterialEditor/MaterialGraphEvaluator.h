#pragma once

#include "MaterialGraph.h"
#include "../../Scene/Components.h"

#include <string>

namespace MaterialEditor {

bool EvaluateGraph(const Graph& graph, MaterialComponent& outMaterial, std::string& outErrorMessage);

/// Names of the rendered fields that differ between two materials, space separated; empty when they render the same.
std::string DescribeRenderedDifferences(const MaterialComponent& expected, const MaterialComponent& actual);

} // namespace MaterialEditor
