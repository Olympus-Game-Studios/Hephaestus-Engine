/**
 * @file UsdModel.h
 * @brief Reads a USD scene (.usd, .usda, .usdc, .usdz) as one static mesh with a submesh per material, composing
 *        its sublayers, references, payloads and variants first.
 */

#pragma once

#include "MeshAsset.h"
#include "ModelMaterialImport.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus::Usd {

bool IsUsd(const std::filesystem::path& path);

/// Meshes in world space (Y up, metres), one submesh per material; outMaterials are index-aligned with
/// outSlotMaterials' values (-1 for a slot without a material), with texture files resolved to absolute paths.
bool Load(const std::filesystem::path& path, MeshData& outMesh, std::vector<SourceMaterial>& outMaterials,
          std::vector<int>& outSlotMaterials, std::string& outError);

} // namespace Hephaestus::Usd
