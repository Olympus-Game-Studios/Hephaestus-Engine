#pragma once

#include <string>
#include <unordered_map>

namespace Hephaestus::HephaestusUI {

class UIFont {
public:
    UIFont() = default;
    UIFont(std::string familyName, float pointSize, bool bold = false, bool italic = false)
        : m_FamilyName(std::move(familyName)), m_PointSize(pointSize), m_Bold(bold), m_Italic(italic) {}

    [[nodiscard]] const std::string& GetFamilyName() const { return m_FamilyName; }
    [[nodiscard]] float GetPointSize() const { return m_PointSize; }
    [[nodiscard]] bool IsBold() const { return m_Bold; }
    [[nodiscard]] bool IsItalic() const { return m_Italic; }

    void SetFamilyName(std::string familyName) { m_FamilyName = std::move(familyName); }
    void SetPointSize(float pointSize) { m_PointSize = pointSize; }
    void SetBold(bool bold) { m_Bold = bold; }
    void SetItalic(bool italic) { m_Italic = italic; }

private:
    std::string m_FamilyName = "Default";
    float m_PointSize = 16.0f;
    bool m_Bold = false;
    bool m_Italic = false;
};

class UIFontRegistry {
public:
    void RegisterFont(const std::string& key, const UIFont& font);
    [[nodiscard]] const UIFont* FindFont(const std::string& key) const;
    [[nodiscard]] const UIFont& GetOrFallback(const std::string& key) const;

private:
    std::unordered_map<std::string, UIFont> m_Fonts;
    UIFont m_Fallback = UIFont{"Default", 16.0f, false, false};
};

} // namespace Hephaestus::HephaestusUI
