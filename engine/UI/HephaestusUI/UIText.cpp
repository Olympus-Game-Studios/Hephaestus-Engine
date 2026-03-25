#include "UIText.h"

#include "UIRenderer.h"

namespace Hephaestus::HephaestusUI {

void UIText::OnEmitDrawCommands(UIRenderer& renderer) const {
    if (m_Text.empty()) {
        return;
    }

    UIColor color = m_UseOverrideColor ? m_OverrideColor : UIColor::White();
    UITextAlignment alignment = m_Alignment;
    std::string fontFamily = m_Font.GetFamilyName();
    float fontSize = m_Font.GetPointSize();
    bool bold = m_Font.IsBold();
    bool italic = m_Font.IsItalic();
    bool shadow = m_Shadow;
    UIColor shadowColor = m_ShadowColor;
    UIVec2 shadowOffset = m_ShadowOffset;

    if (const UIStyle* style = ResolveStyle()) {
        if (!m_UseOverrideColor) {
            color = style->textColor;
        }
        if (!style->fontFamily.empty()) {
            fontFamily = style->fontFamily;
        }
        if (style->fontSize > 0.0f) {
            fontSize = style->fontSize;
        }
        if (style->fontBold) {
            bold = true;
        }
        if (style->fontItalic) {
            italic = true;
        }
        alignment = style->textAlignment;
        if (style->textShadow) {
            shadow = true;
            shadowColor = style->textShadowColor;
            shadowOffset = style->textShadowOffset;
        }
    }

    renderer.DrawText({
        .rect = GetContentRect(),
        .text = m_Text,
        .fontFamily = fontFamily,
        .fontSize = fontSize,
        .bold = bold,
        .italic = italic,
        .alignment = alignment,
        .wordWrap = m_WordWrap,
        .textColor = color,
        .opacity = ResolveOpacity(),
        .zOrder = GetZOrder(),
        .shadow = shadow,
        .shadowColor = shadowColor,
        .shadowOffset = shadowOffset
    });
}

} // namespace Hephaestus::HephaestusUI
