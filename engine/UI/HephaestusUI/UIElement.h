#pragma once

#include "UIStyle.h"

#include <memory>
#include <vector>

namespace Hephaestus::HephaestusUI {

class UIRenderer;

class UIElement {
public:
    UIElement() = default;
    virtual ~UIElement() = default;

    UIElement(const UIElement&) = delete;
    UIElement& operator=(const UIElement&) = delete;

    UIElement* AddChild(std::unique_ptr<UIElement> child);
    bool RemoveChild(UIElement* child);

    void SetPosition(const UIVec2& position) { m_Position = position; }
    void SetSize(const UIVec2& size) { m_Size = size; }
    void SetVisible(bool visible) { m_Visible = visible; }
    void SetOpacity(float opacity) { m_Opacity = opacity; }
    void SetPadding(const UISpacing& padding) { m_Padding = padding; }
    void SetMargin(const UISpacing& margin) { m_Margin = margin; }
    void SetAnchor(const UIAnchor& anchor) { m_Anchor = anchor; }
    void SetZOrder(int zOrder) { m_ZOrder = zOrder; }
    void SetClippingEnabled(bool clippingEnabled) { m_ClippingEnabled = clippingEnabled; }
    void SetInteractable(bool interactable) { m_Interactable = interactable; }
    void SetStyle(const UIStyle* style) { m_Style = style; }

    [[nodiscard]] const UIVec2& GetPosition() const { return m_Position; }
    [[nodiscard]] const UIVec2& GetSize() const { return m_Size; }
    [[nodiscard]] bool IsVisible() const { return m_Visible; }
    [[nodiscard]] float GetOpacity() const { return m_Opacity; }
    [[nodiscard]] const UISpacing& GetPadding() const { return m_Padding; }
    [[nodiscard]] const UISpacing& GetMargin() const { return m_Margin; }
    [[nodiscard]] const UIAnchor& GetAnchor() const { return m_Anchor; }
    [[nodiscard]] int GetZOrder() const { return m_ZOrder; }
    [[nodiscard]] bool IsClippingEnabled() const { return m_ClippingEnabled; }
    [[nodiscard]] bool IsInteractable() const { return m_Interactable; }
    [[nodiscard]] UIElement* GetParent() const { return m_Parent; }
    [[nodiscard]] const UIRect& GetLayoutRect() const { return m_LayoutRect; }
    [[nodiscard]] UIRect GetContentRect() const;

    [[nodiscard]] const std::vector<std::unique_ptr<UIElement>>& GetChildren() const { return m_Children; }
    [[nodiscard]] std::vector<std::unique_ptr<UIElement>>& GetChildren() { return m_Children; }

    virtual void UpdateLayout();
    virtual void Draw(UIRenderer& renderer) const;

    [[nodiscard]] virtual UIElement* HitTest(const UIVec2& point);

    virtual void OnHoverEnter() {}
    virtual void OnHoverExit() {}
    virtual void OnPressed() {}
    virtual void OnReleased() {}
    virtual void OnClicked() {}

protected:
    [[nodiscard]] const UIStyle* ResolveStyle() const;
    [[nodiscard]] float ResolveOpacity() const;
    [[nodiscard]] std::vector<const UIElement*> GetChildrenSortedByZ() const;
    [[nodiscard]] std::vector<UIElement*> GetChildrenSortedByZ();

    virtual void OnUpdateLayout() {}
    virtual void OnEmitDrawCommands(UIRenderer& renderer) const;

private:
    UIElement* m_Parent = nullptr;
    std::vector<std::unique_ptr<UIElement>> m_Children;

    UIVec2 m_Position = {0.0f, 0.0f};
    UIVec2 m_Size = {0.0f, 0.0f};
    bool m_Visible = true;
    float m_Opacity = 1.0f;
    UISpacing m_Padding = {};
    UISpacing m_Margin = {};
    UIAnchor m_Anchor = {};
    int m_ZOrder = 0;
    bool m_ClippingEnabled = false;
    bool m_Interactable = true;

    const UIStyle* m_Style = nullptr;
    UIRect m_LayoutRect = {};
};

} // namespace Hephaestus::HephaestusUI
