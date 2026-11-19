#include "UIFontAtlas.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace Hephaestus::HephaestusUI {

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// UTF-8 decoding
// ─────────────────────────────────────────────────────────────────────────────

namespace {

uint32_t DecodeUTF8(std::string_view view, size_t& index) {
    const uint8_t c = static_cast<uint8_t>(view[index]);
    if (c < 0x80) {
        ++index;
        return c;
    }
    uint32_t cp = 0;
    int extra = 0;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else {
        // Invalid leading byte: emit replacement and advance one.
        ++index;
        return 0xFFFD;
    }
    if (index + extra >= view.size()) {
        index = view.size();
        return 0xFFFD;
    }
    for (int i = 0; i < extra; ++i) {
        const uint8_t cont = static_cast<uint8_t>(view[index + 1 + i]);
        if ((cont & 0xC0) != 0x80) {
            index += 1;
            return 0xFFFD;
        }
        cp = (cp << 6) | (cont & 0x3F);
    }
    index += 1 + extra;
    return cp;
}

bool EndsWith(const std::string& s, const char* suffix) {
    const std::size_t len = std::strlen(suffix);
    if (s.size() < len) return false;
    return s.compare(s.size() - len, len, suffix) == 0;
}

bool FileExists(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Font key hashing
// ─────────────────────────────────────────────────────────────────────────────

std::size_t UIFontKeyHash::operator()(const UIFontKey& key) const {
    std::size_t h = std::hash<std::string>{}(key.family);
    h ^= std::hash<int>{}(static_cast<int>(key.size * 1000.0f)) + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(key.bold) + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(key.italic) + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// Font file resolution
// ─────────────────────────────────────────────────────────────────────────────

std::string UIFontAtlas::ResolveFontFile(const std::string& family, bool bold, bool italic,
                                         const std::vector<std::string>& searchPaths) {
    // Normalize the family name.
    std::string norm;
    norm.reserve(family.size());
    for (char c : family) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            norm.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (norm.empty()) norm = "default";

    // 1. Explicit font file referenced by the family name (e.g. "My-Font.ttf", "My-Font", or an absolute path).
    const fs::path named = fs::u8path(family);
    std::string extension = named.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::vector<fs::path> names;
    if (extension == ".ttf" || extension == ".otf") {
        names.push_back(named);
    } else {
        for (const char* ext : {".ttf", ".otf"}) {
            names.push_back(fs::u8path(family + ext));
            names.push_back(fs::path(norm + ext));
        }
    }
    for (const fs::path& name : names) {
        if (name.is_absolute() && FileExists(name)) {
            return name.string();
        }
    }
    for (const std::string& dir : searchPaths) {
        for (const fs::path& name : names) {
            const fs::path candidate = fs::path(dir) / name;
            if (!name.is_absolute() && FileExists(candidate)) {
                return candidate.string();
            }
        }
    }

    // 2. Bundled Roboto (assets/fonts) for common sans-serif families.
    const bool isSans = norm == "default" || norm == "sans" || norm == "sansserif" ||
                        norm == "arial" || norm == "segoeui" || norm == "helvetica" ||
                        norm == "roboto" || norm == "systemui" || norm == "open sans" || norm == "opensans";
    const bool isSerif = norm == "serif" || norm == "times" || norm == "timesnewroman" || norm == "georgia";
    const bool isMono = norm == "mono" || norm == "monospace" || norm == "courier" || norm == "consolas";

    for (const std::string& dir : searchPaths) {
        const fs::path fontsDir = fs::path(dir) / "fonts";
        if (!fs::is_directory(fontsDir)) continue;

        if (isSans) {
            const char* name =
                bold ? (italic ? "Roboto-BoldItalic.ttf" : "Roboto-Bold.ttf")
                     : (italic ? "Roboto-Italic.ttf" : "Roboto-Regular.ttf");
            const fs::path candidate = fontsDir / name;
            if (FileExists(candidate)) return candidate.string();
        } else if (isSerif) {
            const char* name = bold ? "Roboto-Bold.ttf" : "Roboto-Regular.ttf";
            const fs::path candidate = fontsDir / name;
            if (FileExists(candidate)) return candidate.string();
        } else if (isMono) {
            const char* name = bold ? "Roboto-Bold.ttf" : "Roboto-Regular.ttf";
            const fs::path candidate = fontsDir / name;
            if (FileExists(candidate)) return candidate.string();
        } else {
            // Unknown family: fall back to the default weight.
            const fs::path candidate = fontsDir / "Roboto-Regular.ttf";
            if (FileExists(candidate)) return candidate.string();
        }
    }

    // 3. System font directories (portable fallback when no fonts are bundled).
    const char* regularFile = nullptr;
    const char* boldFile = nullptr;
    const char* italicFile = nullptr;
    const char* boldItalicFile = nullptr;

#ifdef _WIN32
    const char* sysFonts = "C:/Windows/Fonts";
    if (isSans) {
        regularFile = "segoeui.ttf";
        boldFile = "segoeuib.ttf";
        italicFile = "segoeuii.ttf";
        boldItalicFile = "segoeuiz.ttf";
    } else if (isSerif) {
        regularFile = "times.ttf";
        boldFile = "timesbd.ttf";
        italicFile = "timesi.ttf";
        boldItalicFile = "timesbi.ttf";
    } else if (isMono) {
        regularFile = "consola.ttf";
        boldFile = "consolab.ttf";
        italicFile = "consolai.ttf";
        boldItalicFile = "consolaz.ttf";
    } else {
        regularFile = "segoeui.ttf";
        boldFile = "segoeuib.ttf";
        italicFile = "segoeuii.ttf";
        boldItalicFile = "segoeuiz.ttf";
    }
    // The face that matches the requested style, then the regular face if that one is missing.
    const char* wanted = bold ? (italic ? boldItalicFile : boldFile) : (italic ? italicFile : regularFile);
    std::vector<std::string> files;
    if (wanted) files.emplace_back(wanted);
    if (regularFile) files.emplace_back(regularFile);
    for (const std::string& file : files) {
        if (file.empty()) continue;
        const fs::path candidate = fs::path(sysFonts) / file;
        if (FileExists(candidate)) return candidate.string();
    }
#elif defined(__APPLE__)
    const char* macFonts = "/System/Library/Fonts";
    const fs::path helvetica = fs::path(macFonts) / "Helvetica.ttc";
    if (FileExists(helvetica)) {
        return helvetica.string(); // index selection handled by caller via LoadFromFile
    }
    const fs::path avenir = fs::path(macFonts) / "Avenir.ttc";
    if (FileExists(avenir)) {
        return avenir.string();
    }
#else
    // Linux and other POSIX systems.
    const char* dejavu = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    if (FileExists(dejavu)) return dejavu;
    const char* liberation = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf";
    if (FileExists(liberation)) return liberation;
#endif

    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Loading
// ─────────────────────────────────────────────────────────────────────────────

bool UIFontAtlas::LoadFromFile(const std::string& filepath, float pixelSize, int fontIndex) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }
    m_FontData.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(m_FontData.data()), size);
    file.close();
    if (!file.good() && !file.eof()) {
        return false;
    }

    auto* info = new stbtt_fontinfo{};
    if (!stbtt_InitFont(info, m_FontData.data(), fontIndex)) {
        delete info;
        return false;
    }
    m_FontHandle = std::shared_ptr<void>(info, [](void* p) {
        delete static_cast<stbtt_fontinfo*>(p);
    });

    m_LoadedPath = filepath;
    m_Loaded = true;

    m_Pixels = std::make_unique<uint8_t[]>(kAtlasSize * kAtlasSize * 4);
    std::memset(m_Pixels.get(), 0, kAtlasSize * kAtlasSize * 4);
    m_AtlasX = kGlyphPadding;
    m_AtlasY = kGlyphPadding;
    m_RowHeight = 0;
    ClearDirty();
    m_Glyphs.clear();

    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    m_Scale = stbtt_ScaleForPixelHeight(info, std::max(pixelSize, 1.0f));
    m_Ascent = static_cast<float>(ascent) * m_Scale;
    m_Descent = static_cast<float>(descent) * m_Scale;
    m_LineGap = static_cast<float>(lineGap) * m_Scale;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Glyph access
// ─────────────────────────────────────────────────────────────────────────────

const UIGlyphInfo* UIFontAtlas::GetGlyph(uint32_t codepoint) {
    if (!m_Loaded) {
        return nullptr;
    }
    auto it = m_Glyphs.find(codepoint);
    if (it != m_Glyphs.end()) {
        return it->second.valid ? &it->second : nullptr;
    }

    auto* info = static_cast<stbtt_fontinfo*>(m_FontHandle.get());

    int advance = 0;
    stbtt_GetCodepointHMetrics(info, codepoint, &advance, nullptr);
    const float advancePx = static_cast<float>(advance) * m_Scale;

    int width = 0, height = 0, xOff = 0, yOff = 0;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(info, m_Scale, m_Scale, codepoint,
                                                     &width, &height, &xOff, &yOff);
    if (!bitmap || width <= 0 || height <= 0) {
        // Glyphs without a bitmap (spaces, control characters) still carry an
        // advance so text layout remains correct.
        if (bitmap) {
            stbtt_FreeBitmap(bitmap, nullptr);
        }
        UIGlyphInfo blank;
        blank.advance = advancePx;
        blank.valid = true;
        m_Glyphs[codepoint] = blank;
        return &m_Glyphs[codepoint];
    }

    UIGlyphInfo glyph{};
    glyph.xOff = static_cast<float>(xOff);
    glyph.yOff = static_cast<float>(yOff);
    glyph.width = static_cast<float>(width);
    glyph.height = static_cast<float>(height);
    glyph.advance = advancePx;

    if (!PackGlyph(codepoint, glyph, bitmap)) {
        stbtt_FreeBitmap(bitmap, nullptr);
        m_Glyphs[codepoint] = UIGlyphInfo{};
        return nullptr;
    }
    stbtt_FreeBitmap(bitmap, nullptr);

    auto stored = m_Glyphs.find(codepoint);
    return stored != m_Glyphs.end() && stored->second.valid ? &stored->second : nullptr;
}

float UIFontAtlas::Kerning(uint32_t first, uint32_t second) const {
    if (!m_Loaded) return 0.0f;
    auto* info = static_cast<stbtt_fontinfo*>(m_FontHandle.get());
    return stbtt_GetCodepointKernAdvance(info, first, second) * m_Scale;
}

bool UIFontAtlas::PackGlyph(uint32_t codepoint, const UIGlyphInfo& info, const uint8_t* bitmap) {
    const uint32_t gw = static_cast<uint32_t>(info.width) + kGlyphPadding * 2;
    const uint32_t gh = static_cast<uint32_t>(info.height) + kGlyphPadding * 2;

    if (gw >= kAtlasSize || gh >= kAtlasSize) {
        return false;
    }

    if (m_AtlasX + gw > kAtlasSize) {
        m_AtlasX = kGlyphPadding;
        m_AtlasY += m_RowHeight + kGlyphPadding;
        m_RowHeight = 0;
    }
    if (m_AtlasY + gh > kAtlasSize) {
        // Atlas is full; fail this glyph (rare for typical UIs).
        return false;
    }

    const uint32_t dstX = m_AtlasX + kGlyphPadding;
    const uint32_t dstY = m_AtlasY + kGlyphPadding;

    const uint32_t bmpW = static_cast<uint32_t>(info.width);
    const uint32_t bmpH = static_cast<uint32_t>(info.height);
    for (uint32_t y = 0; y < bmpH; ++y) {
        const uint8_t* srcRow = bitmap + y * bmpW;
        uint8_t* dstRow = m_Pixels.get() + ((dstY + y) * kAtlasSize + dstX) * 4;
        for (uint32_t x = 0; x < bmpW; ++x) {
            const uint8_t coverage = srcRow[x];
            dstRow[x * 4 + 0] = 0xFF;
            dstRow[x * 4 + 1] = 0xFF;
            dstRow[x * 4 + 2] = 0xFF;
            dstRow[x * 4 + 3] = coverage;
        }
    }

    UIGlyphInfo stored = info;
    stored.u0 = static_cast<float>(dstX) / static_cast<float>(kAtlasSize);
    stored.v0 = static_cast<float>(dstY) / static_cast<float>(kAtlasSize);
    stored.u1 = static_cast<float>(dstX + bmpW) / static_cast<float>(kAtlasSize);
    stored.v1 = static_cast<float>(dstY + bmpH) / static_cast<float>(kAtlasSize);
    stored.valid = true;
    m_Glyphs[codepoint] = stored;

    m_AtlasX += gw;
    m_RowHeight = std::max(m_RowHeight, gh);

    m_Dirty = true;
    m_DirtyMinX = std::min(m_DirtyMinX, dstX);
    m_DirtyMinY = std::min(m_DirtyMinY, dstY);
    m_DirtyMaxX = std::max(m_DirtyMaxX, dstX + bmpW);
    m_DirtyMaxY = std::max(m_DirtyMaxY, dstY + bmpH);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Text layout
// ─────────────────────────────────────────────────────────────────────────────

float UIFontAtlas::MeasureWordWidth(const std::vector<uint32_t>& codepoints) {
    float width = 0.0f;
    uint32_t previous = 0;
    bool hasPrevious = false;
    for (uint32_t cp : codepoints) {
        if (cp == ' ') continue;
        const UIGlyphInfo* glyph = GetGlyph(cp);
        if (!glyph) continue;
        if (hasPrevious) width += Kerning(previous, cp);
        width += glyph->advance;
        previous = cp;
        hasPrevious = true;
    }
    return width;
}

UITextLayoutResult UIFontAtlas::LayoutText(const std::string& text, float maxWidth, bool wordWrap) {
    UITextLayoutResult result;
    if (!m_Loaded || text.empty()) {
        result.ok = m_Loaded;
        return result;
    }

    result.lineHeight = GetLineHeight();
    result.ascent = m_Ascent;

    // Parse into tokens: words, single spaces, newlines.
    struct Token {
        std::vector<uint32_t> codepoints;
        bool isSpace = false;
        bool isNewline = false;
        float width = 0.0f;
    };
    std::vector<Token> tokens;

    size_t index = 0;
    const std::string_view view(text);
    while (index < view.size()) {
        const uint32_t cp = DecodeUTF8(view, index);
        if (cp == '\n') {
            Token t;
            t.isNewline = true;
            tokens.push_back(std::move(t));
            continue;
        }
        if (cp == ' ' || cp == '\t') {
            Token t;
            t.isSpace = true;
            t.codepoints.push_back(cp);
            t.width = GetGlyph(cp) ? GetGlyph(cp)->advance : 0.0f;
            tokens.push_back(std::move(t));
            continue;
        }
        Token t;
        t.codepoints.push_back(cp);
        while (index < view.size()) {
            const unsigned char nextByte = static_cast<unsigned char>(view[index]);
            if (nextByte == ' ' || nextByte == '\t' || nextByte == '\n') {
                break;
            }
            const uint32_t next = DecodeUTF8(view, index);
            t.codepoints.push_back(next);
        }
        t.width = MeasureWordWidth(t.codepoints);
        tokens.push_back(std::move(t));
    }

    // Greedy word-wrap assembly.
    std::vector<const Token*> currentLine;
    float currentWidth = 0.0f;
    bool lineHasContent = false;

    auto flushLine = [&]() {
        UITextLineLayout line;
        float penX = 0.0f;
        uint32_t previous = 0;
        bool hasPrevious = false;
        for (const Token* token : currentLine) {
            for (uint32_t cp : token->codepoints) {
                const UIGlyphInfo* glyph = GetGlyph(cp);
                if (!glyph) continue;
                if (hasPrevious) penX += Kerning(previous, cp);
                UILaidOutGlyph laid;
                laid.codepoint = cp;
                laid.x = penX + glyph->xOff;
                laid.yOff = glyph->yOff;
                laid.width = glyph->width;
                laid.height = glyph->height;
                line.glyphs.push_back(laid);
                penX += glyph->advance;
                previous = cp;
                hasPrevious = true;
            }
        }
        line.width = penX;
        result.lines.push_back(std::move(line));
    };

    for (const Token& token : tokens) {
        if (token.isNewline) {
            flushLine();
            currentLine.clear();
            currentWidth = 0.0f;
            lineHasContent = false;
            continue;
        }
        if (token.isSpace) {
            if (lineHasContent) {
                currentLine.push_back(&token);
                currentWidth += token.width;
            }
            continue;
        }
        // Word token.
        if (wordWrap && maxWidth > 0.0f && lineHasContent && currentWidth + token.width > maxWidth) {
            flushLine();
            currentLine.clear();
            currentWidth = 0.0f;
            lineHasContent = false;
        }
        currentLine.push_back(&token);
        currentWidth += token.width;
        lineHasContent = true;
    }
    if (!currentLine.empty() || result.lines.empty()) {
        flushLine();
    }

    result.ok = true;
    return result;
}

} // namespace Hephaestus::HephaestusUI