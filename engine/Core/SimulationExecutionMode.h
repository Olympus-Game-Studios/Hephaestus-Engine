#pragma once

namespace Hephaestus {

/**
 * Controls whether host wall-clock frames may advance gameplay simulation.
 * Hosts remain responsible for rendering and servicing their event loops when
 * realtime is disabled; explicitly requested simulation steps bypass this gate.
 */
class SimulationExecutionMode {
public:
    [[nodiscard]] bool IsRealtime() const noexcept { return m_realtime; }
    [[nodiscard]] bool ShouldAdvanceRealtime() const noexcept { return m_realtime; }
    void SetRealtime(bool realtime) noexcept { m_realtime = realtime; }
    void Reset() noexcept { m_realtime = true; }

private:
    bool m_realtime = true;
};

} // namespace Hephaestus
