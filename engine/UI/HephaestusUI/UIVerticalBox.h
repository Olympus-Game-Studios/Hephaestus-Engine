#pragma once

#include "UIElement.h"

namespace Hephaestus::HephaestusUI {

class UIVerticalBox : public UIElement {
public:
    void SetSpacing(float spacing) { m_Spacing = spacing; }
    [[nodiscard]] float GetSpacing() const { return m_Spacing; }

protected:
    void OnUpdateLayout() override;

private:
    float m_Spacing = 8.0f;
};

} // namespace Hephaestus::HephaestusUI
