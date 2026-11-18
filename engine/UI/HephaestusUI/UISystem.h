#pragma once

#include "UITheme.h"

#include <memory>
#include <string>
#include <vector>

namespace Hephaestus::HephaestusUI {

class UIContext;
class UIRenderer;
class UIScreen;

/// High-level cursor/input mode for switching between gameplay and UI interaction.
enum class CursorMode {
    Game, ///< Cursor hidden + captured; mouse routed to gameplay (camera look, etc.)
    UI    ///< Cursor visible + unlocked; mouse routed to UI widgets
};

class UISystem {
public:
    void AddScreen(const std::shared_ptr<UIScreen>& screen);
    bool RemoveScreen(const std::shared_ptr<UIScreen>& screen);
    void ClearScreens();
    [[nodiscard]] std::shared_ptr<UIScreen> FindScreenByName(const std::string& name) const;

    [[nodiscard]] const std::vector<std::shared_ptr<UIScreen>>& GetScreens() const { return m_Screens; }

    void Update(UIContext& context);
    void Render(UIRenderer& renderer) const;

    void SetCursorMode(CursorMode mode);
    [[nodiscard]] CursorMode GetCursorMode() const { return m_CursorMode; }

    /// Returns true when the mode has changed since last call to ConsumeCursorModeChange().
    [[nodiscard]] bool HasCursorModeChanged() const { return m_CursorModeDirty; }
    CursorMode ConsumeCursorModeChange();

    [[nodiscard]] UITheme& GetTheme() { return m_Theme; }
    [[nodiscard]] const UITheme& GetTheme() const { return m_Theme; }

private:
    std::vector<std::shared_ptr<UIScreen>> m_Screens;
    CursorMode m_CursorMode = CursorMode::Game;
    bool m_CursorModeDirty = false;
    UITheme m_Theme = UITheme::CreateDefaultTheme();
    float m_Scale = 1.0f; ///< from the context at the last Update, applied by Render
};

} // namespace Hephaestus::HephaestusUI
