#pragma once

#include "MaterialLayers.h"

#include <nlohmann/json.hpp>

/// Reads a material's "layers" array. Returns false with a message for a
/// malformed entry; `layers` then holds the layers read before it.
bool ParseMaterialLayers(const nlohmann::json& layersJson, std::vector<MaterialLayer>& layers, std::string& error);
nlohmann::json MaterialLayersToJson(const std::vector<MaterialLayer>& layers);
