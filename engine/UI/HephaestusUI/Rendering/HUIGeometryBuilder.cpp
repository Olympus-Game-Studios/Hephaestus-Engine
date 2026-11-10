#include "HUIGeometryBuilder.h"

#include "../../../Core/AppPaths.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <utility>

namespace Hephaestus::HephaestusUI {

namespace {

UIRect IntersectRect(const UIRect& a, const UIRect& b) {
    UIRect out;
    out.x = std::max(a.x, b.x);
    out.y = std::max(a.y, b.y);
    const float right = std::min(a.x + a.width, b.x + b.width);
    const float bottom = std::min(a.y + a.height, b.y + b.height);
    out.width = std::max(0.0f, right - out.x);
    out.height = std::max(0.0f, bottom - out.y);
    return out;
}

bool SameRect(const UIRect& a, const UIRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void HUIGeometryBuilder::Reset() {
    m_Data = UIBatchData{};
    m_ClipStack.clear();
    m_HasBatch = false;
    m_CurrentBatch = {};
}

UIRect HUIGeometryBuilder::CurrentClip() const {
    if (m_ClipStack.empty()) {
        return {0.0f, 0.0f, m_Viewport.x, m_Viewport.y};
    }
    UIRect clip = m_ClipStack.front();
    for (std::size_t i = 1; i < m_ClipStack.size(); ++i) {
        clip = IntersectRect(clip, m_ClipStack[i]);
    }
    return clip;
}

UIBatchData HUIGeometryBuilder::Build(const std::vector<UIDrawCommand>& commands,
                                      const UIVec2& viewport,
                                      const UITextureResolver& resolveTexture,
                                      IFontAtlasProvider& fonts) {
    Reset();
    m_Viewport = viewport;

    for (const UIDrawCommand& command : commands) {
        if (const auto* rectCmd = std::get_if<UIDrawRectCommand>(&command)) {
            EmitRect(rectCmd->rect, rectCmd->fillColor, rectCmd->opacity,
                     rectCmd->borderColor, rectCmd->borderWidth, rectCmd->cornerRadius,
                     rectCmd->opacity, rectCmd->backgroundImageId,
                     rectCmd->imageTint, rectCmd->imageScaleMode,
                     resolveTexture, rectCmd->zOrder);
        } else if (const auto* imageCmd = std::get_if<UIDrawImageCommand>(&command)) {
            EmitImage(imageCmd->rect, imageCmd->imageId, imageCmd->tint,
                      imageCmd->scaleMode, imageCmd->opacity, imageCmd->zOrder,
                      resolveTexture);
        } else if (const auto* textCmd = std::get_if<UIDrawTextCommand>(&command)) {
            EmitText(*textCmd, fonts);
        } else if (std::get_if<UIPushClipRectCommand>(&command)) {
            const auto& push = std::get<UIPushClipRectCommand>(command);
            // Intersect with the currently active clip (Qt IntersectClip semantics).
            const UIRect current = CurrentClip();
            m_ClipStack.push_back(IntersectRect(current, push.rect));
            // Scissor changed: close any in-progress batch.
            if (m_HasBatch) {
                m_Data.batches.push_back(m_CurrentBatch);
                m_HasBatch = false;
            }
        } else if (std::get_if<UIPopClipRectCommand>(&command)) {
            if (!m_ClipStack.empty()) {
                m_ClipStack.pop_back();
            }
            if (m_HasBatch) {
                m_Data.batches.push_back(m_CurrentBatch);
                m_HasBatch = false;
            }
        }
    }

    if (m_HasBatch) {
        m_Data.batches.push_back(m_CurrentBatch);
        m_HasBatch = false;
    }
    return std::move(m_Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Quad emission
// ─────────────────────────────────────────────────────────────────────────────

void HUIGeometryBuilder::EmitQuad(const UIRect& rect, float u0, float v0, float u1, float v1,
                                  const UIColor& color, float opacity, float cornerRadius,
                                  int32_t textureSlot, int zOrder) {
    if (rect.width <= 0.0f || rect.height <= 0.0f) return;
    if (color.a * opacity <= 0.0f) return;

    const UIRect scissor = CurrentClip();
    if (!m_HasBatch || m_CurrentBatch.textureSlot != textureSlot ||
        !SameRect(m_CurrentBatch.scissor, scissor)) {
        if (m_HasBatch) {
            m_Data.batches.push_back(m_CurrentBatch);
        }
        m_CurrentBatch = {};
        m_CurrentBatch.indexOffset = static_cast<uint32_t>(m_Data.indices.size());
        m_CurrentBatch.indexCount = 0;
        m_CurrentBatch.textureSlot = textureSlot;
        m_CurrentBatch.scissor = scissor;
        m_CurrentBatch.zOrder = zOrder;
        m_HasBatch = true;
    }

    const float x0 = rect.x;
    const float y0 = rect.y;
    const float x1 = rect.x + rect.width;
    const float y1 = rect.y + rect.height;
    const float radius = std::max(0.0f, cornerRadius);

    const uint32_t base = static_cast<uint32_t>(m_Data.vertices.size());

    UIVertex v[4];
    for (int i = 0; i < 4; ++i) {
        v[i].SetRect(rect);
        v[i].cornerRadius = radius;
        v[i].SetColor(color, opacity);
    }
    v[0].SetPos(x0, y0); v[0].SetUV(u0, v0);
    v[1].SetPos(x1, y0); v[1].SetUV(u1, v0);
    v[2].SetPos(x1, y1); v[2].SetUV(u1, v1);
    v[3].SetPos(x0, y1); v[3].SetUV(u0, v1);

    m_Data.vertices.push_back(v[0]);
    m_Data.vertices.push_back(v[1]);
    m_Data.vertices.push_back(v[2]);
    m_Data.vertices.push_back(v[3]);

    m_Data.indices.push_back(base + 0);
    m_Data.indices.push_back(base + 1);
    m_Data.indices.push_back(base + 2);
    m_Data.indices.push_back(base + 0);
    m_Data.indices.push_back(base + 2);
    m_Data.indices.push_back(base + 3);

    m_CurrentBatch.indexCount += 6;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rect commands (fill / background image / border)
// ─────────────────────────────────────────────────────────────────────────────

void HUIGeometryBuilder::EmitRect(const UIRect& rect, const UIColor& fill, float fillOpacity,
                                  const UIColor& border, float borderWidth, float cornerRadius,
                                  float commandOpacity, const std::string& backgroundImageId,
                                  UIColor imageTint, UIImageScaleMode imageScaleMode,
                                  const UITextureResolver& resolveTexture, int zOrder) {
    if (rect.width <= 0.0f || rect.height <= 0.0f) return;

    const float radius = std::max(0.0f, cornerRadius);
    const bool hasBorder = borderWidth > 0.0f && border.a > 0.0f && commandOpacity > 0.0f;

    // 1. Fill color.
    if (fill.a > 0.0f && commandOpacity > 0.0f) {
        EmitQuad(rect, 0.0f, 0.0f, 1.0f, 1.0f, fill, commandOpacity, radius, -1, zOrder);
    }

    // 2. Background image (drawn over the fill, clipped to the rounded rect).
    if (!backgroundImageId.empty()) {
        UITextureInfo tex = resolveTexture(backgroundImageId);
        if (tex.slot >= 0) {
            EmitImageQuad(rect, tex, imageScaleMode, imageTint, commandOpacity, radius, zOrder);
        } else {
            // Missing image: tinted solid placeholder (matches the editor overlay).
            EmitQuad(rect, 0.0f, 0.0f, 1.0f, 1.0f, imageTint, commandOpacity, radius, -1, zOrder);
        }
    }

    // 3. Border.
    if (hasBorder) {
        if (backgroundImageId.empty()) {
            // Rounded ring: border quad over the full rect, then fill re-drawn inset.
            EmitQuad(rect, 0.0f, 0.0f, 1.0f, 1.0f, border, commandOpacity, radius, -1, zOrder);
            const float inset = std::min(borderWidth, std::min(rect.width, rect.height) * 0.5f);
            UIRect inner = rect;
            inner.x += inset;
            inner.y += inset;
            inner.width = std::max(0.0f, inner.width - inset * 2.0f);
            inner.height = std::max(0.0f, inner.height - inset * 2.0f);
            const float innerRadius = std::max(0.0f, radius - inset);
            if (fill.a > 0.0f && commandOpacity > 0.0f && inner.width > 0.0f && inner.height > 0.0f) {
                EmitQuad(inner, 0.0f, 0.0f, 1.0f, 1.0f, fill, commandOpacity, innerRadius, -1, zOrder);
            }
        } else {
            // Square border edges on top of the image (keeps the image visible).
            const float bw = std::min(borderWidth, std::min(rect.width, rect.height) * 0.5f);
            const float x0 = rect.x;
            const float y0 = rect.y;
            const float x1 = rect.x + rect.width;
            const float y1 = rect.y + rect.height;
            EmitQuad({x0, y0, rect.width, bw}, 0.0f, 0.0f, 1.0f, 1.0f, border, commandOpacity, 0.0f, -1, zOrder);
            EmitQuad({x0, y1 - bw, rect.width, bw}, 0.0f, 0.0f, 1.0f, 1.0f, border, commandOpacity, 0.0f, -1, zOrder);
            EmitQuad({x0, y0 + bw, bw, rect.height - bw * 2.0f}, 0.0f, 0.0f, 1.0f, 1.0f, border, commandOpacity, 0.0f, -1, zOrder);
            EmitQuad({x1 - bw, y0 + bw, bw, rect.height - bw * 2.0f}, 0.0f, 0.0f, 1.0f, 1.0f, border, commandOpacity, 0.0f, -1, zOrder);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Image commands
// ─────────────────────────────────────────────────────────────────────────────

void HUIGeometryBuilder::EmitImageQuad(const UIRect& rect, const UITextureInfo& tex,
                                       UIImageScaleMode scaleMode, UIColor tint,
                                       float opacity, float cornerRadius, int zOrder) {
    const float texW = static_cast<float>(tex.width);
    const float texH = static_cast<float>(tex.height);
    if (texW <= 0.0f || texH <= 0.0f) return;

    const UIImagePlacement placement = PlaceImage(rect, texW, texH, scaleMode);
    const UIRect& src = placement.source;
    EmitQuad(placement.destination, src.x / texW, src.y / texH, (src.x + src.width) / texW,
             (src.y + src.height) / texH, tint, opacity, cornerRadius, tex.slot, zOrder);
}

UIImagePlacement PlaceImage(const UIRect& rect, float texW, float texH, UIImageScaleMode scaleMode) {
    UIImagePlacement out;
    out.destination = rect;
    out.source = {0.0f, 0.0f, texW, texH};
    if (texW <= 0.0f || texH <= 0.0f) return out;

    switch (scaleMode) {
        case UIImageScaleMode::Stretch:
            break;
        case UIImageScaleMode::Fit: {
            const float scale = std::min(rect.width / texW, rect.height / texH);
            const float outW = texW * scale;
            const float outH = texH * scale;
            out.destination = {rect.x + (rect.width - outW) * 0.5f, rect.y + (rect.height - outH) * 0.5f, outW, outH};
            break;
        }
        case UIImageScaleMode::Fill: {
            const float scale = std::max(rect.width / texW, rect.height / texH);
            float srcW = rect.width / scale;
            float srcH = rect.height / scale;
            float srcX = (texW - srcW) * 0.5f;
            float srcY = (texH - srcH) * 0.5f;
            srcW = std::min(srcW, texW);
            srcH = std::min(srcH, texH);
            srcX = std::min(srcX, texW - srcW);
            srcY = std::min(srcY, texH - srcH);
            out.source = {srcX, srcY, srcW, srcH};
            break;
        }
        case UIImageScaleMode::Center:
            out.destination = {rect.x + (rect.width - texW) * 0.5f, rect.y + (rect.height - texH) * 0.5f, texW, texH};
            break;
    }
    return out;
}

std::vector<std::string> HUIFontSearchPaths(const std::string& projectRoot) {
    std::vector<std::string> paths;
    if (!projectRoot.empty()) {
        paths = {projectRoot, projectRoot + "/assets", projectRoot + "/assets/fonts"};
    }
    const std::filesystem::path exeDir = ExecutableDirectory();
    if (!exeDir.empty()) {
        paths.push_back((ResourceRootFor(exeDir) / "assets").string());
    }
    return paths;
}

UITextMeasure MeasureHUIText(const std::string& projectRoot, const std::string& text, const UIFontKey& key) {
    // Layout only (no GPU); one atlas per font, kept for the life of the process.
    static std::unordered_map<UIFontKey, std::unique_ptr<UIFontAtlas>, UIFontKeyHash> atlases;
    auto& atlas = atlases[key];
    if (!atlas) {
        atlas = std::make_unique<UIFontAtlas>();
        const std::string file =
            UIFontAtlas::ResolveFontFile(key.family, key.bold, key.italic, HUIFontSearchPaths(projectRoot));
        if (file.empty() || !atlas->LoadFromFile(file, key.size)) {
            return {};
        }
    }
    if (!atlas->IsLoaded()) {
        return {};
    }
    const UITextLayoutResult layout = atlas->LayoutText(text, 0.0f, false);
    UITextMeasure measure;
    bool anyInk = false;
    for (const UITextLineLayout& line : layout.lines) {
        measure.width = std::max(measure.width, line.width);
        for (const UILaidOutGlyph& glyph : line.glyphs) {
            if (glyph.width <= 0.0f) continue;
            measure.inkLeft = anyInk ? std::min(measure.inkLeft, glyph.x) : glyph.x;
            measure.inkRight = anyInk ? std::max(measure.inkRight, glyph.x + glyph.width) : glyph.x + glyph.width;
            anyInk = true;
        }
    }
    return measure;
}

std::string ResolveHUIImagePath(const std::string& projectRoot, const std::string& imageId) {
    namespace fs = std::filesystem;
    if (imageId.empty()) return {};

    const fs::path asset(imageId);
    std::error_code ec;
    if (asset.is_absolute() && fs::is_regular_file(asset, ec)) {
        return imageId;
    }
    if (projectRoot.empty()) return {};

    auto tryPath = [](const fs::path& p) -> std::string {
        std::error_code ec2;
        return fs::is_regular_file(p, ec2) ? p.string() : std::string();
    };
    const fs::path root(projectRoot);
    const std::string rel = asset.generic_string();

    // Project root + full relative path, then + assets/ prefix, then without a leading "assets/".
    std::string found = tryPath(root / asset);
    if (!found.empty()) return found;
    found = tryPath(root / "assets" / rel);
    if (!found.empty()) return found;
    if (rel.rfind("assets/", 0) == 0) {
        found = tryPath(root / rel.substr(7));
    }
    return found;
}

void HUIGeometryBuilder::EmitImage(const UIRect& rect, const std::string& imageId,
                                   UIColor tint, UIImageScaleMode scaleMode, float opacity,
                                   int zOrder, const UITextureResolver& resolveTexture) {
    if (rect.width <= 0.0f || rect.height <= 0.0f) return;
    UITextureInfo tex = resolveTexture(imageId);
    if (tex.slot >= 0) {
        EmitImageQuad(rect, tex, scaleMode, tint, opacity, 0.0f, zOrder);
    } else {
        // Missing image: tinted solid placeholder (matches the editor overlay).
        EmitQuad(rect, 0.0f, 0.0f, 1.0f, 1.0f, tint, opacity, 0.0f, -1, zOrder);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Text commands
// ─────────────────────────────────────────────────────────────────────────────

void HUIGeometryBuilder::EmitText(const UIDrawTextCommand& command, IFontAtlasProvider& fonts) {
    if (command.text.empty()) return;
    if (command.opacity <= 0.0f) return;
    if (command.rect.width <= 0.0f || command.rect.height <= 0.0f) return;

    UIFontKey key;
    key.family = command.fontFamily.empty() ? "Default" : command.fontFamily;
    key.size = command.fontSize > 0.0f ? command.fontSize : 16.0f;
    key.bold = command.bold;
    key.italic = command.italic;

    UIFontAtlas* atlas = fonts.GetOrCreateAtlas(key);
    if (!atlas) {
        return;
    }

    const float maxWidth = command.wordWrap ? command.rect.width : 0.0f;
    UITextLayoutResult layout = atlas->LayoutText(command.text, maxWidth, command.wordWrap);
    if (!layout.ok || layout.lines.empty()) {
        return;
    }

    const float totalHeight = layout.lineHeight * static_cast<float>(layout.lines.size());
    float startBaselineY = command.rect.y + (command.rect.height - totalHeight) * 0.5f + layout.ascent;
    // A single line is centred on its capitals (the top of H to the baseline), not on the line box.
    if (layout.lines.size() == 1) {
        if (const UIGlyphInfo* capital = atlas->GetGlyph('H'); capital && capital->valid && capital->yOff < 0.0f) {
            startBaselineY = command.rect.y + (command.rect.height - capital->yOff) * 0.5f;
        }
    }

    const auto emitGlyph = [&](const UILaidOutGlyph& glyph, float startX, float baselineY,
                               const UIColor& color) {
        const UIGlyphInfo* info = atlas->GetGlyph(glyph.codepoint);
        if (!info) return;
        const UIRect quad{glyph.x + startX, baselineY + glyph.yOff, glyph.width, glyph.height};
        EmitQuad(quad, info->u0, info->v0, info->u1, info->v1, color, command.opacity, 0.0f,
                 static_cast<int32_t>(atlas->GetTextureHandle()), command.zOrder);
    };

    for (std::size_t lineIndex = 0; lineIndex < layout.lines.size(); ++lineIndex) {
        const UITextLineLayout& line = layout.lines[lineIndex];
        const float baselineY = startBaselineY + static_cast<float>(lineIndex) * layout.lineHeight;

        float startX = command.rect.x;
        switch (command.alignment) {
            case UITextAlignment::Left:
                startX = command.rect.x;
                break;
            case UITextAlignment::Center:
                startX = command.rect.x + (command.rect.width - line.width) * 0.5f;
                break;
            case UITextAlignment::Right:
                startX = command.rect.x + command.rect.width - line.width;
                break;
        }

        if (command.shadow) {
            const float shadowX = startX + command.shadowOffset.x;
            const float shadowY = baselineY + command.shadowOffset.y;
            for (const UILaidOutGlyph& glyph : line.glyphs) {
                emitGlyph(glyph, shadowX, shadowY, command.shadowColor);
            }
        }
        for (const UILaidOutGlyph& glyph : line.glyphs) {
            emitGlyph(glyph, startX, baselineY, command.textColor);
        }
    }
}

} // namespace Hephaestus::HephaestusUI