#pragma once

#include "../Core/Math.h"

#include <entt/entt.hpp>

#include <filesystem>
#include <string>
#include <vector>

class Scene;

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
    float jumpSpeed = 0.0f;
    float walkSpeed = 0.0f;
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

// Checks the level against what its characters can do. Construction shapes must already be evaluated (their
// collision is what the navmesh is built from).
LevelCheckReport CheckLevel(Scene& scene, const std::filesystem::path& projectRoot = {});

} // namespace Hephaestus::Analysis
