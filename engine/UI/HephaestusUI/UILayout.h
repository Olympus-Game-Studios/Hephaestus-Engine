#pragma once

namespace Hephaestus::HephaestusUI {

class UIElement;

class UILayout {
public:
    static void ApplyVerticalLayout(UIElement& container, float spacing);
    static void ApplyHorizontalLayout(UIElement& container, float spacing);
};

} // namespace Hephaestus::HephaestusUI
