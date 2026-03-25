#include "UITheme.h"

namespace Hephaestus::HephaestusUI {

void UITheme::SetStyle(const std::string& name, const UIStyle& style) {
    m_Styles[name] = style;
}

const UIStyle* UITheme::GetStyle(const std::string& name) const {
    const auto it = m_Styles.find(name);
    if (it == m_Styles.end()) {
        return nullptr;
    }
    return &it->second;
}

bool UITheme::HasStyle(const std::string& name) const {
    return m_Styles.find(name) != m_Styles.end();
}

void UITheme::RemoveStyle(const std::string& name) {
    m_Styles.erase(name);
}

void UITheme::Clear() {
    m_Styles.clear();
}

UITheme UITheme::CreateDefaultTheme() {
    UITheme theme;

    UIStyle defaultButton;
    defaultButton.backgroundColor = {0.17f, 0.18f, 0.22f, 1.0f};
    defaultButton.borderColor = {0.35f, 0.37f, 0.42f, 1.0f};
    defaultButton.borderWidth = 1.0f;
    defaultButton.cornerRadius = 6.0f;
    defaultButton.textColor = UIColor::White();
    defaultButton.fontSize = 16.0f;
    defaultButton.textAlignment = UITextAlignment::Center;
    defaultButton.padding = UISpacing::Uniform(12.0f);
    theme.SetStyle("default_button", defaultButton);

    UIStyle primaryButton = defaultButton;
    primaryButton.backgroundColor = {0.15f, 0.38f, 0.85f, 1.0f};
    primaryButton.borderColor = {0.2f, 0.45f, 0.95f, 1.0f};
    primaryButton.cornerRadius = 8.0f;
    theme.SetStyle("primary_button", primaryButton);

    UIStyle dangerButton = defaultButton;
    dangerButton.backgroundColor = {0.75f, 0.15f, 0.15f, 1.0f};
    dangerButton.borderColor = {0.85f, 0.2f, 0.2f, 1.0f};
    theme.SetStyle("danger_button", dangerButton);

    UIStyle titleText;
    titleText.textColor = UIColor::White();
    titleText.fontSize = 36.0f;
    titleText.fontBold = true;
    titleText.textAlignment = UITextAlignment::Center;
    titleText.textShadow = true;
    titleText.textShadowColor = {0.0f, 0.0f, 0.0f, 0.4f};
    titleText.textShadowOffset = {2.0f, 2.0f};
    theme.SetStyle("title_text", titleText);

    UIStyle headingText;
    headingText.textColor = UIColor::White();
    headingText.fontSize = 24.0f;
    headingText.fontBold = true;
    headingText.textAlignment = UITextAlignment::Left;
    theme.SetStyle("heading_text", headingText);

    UIStyle bodyText;
    bodyText.textColor = {0.85f, 0.85f, 0.85f, 1.0f};
    bodyText.fontSize = 14.0f;
    bodyText.textAlignment = UITextAlignment::Left;
    theme.SetStyle("body_text", bodyText);

    UIStyle labelText;
    labelText.textColor = {0.7f, 0.7f, 0.7f, 1.0f};
    labelText.fontSize = 12.0f;
    labelText.textAlignment = UITextAlignment::Left;
    theme.SetStyle("label_text", labelText);

    UIStyle panelBackground;
    panelBackground.backgroundColor = {0.08f, 0.08f, 0.1f, 0.92f};
    panelBackground.borderColor = {0.25f, 0.25f, 0.3f, 0.8f};
    panelBackground.borderWidth = 1.0f;
    panelBackground.cornerRadius = 12.0f;
    panelBackground.padding = UISpacing::Uniform(24.0f);
    theme.SetStyle("panel_background", panelBackground);

    UIStyle menuContainer;
    menuContainer.backgroundColor = UIColor::Transparent();
    menuContainer.padding = {16.0f, 16.0f, 16.0f, 16.0f};
    theme.SetStyle("menu_container", menuContainer);

    UIStyle overlayBackground;
    overlayBackground.backgroundColor = {0.0f, 0.0f, 0.0f, 0.6f};
    theme.SetStyle("overlay_background", overlayBackground);

    return theme;
}

} // namespace Hephaestus::HephaestusUI
