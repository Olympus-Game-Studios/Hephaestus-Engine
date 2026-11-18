/**
 * @file MeshSettings.h
 * @brief A model's settings file (<model>.hpmesh beside it): the default material of each slot, used wherever the
 *        mesh is drawn with that slot left empty.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus::MeshSettings {

inline constexpr const char* kExtension = ".hpmesh";

/// "<model file>.hpmesh", for example Crate.fbx.hpmesh.
std::filesystem::path SidecarPath(const std::filesystem::path& model);

/// Portable material paths by slot; empty when the model has no settings file.
std::vector<std::string> LoadDefaultMaterials(const std::filesystem::path& model);

bool SaveDefaultMaterials(const std::filesystem::path& model, const std::vector<std::string>& materials,
                          std::string& outError);

} // namespace Hephaestus::MeshSettings
