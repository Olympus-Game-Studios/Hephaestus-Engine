#include "LevelChecks.h"

#include "../Construction/ConstructionGeometry.h"
#include "../Construction/ConstructionShape.h"
#include "../Navigation/NavMesh.h"
#include "../Scene/CharacterControllerComponent.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Hephaestus::Analysis {

namespace {

constexpr float kGravity = 9.81f;
constexpr float kPi = 3.14159265f;
// Smaller navmesh areas are ledges and the tops of walls, not floors anyone means to reach.
constexpr float kMinFloorArea = 1.0f;

std::string Metres(float value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f m", value);
    return text;
}

std::string NameOf(Scene& scene, entt::entity entity)
{
    const auto* tag = scene.GetRegistry().try_get<TagComponent>(entity);
    return tag && !tag->tag.empty() ? tag->tag : std::string("A shape");
}

void CheckOpenings(Scene& scene, const TraversalMetrics& metrics, float cellSize, LevelCheckReport& report)
{
    using namespace Construction;
    entt::registry& registry = scene.GetRegistry();
    for (auto entity : registry.view<ConstructionShapeComponent>()) {
        const auto& opening = registry.get<ConstructionShapeComponent>(entity);
        if (opening.recipe != Recipe::Opening) continue;
        const auto& transform = registry.get<TransformComponent>(entity);
        const auto* host = transform.parent != entt::null && registry.valid(transform.parent)
            ? registry.try_get<ConstructionShapeComponent>(transform.parent) : nullptr;
        if (!host || !CanHostOpenings(*host)) continue;
        const float floorTop = HostFloorTop(*host);
        if (opening.opening.sill > floorTop + 0.05f) continue;  // a window, not a way through
        WallFrame frame;
        if (!GetHostWallFrame(*host, opening.opening.side, opening.opening.segment, frame)) continue;
        const Vec3 local = vadd(vadd(frame.center, vscale(frame.u, opening.opening.offset)),
                                vadd(Vec3(0.0f, opening.opening.sill + opening.opening.height * 0.5f, 0.0f),
                                     vscale(frame.normal, -frame.thickness * 0.5f)));
        const Vec3 position = Mat4::TransformPoint(scene.GetWorldTransform(Entity(transform.parent, &scene)), local);
        const float width = opening.opening.width;
        const float clear = opening.opening.height - std::max(0.0f, floorTop - opening.opening.sill);
        const std::string name = "The doorway in " + NameOf(scene, transform.parent);
        if (width < metrics.radius * 2.0f) {
            report.problems.push_back({"door-too-narrow", Severity::Error,
                                       name + " is " + Metres(width) + " wide; characters need " +
                                           Metres(metrics.radius * 2.0f) + " to get through.",
                                       position, entity});
        } else if (width < (metrics.radius + cellSize) * 2.0f) {
            report.problems.push_back({"door-too-narrow", Severity::Warning,
                                       name + " is " + Metres(width) + " wide: the player fits, but bots need " +
                                           Metres((metrics.radius + cellSize) * 2.0f) + " to path through it.",
                                       position, entity});
        }
        if (clear < metrics.height) {
            report.problems.push_back({"door-too-low", Severity::Error,
                                       name + " is " + Metres(clear) + " high; characters are " +
                                           Metres(metrics.height) + " tall.",
                                       position, entity});
        }
    }
}

void CheckStairsAndRamps(Scene& scene, const TraversalMetrics& metrics, LevelCheckReport& report)
{
    using namespace Construction;
    entt::registry& registry = scene.GetRegistry();
    for (auto entity : registry.view<ConstructionShapeComponent>()) {
        const auto& shape = registry.get<ConstructionShapeComponent>(entity);
        if (shape.operation != Operation::Add) continue;
        const Mat4 world = scene.GetWorldTransform(Entity(entity, &scene));
        const Vec3 position = Mat4::TransformPoint(world, Vec3(0.0f, shape.box.height * 0.5f, 0.0f));
        if (shape.recipe == Recipe::Stairs) {
            const float rise = shape.box.height / static_cast<float>(StairCount(shape));
            if (rise > metrics.stepHeight + 1e-3f) {
                report.problems.push_back({"step-too-high", Severity::Error,
                                           NameOf(scene, entity) + " has " + Metres(rise) + " steps; characters step up " +
                                               Metres(metrics.stepHeight) + ". Add steps or lower them.",
                                           position, entity});
            }
        } else if (shape.recipe == Recipe::Ramp && shape.box.depth > 0.0f) {
            const float degrees = std::atan2(shape.box.height, shape.box.depth) * 180.0f / kPi;
            if (degrees > metrics.maxSlopeDegrees + 0.05f) {
                char text[160];
                std::snprintf(text, sizeof(text), " slopes %.1f degrees; characters walk up to %.1f.", degrees,
                              metrics.maxSlopeDegrees);
                report.problems.push_back({"ramp-too-steep", Severity::Error, NameOf(scene, entity) + text, position,
                                           entity});
            }
        }
    }
}

} // namespace

const char* SeverityName(Severity severity)
{
    switch (severity) {
        case Severity::Error: return "error";
        case Severity::Warning: return "warning";
        case Severity::Info: return "info";
    }
    return "info";
}

TraversalMetrics MetricsForScene(Scene& scene)
{
    const Navigation::NavBuildSettings settings = Navigation::SettingsForScene(scene);
    TraversalMetrics metrics;
    metrics.height = settings.agentHeight;
    metrics.radius = settings.agentRadius;
    metrics.stepHeight = settings.agentMaxClimb;
    metrics.maxSlopeDegrees = settings.agentMaxSlopeDegrees;
    for (auto handle : scene.GetRegistry().view<CharacterControllerComponent>()) {
        const auto& controller = scene.GetRegistry().get<CharacterControllerComponent>(handle);
        if (!controller.enabled) continue;
        const float gravity = kGravity * std::max(controller.gravityScale, 0.01f);
        const float jump = controller.jumpSpeed * controller.jumpSpeed / (2.0f * gravity);
        metrics.jumpHeight = metrics.fromControllers ? std::min(metrics.jumpHeight, jump) : jump;
        metrics.fromControllers = true;
    }
    return metrics;
}

LevelCheckReport CheckLevel(Scene& scene, const std::filesystem::path& projectRoot)
{
    LevelCheckReport report;
    report.metrics = MetricsForScene(scene);
    Navigation::NavBuildSettings settings = Navigation::SettingsForScene(scene);
    CheckOpenings(scene, report.metrics, settings.cellSize, report);
    CheckStairsAndRamps(scene, report.metrics, report);

    std::vector<Vec3> starts;
    for (auto handle : scene.GetRegistry().view<CharacterControllerComponent>()) {
        if (!scene.GetRegistry().get<CharacterControllerComponent>(handle).enabled) continue;
        starts.push_back(Mat4::TransformPoint(scene.GetWorldTransform(Entity(handle, &scene)), Vec3(0.0f, 0.0f, 0.0f)));
    }
    if (starts.empty()) {
        report.problems.push_back({"no-characters", Severity::Info,
                                   "No character in the scene, so reachability was not checked and the default "
                                   "player size was used.",
                                   Vec3(0.0f, 0.0f, 0.0f), entt::null});
        return report;
    }
    Navigation::NavGeometry geometry = Navigation::CollectSceneGeometry(scene, nullptr, projectRoot);
    Navigation::AddVaultLinks(scene, settings, geometry);
    Navigation::NavMesh navMesh;
    std::string error;
    if (!navMesh.Build(geometry, settings, error)) {
        report.problems.push_back({"navmesh", Severity::Error, "The navmesh could not be built: " + error + ".",
                                   starts.front(), entt::null});
        return report;
    }
    report.navPolygons = navMesh.PolygonCount();
    // A character's origin is at its feet or its centre; search down far enough for either.
    const std::vector<Navigation::NavIsland> islands =
        navMesh.Islands(starts, Vec3(1.0f, report.metrics.height + 1.0f, 1.0f));
    report.islands = static_cast<int>(islands.size());
    for (const Navigation::NavIsland& island : islands) {
        if (island.reached || island.area < kMinFloorArea) continue;
        char text[200];
        std::snprintf(text, sizeof(text),
                      "%.1f square metres of floor at %.1f m height can't be walked to from where the characters "
                      "start.",
                      island.area, island.centre.y);
        report.problems.push_back({"unreachable-floor", Severity::Warning, text, island.centre, entt::null});
    }
    return report;
}

} // namespace Hephaestus::Analysis
