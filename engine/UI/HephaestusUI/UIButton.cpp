#include "UIButton.h"

#include "UIRenderer.h"
#include "UIText.h"

#include <memory>

namespace Hephaestus::HephaestusUI {

UIButton::UIButton(const std::string& labelText) {
    SetPadding(UISpacing::Uniform(8.0f));

    m_NormalStyle = {
        .backgroundColor = {0.17f, 0.18f, 0.22f, 1.0f},
        .borderColor = {0.35f, 0.37f, 0.42f, 1.0f},
        .borderWidth = 1.0f,
        .cornerRadius = 4.0f,
        .textColor = {1.0f, 1.0f, 1.0f, 1.0f},
        .padding = UISpacing::Uniform(8.0f)
    };

    m_HoveredStyle = m_NormalStyle;
    m_HoveredStyle.backgroundColor = {0.24f, 0.26f, 0.32f, 1.0f};

    m_PressedStyle = m_NormalStyle;
    m_PressedStyle.backgroundColor = {0.12f, 0.14f, 0.18f, 1.0f};

    auto label = std::make_unique<UIText>(labelText);
    label->SetInteractable(false);
    label->SetAlignment(UITextAlignment::Center);
    label->SetWordWrap(false);
    m_Label = label.get();
    AddChild(std::move(label));
}

void UIButton::SetText(const std::string& text) {
    if (m_Label) {
        m_Label->SetText(text);
    }
}

void UIButton::OnHoverEnter() {
    if (m_State != UIButtonState::Pressed) {
        m_State = UIButtonState::Hovered;
    }
}

void UIButton::OnHoverExit() {
    if (m_State != UIButtonState::Pressed) {
        m_State = UIButtonState::Normal;
    }
}

void UIButton::OnPressed() {
    m_State = UIButtonState::Pressed;
}

void UIButton::OnReleased() {
    m_State = UIButtonState::Hovered;
}

void UIButton::OnClicked() {
    if (m_OnClicked) {
        m_OnClicked();
    }
}

void UIButton::OnUpdateLayout() {
    if (!m_Label) {
        return;
    }

    const UIRect contentRect = GetContentRect();
    m_Label->SetPosition({0.0f, 0.0f});
    m_Label->SetSize({contentRect.width, contentRect.height});
    m_Label->SetColor(GetStateStyle().textColor);
}

void UIButton::OnEmitDrawCommands(UIRenderer& renderer) const {
    const UIStyle& style = GetStateStyle();
    renderer.DrawRect({
        .rect = GetLayoutRect(),
        .fillColor = style.backgroundColor,
        .borderColor = style.borderColor,
        .borderWidth = style.borderWidth,
        .cornerRadius = style.cornerRadius,
        .opacity = ResolveOpacity(),
        .zOrder = GetZOrder()
    });
}

const UIStyle& UIButton::GetStateStyle() const {
    switch (m_State) {
    case UIButtonState::Hovered:
        return m_HoveredStyle;
    case UIButtonState::Pressed:
        return m_PressedStyle;
    case UIButtonState::Normal:
    default:
        return m_NormalStyle;
    }
}

} // namespace Hephaestus::HephaestusUI
