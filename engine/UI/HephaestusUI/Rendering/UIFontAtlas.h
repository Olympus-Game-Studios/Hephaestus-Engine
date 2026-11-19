#pragma once

#include "../UIStyle.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hephaestus::HephaestusUI {

/**
 * UIFontAtlas.h
 *
 * CPU-side font rasterization, glyph atlas, and text layout for the HUI
 * Vulkan backend. Uses stb_truetype for glyph rasterization.
 *
 * One UIFontAtlas instance corresponds to a single (font family, pixel size,
 * bold, italic) combination. Glyphs are rasterized lazily as they are first
 * requested and packed into a fixed-size RGBA8 atlas texture. The owning GPU
 * backend uploads the atlas buffer whenever IsDirty() is true.
 *
 * This class has no GPU dependency so it can be exercised headlessly.
 */

struct UIFontKey {
    std::string family = "Default";
    float size = 16.0f;
    bool bold = false;
    bool italic = false;

    bool operator==(const UIFontKey&) const = default;
};

struct UIFontKeyHash {
    std::size_t operator()(const UIFontKey& key) const;
};

/** Per-glyph metadata: atlas UVs plus pixel geometry/metrics. */
struct UIGlyphInfo {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float xOff = 0.0f;   // left offset of the quad relative to the pen (pixels)
    float yOff = 0.0f;   // top offset of the quad relative to the baseline (pixels)
    float width = 0.0f;  // quad size in pixels
    float height = 0.0f;
    float advance = 0.0f; // horizontal advance in pixels
    bool valid = false;
};

/** A single glyph positioned relative to its line origin. */
struct UILaidOutGlyph {
    uint32_t codepoint = 0;
    float x = 0.0f;       // left edge of the quad, relative to the line left edge
    float yOff = 0.0f;    // top edge of the quad, relative to the baseline
    float width = 0.0f;
    float height = 0.0f;
};

struct UITextLineLayout {
    std::vector<UILaidOutGlyph> glyphs;
    float width = 0.0f;
};

struct UITextLayoutResult {
    std::vector<UITextLineLayout> lines;
    float lineHeight = 0.0f; // ascent - descent + lineGap (pixels)
    float ascent = 0.0f;
    bool ok = false;
};

class UIFontAtlas {
public:
    static constexpr uint32_t kAtlasSize = 2048;
    static constexpr uint32_t kGlyphPadding = 2;

    UIFontAtlas() = default;

    UIFontAtlas(const UIFontAtlas&) = delete;
    UIFontAtlas& operator=(const UIFontAtlas&) = delete;

    /**
     * Load a TrueType/OpenType font file (or .ttc collection via fontIndex)
     * at the given pixel size. Returns false if the file cannot be loaded.
     */
    bool LoadFromFile(const std::string& filepath, float pixelSize, int fontIndex = 0);

    /**
     * Resolve a font file for a family/weight/style.
     * Search order:
     *   1. The file <family> names: an absolute path, or a file in any search path, as given or with
     *      .ttf/.otf added, or with its letters and digits lowercased.
     *   2. The bundled Roboto font (assets/fonts) for common families.
     *   3. Platform system font directories.
     * Returns an empty string when nothing can be resolved.
     */
    static std::string ResolveFontFile(const std::string& family, bool bold, bool italic,
                                       const std::vector<std::string>& searchPaths);

    [[nodiscard]] bool IsLoaded() const { return m_Loaded; }
    [[nodiscard]] const std::string& GetLoadedPath() const { return m_LoadedPath; }

    /**
     * Backend-assigned handle identifying this atlas's GPU texture (e.g. a
     * texture-cache slot). Geometry conversion uses it so glyph quads batch
     * against the correct atlas texture. -1 means "not yet assigned".
     */
    void SetTextureHandle(int64_t handle) { m_TextureHandle = handle; }
    [[nodiscard]] int64_t GetTextureHandle() const { return m_TextureHandle; }

    /**
     * Get (rasterizing and packing on demand) a glyph. Returns nullptr for
     * codepoints that could not be rasterized (e.g. missing glyph or full
     * atlas).
     */
    const UIGlyphInfo* GetGlyph(uint32_t codepoint);

    /** Layout text into wrapped, aligned lines of positioned glyphs. */
    UITextLayoutResult LayoutText(const std::string& text, float maxWidth, bool wordWrap);

    // Atlas state
    [[nodiscard]] const uint8_t* GetPixels() const { return m_Pixels.get(); }
    [[nodiscard]] uint32_t GetAtlasSize() const { return kAtlasSize; }
    [[nodiscard]] bool IsDirty() const { return m_Dirty; }

    /** Dirty pixel region since the last ClearDirty(). */
    void GetDirtyRect(uint32_t& x, uint32_t& y, uint32_t& w, uint32_t& h) const {
        x = m_DirtyMinX;
        y = m_DirtyMinY;
        w = m_DirtyMaxX > m_DirtyMinX ? (m_DirtyMaxX - m_DirtyMinX) : 0;
        h = m_DirtyMaxY > m_DirtyMinY ? (m_DirtyMaxY - m_DirtyMinY) : 0;
    }

    void ClearDirty() {
        m_Dirty = false;
        m_DirtyMinX = kAtlasSize;
        m_DirtyMinY = kAtlasSize;
        m_DirtyMaxX = 0;
        m_DirtyMaxY = 0;
    }

    // Metrics (scaled to the atlas pixel size)
    [[nodiscard]] float GetAscent() const { return m_Ascent; }
    [[nodiscard]] float GetDescent() const { return m_Descent; }
    [[nodiscard]] float GetLineGap() const { return m_LineGap; }
    [[nodiscard]] float GetLineHeight() const { return m_Ascent - m_Descent + m_LineGap; }

private:
    float MeasureWordWidth(const std::vector<uint32_t>& codepoints);
    float Kerning(uint32_t first, uint32_t second) const;
    bool PackGlyph(uint32_t codepoint, const UIGlyphInfo& info, const uint8_t* bitmap);

    std::vector<uint8_t> m_FontData;
    std::string m_LoadedPath;
    bool m_Loaded = false;
    int64_t m_TextureHandle = -1;

    // stb_truetype font handle (opaque storage, defined in the .cpp)
    std::shared_ptr<void> m_FontHandle;

    // Atlas buffer (RGBA8, kAtlasSize x kAtlasSize)
    std::unique_ptr<uint8_t[]> m_Pixels;
    uint32_t m_AtlasX = kGlyphPadding;
    uint32_t m_AtlasY = kGlyphPadding;
    uint32_t m_RowHeight = 0;
    bool m_Dirty = false;
    uint32_t m_DirtyMinX = kAtlasSize;
    uint32_t m_DirtyMinY = kAtlasSize;
    uint32_t m_DirtyMaxX = 0;
    uint32_t m_DirtyMaxY = 0;

    // Font metrics scaled to pixel size
    float m_Scale = 1.0f;
    float m_Ascent = 0.0f;
    float m_Descent = 0.0f;
    float m_LineGap = 0.0f;

    std::unordered_map<uint32_t, UIGlyphInfo> m_Glyphs;
};

} // namespace Hephaestus::HephaestusUI