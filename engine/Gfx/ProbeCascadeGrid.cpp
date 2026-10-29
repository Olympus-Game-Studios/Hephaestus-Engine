#include "ProbeCascadeGrid.h"

#include <algorithm>
#include <cmath>

namespace Hephaestus {

namespace {

int Wrap(int value, int count) {
    const int m = value % count;
    return m < 0 ? m + count : m;
}

} // namespace

Cell ProbeCascadeGrid::MinCell(int cascade, const Vec3& camera) const {
    const float spacing = Spacing(cascade);
    const float pos[3] = {camera.x, camera.y, camera.z};
    Cell cell{};
    for (int a = 0; a < 3; ++a) {
        cell[a] = static_cast<int>(std::floor(pos[a] / spacing)) - counts[a] / 2;
    }
    return cell;
}

int ProbeCascadeGrid::SlotOfCell(const Cell& cell) const {
    return Wrap(cell[0], counts[0]) + Wrap(cell[1], counts[1]) * counts[0] +
           Wrap(cell[2], counts[2]) * counts[0] * counts[1];
}

Cell ProbeCascadeGrid::CellOfSlot(int slot, const Cell& minCell) const {
    const int s[3] = {slot % counts[0], (slot / counts[0]) % counts[1], slot / (counts[0] * counts[1])};
    Cell cell{};
    for (int a = 0; a < 3; ++a) {
        cell[a] = minCell[a] + Wrap(s[a] - minCell[a], counts[a]);
    }
    return cell;
}

void ProbeCascadeScheduler::Reset(const ProbeCascadeGrid& grid) {
    m_grid = grid;
    m_placed = false;
    for (int c = 0; c < ProbeCascadeGrid::kCascades; ++c) {
        m_isPending[c].assign(static_cast<size_t>(grid.ProbesPerCascade()), 0);
        m_pending[c].clear();
        m_cursor[c] = 0;
        m_scheduledFrame[c].assign(static_cast<size_t>(grid.ProbesPerCascade()), 0);
    }
}

void ProbeCascadeScheduler::Move(const std::array<Cell, ProbeCascadeGrid::kCascades>& minCells) {
    const int probes = m_grid.ProbesPerCascade();
    for (int c = 0; c < ProbeCascadeGrid::kCascades; ++c) {
        if (m_placed && minCells[c] == m_minCells[c]) continue;
        for (int slot = 0; slot < probes; ++slot) {
            bool entered = !m_placed;
            if (!entered) {
                const Cell cell = m_grid.CellOfSlot(slot, minCells[c]);
                for (int a = 0; a < 3; ++a) {
                    entered |= cell[a] < m_minCells[c][a] || cell[a] >= m_minCells[c][a] + m_grid.counts[a];
                }
            }
            if (entered && !m_isPending[c][slot]) {
                m_isPending[c][slot] = 1;
                m_pending[c].push_back(slot);
            }
        }
    }
    m_minCells = minCells;
    m_placed = true;
}

std::vector<uint32_t> ProbeCascadeScheduler::Schedule(uint32_t budget) {
    std::vector<uint32_t> jobs;
    jobs.reserve(budget);
    ++m_frame;
    auto job = [](int cascade, int slot) {
        return static_cast<uint32_t>(cascade) << kSlotBits | static_cast<uint32_t>(slot);
    };
    for (int c = 0; c < ProbeCascadeGrid::kCascades && jobs.size() < budget; ++c) {
        while (!m_pending[c].empty() && jobs.size() < budget) {
            const int slot = m_pending[c].front();
            m_pending[c].pop_front();
            m_isPending[c][slot] = 0;
            m_scheduledFrame[c][slot] = m_frame;
            jobs.push_back(job(c, slot));
        }
    }
    // The rest is shared 4:3:2:1 from the finest cascade out.
    const uint32_t remaining = budget - static_cast<uint32_t>(jobs.size());
    const int probes = m_grid.ProbesPerCascade();
    const uint32_t shares[ProbeCascadeGrid::kCascades] = {4, 3, 2, 1};
    for (int c = 0; c < ProbeCascadeGrid::kCascades; ++c) {
        const uint32_t count = remaining * shares[c] / 10;
        uint32_t added = 0;
        for (int visited = 0; visited < probes && added < count; ++visited) {
            const int slot = m_cursor[c];
            m_cursor[c] = (m_cursor[c] + 1) % probes;
            if (m_scheduledFrame[c][slot] == m_frame) continue;
            m_scheduledFrame[c][slot] = m_frame;
            jobs.push_back(job(c, slot));
            ++added;
        }
    }
    return jobs;
}

size_t ProbeCascadeScheduler::PendingCount() const {
    size_t total = 0;
    for (const auto& queue : m_pending) total += queue.size();
    return total;
}

} // namespace Hephaestus
