#include "Playtest.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace Hephaestus::Analysis {

namespace {

struct Recorder {
    bool running = false;
    float clock = 0.0f;
    std::vector<PlaytestEvent> events;
};

Recorder& State()
{
    static Recorder recorder;
    return recorder;
}

Vec3 Ramp(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    if (t < 0.5f) {
        const float u = t * 2.0f;
        return Vec3(u, 0.35f + u * 0.55f, 1.0f - u);  // blue to yellow
    }
    const float u = (t - 0.5f) * 2.0f;
    return Vec3(1.0f, 0.9f - u * 0.75f, 0.0f);  // yellow to red
}

} // namespace

void BeginPlaytest()
{
    State() = Recorder{};
    State().running = true;
}

bool IsPlaytestRunning()
{
    return State().running;
}

void RecordPlaytestEvent(PlaytestEvent event)
{
    if (!State().running) {
        return;
    }
    event.time = State().clock;
    State().events.push_back(std::move(event));
}

void AdvancePlaytestClock(float seconds)
{
    if (State().running) {
        State().clock += seconds;
    }
}

float PlaytestClock()
{
    return State().clock;
}

std::vector<PlaytestEvent> EndPlaytest()
{
    std::vector<PlaytestEvent> events = std::move(State().events);
    State() = Recorder{};
    return events;
}

Heatmap BuildHeatmap(const std::vector<PlaytestEvent>& events, const std::string& kind, float cellSize)
{
    Heatmap heatmap;
    heatmap.kind = kind;
    heatmap.cellSize = cellSize;
    std::map<std::pair<int, int>, HeatmapCell> cells;
    for (const PlaytestEvent& event : events) {
        if (event.kind != kind) continue;
        const int x = static_cast<int>(std::floor(event.position.x / cellSize));
        const int z = static_cast<int>(std::floor(event.position.z / cellSize));
        HeatmapCell& cell = cells[{x, z}];
        cell.x = x;
        cell.z = z;
        cell.height += event.position.y;
        ++cell.count;
        ++heatmap.total;
    }
    for (auto& [key, cell] : cells) {
        cell.height /= static_cast<float>(cell.count);
        heatmap.maxCount = std::max(heatmap.maxCount, cell.count);
        heatmap.cells.push_back(cell);
    }
    std::sort(heatmap.cells.begin(), heatmap.cells.end(),
              [](const HeatmapCell& a, const HeatmapCell& b) { return a.count > b.count; });
    return heatmap;
}

void AppendHeatmapLines(const Heatmap& heatmap, std::vector<EditorDebugLine>& lines)
{
    if (heatmap.maxCount <= 0) {
        return;
    }
    const float size = heatmap.cellSize;
    const float inset = size * 0.08f;
    for (const HeatmapCell& cell : heatmap.cells) {
        // A square root keeps a few very busy cells from washing out the rest.
        const Vec3 colour = Ramp(std::sqrt(static_cast<float>(cell.count) / static_cast<float>(heatmap.maxCount)));
        const float y = cell.height + 0.08f;
        const float x0 = cell.x * size + inset;
        const float x1 = (cell.x + 1) * size - inset;
        const float z0 = cell.z * size + inset;
        const float z1 = (cell.z + 1) * size - inset;
        constexpr int kStripes = 6;
        for (int n = 0; n <= kStripes; ++n) {
            const float z = z0 + (z1 - z0) * static_cast<float>(n) / kStripes;
            lines.push_back({Vec3(x0, y, z), Vec3(x1, y, z), colour});
        }
        lines.push_back({Vec3(x0, y, z0), Vec3(x0, y, z1), colour});
        lines.push_back({Vec3(x1, y, z0), Vec3(x1, y, z1), colour});
    }
}

} // namespace Hephaestus::Analysis
