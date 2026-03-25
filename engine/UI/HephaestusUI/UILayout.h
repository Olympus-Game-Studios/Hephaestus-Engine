#pragma once

#include "UIStyle.h"

namespace Hephaestus::HephaestusUI {

class UIElement;

class UILayout {
public:
    static void ApplyVerticalLayout(UIElement& container, float spacing,
                                    UIHAlignment childHAlign = UIHAlignment::Stretch);
    static void ApplyHorizontalLayout(UIElement& container, float spacing,
                                      UIVAlignment childVAlign = UIVAlignment::Stretch);
};

} // namespace Hephaestus::HephaestusUI
