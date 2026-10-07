#pragma once

#include "UIStyle.h"

#include <cstdint>
#include <vector>

namespace Hephaestus::HephaestusUI {

enum class UIMouseEventType {
    Move,
    Press,
    Release
};

struct UIMouseEvent {
    UIMouseEventType type = UIMouseEventType::Move;
    UIVec2 position = {};
    int button = 0;
    /// 0 is real player input. Non-zero tags identify one automation click.
    std::uint64_t tag = 0;
};

class UIContext {
public:
    void SetViewportSize(const UIVec2& viewportSize) { m_ViewportSize = viewportSize; }
    void SetDeltaTimeSeconds(float deltaTimeSeconds) { m_DeltaTimeSeconds = deltaTimeSeconds; }

    [[nodiscard]] const UIVec2& GetViewportSize() const { return m_ViewportSize; }
    [[nodiscard]] float GetDeltaTimeSeconds() const { return m_DeltaTimeSeconds; }

    void QueueMouseMove(const UIVec2& position);
    void QueueMousePress(const UIVec2& position, int button);
    void QueueMouseRelease(const UIVec2& position, int button);
    void QueueMousePress(const UIVec2& position, int button, std::uint64_t tag);
    void QueueMouseRelease(const UIVec2& position, int button, std::uint64_t tag);

    /// Press and release sharing a new non-zero tag. Real input is never tagged.
    [[nodiscard]] std::uint64_t QueueSyntheticClick(const UIVec2& position, int button);

    /// Remove queued events with this tag. Tag 0 is ignored so player input stays.
    void CancelTaggedMouseEvents(std::uint64_t tag);

    [[nodiscard]] std::vector<UIMouseEvent> ConsumeMouseEvents();

private:
    UIVec2 m_ViewportSize = {};
    float m_DeltaTimeSeconds = 0.0f;
    std::vector<UIMouseEvent> m_MouseEvents;
    std::uint64_t m_NextSyntheticTag = 1;
};

} // namespace Hephaestus::HephaestusUI
