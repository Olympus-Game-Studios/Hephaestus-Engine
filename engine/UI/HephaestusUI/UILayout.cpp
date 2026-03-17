#include "UILayout.h"

#include "UIElement.h"

#include <algorithm>

namespace Hephaestus::HephaestusUI {

void UILayout::ApplyVerticalLayout(UIElement& container, float spacing) {
    const UIRect contentRect = container.GetContentRect();
    float cursorY = 0.0f;

    for (const std::unique_ptr<UIElement>& childPtr : container.GetChildren()) {
        UIElement* child = childPtr.get();
        if (!child || !child->IsVisible()) {
            continue;
        }

        const UISpacing margin = child->GetMargin();
        const UIVec2 oldSize = child->GetSize();
        const float computedWidth = oldSize.x > 0.0f
            ? oldSize.x
            : std::max(0.0f, contentRect.width - (margin.left + margin.right));

        child->SetPosition({0.0f, cursorY});
        child->SetSize({computedWidth, oldSize.y});

        cursorY += margin.top + std::max(0.0f, oldSize.y) + margin.bottom + spacing;
    }
}

void UILayout::ApplyHorizontalLayout(UIElement& container, float spacing) {
    const UIRect contentRect = container.GetContentRect();
    float cursorX = 0.0f;

    for (const std::unique_ptr<UIElement>& childPtr : container.GetChildren()) {
        UIElement* child = childPtr.get();
        if (!child || !child->IsVisible()) {
            continue;
        }

        const UISpacing margin = child->GetMargin();
        const UIVec2 oldSize = child->GetSize();
        const float computedHeight = oldSize.y > 0.0f
            ? oldSize.y
            : std::max(0.0f, contentRect.height - (margin.top + margin.bottom));

        child->SetPosition({cursorX, 0.0f});
        child->SetSize({oldSize.x, computedHeight});

        cursorX += margin.left + std::max(0.0f, oldSize.x) + margin.right + spacing;
    }
}

} // namespace Hephaestus::HephaestusUI
