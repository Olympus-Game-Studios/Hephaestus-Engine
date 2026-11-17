#include "ConstructionBuilding.h"

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
constexpr float kMinApproach = 0.7f;  // floor in front of the first step
constexpr float kHalfPi = 1.57079633f;

bool IsRoom(const ConstructionShapeComponent* shape)
{
    return shape && shape->source == SourceKind::Recipe && shape->recipe == Recipe::Box && shape->box.hollow &&
           shape->operation == Operation::Add;
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
        error = "only rooms (hollow blocks) can have a storey added";
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
    const float insideWidth = box.width - 2.0f * t;
    const float insideDepth = box.depth - 2.0f * t;
    // From this room's floor to the top of the storey's floor, which sits on this room's walls.
    const float rise = box.height + registry.get<ConstructionShapeComponent>(above).box.wallThickness - floorTop;
    const int steps = std::max(1, static_cast<int>(std::ceil(rise / std::max(maxRise, 0.05f) - 1e-4f)));
    const float length = steps * kStepRun;
    const bool alongX = insideWidth >= insideDepth;
    const float longSide = alongX ? insideWidth : insideDepth;
    const float shortSide = alongX ? insideDepth : insideWidth;
    const float flightWidth = std::min(kFlightWidth, shortSide - 0.2f);
    // Room at the foot of the flight to step onto it; up to a metre.
    const float approach = std::min(1.0f, longSide - 0.1f - length);
    if (approach < kMinApproach || flightWidth < 0.8f) {
        char text[160];
        std::snprintf(text, sizeof(text),
                      "the room is too small for stairs: %d steps need %.1f m along one inside wall and 1.0 m across",
                      steps, length + 0.1f + kMinApproach);
        error = text;
        return entt::null;
    }

    // Rectangles on the floor (room frame, x then z) already taken by flights and by the stairwell of the flight
    // from the storey below, which would leave a new flight standing over a hole.
    struct Rect {
        float x0, z0, x1, z1;
    };
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
    // Along a long inside wall, foot first: -Z (or -X) wall climbing +X (or +Z), then the other wall back the other way.
    const float halfLong = longSide * 0.5f;
    const float halfShort = shortSide * 0.5f;
    bool placed = false;
    float flightMin = 0.0f, sideMin = 0.0f;
    int direction = 1;
    for (const auto& [side, dir] : {std::pair{-1, 1}, std::pair{1, -1}, std::pair{-1, -1}, std::pair{1, 1}}) {
        const float footStart = dir > 0 ? -halfLong : halfLong - approach;
        const float lo = dir > 0 ? -halfLong : halfLong - approach - length;
        const float hi = lo + approach + length;
        const float s0 = side < 0 ? -halfShort : halfShort - flightWidth;
        const Rect area = alongX ? Rect{lo, s0, hi, s0 + flightWidth} : Rect{s0, lo, s0 + flightWidth, hi};
        const bool clear = std::none_of(taken.begin(), taken.end(), [&](const Rect& r) {
            return area.x0 < r.x1 - 0.01f && area.x1 > r.x0 + 0.01f && area.z0 < r.z1 - 0.01f && area.z1 > r.z0 + 0.01f;
        });
        if (clear) {
            flightMin = dir > 0 ? footStart + approach : lo;
            sideMin = s0;
            direction = dir;
            placed = true;
            break;
        }
    }
    if (!placed) {
        error = "there is no free inside wall for another flight in this room";
        return entt::null;
    }
    const float along = flightMin + length * 0.5f;
    const float across = sideMin + flightWidth * 0.5f;
    Vec3 centre = alongX ? Vec3(along, floorTop, across) : Vec3(across, floorTop, along);
    const float yaw = alongX ? (direction > 0 ? kHalfPi : -kHalfPi) : (direction > 0 ? 0.0f : 2.0f * kHalfPi);
    Entity stairs = scene.CreateEntity("Stairs Up");
    scene.SetParent(stairs, Entity(room, &scene), false);
    auto& stairsTransform = stairs.GetComponent<TransformComponent>();
    stairsTransform.position = centre;
    stairsTransform.rotation = Vec3(0.0f, yaw, 0.0f);
    stairsTransform.scale = Vec3(1.0f, 1.0f, 1.0f);
    ConstructionShapeComponent flight;
    flight.recipe = Recipe::Stairs;
    flight.role = "stairs";
    flight.box.width = flightWidth;
    flight.box.depth = length;
    flight.box.height = rise;
    flight.stairs.steps = steps;
    flight.stairs.maxRise = maxRise;
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
    cut.box.width = (alongX ? length : flightWidth) - 0.02f;
    cut.box.depth = (alongX ? flightWidth : length) - 0.02f;
    cut.box.height = storey.wallThickness + 0.1f;
    well.AddComponent<ConstructionShapeComponent>(cut);
    ConstructionSystem::SetOperation(scene, well, Operation::Subtract);
    return stairs;
}

} // namespace Hephaestus::Construction
