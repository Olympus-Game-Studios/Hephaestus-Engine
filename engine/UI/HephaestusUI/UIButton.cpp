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
    m_HoveredStyle.borderColor = {0.5f, 0.52f, 0.6f, 1.0f};

    m_PressedStyle = m_NormalStyle;
    m_PressedStyle.backgroundColor = {0.12f, 0.14f, 0.18f, 1.0f};

    m_FocusedStyle = m_NormalStyle;
    m_FocusedStyle.borderColor = {0.4f, 0.6f, 1.0f, 1.0f};
    m_FocusedStyle.borderWidth = 2.0f;

    m_DisabledStyle = m_NormalStyle;
    m_DisabledStyle.backgroundColor = {0.12f, 0.12f, 0.14f, 0.6f};
    m_DisabledStyle.textColor = {0.5f, 0.5f, 0.5f, 1.0f};
    m_DisabledStyle.borderColor = {0.2f, 0.2f, 0.22f, 0.6f};

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

void UIButton::SetFocused(bool focused) {
    m_Focused = focused;
    UpdateVisualState();
}

void UIButton::OnHoverEnter() {
    if (m_State != UIButtonState::Pressed && m_State != UIButtonState::Disabled) {
        m_State = UIButtonState::Hovered;
    }
    if (m_OnHovered) {
        m_OnHovered(true);
    }
}

void UIButton::OnHoverExit() {
    if (m_State != UIButtonState::Pressed && m_State != UIButtonState::Disabled) {
        UpdateVisualState();
    }
    if (m_OnHovered) {
        m_OnHovered(false);
    }
}

void UIButton::OnPressed() {
    if (IsEnabled()) {
        m_State = UIButtonState::Pressed;
    }
}

void UIButton::OnReleased() {
    if (IsEnabled()) {
        m_State = UIButtonState::Hovered;
    }
}

void UIButton::OnClicked() {
    if (IsEnabled() && m_OnClicked) {
        m_OnClicked();
    }
}

void UIButton::UpdateVisualState() {
    if (!IsEnabled()) {
        m_State = UIButtonState::Disabled;
        return;
    }
    if (m_Focused) {
        m_State = UIButtonState::Focused;
        return;
    }
    m_State = UIButtonState::Normal;
}

void UIButton::OnUpdateLayout() {
    if (!m_Label) {
        return;
    }

    if (!IsEnabled() && m_State != UIButtonState::Disabled) {
        m_State = UIButtonState::Disabled;
    }

    const UIRect contentRect = GetContentRect();
    m_Label->SetPosition({0.0f, 0.0f});
    m_Label->SetSize({contentRect.width, contentRect.height});

    const UIStyle& style = GetStateStyle();
    m_Label->SetColor(style.textColor);

    if (style.fontSize > 0.0f) {
        UIFont font;
        if (!style.fontFamily.empty()) {
            font.SetFamilyName(style.fontFamily);
        }
        font.SetPointSize(style.fontSize);
        font.SetBold(style.fontBold);
        font.SetItalic(style.fontItalic);
        m_Label->SetFont(font);
    }
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
        .zOrder = GetZOrder(),
        .backgroundImageId = style.backgroundImageId,
        .imageScaleMode = style.imageScaleMode,
        .imageTint = style.imageTint
    });
}

const UIStyle& UIButton::GetStateStyle() const {
    switch (m_State) {
    case UIButtonState::Hovered:
        return m_HoveredStyle;
    case UIButtonState::Pressed:
        return m_PressedStyle;
    case UIButtonState::Focused:
        return m_FocusedStyle;
    case UIButtonState::Disabled:
        return m_DisabledStyle;
    case UIButtonState::Normal:
    default:
        return m_NormalStyle;
    }
}

} // namespace Hephaestus::HephaestusUI
