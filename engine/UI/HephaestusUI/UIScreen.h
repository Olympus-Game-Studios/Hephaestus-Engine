#pragma once

#include "UIContext.h"
#include "UIInputRouter.h"

#include <memory>
#include <string>
#include <vector>

namespace Hephaestus::HephaestusUI {

class UICanvas;
class UIElement;
class UIRenderer;

class UIScreen {
public:
    explicit UIScreen(std::string name);

    [[nodiscard]] const std::string& GetName() const { return m_Name; }
    [[nodiscard]] bool IsActive() const { return m_Active; }
    [[nodiscard]] bool IsVisible() const { return m_Visible; }

    void SetActive(bool active) { m_Active = active; }
    void SetVisible(bool visible) { m_Visible = visible; }

    void SetViewportSize(const UIVec2& viewportSize);
    [[nodiscard]] const UIVec2& GetViewportSize() const { return m_ViewportSize; }

    [[nodiscard]] UICanvas* GetRootCanvas() const { return m_RootCanvas.get(); }

    void Update(const UIContext& context, const std::vector<UIMouseEvent>& mouseEvents);
    void Draw(UIRenderer& renderer) const;

    [[nodiscard]] UIElement* HitTest(const UIVec2& position);
    [[nodiscard]] UIInputRouter& GetInputRouter() { return m_InputRouter; }

private:
    std::string m_Name;
    bool m_Active = true;
    bool m_Visible = true;
    UIVec2 m_ViewportSize = {};

    std::unique_ptr<UICanvas> m_RootCanvas;
    UIInputRouter m_InputRouter;
};

} // namespace Hephaestus::HephaestusUI
