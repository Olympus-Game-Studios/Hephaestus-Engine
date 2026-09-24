#pragma once

#include "../UIStyle.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Hephaestus::HephaestusUI {

/**
 * HUICommon.h
 *
 * Backend-agnostic geometry types shared by the HUI rendering pipeline.
 *
 * The draw command list (see UIRenderer.h) is converted into a list of
 * textured quads (UIVertex) grouped into batches (UIBatch). A batch is the
 * smallest unit the GPU backend can draw with a single vkCmdDrawIndexed call:
 * it groups consecutive quads that share the same texture and the same
 * scissor rect, so compatible UI primitives can be submitted together.
 *
 * This header must stay free of Vulkan/Qt so that headless unit tests can
 * exercise the geometry conversion layer without a GPU.
 */

/**
 * Per-vertex UI geometry. Positions are in viewport pixel coordinates with a
 * top-left origin (y grows downward). Colors are straight (non-premultiplied)
 * RGBA; the shader performs premultiplication for correct alpha blending.
 *
 * `rect` and `cornerRadius` describe the rounded-rect the fragment belongs to
 * so the fragment shader can apply an SDF-based rounded-corner fade.
 */
struct UIVertex {
    float posX = 0.0f;
    float posY = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    float rectMinX = 0.0f;
    float rectMinY = 0.0f;
    float rectMaxX = 0.0f;
    float rectMaxY = 0.0f;
    float cornerRadius = 0.0f;
    float pad0 = 0.0f;

    void SetPos(float x, float y) { posX = x; posY = y; }
    void SetUV(float su, float sv) { u = su; v = sv; }
    void SetColor(const UIColor& color, float opacity = 1.0f) {
        r = color.r;
        g = color.g;
        b = color.b;
        a = color.a * opacity;
    }
    void SetRect(const UIRect& rect) {
        rectMinX = rect.x;
        rectMinY = rect.y;
        rectMaxX = rect.x + rect.width;
        rectMaxY = rect.y + rect.height;
    }
};

/**
 * Texture resolution result used by the geometry builder.
 *
 * `slot` is a backend-assigned handle for a GPU texture (e.g. an index into a
 * texture cache). A slot of -1 means "no texture" (solid white quad). Width
 * and height are the texture's pixel dimensions, required to implement the
 * Fit / Fill / Center scale modes.
 */
struct UITextureInfo {
    int32_t slot = -1;
    uint32_t width = 1;
    uint32_t height = 1;
};

/**
 * Resolver callback the geometry builder uses to turn an imageId string into
 * a backend texture. Returns slot -1 (with log) when the asset is missing so
 * the builder falls back to a tinted solid quad.
 */
using UITextureResolver = std::function<UITextureInfo(const std::string& imageId)>;

/**
 * One drawable group of quads. indexOffset/count index into a shared index
 * buffer; textureSlot selects the texture descriptor set; scissor is the
 * active clip rect in viewport pixels (must be applied with the draw).
 */
struct UIBatch {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    int32_t textureSlot = -1;
    UIRect scissor = {};
    int zOrder = 0;
};

/**
 * The output of the geometry conversion step: vertex/index data plus the
 * ordered batch list. GPU backends upload the buffers and submit the batches
 * in order.
 */
struct UIBatchData {
    std::vector<UIVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<UIBatch> batches;
};

} // namespace Hephaestus::HephaestusUI