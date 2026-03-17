#include "UIContext.h"

namespace Hephaestus::HephaestusUI {

void UIContext::QueueMouseMove(const UIVec2& position) {
    m_MouseEvents.push_back({.type = UIMouseEventType::Move, .position = position, .button = 0});
}

void UIContext::QueueMousePress(const UIVec2& position, int button) {
    m_MouseEvents.push_back({.type = UIMouseEventType::Press, .position = position, .button = button});
}

void UIContext::QueueMouseRelease(const UIVec2& position, int button) {
    m_MouseEvents.push_back({.type = UIMouseEventType::Release, .position = position, .button = button});
}

std::vector<UIMouseEvent> UIContext::ConsumeMouseEvents() {
    std::vector<UIMouseEvent> events;
    events.swap(m_MouseEvents);
    return events;
}

} // namespace Hephaestus::HephaestusUI
