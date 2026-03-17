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

void UIElement::UpdateLayout() {
    if (m_Parent) {
        const UIRect parentContent = m_Parent->GetContentRect();

        m_LayoutRect.x = parentContent.x + m_Position.x + m_Margin.left;
        m_LayoutRect.y = parentContent.y + m_Position.y + m_Margin.top;
        m_LayoutRect.width = std::max(0.0f, m_Size.x);
        m_LayoutRect.height = std::max(0.0f, m_Size.y);
    } else {
        m_LayoutRect.x = m_Position.x;
        m_LayoutRect.y = m_Position.y;
        m_LayoutRect.width = std::max(0.0f, m_Size.x);
        m_LayoutRect.height = std::max(0.0f, m_Size.y);
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
    if (!m_Visible) {
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
