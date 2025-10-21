#pragma once

#include "MaterialGraph.h"
#include "../../Scene/Components.h"

#include <string>

namespace MaterialEditor {

bool EvaluateGraph(const Graph& graph, MaterialComponent& outMaterial, std::string& outErrorMessage);

} // namespace MaterialEditor
