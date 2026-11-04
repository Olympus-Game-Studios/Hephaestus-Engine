/**
 * @file AssetThumbnailRenderer.h
 * @brief CPU preview renders for Content Browser thumbnails (materials, meshes, prefabs).
 */

#pragma once

#include "AssetTypes.h"
#include "MeshAsset.h"
#include "../Scene/Components.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus {

/** 8-bit RGBA, straight alpha, rows top to bottom. */
struct ThumbnailImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

/** A material with its texture maps decoded; null maps fall back to the scalar values. */
struct ThumbnailMaterial {
    MaterialComponent params;
    const ThumbnailImage* albedo = nullptr;
    const ThumbnailImage* normal = nullptr;
    const ThumbnailImage* roughness = nullptr;
    const ThumbnailImage* metallic = nullptr;
    const ThumbnailImage* ao = nullptr;
};

struct ThumbnailDraw {
    const MeshData* mesh = nullptr;
    Mat4 world = Mat4::Identity();
    /** Indexed by submesh material slot; a missing or null slot uses the default material. */
    std::vector<const ThumbnailMaterial*> materials;
};

enum class ThumbnailView {
    Front,        ///< Straight on, for material spheres
    ThreeQuarter  ///< From the front-right and above, for meshes and prefabs
};

/** Renders the draws framed to fill a size x size image with a transparent background; empty when nothing drew. */
ThumbnailImage RenderThumbnail(const std::vector<ThumbnailDraw>& draws, int size, ThumbnailView view);

/** Decodes an image file the way the engine's texture loader does (stb_image). */
bool LoadThumbnailImage(const std::filesystem::path& file, ThumbnailImage& out);

/** Halves the image (box filter) until neither side exceeds maxSide. */
void ShrinkThumbnailImage(ThumbnailImage& image, int maxSide);

/**
 * Renders a Material (on a sphere), StaticMesh or Prefab asset file.
 * outDependencies lists every file the render read, including assetFile, so callers can
 * tell when a cached thumbnail is stale. Returns false when nothing could be drawn.
 */
bool RenderAssetThumbnail(const std::filesystem::path& projectRoot,
                          const std::filesystem::path& assetRoot,
                          const std::filesystem::path& assetFile,
                          AssetType type,
                          int size,
                          ThumbnailImage& out,
                          std::vector<std::filesystem::path>& outDependencies,
                          std::string& outError);

} // namespace Hephaestus
