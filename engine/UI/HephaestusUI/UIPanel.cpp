#include "UIPanel.h"

#include "UIRenderer.h"

namespace Hephaestus::HephaestusUI {

UIPanel::UIPanel() {
    m_LocalStyle.backgroundColor = {0.08f, 0.08f, 0.08f, 0.9f};
    m_LocalStyle.borderColor = {0.35f, 0.35f, 0.35f, 1.0f};
    m_LocalStyle.borderWidth = 1.0f;
    m_LocalStyle.cornerRadius = 4.0f;
}

void UIPanel::OnEmitDrawCommands(UIRenderer& renderer) const {
    const UIStyle defaultStyle;
    const UIStyle& style = m_UseLocalStyle ? m_LocalStyle : (ResolveStyle() ? *ResolveStyle() : defaultStyle);

    renderer.DrawRect({
        .rect = GetLayoutRect(),
        .fillColor = style.backgroundColor,
        .borderColor = style.borderColor,
        .borderWidth = style.borderWidth,
        .cornerRadius = style.cornerRadius,
        .opacity = ResolveOpacity() * style.opacity,
        .zOrder = GetZOrder(),
        .backgroundImageId = style.backgroundImageId,
        .imageScaleMode = style.imageScaleMode,
        .imageTint = style.imageTint
    });
}

} // namespace Hephaestus::HephaestusUI
