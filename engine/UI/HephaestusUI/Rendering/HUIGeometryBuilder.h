#pragma once

#include "HUICommon.h"
#include "UIFontAtlas.h"
#include "../UIRenderer.h"

#include <memory>
#include <string>
#include <vector>

namespace Hephaestus::HephaestusUI {

/**
 * HUIGeometryBuilder.h
 *
 * Converts the renderer-independent HUI draw command list into batched quad
 * geometry (UIBatchData). Pure CPU work with no GPU dependency:
 *
 *   - Resolves images through a UITextureResolver callback
 *   - Resolves fonts through an IFontAtlasProvider callback (lazy rasterization)
 *   - Applies opacity, borders, rounded corners, scale modes, alignment,
 *     clipping/scissor tracking, and draw ordering
 *
 * The GPU backend uploads the produced buffers and submits the batches in
 * order; headless tests can feed the same input and assert on the geometry.
 */

class IFontAtlasProvider {
public:
    virtual ~IFontAtlasProvider() = default;
    /** Return a loaded atlas for the key (creating it if necessary), or nullptr. */
    virtual UIFontAtlas* GetOrCreateAtlas(const UIFontKey& key) = 0;
};

class HUIGeometryBuilder {
public:
    /** Reset per-frame state. */
    void Reset();

    /**
     * Convert commands into vertex/index/batch data.
     * @param commands       Renderer-independent draw commands (paint order)
     * @param viewport       Logical viewport size in pixels
     * @param resolveTexture Callback mapping an imageId to a texture slot
     * @param fonts          Font atlas provider
     */
    UIBatchData Build(const std::vector<UIDrawCommand>& commands,
                      const UIVec2& viewport,
                      const UITextureResolver& resolveTexture,
                      IFontAtlasProvider& fonts);

private:
    using VertexIndex = std::uint32_t;

    void EmitQuad(const UIRect& rect, float u0, float v0, float u1, float v1,
                  const UIColor& color, float opacity, float cornerRadius,
                  int32_t textureSlot, int zOrder);

    void EmitRect(const UIRect& rect, const UIColor& fill, float fillOpacity,
                  const UIColor& border, float borderWidth, float cornerRadius,
                  float commandOpacity, const std::string& backgroundImageId,
                  UIColor imageTint, UIImageScaleMode imageScaleMode,
                  const UITextureResolver& resolveTexture, int zOrder);

    void EmitImage(const UIRect& rect, const std::string& imageId,
                   UIColor tint, UIImageScaleMode scaleMode, float opacity,
                   int zOrder, const UITextureResolver& resolveTexture);

    void EmitImageQuad(const UIRect& rect, const UITextureInfo& tex,
                       UIImageScaleMode scaleMode, UIColor tint,
                       float opacity, float cornerRadius, int zOrder);

    void EmitText(const UIDrawTextCommand& command, IFontAtlasProvider& fonts);

    UIRect CurrentClip() const;

    UIBatchData m_Data;
    std::vector<UIRect> m_ClipStack;
    bool m_HasBatch = false;
    UIBatch m_CurrentBatch{};
    UIVec2 m_Viewport{};
};

} // namespace Hephaestus::HephaestusUI