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

    [[nodiscard]] const std::string& GetText() const { return m_Text; }

protected:
    void OnEmitDrawCommands(UIRenderer& renderer) const override;

private:
    std::string m_Text;
    UIFont m_Font;
    UITextAlignment m_Alignment = UITextAlignment::Left;
    bool m_WordWrap = false;
    UIColor m_OverrideColor = UIColor::White();
    bool m_UseOverrideColor = false;
};

} // namespace Hephaestus::HephaestusUI
