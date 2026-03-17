#pragma once

#include "UIElement.h"

namespace Hephaestus::HephaestusUI {

class UICanvas : public UIElement {
protected:
    void OnUpdateLayout() override;
};

} // namespace Hephaestus::HephaestusUI
