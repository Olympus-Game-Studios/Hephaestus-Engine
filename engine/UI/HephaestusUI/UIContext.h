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

/// Game UI is designed at this viewport height and scaled to the real one, in Editor Play and packaged games.
inline constexpr float kGameUIReferenceHeight = 1080.0f;

class UIContext {
public:
    /// The viewport in pixels. Layout happens in GetLayoutSize() units and is drawn GetScale() pixels per unit.
    void SetViewportSize(const UIVec2& viewportSize) { m_ViewportSize = viewportSize; }
    /// With a reference height, the UI keeps its proportions at any resolution: the viewport is always that many
    /// layout units tall. 0 (the default) lays out in viewport pixels.
    void SetReferenceHeight(float height) { m_ReferenceHeight = height > 0.0f ? height : 0.0f; }
    /// A player's size preference on top of the resolution scale, kept within 0.5 to 2.
    void SetUserScale(float scale);
    void SetDeltaTimeSeconds(float deltaTimeSeconds) { m_DeltaTimeSeconds = deltaTimeSeconds; }

    [[nodiscard]] const UIVec2& GetViewportSize() const { return m_ViewportSize; }
    [[nodiscard]] float GetReferenceHeight() const { return m_ReferenceHeight; }
    [[nodiscard]] float GetUserScale() const { return m_UserScale; }
    /// Viewport pixels per layout unit.
    [[nodiscard]] float GetScale() const;
    [[nodiscard]] UIVec2 GetLayoutSize() const;
    [[nodiscard]] float GetDeltaTimeSeconds() const { return m_DeltaTimeSeconds; }

    /// Player input in viewport pixels; it is queued in layout units.
    void QueueMouseMove(const UIVec2& position);
    void QueueMousePress(const UIVec2& position, int button);
    void QueueMouseRelease(const UIVec2& position, int button);
    void QueueMousePress(const UIVec2& position, int button, std::uint64_t tag);
    void QueueMouseRelease(const UIVec2& position, int button, std::uint64_t tag);

    /// Press and release sharing a new non-zero tag, at a point in layout units. Real input is never tagged.
    [[nodiscard]] std::uint64_t QueueSyntheticClick(const UIVec2& position, int button);

    /// Remove queued events with this tag. Tag 0 is ignored so player input stays.
    void CancelTaggedMouseEvents(std::uint64_t tag);
    void ClearQueuedMouseEvents() { m_MouseEvents.clear(); }

    [[nodiscard]] std::vector<UIMouseEvent> ConsumeMouseEvents();

private:
    UIVec2 m_ViewportSize = {};
    float m_ReferenceHeight = 0.0f;
    float m_UserScale = 1.0f;
    float m_DeltaTimeSeconds = 0.0f;
    std::vector<UIMouseEvent> m_MouseEvents;
    std::uint64_t m_NextSyntheticTag = 1;
};

} // namespace Hephaestus::HephaestusUI
