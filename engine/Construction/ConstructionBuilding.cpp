#include "ConstructionBuilding.h"

#include "ConstructionGeometry.h"
#include "ConstructionShape.h"
#include "ConstructionSystem.h"

#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace Hephaestus::Construction {

namespace {

constexpr float kStepRun = 0.32f;  // tread depth of generated stairs: two navmesh cells for the default player
constexpr float kFlightWidth = 1.2f;

bool IsRoom(const ConstructionShapeComponent* shape)
{
    if (!shape || shape->source != SourceKind::Recipe || shape->operation != Operation::Add) {
        return false;
    }
    return (shape->recipe == Recipe::Box && shape->box.hollow) ||
           (shape->recipe == Recipe::Wall && shape->wall.closed && shape->box.floor && shape->wall.points.size() >= 3);
}

// The centre line of a room's walls in its own frame.
std::vector<WallPoint> RoomOutline(const ConstructionShapeComponent& shape)
{
    if (shape.recipe == Recipe::Wall) {
        return shape.wall.points;
    }
    const float hx = (shape.box.width - shape.box.wallThickness) * 0.5f;
    const float hz = (shape.box.depth - shape.box.wallThickness) * 0.5f;
    return {{-hx, -hz}, {hx, -hz}, {hx, hz}, {-hx, hz}};
}

// A rectangle on the floor, in the room's frame.
struct Rect {
    float x0, z0, x1, z1;
};

bool Overlap(const Rect& a, const Rect& b)
{
    return a.x0 < b.x1 - 0.01f && a.x1 > b.x0 + 0.01f && a.z0 < b.z1 - 0.01f && a.z1 > b.z0 + 0.01f;
}

// Whether a rectangle lies on the room's floor: its corners inside the walls' inside faces and no wall corner in it.
bool InsideRoom(const std::vector<WallPoint>& outline, float thickness, const Rect& r)
{
    const float half = thickness * 0.5f - 1e-3f;
    auto distanceToWalls = [&](float x, float z) {
        float best = 1e9f;
        for (size_t n = 0; n < outline.size(); ++n) {
            const WallPoint& a = outline[n];
            const WallPoint& b = outline[(n + 1) % outline.size()];
            const float dx = b.x - a.x, dz = b.z - a.z;
            const float l2 = dx * dx + dz * dz;
            const float u = l2 > 0.0f ? std::clamp(((x - a.x) * dx + (z - a.z) * dz) / l2, 0.0f, 1.0f) : 0.0f;
            best = std::min(best, std::hypot(x - (a.x + dx * u), z - (a.z + dz * u)));
        }
        return best;
    };
    for (const auto& [x, z] : {std::pair{r.x0, r.z0}, std::pair{r.x1, r.z0}, std::pair{r.x1, r.z1}, std::pair{r.x0, r.z1}}) {
        if (!PointInPolygon(outline, x, z) || distanceToWalls(x, z) < half) return false;
    }
    for (const WallPoint& p : outline) {
        if (p.x > r.x0 - half && p.x < r.x1 + half && p.z > r.z0 - half && p.z < r.z1 + half) return false;
    }
    return true;
}

} // namespace

entt::entity StoreyAbove(Scene& scene, entt::entity room)
{
    entt::registry& registry = scene.GetRegistry();
    const auto* shape = registry.valid(room) ? registry.try_get<ConstructionShapeComponent>(room) : nullptr;
    if (!IsRoom(shape)) {
        return entt::null;
    }
    for (entt::entity child : registry.get<TransformComponent>(room).children) {
        const auto* above = registry.try_get<ConstructionShapeComponent>(child);
        const auto& transform = registry.get<TransformComponent>(child);
        if (IsRoom(above) && std::fabs(transform.position.y - shape->box.height) < 1e-3f &&
            std::fabs(transform.position.x) < 1e-3f && std::fabs(transform.position.z) < 1e-3f) {
            return child;
        }
    }
    return entt::null;
}

entt::entity AddStorey(Scene& scene, entt::entity room, std::string& error)
{
    entt::registry& registry = scene.GetRegistry();
    auto* shape = registry.valid(room) ? registry.try_get<ConstructionShapeComponent>(room) : nullptr;
    if (!IsRoom(shape)) {
        error = "only rooms (hollow blocks, or walls closed into a room with a floor) can have a storey added";
        return entt::null;
    }
    if (StoreyAbove(scene, room) != entt::null) {
        error = "this room already has a storey above it";
        return entt::null;
    }
    ConstructionShapeComponent storey = *shape;
    storey.kitSwaps.clear();
    storey.box.floor = true;
    shape->box.ceiling = false;
    const auto* tag = registry.try_get<TagComponent>(room);
    Entity above = scene.CreateEntity((tag ? tag->tag : std::string("Room")) + " Above");
    scene.SetParent(above, Entity(room, &scene), false);
    auto& transform = above.GetComponent<TransformComponent>();
    transform.position = Vec3(0.0f, shape->box.height, 0.0f);
    transform.rotation = Vec3(0.0f, 0.0f, 0.0f);
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);
    above.AddComponent<ConstructionShapeComponent>(storey);
    if (const auto* material = registry.try_get<MaterialComponent>(room)) {
        above.AddComponent<MaterialComponent>(*material);
    } else {
        ConstructionSystem::AddBlockoutMaterial(scene, above);
    }
    return above;
}

entt::entity AddStairsUp(Scene& scene, entt::entity room, float maxRise, std::string& error)
{
    entt::registry& registry = scene.GetRegistry();
    const auto* shape = registry.valid(room) ? registry.try_get<ConstructionShapeComponent>(room) : nullptr;
    const entt::entity above = StoreyAbove(scene, room);
    if (!IsRoom(shape) || above == entt::null) {
        error = "add a storey above this room first";
        return entt::null;
    }
    const BoxParams box = shape->box;
    const float t = box.wallThickness;
    const float floorTop = box.floor ? t : 0.0f;
    // From this room's floor to the top of the storey's floor, which sits on this room's walls.
    const float rise = box.height + registry.get<ConstructionShapeComponent>(above).box.wallThickness - floorTop;
    const int steps = std::max(3, static_cast<int>(std::ceil(rise / std::max(maxRise, 0.05f) - 1e-4f)));
    const float length = steps * kStepRun;
    const std::vector<WallPoint> outline = RoomOutline(*shape);

    // Floor already taken: flights, and the stairwell of the flight from the storey below (a flight there would stand
    // over a hole).
    std::vector<Rect> taken;
    for (entt::entity child : registry.get<TransformComponent>(room).children) {
        const auto* other = registry.try_get<ConstructionShapeComponent>(child);
        if (!other || (other->recipe != Recipe::Stairs && other->role != "stairwell")) continue;
        const auto& placed = registry.get<TransformComponent>(child);
        const bool turned = std::fabs(std::sin(placed.rotation.y)) > 0.5f;
        const float hx = (turned ? other->box.depth : other->box.width) * 0.5f;
        const float hz = (turned ? other->box.width : other->box.depth) * 0.5f;
        taken.push_back({placed.position.x - hx, placed.position.z - hz, placed.position.x + hx, placed.position.z + hz});
    }
    // And the floor inside each doorway, so a flight never blocks the way in.
    for (entt::entity child : registry.get<TransformComponent>(room).children) {
        const auto* opening = registry.try_get<ConstructionShapeComponent>(child);
        WallFrame frame;
        if (!opening || opening->recipe != Recipe::Opening || opening->opening.sill > floorTop + 0.05f ||
            !GetHostWallFrame(*shape, opening->opening.side, opening->opening.segment, frame)) {
            continue;
        }
        // The frame sits on one face of the wall; start from the wall's centre line and go into the room.
        const Vec3 middle = vadd(vadd(frame.center, vscale(frame.u, opening->opening.offset)),
                                 vscale(frame.normal, -frame.thickness * 0.5f));
        const Vec3 probe = vadd(middle, vscale(frame.normal, t));
        const Vec3 inward = PointInPolygon(outline, probe.x, probe.z) ? frame.normal : vscale(frame.normal, -1.0f);
        const float half = opening->opening.width * 0.5f + 0.3f;
        Rect clear{1e9f, 1e9f, -1e9f, -1e9f};
        for (const float along : {-half, half}) {
            for (const float in : {0.0f, t * 0.5f + 1.5f}) {
                const Vec3 p = vadd(middle, vadd(vscale(frame.u, along), vscale(inward, in)));
                clear = {std::min(clear.x0, p.x), std::min(clear.z0, p.z), std::max(clear.x1, p.x), std::max(clear.z1, p.z)};
            }
        }
        taken.push_back(clear);
    }
    auto free = [&](const Rect& r) {
        return InsideRoom(outline, t, r) && std::none_of(taken.begin(), taken.end(), [&](const Rect& o) { return Overlap(r, o); });
    };

    // Each straight, axis-aligned inside wall, longest first; the flight stands against it.
    struct Side {
        Vec3 start;   // on the inside face
        Vec3 along;   // unit, along the wall
        Vec3 inward;  // unit, into the room
        float length;
    };
    std::vector<Side> sides;
    for (size_t n = 0; n < outline.size(); ++n) {
        const WallPoint& a = outline[n];
        const WallPoint& b = outline[(n + 1) % outline.size()];
        const Vec3 d(b.x - a.x, 0.0f, b.z - a.z);
        const float len = vlen(d);
        if (len < 1e-3f || (std::fabs(d.x) > 1e-3f && std::fabs(d.z) > 1e-3f)) continue;
        const Vec3 along = vscale(d, 1.0f / len);
        Vec3 inward(-along.z, 0.0f, along.x);
        const Vec3 mid((a.x + b.x) * 0.5f, 0.0f, (a.z + b.z) * 0.5f);
        if (!PointInPolygon(outline, mid.x + inward.x * t, mid.z + inward.z * t)) inward = vscale(inward, -1.0f);
        sides.push_back({vadd(Vec3(a.x, 0.0f, a.z), vscale(inward, t * 0.5f)), along, inward, len});
    }
    std::sort(sides.begin(), sides.end(), [](const Side& a, const Side& b) { return a.length > b.length; });

    // A rectangle on the floor from `s0` to `s1` along a side and from `w0` to `w1` in from its face.
    auto rectOn = [](const Side& side, float s0, float s1, float w0, float w1) {
        const Vec3 p0 = vadd(side.start, vadd(vscale(side.along, s0), vscale(side.inward, w0)));
        const Vec3 p1 = vadd(side.start, vadd(vscale(side.along, s1), vscale(side.inward, w1)));
        return Rect{std::min(p0.x, p1.x), std::min(p0.z, p1.z), std::max(p0.x, p1.x), std::max(p0.z, p1.z)};
    };
    constexpr float kSlide = 0.25f;  // how far along a wall each try moves
    const float approach = 1.0f;

    Rect footprint{};
    Vec3 climb;       // the flight's local +Z in the room
    bool uTurn = false;
    bool found = false;
    for (const Side& side : sides) {
        for (float s0 = 0.0f; !found && s0 + approach + length <= side.length + 1e-3f; s0 += kSlide) {
            for (int dir : {1, -1}) {
                const float footAt = dir > 0 ? s0 : side.length - s0 - approach;
                const float flight0 = dir > 0 ? s0 + approach : side.length - s0 - approach - length;
                const Rect flight = rectOn(side, flight0, flight0 + length, 0.0f, kFlightWidth);
                const Rect front = rectOn(side, footAt, footAt + approach, 0.0f, kFlightWidth);
                if (free(flight) && free(front)) {
                    footprint = flight;
                    climb = vscale(side.along, static_cast<float>(dir));
                    found = true;
                    break;
                }
            }
        }
        if (found) break;
    }
    // A flight that turns back on itself fits rooms too short for a straight one: two flights side by side, with the
    // landing against the wall and the foot and the top facing into the room.
    const int first = steps / 2;
    const float turnDepth = static_cast<float>(first) * kStepRun + kFlightWidth;
    for (size_t n = 0; !found && n < sides.size(); ++n) {
        const Side& side = sides[n];
        for (float s0 = 0.0f; s0 + 2.0f * kFlightWidth <= side.length + 1e-3f; s0 += kSlide) {
            const Rect flight = rectOn(side, s0, s0 + 2.0f * kFlightWidth, 0.0f, turnDepth);
            const Rect front = rectOn(side, s0, s0 + 2.0f * kFlightWidth, turnDepth, turnDepth + approach);
            if (free(flight) && free(front)) {
                footprint = flight;
                climb = vscale(side.inward, -1.0f);
                uTurn = true;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        char text[200];
        std::snprintf(text, sizeof(text),
                      "the room is too small for stairs: %d steps need a free straight wall %.1f m long with %.1f m of "
                      "floor beside it, or for a flight that turns back, %.1f m of wall and %.1f m of floor in front",
                      steps, length + approach, kFlightWidth, 2.0f * kFlightWidth, turnDepth + approach);
        error = text;
        return entt::null;
    }

    const Vec3 centre((footprint.x0 + footprint.x1) * 0.5f, floorTop, (footprint.z0 + footprint.z1) * 0.5f);
    Entity stairs = scene.CreateEntity("Stairs Up");
    scene.SetParent(stairs, Entity(room, &scene), false);
    auto& stairsTransform = stairs.GetComponent<TransformComponent>();
    stairsTransform.position = centre;
    stairsTransform.rotation = Vec3(0.0f, std::atan2(climb.x, climb.z), 0.0f);
    stairsTransform.scale = Vec3(1.0f, 1.0f, 1.0f);
    ConstructionShapeComponent flight;
    flight.recipe = Recipe::Stairs;
    flight.role = "stairs";
    flight.box.width = uTurn ? 2.0f * kFlightWidth : kFlightWidth;
    flight.box.depth = uTurn ? turnDepth : length;
    flight.box.height = rise;
    flight.stairs.steps = steps;
    flight.stairs.maxRise = maxRise;
    flight.stairs.turn = uTurn ? StairTurn::UTurnLeft : StairTurn::Straight;
    flight.stairs.flightWidth = kFlightWidth;
    stairs.AddComponent<ConstructionShapeComponent>(flight);
    ConstructionSystem::AddBlockoutMaterial(scene, stairs);

    // The stairwell: a cut through the storey's floor over the whole flight, a centimetre clear of its walls.
    const auto& storey = registry.get<ConstructionShapeComponent>(above).box;
    Entity well = scene.CreateEntity("Stairwell");
    scene.SetParent(well, Entity(above, &scene), false);
    auto& wellTransform = well.GetComponent<TransformComponent>();
    wellTransform.position = Vec3(centre.x, -0.05f, centre.z);
    wellTransform.rotation = Vec3(0.0f, 0.0f, 0.0f);
    wellTransform.scale = Vec3(1.0f, 1.0f, 1.0f);
    ConstructionShapeComponent cut;
    cut.recipe = Recipe::Box;
    cut.role = "stairwell";
    cut.box.width = footprint.x1 - footprint.x0 - 0.02f;
    cut.box.depth = footprint.z1 - footprint.z0 - 0.02f;
    cut.box.height = storey.wallThickness + 0.1f;
    well.AddComponent<ConstructionShapeComponent>(cut);
    ConstructionSystem::SetOperation(scene, well, Operation::Subtract);
    return stairs;
}

namespace {

constexpr float kRoofPitch = 0.5236f;  // 30 degrees
constexpr float kRailHeight = 1.0f;

Entity AddChildShape(Scene& scene, entt::entity parent, const std::string& name, const ConstructionShapeComponent& shape,
                     const Vec3& position, float yaw)
{
    Entity entity = scene.CreateEntity(name);
    scene.SetParent(entity, Entity(parent, &scene), false);
    auto& transform = entity.GetComponent<TransformComponent>();
    transform.position = position;
    transform.rotation = Vec3(0.0f, yaw, 0.0f);
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);
    entity.AddComponent<ConstructionShapeComponent>(shape);
    ConstructionSystem::AddBlockoutMaterial(scene, entity);
    return entity;
}

ConstructionShapeComponent Block(const std::string& role, float width, float height, float depth)
{
    ConstructionShapeComponent block;
    block.recipe = Recipe::Box;
    block.role = role;
    block.box.width = width;
    block.box.height = height;
    block.box.depth = depth;
    return block;
}

bool HasRoof(Scene& scene, entt::entity room)
{
    entt::registry& registry = scene.GetRegistry();
    for (entt::entity child : registry.get<TransformComponent>(room).children) {
        const auto* shape = registry.try_get<ConstructionShapeComponent>(child);
        const auto* tag = registry.try_get<TagComponent>(child);
        if ((shape && shape->role == "roof") || (!shape && tag && tag->tag == "Roof")) return true;
    }
    return false;
}

} // namespace

entt::entity AddRoof(Scene& scene, entt::entity room, RoofStyle style, float overhang, std::string& error)
{
    entt::registry& registry = scene.GetRegistry();
    auto* shape = registry.valid(room) ? registry.try_get<ConstructionShapeComponent>(room) : nullptr;
    if (!IsRoom(shape)) {
        error = "only rooms can have a roof added";
        return entt::null;
    }
    if (StoreyAbove(scene, room) != entt::null) {
        error = "this room has a storey above it; put the roof on the top storey";
        return entt::null;
    }
    if (HasRoof(scene, room)) {
        error = "this room already has a roof";
        return entt::null;
    }
    const bool box = shape->recipe == Recipe::Box;
    if (style == RoofStyle::Pitched && !box) {
        error = "pitched roofs are for rooms made as hollow blocks; use a flat roof on rooms drawn with walls";
        return entt::null;
    }
    const float t = shape->box.wallThickness;
    const float height = shape->box.height;
    overhang = std::max(0.0f, overhang);
    shape->box.ceiling = false;
    if (style == RoofStyle::Flat) {
        if (box) {
            return AddChildShape(scene, room, "Roof",
                                 Block("roof", shape->box.width + 2.0f * overhang, t, shape->box.depth + 2.0f * overhang),
                                 Vec3(0.0f, height, 0.0f), 0.0f);
        }
        // A wall room one slab high with a floor is a slab over its whole outline.
        ConstructionShapeComponent slab = *shape;
        slab.role = "roof";
        slab.kit.clear();
        slab.kitSwaps.clear();
        slab.faceMaterials.clear();
        slab.box.height = t;
        slab.box.floor = true;
        return AddChildShape(scene, room, "Roof", slab, Vec3(0.0f, height, 0.0f), 0.0f);
    }
    // Two slopes meeting over the middle, the ridge along the longer side.
    const bool ridgeAlongX = shape->box.width >= shape->box.depth;
    const float along = (ridgeAlongX ? shape->box.width : shape->box.depth) + 2.0f * overhang;
    const float half = (ridgeAlongX ? shape->box.depth : shape->box.width) * 0.5f + overhang;
    const float rise = half * std::tan(kRoofPitch);
    Entity roof = scene.CreateEntity("Roof");
    scene.SetParent(roof, Entity(room, &scene), false);
    auto& roofTransform = roof.GetComponent<TransformComponent>();
    roofTransform.position = Vec3(0.0f, height, 0.0f);
    roofTransform.rotation = Vec3(0.0f, ridgeAlongX ? 0.0f : 1.57079633f, 0.0f);
    roofTransform.scale = Vec3(1.0f, 1.0f, 1.0f);
    ConstructionShapeComponent slope;
    slope.recipe = Recipe::Ramp;
    slope.role = "roof";
    slope.box.width = along;
    slope.box.height = rise;
    slope.box.depth = half;
    // A ramp rises towards its +Z; one climbs from the -Z eave and the other, turned round, from the +Z eave.
    AddChildShape(scene, roof, "Roof Slope", slope, Vec3(0.0f, 0.0f, -half * 0.5f), 0.0f);
    AddChildShape(scene, roof, "Roof Slope", slope, Vec3(0.0f, 0.0f, half * 0.5f), 3.14159265f);
    return roof;
}

entt::entity AddBalcony(Scene& scene, entt::entity opening, float depth, std::string& error)
{
    entt::registry& registry = scene.GetRegistry();
    const auto* door = registry.valid(opening) ? registry.try_get<ConstructionShapeComponent>(opening) : nullptr;
    const entt::entity host = door ? registry.get<TransformComponent>(opening).parent : entt::null;
    const auto* hostShape = host != entt::null && registry.valid(host)
        ? registry.try_get<ConstructionShapeComponent>(host) : nullptr;
    WallFrame frame;
    if (!door || door->recipe != Recipe::Opening || !hostShape || !CanHostOpenings(*hostShape) ||
        !GetHostWallFrame(*hostShape, door->opening.side, door->opening.segment, frame)) {
        error = "select a doorway in a wall to add a balcony outside it";
        return entt::null;
    }
    const float floorTop = HostFloorTop(*hostShape);
    if (door->opening.sill > floorTop + 0.05f) {
        error = "balconies go outside doorways; this opening is a window";
        return entt::null;
    }
    depth = std::max(0.5f, depth);
    const float t = frame.thickness;
    const Vec3 middle = vadd(vadd(frame.center, vscale(frame.u, door->opening.offset)), vscale(frame.normal, -t * 0.5f));
    Vec3 outward = frame.normal;
    if (IsRoom(hostShape)) {
        const Vec3 probe = vadd(middle, vscale(frame.normal, t));
        if (PointInPolygon(RoomOutline(*hostShape), probe.x, probe.z)) outward = vscale(frame.normal, -1.0f);
    }
    const float width = door->opening.width + 2.0f;
    const float slab = std::max(hostShape->box.wallThickness, 0.1f);
    const Vec3 centre = vadd(middle, vscale(outward, t * 0.5f + depth * 0.5f));
    // One shape: local +Z (the railed edge) points away from the wall, local X along it.
    const float yaw = std::atan2(outward.x, outward.z);
    ConstructionShapeComponent shape;
    shape.recipe = Recipe::Balcony;
    shape.role = "balcony";
    shape.box.width = width;
    shape.box.depth = depth;
    shape.box.height = slab + kRailHeight;
    shape.box.wallThickness = slab;
    Entity balcony = AddChildShape(scene, host, "Balcony", shape, Vec3(centre.x, floorTop - slab, centre.z), yaw);
    return balcony;
}

} // namespace Hephaestus::Construction
