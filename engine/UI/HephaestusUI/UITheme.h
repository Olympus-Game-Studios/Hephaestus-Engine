#pragma once

#include "UIStyle.h"

#include <string>
#include <unordered_map>

namespace Hephaestus::HephaestusUI {

class UITheme {
public:
    void SetStyle(const std::string& name, const UIStyle& style);
    [[nodiscard]] const UIStyle* GetStyle(const std::string& name) const;
    [[nodiscard]] bool HasStyle(const std::string& name) const;
    void RemoveStyle(const std::string& name);
    void Clear();

    [[nodiscard]] const std::unordered_map<std::string, UIStyle>& GetAllStyles() const { return m_Styles; }

    static UITheme CreateDefaultTheme();

private:
    std::unordered_map<std::string, UIStyle> m_Styles;
};

} // namespace Hephaestus::HephaestusUI
