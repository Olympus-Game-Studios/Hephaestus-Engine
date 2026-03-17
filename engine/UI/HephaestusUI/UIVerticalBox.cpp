#include "UIVerticalBox.h"

#include "UILayout.h"

namespace Hephaestus::HephaestusUI {

void UIVerticalBox::OnUpdateLayout() {
    UILayout::ApplyVerticalLayout(*this, m_Spacing);
}

} // namespace Hephaestus::HephaestusUI
