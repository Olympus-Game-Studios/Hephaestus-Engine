#include "UIHorizontalBox.h"

#include "UILayout.h"

namespace Hephaestus::HephaestusUI {

void UIHorizontalBox::OnUpdateLayout() {
    UILayout::ApplyHorizontalLayout(*this, m_Spacing, m_ChildVAlignment);
}

} // namespace Hephaestus::HephaestusUI
