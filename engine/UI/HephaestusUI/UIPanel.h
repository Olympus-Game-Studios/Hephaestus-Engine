#pragma once

#include "UIElement.h"

namespace Hephaestus::HephaestusUI {

class UIPanel : public UIElement {
public:
    UIPanel();

    void SetLocalStyle(const UIStyle& style) { m_LocalStyle = style; m_UseLocalStyle = true; }
    void ClearLocalStyle() { m_UseLocalStyle = false; }

    [[nodiscard]] const UIStyle& GetLocalStyle() const { return m_LocalStyle; }
    [[nodiscard]] bool HasLocalStyle() const { return m_UseLocalStyle; }

protected:
    void OnEmitDrawCommands(UIRenderer& renderer) const override;

private:
    UIStyle m_LocalStyle;
    bool m_UseLocalStyle = false;
};

} // namespace Hephaestus::HephaestusUI
