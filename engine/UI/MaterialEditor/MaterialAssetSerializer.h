#pragma once

#include "MaterialGraph.h"
#include "../../Scene/Components.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace MaterialEditor {

using NodePositionProvider = std::function<bool(NodeID, float&, float&)>;

bool LoadMaterialAssetCompat(const std::filesystem::path& path,
                             MaterialComponent& outMaterial,
                             std::string& outDisplayName,
                             Graph& outGraph,
                             std::string& outError);

/// Also keeps `<name>.hemshader` in step: compiled when the graph needs its own shader (GraphNeedsCompiledShader),
/// removed otherwise. A graph that fails to compile still saves; the reasons go to shaderDiagnostics and the material
/// draws with the standard surface until it compiles.
bool SaveMaterialAssetCompat(const std::filesystem::path& path,
                             const MaterialComponent& material,
                             const std::string& displayName,
                             const Graph& graph,
                             const NodePositionProvider& nodePositionProvider,
                             std::string& outError,
                             std::vector<std::string>* shaderDiagnostics = nullptr);

void BuildMinimalGraphFromLegacyPbr(const MaterialComponent& material, Graph& graph);

} // namespace MaterialEditor
