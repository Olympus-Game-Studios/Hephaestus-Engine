#include "FPSLevelChecks.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& message)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
    g_failures += condition ? 0 : 1;
}

struct Box {
    float min[3];
    float max[3];
};

// A flat 120 x 40 m floor at y = 0 with boxes standing on it; walking is a straight line unless `cutOffX` splits it.
class FakeLevel final : public Hephaestus::Plugins::ILevelCheckRun {
public:
    std::vector<Box> boxes;
    std::map<std::string, std::string> values;
    std::string scriptPath = "plugin://com.hephaestus.fps/Scripts/FPS/TeamDeathmatch.lua";
    float cutOffX = 1e9f;
    Hephaestus::Plugins::LevelMetrics metrics;
    std::vector<Hephaestus::Plugins::LevelProblemDescriptor> problems;
    std::vector<std::string> messages;
    std::vector<std::string> checks;

    const Hephaestus::Plugins::LevelMetrics& Metrics() const override { return metrics; }
    bool Raycast(const float from[3], const float to[3], float hit[3]) const override
    {
        float best = 2.0f;
        for (const Box& box : boxes) {
            float t0 = 0.0f, t1 = 1.0f;
            bool miss = false;
            for (int axis = 0; axis < 3 && !miss; ++axis) {
                const float d = to[axis] - from[axis];
                if (std::fabs(d) < 1e-9f) {
                    miss = from[axis] < box.min[axis] || from[axis] > box.max[axis];
                    continue;
                }
                float a = (box.min[axis] - from[axis]) / d;
                float b = (box.max[axis] - from[axis]) / d;
                if (a > b) std::swap(a, b);
                t0 = std::max(t0, a);
                t1 = std::min(t1, b);
                miss = t0 > t1;
            }
            if (!miss) best = std::min(best, t0);
        }
        if (best > 1.0f) return false;
        for (int axis = 0; axis < 3; ++axis) hit[axis] = from[axis] + (to[axis] - from[axis]) * best;
        return true;
    }
    bool WalkDistance(const float from[3], const float to[3], float* metres) const override
    {
        if ((from[0] > cutOffX) != (to[0] > cutOffX)) return false;
        *metres = std::hypot(to[0] - from[0], to[2] - from[2]);
        return true;
    }
    std::uint32_t EntityCount() const override { return 2; }
    bool GetEntity(std::uint32_t index, Hephaestus::Plugins::LevelEntityInfo& out) const override
    {
        out.id = index == 0 ? 3 : 7;
        out.name = index == 0 ? "Sun" : "Match";
        out.script = index == 0 ? nullptr : scriptPath.c_str();
        return index < 2;
    }
    void Report(const Hephaestus::Plugins::LevelProblemDescriptor& problem) override
    {
        problems.push_back(problem);
        messages.push_back(problem.message);
        checks.push_back(problem.check ? problem.check : "");
    }
    bool GetScriptValue(std::uint64_t entityId, const char* name, char* out, std::size_t outSize) const override
    {
        const auto found = values.find(name);
        if (entityId != 7 || found == values.end() || found->second.size() + 1 > outSize) return false;
        std::memcpy(out, found->second.c_str(), found->second.size() + 1);
        return true;
    }
    bool ClosestWalkable(const float point[3], float out[3]) const override
    {
        if (std::fabs(point[0]) > 60.0f || std::fabs(point[2]) > 20.0f) return false;
        out[0] = point[0];
        out[1] = 0.0f;
        out[2] = point[2];
        return true;
    }

    int Count(const std::string& check) const
    {
        int n = 0;
        for (const std::string& c : checks) n += c == check ? 1 : 0;
        return n;
    }
    bool Has(const std::string& check, const std::string& text) const
    {
        for (size_t i = 0; i < checks.size(); ++i) {
            if (checks[i] == check && messages[i].find(text) != std::string::npos) return true;
        }
        return false;
    }
};

} // namespace

int main()
{
    const auto points = HephaestusFPS::ParseSpawnPoints("-8,0; -6.5,-10 ;3,4,5;bad;7,");
    Check(points.size() == 3 && points[1].x == -6.5f && points[1].z == -10.0f && points[2].z == 4.0f,
          "spawn points parse like FPS.Waves.parsePoints, skipping malformed entries");

    FakeLevel level;
    level.values["friendlySpawns"] = "-40,0;-10,18;200,0";
    level.values["enemySpawns"] = "40,0;10,18";
    level.values["minSpawnDistance"] = "25";
    level.boxes.push_back({{-1.0f, 0.0f, -20.0f}, {1.0f, 5.0f, 16.0f}});  // a wall with a gap at its +z end
    level.boxes.push_back({{-38.0f, 0.0f, -1.0f}, {-36.0f, 1.5f, 1.0f}});  // cover beside each far spawn
    level.boxes.push_back({{36.0f, 0.0f, -1.0f}, {38.0f, 1.5f, 1.0f}});
    HephaestusFPS::SpawnChecks::Run(level, nullptr);
    for (const std::string& message : level.messages) std::cout << "  " << message << '\n';

    Check(level.Count("fps.spawn-off-navmesh") == 1 && level.Has("fps.spawn-off-navmesh", "BLUE spawn (200, 0)"),
          "a spawn with no floor near it is an error, named with its team and position");
    Check(level.Count("fps.spawn-sees-enemy-spawn") == 2 && level.Has("fps.spawn-sees-enemy-spawn", "(-10, 18)") &&
              level.Has("fps.spawn-sees-enemy-spawn", "(10, 18)"),
          "the two spawns facing each other through the gap see each other; the wall hides the rest");
    Check(level.Count("fps.spawn-too-close") == 2 && level.Has("fps.spawn-too-close", "20 m walk"),
          "spawns a 20 m walk apart are under minSpawnDistance 25");
    Check(level.Count("fps.spawn-in-the-open") == 2 && !level.Has("fps.spawn-in-the-open", "(-40, 0)") &&
              !level.Has("fps.spawn-in-the-open", "(40, 0)"),
          "spawns with a box beside them have cover; the two by the gap don't");
    Check(level.Count("fps.spawn-cut-off") == 0 && level.problems.front().entityId == 7,
          "every spawn can walk to the other team, and problems point at the match entity");

    FakeLevel cut = level;
    cut.problems.clear();
    cut.messages.clear();
    cut.checks.clear();
    cut.cutOffX = 30.0f;
    cut.values["teamNames"] = "Alpha;Bravo";
    HephaestusFPS::SpawnChecks::Run(cut, nullptr);
    Check(cut.Count("fps.spawn-cut-off") == 1 && cut.Has("fps.spawn-cut-off", "Bravo spawn (40, 0)"),
          "a spawn that can't walk to the other team is reported with the script's team names");

    FakeLevel empty;
    empty.values["friendlySpawns"] = "-40,0";
    HephaestusFPS::SpawnChecks::Run(empty, nullptr);
    Check(empty.Count("fps.spawns-missing") == 1 && empty.Has("fps.spawns-missing", "RED"),
          "a team without spawn points is an error");

    FakeLevel noMatch;
    noMatch.scriptPath = "assets/scripts/Door.lua";
    noMatch.values["friendlySpawns"] = "200,0";
    HephaestusFPS::SpawnChecks::Run(noMatch, nullptr);
    Check(noMatch.problems.empty(), "levels without a team deathmatch script are left alone");

    if (g_failures > 0) {
        std::cerr << g_failures << " FPS level check(s) failed\n";
        return 1;
    }
    std::cout << "FPSLevelChecksTests passed\n";
    return 0;
}
