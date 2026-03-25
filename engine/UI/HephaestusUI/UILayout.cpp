#include "UILayout.h"

#include "UIElement.h"

#include <algorithm>

namespace Hephaestus::HephaestusUI {

void UILayout::ApplyVerticalLayout(UIElement& container, float spacing, UIHAlignment childHAlign) {
    const UIRect contentRect = container.GetContentRect();
    float cursorY = 0.0f;

    for (const std::unique_ptr<UIElement>& childPtr : container.GetChildren()) {
        UIElement* child = childPtr.get();
        if (!child || !child->IsVisible()) {
            continue;
        }

        const UISpacing margin = child->GetMargin();
        const UIVec2 oldSize = child->GetSize();

        float computedWidth = oldSize.x;
        const float availableWidth = std::max(0.0f, contentRect.width - (margin.left + margin.right));

        UIHAlignment effectiveHAlign = child->GetHAlignment();
        if (effectiveHAlign == UIHAlignment::Left && childHAlign != UIHAlignment::Stretch) {
            effectiveHAlign = childHAlign;
        }

        float posX = 0.0f;
        switch (effectiveHAlign) {
        case UIHAlignment::Center:
            if (computedWidth > 0.0f) {
                posX = (availableWidth - computedWidth) * 0.5f;
            } else {
                computedWidth = availableWidth;
            }
            break;
        case UIHAlignment::Right:
            if (computedWidth > 0.0f) {
                posX = availableWidth - computedWidth;
            } else {
                computedWidth = availableWidth;
            }
            break;
        case UIHAlignment::Stretch:
            computedWidth = availableWidth;
            break;
        case UIHAlignment::Left:
        default:
            if (computedWidth <= 0.0f) {
                computedWidth = availableWidth;
            }
            break;
        }

        child->SetPosition({posX, cursorY});
        child->SetSize({computedWidth, oldSize.y});

        cursorY += margin.top + std::max(0.0f, oldSize.y) + margin.bottom + spacing;
    }
}

void UILayout::ApplyHorizontalLayout(UIElement& container, float spacing, UIVAlignment childVAlign) {
    const UIRect contentRect = container.GetContentRect();
    float cursorX = 0.0f;

    for (const std::unique_ptr<UIElement>& childPtr : container.GetChildren()) {
        UIElement* child = childPtr.get();
        if (!child || !child->IsVisible()) {
            continue;
        }

        const UISpacing margin = child->GetMargin();
        const UIVec2 oldSize = child->GetSize();

        float computedHeight = oldSize.y;
        const float availableHeight = std::max(0.0f, contentRect.height - (margin.top + margin.bottom));

        UIVAlignment effectiveVAlign = child->GetVAlignment();
        if (effectiveVAlign == UIVAlignment::Top && childVAlign != UIVAlignment::Stretch) {
            effectiveVAlign = childVAlign;
        }

        float posY = 0.0f;
        switch (effectiveVAlign) {
        case UIVAlignment::Center:
            if (computedHeight > 0.0f) {
                posY = (availableHeight - computedHeight) * 0.5f;
            } else {
                computedHeight = availableHeight;
            }
            break;
        case UIVAlignment::Bottom:
            if (computedHeight > 0.0f) {
                posY = availableHeight - computedHeight;
            } else {
                computedHeight = availableHeight;
            }
            break;
        case UIVAlignment::Stretch:
            computedHeight = availableHeight;
            break;
        case UIVAlignment::Top:
        default:
            if (computedHeight <= 0.0f) {
                computedHeight = availableHeight;
            }
            break;
        }

        child->SetPosition({cursorX, posY});
        child->SetSize({oldSize.x, computedHeight});

        cursorX += margin.left + std::max(0.0f, oldSize.x) + margin.right + spacing;
    }
}

} // namespace Hephaestus::HephaestusUI
