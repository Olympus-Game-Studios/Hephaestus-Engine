#include "UISystem.h"

#include "UIContext.h"
#include "UIRenderer.h"
#include "UIScreen.h"

#include <algorithm>

namespace Hephaestus::HephaestusUI {

void UISystem::AddScreen(const std::shared_ptr<UIScreen>& screen) {
    if (!screen) {
        return;
    }
    m_Screens.push_back(screen);
}

bool UISystem::RemoveScreen(const std::shared_ptr<UIScreen>& screen) {
    const auto it = std::find(m_Screens.begin(), m_Screens.end(), screen);
    if (it == m_Screens.end()) {
        return false;
    }
    m_Screens.erase(it);
    return true;
}

void UISystem::ClearScreens() {
    m_Screens.clear();
}

std::shared_ptr<UIScreen> UISystem::FindScreenByName(const std::string& name) const {
    for (const std::shared_ptr<UIScreen>& screen : m_Screens) {
        if (screen && screen->GetName() == name) {
            return screen;
        }
    }
    return nullptr;
}

void UISystem::Update(UIContext& context) {
    const std::vector<UIMouseEvent> mouseEvents = context.ConsumeMouseEvents();
    for (const std::shared_ptr<UIScreen>& screen : m_Screens) {
        if (!screen || !screen->IsActive()) {
            continue;
        }
        screen->SetViewportSize(context.GetViewportSize());
        screen->Update(context, mouseEvents);
    }
}

void UISystem::Render(UIRenderer& renderer) const {
    for (const std::shared_ptr<UIScreen>& screen : m_Screens) {
        if (!screen || !screen->IsVisible()) {
            continue;
        }
        screen->Draw(renderer);
    }
}

} // namespace Hephaestus::HephaestusUI
