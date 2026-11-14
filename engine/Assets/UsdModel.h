/**
 * @file UsdModel.h
 * @brief Reads a USD scene (.usd, .usda, .usdc, .usdz) as static mesh parts with their materials, composing its
 *        sublayers, references, payloads and variants first.
 */

#pragma once

#include "MeshAsset.h"
#include "ModelMaterialImport.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus::Usd {

bool IsUsd(const std::filesystem::path& path);

/// One Mesh prim: geometry with the prim's world rotation and scale baked in, around its world position.
struct Part {
    std::string name;           ///< from the prim path below its group (or the default prim); unique in its group
    std::string group;          ///< the top-level component it belongs to (e.g. a wheel), empty when it is one
    Vec3 groupOrigin{0.0f, 0.0f, 0.0f}; ///< world position of that component
    MeshData mesh;              ///< Y up, metres, one submesh per material
    std::vector<int> slotMaterials; ///< per submesh: index into the materials list, -1 for none
    Vec3 origin{0.0f, 0.0f, 0.0f};
    bool collision = false;     ///< under a prim with UsdPhysics' PhysicsCollisionAPI
};

/// Every drawn Mesh prim (prototypes in `class` prims only through their instances; guide, proxy and invisible prims
/// skipped) plus collision meshes; materials are shared by all parts, with texture files resolved to absolute paths.
bool LoadParts(const std::filesystem::path& path, std::vector<Part>& outParts,
               std::vector<SourceMaterial>& outMaterials, std::string& outError);

} // namespace Hephaestus::Usd
