#include "UIContext.h"

#include <algorithm>

namespace Hephaestus::HephaestusUI {

void UIContext::SetUserScale(float scale) {
    m_UserScale = std::clamp(scale, 0.5f, 2.0f);
}

float UIContext::GetScale() const {
    const float resolution = m_ReferenceHeight > 0.0f && m_ViewportSize.y > 0.0f ? m_ViewportSize.y / m_ReferenceHeight : 1.0f;
    return resolution * m_UserScale;
}

UIVec2 UIContext::GetLayoutSize() const {
    const float scale = GetScale();
    return {m_ViewportSize.x / scale, m_ViewportSize.y / scale};
}

void UIContext::QueueMouseMove(const UIVec2& position) {
    const float scale = GetScale();
    m_MouseEvents.push_back({.type = UIMouseEventType::Move, .position = {position.x / scale, position.y / scale},
                             .button = 0, .tag = 0});
}

void UIContext::QueueMousePress(const UIVec2& position, int button) {
    const float scale = GetScale();
    QueueMousePress({position.x / scale, position.y / scale}, button, 0);
}

void UIContext::QueueMouseRelease(const UIVec2& position, int button) {
    const float scale = GetScale();
    QueueMouseRelease({position.x / scale, position.y / scale}, button, 0);
}

void UIContext::QueueMousePress(const UIVec2& position, int button, std::uint64_t tag) {
    m_MouseEvents.push_back({.type = UIMouseEventType::Press, .position = position, .button = button, .tag = tag});
}

void UIContext::QueueMouseRelease(const UIVec2& position, int button, std::uint64_t tag) {
    m_MouseEvents.push_back({.type = UIMouseEventType::Release, .position = position, .button = button, .tag = tag});
}

std::uint64_t UIContext::QueueSyntheticClick(const UIVec2& position, int button) {
    const std::uint64_t tag = m_NextSyntheticTag++;
    if (m_NextSyntheticTag == 0) m_NextSyntheticTag = 1;
    QueueMousePress(position, button, tag);
    QueueMouseRelease(position, button, tag);
    return tag;
}

void UIContext::CancelTaggedMouseEvents(std::uint64_t tag) {
    if (tag == 0) return;
    std::erase_if(m_MouseEvents, [tag](const UIMouseEvent& event) { return event.tag == tag; });
}

std::vector<UIMouseEvent> UIContext::ConsumeMouseEvents() {
    std::vector<UIMouseEvent> events;
    events.swap(m_MouseEvents);
    return events;
}

} // namespace Hephaestus::HephaestusUI
