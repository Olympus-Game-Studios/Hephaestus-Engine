#pragma once

#include "../Core/Math.h"

#include <entt/entt.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class Scene;

namespace Hephaestus::Navigation {
class NavMesh;
struct NavGeometry;
}

namespace Hephaestus::Analysis {

// What the scene's characters can do, from their Character Controllers: the largest body, and the smallest step,
// slope and jump, so a level that passes works for every character.
struct TraversalMetrics {
    float height = 1.8f;
    float radius = 0.3f;
    float stepHeight = 0.3f;
    float maxSlopeDegrees = 45.0f;
    float jumpHeight = 0.0f;  // how high a standing jump lifts the feet
    // The character that jumps lowest: its take-off speed, walking speed, gravity and grace time after a ledge.
    float jumpSpeed = 5.5f;
    float walkSpeed = 5.0f;
    float gravity = 9.81f;
    float coyoteTime = 0.0f;
    bool fromControllers = false;
};

TraversalMetrics MetricsForScene(Scene& scene);
// How far a running jump at walking speed carries the character while landing `rise` metres higher (negative:
// lower); negative when it can't get that high.
float JumpReach(const TraversalMetrics& metrics, float rise);

enum class Severity { Error, Warning, Info };
const char* SeverityName(Severity severity);

struct LevelProblem {
    std::string check;  // door-too-narrow, door-too-low, step-too-high, ramp-too-steep, unreachable-floor, ...
    Severity severity = Severity::Warning;
    std::string message;
    Vec3 position;
    entt::entity entity = entt::null;  // the shape at fault, or null for a navmesh area
};

struct LevelCheckReport {
    TraversalMetrics metrics;
    std::vector<LevelProblem> problems;
    int navPolygons = 0;
    int islands = 0;
};

// The walking route between two points on the navmesh, as a character walks it.
struct WalkRoute {
    bool found = false;     // both points are near the navmesh
    bool complete = false;  // the route ends at `to`; otherwise it stops at the closest point it can reach
    std::vector<Vec3> corners;
    float length = 0.0f;    // along the route
    float straight = 0.0f;  // from start to end, as the crow flies
    float seconds = 0.0f;   // at walking speed
    int links = 0;          // vaults over cover on the way
};

// Builds the navmesh a level check uses: the scene's collision, sized to its characters, with vault links.
bool BuildLevelNavMesh(Scene& scene, const std::filesystem::path& projectRoot, Navigation::NavMesh& out,
                       std::string& error);
WalkRoute MeasureWalk(const Navigation::NavMesh& navMesh, const TraversalMetrics& metrics, const Vec3& from,
                      const Vec3& to);
// The same, building the level navmesh first; false with error when it can't be built.
bool MeasureLevelWalk(Scene& scene, const std::filesystem::path& projectRoot, const Vec3& from, const Vec3& to,
                      WalkRoute& out, std::string& error);

// What a check added by a plugin sees: the level's characters, collision and navmesh.
struct LevelCheckContext {
    Scene& scene;
    const TraversalMetrics& metrics;
    const Navigation::NavMesh& navMesh;
    const Navigation::NavGeometry& geometry;
};

// A check that runs after the built-in ones whenever the level is checked, such as a plugin's genre rules.
struct LevelCheck {
    std::string owner;  // who registered it, so it can be removed with UnregisterLevelChecks
    std::string id;     // unique; problems it reports without a check name get this one
    std::string name;
    std::function<void(const LevelCheckContext& context, std::vector<LevelProblem>& problems)> run;
};

bool RegisterLevelCheck(LevelCheck check, std::string& error);
void UnregisterLevelChecks(const std::string& owner);
std::vector<std::string> LevelCheckIds();

// The nearest point where the segment from `from` to `to` meets the level's collision, as the navmesh is built from.
bool RaycastLevel(const Navigation::NavGeometry& geometry, const Vec3& from, const Vec3& to, Vec3& hit);

// Checks the level against what its characters can do, then runs every registered check. Construction shapes must already be evaluated (their
// collision is what the navmesh is built from).
LevelCheckReport CheckLevel(Scene& scene, const std::filesystem::path& projectRoot = {});

} // namespace Hephaestus::Analysis
