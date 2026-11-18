/**
 * @file MeshSettings.cpp
 * @brief A model's settings file (<model>.hpmesh beside it): the default material of each slot.
 */

#include "MeshSettings.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace Hephaestus::MeshSettings {

std::filesystem::path SidecarPath(const std::filesystem::path& model) {
    std::filesystem::path sidecar = model;
    sidecar += kExtension;
    return sidecar;
}

std::vector<std::string> LoadDefaultMaterials(const std::filesystem::path& model) {
    std::vector<std::string> materials;
    std::ifstream in(SidecarPath(model));
    if (!in) {
        return materials;
    }
    const nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
    if (!root.is_object()) {
        return materials;
    }
    for (const nlohmann::json& entry : root.value("appliedMaterialPaths", nlohmann::json::array())) {
        materials.push_back(entry.is_string() ? entry.get<std::string>() : std::string());
    }
    return materials;
}

bool SaveDefaultMaterials(const std::filesystem::path& model, const std::vector<std::string>& materials,
                          std::string& outError) {
    // appliedMaterialPaths is the key AssetReferenceIndex rewrites when a material is renamed or moved.
    const nlohmann::json root = {{"schema", "HephaestusMesh"}, {"appliedMaterialPaths", materials}};
    std::ofstream out(SidecarPath(model), std::ios::trunc);
    out << root.dump(2) << '\n';
    if (!out) {
        outError = "Could not write " + SidecarPath(model).string();
        return false;
    }
    return true;
}

} // namespace Hephaestus::MeshSettings
