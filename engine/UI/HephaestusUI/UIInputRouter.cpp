#include "UIInputRouter.h"

#include "UIElement.h"
#include "UIScreen.h"

namespace Hephaestus::HephaestusUI {

bool UIInputRouter::RouteMouseMove(UIScreen& screen, const UIVec2& position) {
    UIElement* newHover = screen.HitTest(position);
    if (newHover != m_HoveredElement) {
        if (m_HoveredElement) {
            m_HoveredElement->OnHoverExit();
        }
        m_HoveredElement = newHover;
        if (m_HoveredElement) {
            m_HoveredElement->OnHoverEnter();
        }
    }

    return m_HoveredElement != nullptr;
}

bool UIInputRouter::RouteMousePress(UIScreen& screen, const UIVec2& position, int button) {
    m_PressedElement = screen.HitTest(position);
    m_PressedButton = button;

    if (m_PressedElement) {
        m_PressedElement->OnPressed();
        return true;
    }
    return false;
}

bool UIInputRouter::RouteMouseRelease(UIScreen& screen, const UIVec2& position, int button) {
    UIElement* releaseTarget = screen.HitTest(position);
    const bool hasPressedElement = m_PressedElement != nullptr;

    if (m_PressedElement && button == m_PressedButton) {
        m_PressedElement->OnReleased();
        if (m_PressedElement == releaseTarget) {
            m_PressedElement->OnClicked();
        }
    }

    m_PressedElement = nullptr;
    m_PressedButton = -1;
    return hasPressedElement;
}

void UIInputRouter::Reset() {
    if (m_HoveredElement) {
        m_HoveredElement->OnHoverExit();
    }
    m_HoveredElement = nullptr;
    m_PressedElement = nullptr;
    m_PressedButton = -1;
}

} // namespace Hephaestus::HephaestusUI
