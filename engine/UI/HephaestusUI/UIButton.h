#pragma once

#include "UIElement.h"

#include <functional>
#include <string>

namespace Hephaestus::HephaestusUI {

class UIText;

enum class UIButtonState {
    Normal,
    Hovered,
    Pressed
};

class UIButton : public UIElement {
public:
    explicit UIButton(const std::string& labelText = "Button");

    void SetText(const std::string& text);
    void SetOnClicked(std::function<void()> callback) { m_OnClicked = std::move(callback); }

    void SetNormalStyle(const UIStyle& style) { m_NormalStyle = style; }
    void SetHoveredStyle(const UIStyle& style) { m_HoveredStyle = style; }
    void SetPressedStyle(const UIStyle& style) { m_PressedStyle = style; }

    [[nodiscard]] UIButtonState GetState() const { return m_State; }

    void OnHoverEnter() override;
    void OnHoverExit() override;
    void OnPressed() override;
    void OnReleased() override;
    void OnClicked() override;

protected:
    void OnUpdateLayout() override;
    void OnEmitDrawCommands(UIRenderer& renderer) const override;

private:
    [[nodiscard]] const UIStyle& GetStateStyle() const;

    UIButtonState m_State = UIButtonState::Normal;
    UIStyle m_NormalStyle;
    UIStyle m_HoveredStyle;
    UIStyle m_PressedStyle;
    UIText* m_Label = nullptr;
    std::function<void()> m_OnClicked;
};

} // namespace Hephaestus::HephaestusUI
