#pragma once

#include "UIElement.h"
#include "UIFont.h"

#include <string>

namespace Hephaestus::HephaestusUI {

class UIText : public UIElement {
public:
    UIText() = default;
    explicit UIText(std::string text) : m_Text(std::move(text)) {}

    void SetText(std::string text) { m_Text = std::move(text); }
    void SetFont(const UIFont& font) { m_Font = font; }
    void SetAlignment(UITextAlignment alignment) { m_Alignment = alignment; }
    void SetWordWrap(bool enabled) { m_WordWrap = enabled; }
    void SetColor(const UIColor& color) { m_OverrideColor = color; m_UseOverrideColor = true; }
    void ClearColorOverride() { m_UseOverrideColor = false; }
    void SetLineSpacing(float spacing) { m_LineSpacing = spacing; }
    void SetShadow(bool enabled) { m_Shadow = enabled; }
    void SetShadowColor(const UIColor& color) { m_ShadowColor = color; }
    void SetShadowOffset(const UIVec2& offset) { m_ShadowOffset = offset; }

    [[nodiscard]] const std::string& GetText() const { return m_Text; }
    [[nodiscard]] const UIFont& GetFont() const { return m_Font; }
    [[nodiscard]] UITextAlignment GetAlignment() const { return m_Alignment; }

protected:
    void OnEmitDrawCommands(UIRenderer& renderer) const override;

private:
    std::string m_Text;
    UIFont m_Font;
    UITextAlignment m_Alignment = UITextAlignment::Left;
    bool m_WordWrap = false;
    UIColor m_OverrideColor = UIColor::White();
    bool m_UseOverrideColor = false;
    float m_LineSpacing = 0.0f;
    bool m_Shadow = false;
    UIColor m_ShadowColor = {0.0f, 0.0f, 0.0f, 0.5f};
    UIVec2 m_ShadowOffset = {1.0f, 1.0f};
};

} // namespace Hephaestus::HephaestusUI
