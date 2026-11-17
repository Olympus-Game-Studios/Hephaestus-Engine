#pragma once

#include "../Core/EditorDebugDraw.h"
#include "../Core/Math.h"

#include <string>
#include <vector>

namespace Hephaestus::Analysis {

// Something that happened during a bot playtest, recorded by game scripts through Lua's Playtest.record.
struct PlaytestEvent {
    std::string kind;  // "kill" (where the killer stood), "death" or "traffic" (a periodic position sample)
    Vec3 position;
    int team = 0;
    float time = 0.0f;  // simulated seconds since the playtest started
};

// The playtest in progress: game scripts check IsPlaytestRunning to play it as a bot match and record into it.
void BeginPlaytest();
bool IsPlaytestRunning();
void RecordPlaytestEvent(PlaytestEvent event);
// Advanced by the Lua engine's update, so event times are simulated seconds however fast the playtest runs.
void AdvancePlaytestClock(float seconds);
float PlaytestClock();
// Stops recording and returns everything recorded since BeginPlaytest.
std::vector<PlaytestEvent> EndPlaytest();

// Events of one kind binned on a horizontal grid.
struct HeatmapCell {
    int x = 0;
    int z = 0;
    int count = 0;
    float height = 0.0f;  // mean height of the events in the cell
};

struct Heatmap {
    std::string kind;
    float cellSize = 2.0f;
    int maxCount = 0;
    int total = 0;
    std::vector<HeatmapCell> cells;  // busiest first
};

Heatmap BuildHeatmap(const std::vector<PlaytestEvent>& events, const std::string& kind, float cellSize = 2.0f);
// The cells as filled squares just above the floor, coloured from blue (few) through yellow to red (most).
void AppendHeatmapLines(const Heatmap& heatmap, std::vector<EditorDebugLine>& lines);

} // namespace Hephaestus::Analysis
