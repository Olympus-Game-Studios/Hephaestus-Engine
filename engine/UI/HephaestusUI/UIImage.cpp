#include "UIImage.h"

#include "UIRenderer.h"

namespace Hephaestus::HephaestusUI {

void UIImage::OnEmitDrawCommands(UIRenderer& renderer) const {
    if (m_ImageId.empty()) {
        return;
    }

    renderer.DrawImage({
        .rect = GetLayoutRect(),
        .imageId = m_ImageId,
        .tint = m_Tint,
        .scaleMode = m_ScaleMode,
        .opacity = ResolveOpacity(),
        .zOrder = GetZOrder()
    });
}

} // namespace Hephaestus::HephaestusUI
