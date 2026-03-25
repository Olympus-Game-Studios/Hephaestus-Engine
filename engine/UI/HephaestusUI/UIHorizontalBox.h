#pragma once

#include "UIElement.h"

namespace Hephaestus::HephaestusUI {

class UIHorizontalBox : public UIElement {
public:
    void SetSpacing(float spacing) { m_Spacing = spacing; }
    [[nodiscard]] float GetSpacing() const { return m_Spacing; }

    void SetChildVAlignment(UIVAlignment alignment) { m_ChildVAlignment = alignment; }
    [[nodiscard]] UIVAlignment GetChildVAlignment() const { return m_ChildVAlignment; }

protected:
    void OnUpdateLayout() override;

private:
    float m_Spacing = 8.0f;
    UIVAlignment m_ChildVAlignment = UIVAlignment::Stretch;
};

} // namespace Hephaestus::HephaestusUI
