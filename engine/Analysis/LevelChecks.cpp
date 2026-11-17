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

float Clamp01(float value)
{
    return std::min(1.0f, std::max(0.0f, value));
}

// The closest points of two edges seen from above, as fractions along each.
void ClosestOnEdges(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1, float& s, float& t)
{
    const float dx = p1.x - p0.x, dz = p1.z - p0.z;
    const float ex = q1.x - q0.x, ez = q1.z - q0.z;
    const float rx = p0.x - q0.x, rz = p0.z - q0.z;
    const float a = dx * dx + dz * dz, e = ex * ex + ez * ez, f = ex * rx + ez * rz;
    if (a < 1e-8f && e < 1e-8f) {
        s = t = 0.0f;
        return;
    }
    if (a < 1e-8f) {
        s = 0.0f;
        t = Clamp01(f / e);
        return;
    }
    const float c = dx * rx + dz * rz;
    if (e < 1e-8f) {
        t = 0.0f;
        s = Clamp01(-c / a);
        return;
    }
    const float b = dx * ex + dz * ez;
    const float denominator = a * e - b * b;
    s = denominator > 1e-8f ? Clamp01((b * f - c * e) / denominator) : 0.0f;
    t = (b * s + f) / e;
    if (t < 0.0f) {
        t = 0.0f;
        s = Clamp01(-c / a);
    } else if (t > 1.0f) {
        t = 1.0f;
        s = Clamp01((b - c) / a);
    }
}

Vec3 Lerp(const Vec3& a, const Vec3& b, float t)
{
    return vadd(a, vscale(vsub(b, a), t));
}

// The easiest jump between two islands' outlines. The navmesh stops a body radius short of each ledge and the body
// may overhang both, so the ledge-to-ledge gap is the outline gap less a body width.
struct Jump {
    bool found = false;     // the islands are within searchDistance of each other
    bool possible = false;
    float gap = 0.0f;       // seen from above
    float rise = 0.0f;      // landing height minus take-off height
    float reach = 0.0f;     // negative: too high to get onto
    float shortfall = 0.0f;
    Vec3 landing;
};

bool SegmentHitsTriangle(const Vec3& origin, const Vec3& direction, const Vec3& a, const Vec3& b, const Vec3& c)
{
    const Vec3 ab = vsub(b, a);
    const Vec3 ac = vsub(c, a);
    const Vec3 p = vcross(direction, ac);
    const float det = vdot(ab, p);
    if (std::fabs(det) < 1e-9f) return false;
    const float inverse = 1.0f / det;
    const Vec3 offset = vsub(origin, a);
    const float u = vdot(offset, p) * inverse;
    if (u < 0.0f || u > 1.0f) return false;
    const Vec3 q = vcross(offset, ab);
    const float v = vdot(direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = vdot(ac, q) * inverse;
    return t > 0.0f && t < 1.0f;
}

// Whether level geometry stands between take-off and landing at the height of the body's middle.
bool JumpBlocked(const Navigation::NavGeometry& geometry, const Vec3& from, const Vec3& to, float bodyHeight)
{
    const Vec3 lift(0.0f, bodyHeight * 0.5f, 0.0f);
    const Vec3 origin = vadd(from, lift);
    const Vec3 direction = vsub(vadd(to, lift), origin);
    const Vec3 low(std::min(origin.x, origin.x + direction.x), std::min(origin.y, origin.y + direction.y),
                   std::min(origin.z, origin.z + direction.z));
    const Vec3 high(std::max(origin.x, origin.x + direction.x), std::max(origin.y, origin.y + direction.y),
                    std::max(origin.z, origin.z + direction.z));
    const float* v = geometry.vertices.data();
    for (size_t i = 0; i + 2 < geometry.triangles.size(); i += 3) {
        const float* a = v + geometry.triangles[i] * 3;
        const float* b = v + geometry.triangles[i + 1] * 3;
        const float* c = v + geometry.triangles[i + 2] * 3;
        if (std::max({a[0], b[0], c[0]}) < low.x || std::min({a[0], b[0], c[0]}) > high.x ||
            std::max({a[1], b[1], c[1]}) < low.y || std::min({a[1], b[1], c[1]}) > high.y ||
            std::max({a[2], b[2], c[2]}) < low.z || std::min({a[2], b[2], c[2]}) > high.z) {
            continue;
        }
        if (SegmentHitsTriangle(origin, direction, Vec3(a[0], a[1], a[2]), Vec3(b[0], b[1], b[2]),
                                Vec3(c[0], c[1], c[2]))) {
            return true;
        }
    }
    return false;
}

Jump EasiestJump(const Navigation::NavIsland& from, const Navigation::NavIsland& to, const TraversalMetrics& metrics,
                 const Navigation::NavGeometry& geometry, float searchDistance)
{
    Jump best;
    if (from.min.x - to.max.x > searchDistance || to.min.x - from.max.x > searchDistance ||
        from.min.z - to.max.z > searchDistance || to.min.z - from.max.z > searchDistance) {
        return best;
    }
    struct Candidate {
        Jump jump;
        Vec3 takeOff;
    };
    std::vector<Candidate> candidates;
    for (const auto& [p0, p1] : from.edges) {
        for (const auto& [q0, q1] : to.edges) {
            float s = 0.0f, t = 0.0f;
            ClosestOnEdges(p0, p1, q0, q1, s, t);
            const Vec3 a = Lerp(p0, p1, s);
            const Vec3 b = Lerp(q0, q1, t);
            const float apart = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
            if (apart > searchDistance) continue;
            const float gap = std::max(0.0f, apart - 2.0f * metrics.radius);
            const float rise = b.y - a.y;
            const float reach = JumpReach(metrics, rise);
            // Out of reach upward ranks behind any gap, by how much height is missing.
            const float shortfall = reach < 0.0f ? searchDistance + rise - metrics.jumpHeight : gap - reach;
            candidates.push_back({{true, shortfall <= 0.0f, gap, rise, reach, shortfall, b}, a});
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& x, const Candidate& y) { return x.jump.shortfall < y.jump.shortfall; });
    // Walls between the islands rule a jump out; the first few clear candidates are enough to find the best.
    constexpr size_t kMaxTests = 64;
    for (size_t n = 0; n < candidates.size() && n < kMaxTests; ++n) {
        if (!JumpBlocked(geometry, candidates[n].takeOff, candidates[n].jump.landing, metrics.height)) {
            return candidates[n].jump;
        }
    }
    return best;
}

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

bool BuildNavMesh(Scene& scene, const std::filesystem::path& projectRoot, Navigation::NavGeometry& geometry,
                  Navigation::NavMesh& out, std::string& error)
{
    const Navigation::NavBuildSettings settings = Navigation::SettingsForScene(scene);
    geometry = Navigation::CollectSceneGeometry(scene, nullptr, projectRoot);
    Navigation::AddVaultLinks(scene, settings, geometry);
    return out.Build(geometry, settings, error);
}

} // namespace

bool BuildLevelNavMesh(Scene& scene, const std::filesystem::path& projectRoot, Navigation::NavMesh& out,
                       std::string& error)
{
    Navigation::NavGeometry geometry;
    return BuildNavMesh(scene, projectRoot, geometry, out, error);
}

WalkRoute MeasureWalk(const Navigation::NavMesh& navMesh, const TraversalMetrics& metrics, const Vec3& from,
                      const Vec3& to)
{
    WalkRoute route;
    route.straight = vlen(vsub(to, from));
    Vec3 start;
    Vec3 end;
    std::vector<unsigned char> linkStarts;
    if (!navMesh.ClosestPoint(from, start) || !navMesh.ClosestPoint(to, end) ||
        !navMesh.FindPath(from, to, route.corners, &linkStarts) || route.corners.empty()) {
        route.corners.clear();
        return route;
    }
    route.found = true;
    for (size_t n = 0; n + 1 < route.corners.size(); ++n) {
        route.length += vlen(vsub(route.corners[n + 1], route.corners[n]));
        route.links += n < linkStarts.size() && linkStarts[n] ? 1 : 0;
    }
    const Vec3 last = route.corners.back();
    route.complete = std::hypot(last.x - end.x, last.z - end.z) < 0.1f && std::fabs(last.y - end.y) < 0.5f;
    route.seconds = metrics.walkSpeed > 0.0f ? route.length / metrics.walkSpeed : 0.0f;
    return route;
}

bool MeasureLevelWalk(Scene& scene, const std::filesystem::path& projectRoot, const Vec3& from, const Vec3& to,
                      WalkRoute& out, std::string& error)
{
    Navigation::NavMesh navMesh;
    if (!BuildLevelNavMesh(scene, projectRoot, navMesh, error)) {
        return false;
    }
    out = MeasureWalk(navMesh, MetricsForScene(scene), from, to);
    return true;
}

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
        if (!metrics.fromControllers || jump < metrics.jumpHeight) {
            metrics.jumpHeight = jump;
            metrics.jumpSpeed = controller.jumpSpeed;
            metrics.walkSpeed = controller.walkSpeed;
            metrics.gravity = gravity;
            metrics.coyoteTime = controller.coyoteTime;
        }
        metrics.fromControllers = true;
    }
    return metrics;
}

float JumpReach(const TraversalMetrics& metrics, float rise)
{
    const float v = metrics.jumpSpeed;
    const float discriminant = v * v - 2.0f * metrics.gravity * rise;
    if (!(metrics.gravity > 0.0f) || discriminant < 0.0f) {
        return -1.0f;
    }
    const float airborne = (v + std::sqrt(discriminant)) / metrics.gravity;
    return metrics.walkSpeed * (airborne + metrics.coyoteTime);
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
    Navigation::NavGeometry geometry;
    Navigation::NavMesh navMesh;
    std::string error;
    if (!BuildNavMesh(scene, projectRoot, geometry, navMesh, error)) {
        report.problems.push_back({"navmesh", Severity::Error, "The navmesh could not be built: " + error + ".",
                                   starts.front(), entt::null});
        return report;
    }
    report.navPolygons = navMesh.PolygonCount();
    // A character's origin is at its feet or its centre; search down far enough for either.
    const std::vector<Navigation::NavIsland> islands =
        navMesh.Islands(starts, Vec3(1.0f, report.metrics.height + 1.0f, 1.0f));
    report.islands = static_cast<int>(islands.size());
    // Spread from the islands the characters stand on to every island a running jump lands on.
    const TraversalMetrics& m = report.metrics;
    const float searchDistance = std::max(0.0f, JumpReach(m, -m.height * 2.0f)) + 2.0f * m.radius + 1.0f;
    std::vector<bool> reached(islands.size());
    std::vector<bool> jumpedTo(islands.size());
    std::vector<size_t> open;
    for (size_t i = 0; i < islands.size(); ++i) {
        reached[i] = islands[i].reached;
        if (reached[i]) open.push_back(i);
    }
    while (!open.empty()) {
        const size_t from = open.back();
        open.pop_back();
        for (size_t to = 0; to < islands.size(); ++to) {
            if (reached[to]) continue;
            if (EasiestJump(islands[from], islands[to], m, geometry, searchDistance).possible) {
                reached[to] = true;
                jumpedTo[to] = true;
                open.push_back(to);
            }
        }
    }
    for (size_t i = 0; i < islands.size(); ++i) {
        const Navigation::NavIsland& island = islands[i];
        if (island.area < kMinFloorArea || (reached[i] && !jumpedTo[i])) continue;
        char text[260];
        if (jumpedTo[i]) {
            std::snprintf(text, sizeof(text),
                          "%.1f square metres of floor at %.1f m height can only be reached by jumping or dropping "
                          "down.",
                          island.area, island.centre.y);
            report.problems.push_back({"jump-only", Severity::Info, text, island.centre, entt::null});
            continue;
        }
        Jump nearest;
        for (size_t from = 0; from < islands.size(); ++from) {
            if (!reached[from]) continue;
            const Jump jump = EasiestJump(islands[from], island, m, geometry, searchDistance);
            if (jump.found && (!nearest.found || jump.shortfall < nearest.shortfall)) nearest = jump;
        }
        if (nearest.found && nearest.reach >= 0.0f) {
            std::snprintf(text, sizeof(text),
                          "The gap to %.1f square metres of floor at %.1f m height is %.2f m; a running jump at "
                          "walking speed clears %.2f m.",
                          island.area, island.centre.y, nearest.gap, nearest.reach);
            report.problems.push_back({"gap-too-wide", Severity::Warning, text, nearest.landing, entt::null});
        } else if (nearest.found) {
            std::snprintf(text, sizeof(text),
                          "%.1f square metres of floor is %.2f m above the nearest floor the characters reach; they "
                          "jump %.2f m.",
                          island.area, nearest.rise, m.jumpHeight);
            report.problems.push_back({"too-high-to-jump", Severity::Warning, text, nearest.landing, entt::null});
        } else {
            std::snprintf(text, sizeof(text),
                          "%.1f square metres of floor at %.1f m height can't be walked or jumped to from where the "
                          "characters start.",
                          island.area, island.centre.y);
            report.problems.push_back({"unreachable-floor", Severity::Warning, text, island.centre, entt::null});
        }
    }
    return report;
}

} // namespace Hephaestus::Analysis
