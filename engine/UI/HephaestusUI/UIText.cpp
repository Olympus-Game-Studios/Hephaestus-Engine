#include "UIText.h"

#include "UIRenderer.h"

namespace Hephaestus::HephaestusUI {

void UIText::OnEmitDrawCommands(UIRenderer& renderer) const {
    if (m_Text.empty()) {
        return;
    }

    UIColor color = m_UseOverrideColor ? m_OverrideColor : UIColor::White();
    if (const UIStyle* style = ResolveStyle()) {
        if (!m_UseOverrideColor) {
            color = style->textColor;
        }
    }

    renderer.DrawText({
        .rect = GetContentRect(),
        .text = m_Text,
        .fontFamily = m_Font.GetFamilyName(),
        .fontSize = m_Font.GetPointSize(),
        .alignment = m_Alignment,
        .wordWrap = m_WordWrap,
        .textColor = color,
        .opacity = ResolveOpacity(),
        .zOrder = GetZOrder()
    });
}

} // namespace Hephaestus::HephaestusUI
