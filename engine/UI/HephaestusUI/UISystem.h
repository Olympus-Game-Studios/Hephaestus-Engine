#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Hephaestus::HephaestusUI {

class UIContext;
class UIRenderer;
class UIScreen;

class UISystem {
public:
    void AddScreen(const std::shared_ptr<UIScreen>& screen);
    bool RemoveScreen(const std::shared_ptr<UIScreen>& screen);
    void ClearScreens();
    [[nodiscard]] std::shared_ptr<UIScreen> FindScreenByName(const std::string& name) const;

    [[nodiscard]] const std::vector<std::shared_ptr<UIScreen>>& GetScreens() const { return m_Screens; }

    void Update(UIContext& context);
    void Render(UIRenderer& renderer) const;

private:
    std::vector<std::shared_ptr<UIScreen>> m_Screens;
};

} // namespace Hephaestus::HephaestusUI
