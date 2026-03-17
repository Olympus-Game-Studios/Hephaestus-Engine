#pragma once

#include "UIStyle.h"

namespace Hephaestus::HephaestusUI {

class UIElement;
class UIScreen;

class UIInputRouter {
public:
    bool RouteMouseMove(UIScreen& screen, const UIVec2& position);
    bool RouteMousePress(UIScreen& screen, const UIVec2& position, int button);
    bool RouteMouseRelease(UIScreen& screen, const UIVec2& position, int button);

    void Reset();

private:
    UIElement* m_HoveredElement = nullptr;
    UIElement* m_PressedElement = nullptr;
    int m_PressedButton = -1;
};

} // namespace Hephaestus::HephaestusUI
