#include "UIFont.h"

namespace Hephaestus::HephaestusUI {

void UIFontRegistry::RegisterFont(const std::string& key, const UIFont& font) {
    m_Fonts[key] = font;
}

const UIFont* UIFontRegistry::FindFont(const std::string& key) const {
    const auto it = m_Fonts.find(key);
    if (it == m_Fonts.end()) {
        return nullptr;
    }
    return &it->second;
}

const UIFont& UIFontRegistry::GetOrFallback(const std::string& key) const {
    if (const UIFont* font = FindFont(key)) {
        return *font;
    }
    return m_Fallback;
}

} // namespace Hephaestus::HephaestusUI
