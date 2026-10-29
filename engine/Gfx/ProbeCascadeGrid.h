#pragma once

#include "../Core/Math.h"

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace Hephaestus {

// Camera-following probe grids: each cascade is a fixed-size grid of probes snapped to its own spacing, stored
// in a ring so that a probe's slot is its world cell modulo the grid size. Pure CPU maths, no Vulkan.
using Cell = std::array<int, 3>;

struct ProbeCascadeGrid {
    static constexpr int kCascades = 4;
    Cell counts{24, 12, 24};
    float baseSpacing = 0.5f;  // cascade 0; each further cascade doubles it

    float Spacing(int cascade) const { return baseSpacing * static_cast<float>(1 << cascade); }
    int ProbesPerCascade() const { return counts[0] * counts[1] * counts[2]; }
    // The grid's lowest world cell, with the camera's cell in the middle.
    Cell MinCell(int cascade, const Vec3& camera) const;
    int SlotOfCell(const Cell& cell) const;
    // The world cell a slot holds while the grid starts at minCell.
    Cell CellOfSlot(int slot, const Cell& minCell) const;
};

// Picks which probes to retrace each frame: probes that just entered a grid first (finest cascade first),
// then every probe in turn, finer cascades more often.
class ProbeCascadeScheduler {
public:
    static constexpr int kSlotBits = 20;  // job = cascade << kSlotBits | slot

    void Reset(const ProbeCascadeGrid& grid);
    // New grid positions for this frame; probes whose cell changed become pending.
    void Move(const std::array<Cell, ProbeCascadeGrid::kCascades>& minCells);
    // Each probe appears at most once.
    std::vector<uint32_t> Schedule(uint32_t budget);
    size_t PendingCount() const;

private:
    ProbeCascadeGrid m_grid;
    bool m_placed = false;
    std::array<Cell, ProbeCascadeGrid::kCascades> m_minCells{};
    std::array<std::vector<uint8_t>, ProbeCascadeGrid::kCascades> m_isPending;
    std::array<std::deque<int>, ProbeCascadeGrid::kCascades> m_pending;
    std::array<int, ProbeCascadeGrid::kCascades> m_cursor{};
    std::array<std::vector<uint32_t>, ProbeCascadeGrid::kCascades> m_scheduledFrame;  // no probe twice in a frame
    uint32_t m_frame = 0;
};

} // namespace Hephaestus
