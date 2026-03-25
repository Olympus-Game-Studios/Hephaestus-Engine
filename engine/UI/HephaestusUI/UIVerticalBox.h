#pragma once

#include "UIElement.h"

namespace Hephaestus::HephaestusUI {

class UIVerticalBox : public UIElement {
public:
    void SetSpacing(float spacing) { m_Spacing = spacing; }
    [[nodiscard]] float GetSpacing() const { return m_Spacing; }

    void SetChildHAlignment(UIHAlignment alignment) { m_ChildHAlignment = alignment; }
    [[nodiscard]] UIHAlignment GetChildHAlignment() const { return m_ChildHAlignment; }

protected:
    void OnUpdateLayout() override;

private:
    float m_Spacing = 8.0f;
    UIHAlignment m_ChildHAlignment = UIHAlignment::Stretch;
};

} // namespace Hephaestus::HephaestusUI
