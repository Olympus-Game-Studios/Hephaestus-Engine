#include "Gfx/ProbeCascadeGrid.h"

#include <iostream>
#include <set>

using namespace Hephaestus;

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

std::array<Cell, ProbeCascadeGrid::kCascades> MinCells(const ProbeCascadeGrid& grid, const Vec3& camera) {
    std::array<Cell, ProbeCascadeGrid::kCascades> cells{};
    for (int c = 0; c < ProbeCascadeGrid::kCascades; ++c) cells[c] = grid.MinCell(c, camera);
    return cells;
}

} // namespace

int main() {
    bool pass = true;
    ProbeCascadeGrid grid;
    grid.counts = {4, 2, 4};
    grid.baseSpacing = 0.5f;

    pass &= Check(grid.Spacing(0) == 0.5f && grid.Spacing(3) == 4.0f, "each cascade doubles the spacing");
    {
        const Cell a = grid.MinCell(0, Vec3(0.1f, 0.1f, 0.1f));
        const Cell b = grid.MinCell(0, Vec3(-0.1f, 0.1f, 0.1f));
        pass &= Check(a == Cell{-2, -1, -2}, "the camera's cell sits in the middle of the grid");
        pass &= Check(b[0] == a[0] - 1 && b[1] == a[1], "crossing zero moves the grid one cell, not two");
        pass &= Check(grid.MinCell(1, Vec3(0.9f, 0, 0)) == grid.MinCell(1, Vec3(0.1f, 0, 0)),
                      "the grid only moves when the camera leaves its cell");
    }
    {
        const Cell minCell{-7, 3, 5};
        std::set<int> slots;
        bool roundTrip = true;
        for (int slot = 0; slot < grid.ProbesPerCascade(); ++slot) {
            const Cell cell = grid.CellOfSlot(slot, minCell);
            for (int a = 0; a < 3; ++a) {
                roundTrip &= cell[a] >= minCell[a] && cell[a] < minCell[a] + grid.counts[a];
            }
            roundTrip &= grid.SlotOfCell(cell) == slot;
            slots.insert(grid.SlotOfCell(cell));
        }
        pass &= Check(roundTrip && static_cast<int>(slots.size()) == grid.ProbesPerCascade(),
                      "every slot holds one cell inside the grid, and that cell maps back to it");
        const Cell moved{-6, 3, 5};
        int kept = 0;
        for (int slot = 0; slot < grid.ProbesPerCascade(); ++slot) {
            kept += grid.CellOfSlot(slot, minCell) == grid.CellOfSlot(slot, moved) ? 1 : 0;
        }
        pass &= Check(kept == grid.ProbesPerCascade() - grid.counts[1] * grid.counts[2],
                      "moving one cell along x changes only one slab of slots");
    }
    {
        ProbeCascadeScheduler scheduler;
        scheduler.Reset(grid);
        scheduler.Move(MinCells(grid, Vec3(0, 0, 0)));
        const size_t total = static_cast<size_t>(grid.ProbesPerCascade()) * ProbeCascadeGrid::kCascades;
        pass &= Check(scheduler.PendingCount() == total, "every probe is new on the first frame");
        const std::vector<uint32_t> first = scheduler.Schedule(40);
        bool finestFirst = first.size() == 40;
        for (uint32_t job : first) finestFirst &= (job >> ProbeCascadeScheduler::kSlotBits) <= 1;
        pass &= Check(finestFirst && scheduler.PendingCount() == total - 40,
                      "new probes go first, finest cascade first, within the budget");
        while (scheduler.PendingCount() > 0) scheduler.Schedule(64);

        scheduler.Move(MinCells(grid, Vec3(0.6f, 0, 0)));
        pass &= Check(scheduler.PendingCount() == static_cast<size_t>(grid.counts[1] * grid.counts[2]),
                      "crossing one cascade-0 cell marks only that cascade's new slab");
        const std::vector<uint32_t> jobs = scheduler.Schedule(48);
        int cascade0 = 0;
        for (uint32_t job : jobs) cascade0 += (job >> ProbeCascadeScheduler::kSlotBits) == 0 ? 1 : 0;
        std::set<uint32_t> unique(jobs.begin(), jobs.end());
        pass &= Check(jobs.size() == 48 && unique.size() == jobs.size() && scheduler.PendingCount() == 0 &&
                          cascade0 >= 8 + 16,
                      "after the new slab, spare budget refreshes old probes, finer cascades more, none twice");
        const std::vector<uint32_t> big = scheduler.Schedule(1000);
        std::set<uint32_t> bigUnique(big.begin(), big.end());
        pass &= Check(bigUnique.size() == big.size() && big.size() <= total,
                      "a budget larger than a cascade still schedules each probe once");

        scheduler.Move(MinCells(grid, Vec3(1000, 0, 0)));
        pass &= Check(scheduler.PendingCount() == total, "a teleport makes every probe new");
    }

    if (pass) {
        std::cout << "ProbeCascadeTests passed\n";
    }
    return pass ? 0 : 1;
}
