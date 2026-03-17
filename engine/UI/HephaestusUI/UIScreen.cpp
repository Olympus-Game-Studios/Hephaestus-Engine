#include "UIScreen.h"

#include "UICanvas.h"
#include "UIElement.h"
#include "UIRenderer.h"

namespace Hephaestus::HephaestusUI {

UIScreen::UIScreen(std::string name)
    : m_Name(std::move(name)),
      m_RootCanvas(std::make_unique<UICanvas>()) {
    m_RootCanvas->SetPosition({0.0f, 0.0f});
    m_RootCanvas->SetSize(m_ViewportSize);
    m_RootCanvas->SetInteractable(false);
}

void UIScreen::SetViewportSize(const UIVec2& viewportSize) {
    m_ViewportSize = viewportSize;
    if (m_RootCanvas) {
        m_RootCanvas->SetSize(m_ViewportSize);
    }
}

void UIScreen::Update(const UIContext& context, const std::vector<UIMouseEvent>& mouseEvents) {
    if (!m_Active || !m_RootCanvas) {
        return;
    }

    m_RootCanvas->SetSize(context.GetViewportSize());

    for (const UIMouseEvent& event : mouseEvents) {
        switch (event.type) {
        case UIMouseEventType::Move:
            m_InputRouter.RouteMouseMove(*this, event.position);
            break;
        case UIMouseEventType::Press:
            m_InputRouter.RouteMousePress(*this, event.position, event.button);
            break;
        case UIMouseEventType::Release:
            m_InputRouter.RouteMouseRelease(*this, event.position, event.button);
            break;
        }
    }

    m_RootCanvas->UpdateLayout();
}

void UIScreen::Draw(UIRenderer& renderer) const {
    if (!m_Visible || !m_RootCanvas) {
        return;
    }
    m_RootCanvas->Draw(renderer);
}

UIElement* UIScreen::HitTest(const UIVec2& position) {
    if (!m_RootCanvas) {
        return nullptr;
    }
    return m_RootCanvas->HitTest(position);
}

} // namespace Hephaestus::HephaestusUI
