#include "UIGridLayout.h"

#include <algorithm>
#include <vector>

namespace Hephaestus::HephaestusUI {

void UIGridLayout::OnUpdateLayout() {
    const UIRect contentRect = GetContentRect();
    const float totalHSpacing = m_HSpacing * static_cast<float>(std::max(0, m_Columns - 1));
    const float cellWidth = (m_Columns > 0)
        ? (contentRect.width - totalHSpacing) / static_cast<float>(m_Columns)
        : contentRect.width;

    std::vector<UIElement*> visibleChildren;
    for (const std::unique_ptr<UIElement>& childPtr : GetChildren()) {
        if (childPtr && childPtr->IsVisible()) {
            visibleChildren.push_back(childPtr.get());
        }
    }

    const int rows = visibleChildren.empty()
        ? 0
        : (static_cast<int>(visibleChildren.size()) + m_Columns - 1) / m_Columns;

    std::vector<float> rowHeights(static_cast<size_t>(rows), 0.0f);
    for (size_t i = 0; i < visibleChildren.size(); ++i) {
        const int row = static_cast<int>(i) / m_Columns;
        const float childHeight = visibleChildren[i]->GetSize().y;
        rowHeights[static_cast<size_t>(row)] = std::max(rowHeights[static_cast<size_t>(row)], childHeight);
    }

    float cursorY = 0.0f;
    for (size_t i = 0; i < visibleChildren.size(); ++i) {
        UIElement* child = visibleChildren[i];
        const int col = static_cast<int>(i) % m_Columns;
        const int row = static_cast<int>(i) / m_Columns;

        if (col == 0 && row > 0) {
            cursorY += rowHeights[static_cast<size_t>(row - 1)] + m_VSpacing;
        }

        const float cellX = static_cast<float>(col) * (cellWidth + m_HSpacing);
        const UIVec2 childSize = child->GetSize();

        float posX = cellX;
        float computedWidth = (childSize.x > 0.0f) ? childSize.x : cellWidth;
        switch (m_CellHAlign) {
        case UIHAlignment::Center:
            posX = cellX + (cellWidth - computedWidth) * 0.5f;
            break;
        case UIHAlignment::Right:
            posX = cellX + cellWidth - computedWidth;
            break;
        case UIHAlignment::Stretch:
            computedWidth = cellWidth;
            break;
        case UIHAlignment::Left:
        default:
            break;
        }

        float posY = cursorY;
        float computedHeight = childSize.y;
        const float rowHeight = rowHeights[static_cast<size_t>(row)];
        switch (m_CellVAlign) {
        case UIVAlignment::Center:
            if (computedHeight > 0.0f) {
                posY = cursorY + (rowHeight - computedHeight) * 0.5f;
            }
            break;
        case UIVAlignment::Bottom:
            if (computedHeight > 0.0f) {
                posY = cursorY + rowHeight - computedHeight;
            }
            break;
        case UIVAlignment::Stretch:
            computedHeight = rowHeight;
            break;
        case UIVAlignment::Top:
        default:
            break;
        }

        child->SetPosition({posX, posY});
        child->SetSize({computedWidth, computedHeight});
    }
}

} // namespace Hephaestus::HephaestusUI
