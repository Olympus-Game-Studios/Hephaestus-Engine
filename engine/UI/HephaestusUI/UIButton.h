#pragma once

#include "UIElement.h"

#include <functional>
#include <string>

namespace Hephaestus::HephaestusUI {

class UIText;

enum class UIButtonState {
    Normal,
    Hovered,
    Pressed,
    Focused,
    Disabled
};

class UIButton : public UIElement {
public:
    explicit UIButton(const std::string& labelText = "Button");

    void SetText(const std::string& text);
    void SetOnClicked(std::function<void()> callback) { m_OnClicked = std::move(callback); }
    void SetOnHovered(std::function<void(bool)> callback) { m_OnHovered = std::move(callback); }

    void SetNormalStyle(const UIStyle& style) { m_NormalStyle = style; }
    void SetHoveredStyle(const UIStyle& style) { m_HoveredStyle = style; }
    void SetPressedStyle(const UIStyle& style) { m_PressedStyle = style; }
    void SetFocusedStyle(const UIStyle& style) { m_FocusedStyle = style; }
    void SetDisabledStyle(const UIStyle& style) { m_DisabledStyle = style; }

    [[nodiscard]] const UIStyle& GetNormalStyle() const { return m_NormalStyle; }
    [[nodiscard]] const UIStyle& GetHoveredStyle() const { return m_HoveredStyle; }
    [[nodiscard]] const UIStyle& GetPressedStyle() const { return m_PressedStyle; }
    [[nodiscard]] const UIStyle& GetFocusedStyle() const { return m_FocusedStyle; }
    [[nodiscard]] const UIStyle& GetDisabledStyle() const { return m_DisabledStyle; }

    [[nodiscard]] UIButtonState GetState() const { return m_State; }
    void SetFocused(bool focused);
    [[nodiscard]] bool IsFocused() const { return m_Focused; }

    void OnHoverEnter() override;
    void OnHoverExit() override;
    void OnPressed() override;
    void OnReleased() override;
    void OnClicked() override;

protected:
    void OnUpdateLayout() override;
    void OnEmitDrawCommands(UIRenderer& renderer) const override;

private:
    void UpdateVisualState();
    [[nodiscard]] const UIStyle& GetStateStyle() const;

    UIButtonState m_State = UIButtonState::Normal;
    bool m_Focused = false;
    UIStyle m_NormalStyle;
    UIStyle m_HoveredStyle;
    UIStyle m_PressedStyle;
    UIStyle m_FocusedStyle;
    UIStyle m_DisabledStyle;
    UIText* m_Label = nullptr;
    std::function<void()> m_OnClicked;
    std::function<void(bool)> m_OnHovered;
};

} // namespace Hephaestus::HephaestusUI
