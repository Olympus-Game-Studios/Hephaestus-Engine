#pragma once

#include "MaterialGraph.h"
#include "../../Scene/Components.h"

#include <filesystem>
#include <functional>
#include <string>

namespace MaterialEditor {

using NodePositionProvider = std::function<bool(NodeID, float&, float&)>;

bool LoadMaterialAssetCompat(const std::filesystem::path& path,
                             MaterialComponent& outMaterial,
                             std::string& outDisplayName,
                             Graph& outGraph,
                             std::string& outError);

bool SaveMaterialAssetCompat(const std::filesystem::path& path,
                             const MaterialComponent& material,
                             const std::string& displayName,
                             const Graph& graph,
                             const NodePositionProvider& nodePositionProvider,
                             std::string& outError);

void BuildMinimalGraphFromLegacyPbr(const MaterialComponent& material, Graph& graph);

} // namespace MaterialEditor
