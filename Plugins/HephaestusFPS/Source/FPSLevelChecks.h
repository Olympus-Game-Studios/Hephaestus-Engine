#pragma once

#include "PluginAPI/PluginAPI.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace HephaestusFPS {

struct SpawnPoint {
    float x = 0.0f;
    float z = 0.0f;
    float ground[3] = {0.0f, 0.0f, 0.0f};
    bool onNavmesh = false;
};

// "x,z;x,z" (extra numbers per point, such as a height, are ignored), as FPS.Waves.parsePoints reads it.
inline std::vector<SpawnPoint> ParseSpawnPoints(const std::string& text)
{
    std::vector<SpawnPoint> points;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = std::min(text.find(';', start), text.size());
        const std::string item = text.substr(start, end - start);
        char* rest = nullptr;
        const float x = std::strtof(item.c_str(), &rest);
        if (rest != item.c_str()) {
            const char* comma = rest;
            while (*comma == ' ') ++comma;
            if (*comma == ',') {
                char* after = nullptr;
                const float z = std::strtof(comma + 1, &after);
                if (after != comma + 1) {
                    SpawnPoint point;
                    point.x = x;
                    point.z = z;
                    points.push_back(point);
                }
            }
        }
        start = end + 1;
    }
    return points;
}

// Spawn checks for every TeamDeathmatch script in the level: spawns off the navmesh, spawns that see the other
// team's spawns, spawns closer to the other team than the script's minSpawnDistance, teams that can't walk to each
// other, and spawns with nothing to hide behind.
class SpawnChecks {
public:
    static constexpr float kCoverRange = 8.0f;
    static constexpr int kCoverRays = 16;

    static void Run(Hephaestus::Plugins::ILevelCheckRun& run, void*)
    {
        for (std::uint32_t i = 0; i < run.EntityCount(); ++i) {
            Hephaestus::Plugins::LevelEntityInfo entity;
            if (!run.GetEntity(i, entity) || !entity.script) continue;
            const std::string script = entity.script;
            const std::string suffix = "TeamDeathmatch.lua";
            if (script.size() < suffix.size() || script.compare(script.size() - suffix.size(), suffix.size(), suffix) != 0) {
                continue;
            }
            CheckMatch(run, entity.id);
        }
    }

private:
    static std::string Value(const Hephaestus::Plugins::ILevelCheckRun& run, std::uint64_t id, const char* name,
                             const std::string& fallback)
    {
        char buffer[4096];
        return run.GetScriptValue(id, name, buffer, sizeof(buffer)) ? std::string(buffer) : fallback;
    }

    static std::string Metres(float value)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.0f m", value);
        return text;
    }

    static std::string Where(const SpawnPoint& point)
    {
        char text[48];
        std::snprintf(text, sizeof(text), "(%g, %g)", point.x, point.z);
        return text;
    }

    static void Report(Hephaestus::Plugins::ILevelCheckRun& run, const char* check,
                       Hephaestus::Plugins::LevelProblemSeverity severity, const std::string& message,
                       const float position[3], std::uint64_t entity)
    {
        Hephaestus::Plugins::LevelProblemDescriptor problem;
        problem.check = check;
        problem.severity = severity;
        problem.message = message.c_str();
        problem.position[0] = position[0];
        problem.position[1] = position[1];
        problem.position[2] = position[2];
        problem.entityId = entity;
        run.Report(problem);
    }

    static void CheckMatch(Hephaestus::Plugins::ILevelCheckRun& run, std::uint64_t id)
    {
        using Severity = Hephaestus::Plugins::LevelProblemSeverity;
        const Hephaestus::Plugins::LevelMetrics& metrics = run.Metrics();
        std::vector<SpawnPoint> teams[2] = {ParseSpawnPoints(Value(run, id, "friendlySpawns", "")),
                                            ParseSpawnPoints(Value(run, id, "enemySpawns", ""))};
        const float minDistance = std::strtof(Value(run, id, "minSpawnDistance", "15").c_str(), nullptr);
        std::string names[2] = {"Team 1", "Team 2"};
        {
            const std::string teamNames = Value(run, id, "teamNames", "BLUE;RED");
            const size_t split = teamNames.find(';');
            if (split != std::string::npos && split > 0 && split + 1 < teamNames.size()) {
                names[0] = teamNames.substr(0, split);
                names[1] = teamNames.substr(split + 1);
            }
        }
        for (int team = 0; team < 2; ++team) {
            if (teams[team].empty()) {
                const float origin[3] = {0.0f, 0.0f, 0.0f};
                Report(run, "fps.spawns-missing", Severity::Error,
                       names[team] + " has no spawn points (" + (team == 0 ? "friendlySpawns" : "enemySpawns") +
                           " on the team deathmatch script).",
                       origin, id);
            }
            for (SpawnPoint& point : teams[team]) {
                // Soldiers respawn at the walkable point nearest (x, 0, z), as the script finds it.
                const float probe[3] = {point.x, 0.0f, point.z};
                point.onNavmesh = run.ClosestWalkable(probe, point.ground);
                if (!point.onNavmesh) {
                    point.ground[0] = point.x;
                    point.ground[1] = 0.0f;
                    point.ground[2] = point.z;
                    Report(run, "fps.spawn-off-navmesh", Severity::Error,
                           names[team] + " spawn " + Where(point) +
                               " has no walkable floor within 4 m of the ground plane, so soldiers who respawn there "
                               "can't move.",
                           point.ground, id);
                }
            }
        }
        const float eye = metrics.height * 0.9f;
        for (int team = 0; team < 2; ++team) {
            for (const SpawnPoint& point : teams[team]) {
                if (!point.onNavmesh) continue;
                CheckSightOfEnemySpawn(run, id, point, teams[1 - team], names[team], names[1 - team], eye);
                CheckDistanceToEnemy(run, id, point, teams[1 - team], names[team], minDistance);
                CheckCover(run, id, point, names[team], metrics.height * 0.65f);
            }
        }
    }

    static void CheckSightOfEnemySpawn(Hephaestus::Plugins::ILevelCheckRun& run, std::uint64_t id,
                                       const SpawnPoint& point, const std::vector<SpawnPoint>& enemies,
                                       const std::string& team, const std::string& enemyTeam, float eye)
    {
        const float from[3] = {point.ground[0], point.ground[1] + eye, point.ground[2]};
        float nearest = -1.0f;
        const SpawnPoint* seen = nullptr;
        for (const SpawnPoint& enemy : enemies) {
            if (!enemy.onNavmesh) continue;
            const float to[3] = {enemy.ground[0], enemy.ground[1] + eye, enemy.ground[2]};
            float hit[3];
            if (run.Raycast(from, to, hit)) continue;
            const float distance = std::hypot(to[0] - from[0], to[2] - from[2]);
            if (!seen || distance < nearest) {
                nearest = distance;
                seen = &enemy;
            }
        }
        if (seen) {
            Report(run, "fps.spawn-sees-enemy-spawn", Hephaestus::Plugins::LevelProblemSeverity::Warning,
                   team + " spawn " + Where(point) + " has a clear line of sight to " + enemyTeam + " spawn " +
                       Where(*seen) + ", " + Metres(nearest) + " away, so soldiers can be shot as they respawn.",
                   point.ground, id);
        }
    }

    static void CheckDistanceToEnemy(Hephaestus::Plugins::ILevelCheckRun& run, std::uint64_t id,
                                     const SpawnPoint& point, const std::vector<SpawnPoint>& enemies,
                                     const std::string& team, float minDistance)
    {
        float nearest = -1.0f;
        bool anyEnemy = false;
        for (const SpawnPoint& enemy : enemies) {
            if (!enemy.onNavmesh) continue;
            anyEnemy = true;
            float metres = 0.0f;
            if (run.WalkDistance(point.ground, enemy.ground, &metres) && (nearest < 0.0f || metres < nearest)) {
                nearest = metres;
            }
        }
        if (!anyEnemy) return;
        if (nearest < 0.0f) {
            Report(run, "fps.spawn-cut-off", Hephaestus::Plugins::LevelProblemSeverity::Error,
                   team + " spawn " + Where(point) + " can't walk to any of the other team's spawns, so its soldiers "
                       "never reach the fight.",
                   point.ground, id);
        } else if (nearest < minDistance) {
            Report(run, "fps.spawn-too-close", Hephaestus::Plugins::LevelProblemSeverity::Warning,
                   team + " spawn " + Where(point) + " is a " + Metres(nearest) +
                       " walk from the other team's nearest spawn, under the script's minSpawnDistance of " +
                       Metres(minDistance) + ".",
                   point.ground, id);
        }
    }

    static void CheckCover(Hephaestus::Plugins::ILevelCheckRun& run, std::uint64_t id, const SpawnPoint& point,
                           const std::string& team, float chest)
    {
        const float from[3] = {point.ground[0], point.ground[1] + chest, point.ground[2]};
        int blocked = 0;
        for (int n = 0; n < kCoverRays; ++n) {
            const float angle = 6.2831853f * static_cast<float>(n) / static_cast<float>(kCoverRays);
            const float to[3] = {from[0] + std::cos(angle) * kCoverRange, from[1], from[2] + std::sin(angle) * kCoverRange};
            float hit[3];
            blocked += run.Raycast(from, to, hit) ? 1 : 0;
        }
        if (blocked < 2) {
            Report(run, "fps.spawn-in-the-open", Hephaestus::Plugins::LevelProblemSeverity::Warning,
                   team + " spawn " + Where(point) + " has nothing within " + Metres(kCoverRange) +
                       " at chest height to take cover behind.",
                   point.ground, id);
        }
    }
};

} // namespace HephaestusFPS
