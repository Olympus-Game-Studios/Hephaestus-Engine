#include "UIPanel.h"

#include "UIRenderer.h"

namespace Hephaestus::HephaestusUI {

UIPanel::UIPanel() {
    // Transparent by default so HUD roots (Stretch + no fill) do not obscure the 3D view.
    // Opaque menus should set an explicit backgroundColor in .hui / SetLocalStyle.
    m_LocalStyle.backgroundColor = UIColor::Transparent();
    m_LocalStyle.borderColor = UIColor::Transparent();
    m_LocalStyle.borderWidth = 0.0f;
    m_LocalStyle.cornerRadius = 0.0f;
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
