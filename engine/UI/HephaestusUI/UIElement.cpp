#include "UIElement.h"

#include "UIRenderer.h"

#include <algorithm>

namespace Hephaestus::HephaestusUI {

UIElement* UIElement::AddChild(std::unique_ptr<UIElement> child) {
    if (!child) {
        return nullptr;
    }

    child->m_Parent = this;
    UIElement* childRawPtr = child.get();
    m_Children.emplace_back(std::move(child));
    return childRawPtr;
}

bool UIElement::RemoveChild(UIElement* child) {
    if (!child) {
        return false;
    }

    const auto it = std::find_if(m_Children.begin(), m_Children.end(),
        [child](const std::unique_ptr<UIElement>& candidate) {
            return candidate.get() == child;
        });

    if (it == m_Children.end()) {
        return false;
    }

    (*it)->m_Parent = nullptr;
    m_Children.erase(it);
    return true;
}

UIRect UIElement::GetContentRect() const {
    return {
        m_LayoutRect.x + m_Padding.left,
        m_LayoutRect.y + m_Padding.top,
        std::max(0.0f, m_LayoutRect.width - (m_Padding.left + m_Padding.right)),
        std::max(0.0f, m_LayoutRect.height - (m_Padding.top + m_Padding.bottom))
    };
}

float UIElement::ApplyMinMaxWidth(float width) const {
    float result = std::max(0.0f, width);
    if (m_MinSize.x > 0.0f) {
        result = std::max(result, m_MinSize.x);
    }
    if (m_MaxSize.x > 0.0f) {
        result = std::min(result, m_MaxSize.x);
    }
    return result;
}

float UIElement::ApplyMinMaxHeight(float height) const {
    float result = std::max(0.0f, height);
    if (m_MinSize.y > 0.0f) {
        result = std::max(result, m_MinSize.y);
    }
    if (m_MaxSize.y > 0.0f) {
        result = std::min(result, m_MaxSize.y);
    }
    return result;
}

void UIElement::UpdateLayout() {
    if (m_Parent) {
        const UIRect parentContent = m_Parent->GetContentRect();
        float computedWidth = ApplyMinMaxWidth(m_Size.x);
        float computedHeight = ApplyMinMaxHeight(m_Size.y);

        float posX = m_Position.x + m_Margin.left;
        float posY = m_Position.y + m_Margin.top;

        const float availWidth = parentContent.width - (m_Margin.left + m_Margin.right);
        const float availHeight = parentContent.height - (m_Margin.top + m_Margin.bottom);

        switch (m_HAlignment) {
        case UIHAlignment::Center:
            posX = m_Position.x + (availWidth - computedWidth) * 0.5f;
            break;
        case UIHAlignment::Right:
            posX = m_Position.x + availWidth - computedWidth;
            break;
        case UIHAlignment::Stretch:
            posX = m_Margin.left;
            computedWidth = ApplyMinMaxWidth(availWidth);
            break;
        case UIHAlignment::Left:
        default:
            break;
        }

        switch (m_VAlignment) {
        case UIVAlignment::Center:
            posY = m_Position.y + (availHeight - computedHeight) * 0.5f;
            break;
        case UIVAlignment::Bottom:
            posY = m_Position.y + availHeight - computedHeight;
            break;
        case UIVAlignment::Stretch:
            posY = m_Margin.top;
            computedHeight = ApplyMinMaxHeight(availHeight);
            break;
        case UIVAlignment::Top:
        default:
            break;
        }

        switch (m_Anchor.preset) {
        case UIAnchorPreset::TopCenter:
            posX = (parentContent.width - computedWidth) * 0.5f;
            break;
        case UIAnchorPreset::TopRight:
            posX = parentContent.width - computedWidth - m_Margin.right;
            break;
        case UIAnchorPreset::CenterLeft:
            posY = (parentContent.height - computedHeight) * 0.5f;
            break;
        case UIAnchorPreset::Center:
            posX = (parentContent.width - computedWidth) * 0.5f;
            posY = (parentContent.height - computedHeight) * 0.5f;
            break;
        case UIAnchorPreset::CenterRight:
            posX = parentContent.width - computedWidth - m_Margin.right;
            posY = (parentContent.height - computedHeight) * 0.5f;
            break;
        case UIAnchorPreset::BottomLeft:
            posY = parentContent.height - computedHeight - m_Margin.bottom;
            break;
        case UIAnchorPreset::BottomCenter:
            posX = (parentContent.width - computedWidth) * 0.5f;
            posY = parentContent.height - computedHeight - m_Margin.bottom;
            break;
        case UIAnchorPreset::BottomRight:
            posX = parentContent.width - computedWidth - m_Margin.right;
            posY = parentContent.height - computedHeight - m_Margin.bottom;
            break;
        case UIAnchorPreset::Stretch:
            posX = m_Margin.left;
            posY = m_Margin.top;
            computedWidth = ApplyMinMaxWidth(parentContent.width - m_Margin.left - m_Margin.right);
            computedHeight = ApplyMinMaxHeight(parentContent.height - m_Margin.top - m_Margin.bottom);
            break;
        case UIAnchorPreset::TopLeft:
        default:
            break;
        }

        m_LayoutRect.x = parentContent.x + posX;
        m_LayoutRect.y = parentContent.y + posY;
        m_LayoutRect.width = computedWidth;
        m_LayoutRect.height = computedHeight;
    } else {
        m_LayoutRect.x = m_Position.x;
        m_LayoutRect.y = m_Position.y;
        m_LayoutRect.width = ApplyMinMaxWidth(m_Size.x);
        m_LayoutRect.height = ApplyMinMaxHeight(m_Size.y);
    }

    OnUpdateLayout();

    for (const std::unique_ptr<UIElement>& child : m_Children) {
        child->UpdateLayout();
    }
}

void UIElement::Draw(UIRenderer& renderer) const {
    if (!m_Visible || m_Opacity <= 0.0f) {
        return;
    }

    if (m_ClippingEnabled) {
        renderer.PushClipRect(m_LayoutRect);
    }

    OnEmitDrawCommands(renderer);

    const std::vector<const UIElement*> sortedChildren = GetChildrenSortedByZ();
    for (const UIElement* child : sortedChildren) {
        child->Draw(renderer);
    }

    if (m_ClippingEnabled) {
        renderer.PopClipRect();
    }
}

UIElement* UIElement::HitTest(const UIVec2& point) {
    if (!m_Visible || !m_Enabled) {
        return nullptr;
    }

    const bool insideThis = m_LayoutRect.Contains(point);
    if (m_ClippingEnabled && !insideThis) {
        return nullptr;
    }

    std::vector<UIElement*> sortedChildren = GetChildrenSortedByZ();
    for (auto it = sortedChildren.rbegin(); it != sortedChildren.rend(); ++it) {
        if (UIElement* hit = (*it)->HitTest(point)) {
            return hit;
        }
    }

    if (insideThis && m_Interactable) {
        return this;
    }

    return nullptr;
}

const UIStyle* UIElement::ResolveStyle() const {
    if (m_UseOwnedStyle) {
        return &m_OwnedStyle;
    }
    return m_Style;
}

float UIElement::ResolveOpacity() const {
    if (!m_Parent) {
        return m_Opacity;
    }
    return m_Opacity * m_Parent->ResolveOpacity();
}

std::vector<const UIElement*> UIElement::GetChildrenSortedByZ() const {
    std::vector<const UIElement*> sorted;
    sorted.reserve(m_Children.size());

    for (const std::unique_ptr<UIElement>& child : m_Children) {
        sorted.push_back(child.get());
    }

    std::stable_sort(sorted.begin(), sorted.end(),
        [](const UIElement* left, const UIElement* right) {
            return left->GetZOrder() < right->GetZOrder();
        });

    return sorted;
}

std::vector<UIElement*> UIElement::GetChildrenSortedByZ() {
    std::vector<UIElement*> sorted;
    sorted.reserve(m_Children.size());

    for (const std::unique_ptr<UIElement>& child : m_Children) {
        sorted.push_back(child.get());
    }

    std::stable_sort(sorted.begin(), sorted.end(),
        [](const UIElement* left, const UIElement* right) {
            return left->GetZOrder() < right->GetZOrder();
        });

    return sorted;
}

void UIElement::OnEmitDrawCommands(UIRenderer&) const {
}

} // namespace Hephaestus::HephaestusUI
