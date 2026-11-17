#include "ConstructionAutomation.h"

#include "AutomationToolSupport.h"

#include "Analysis/LevelChecks.h"
#include "Analysis/Playtest.h"
#include "Assets/AssetManager.h"
#include "Construction/ConstructionBuilding.h"
#include "Construction/ConstructionSystem.h"
#include "Scene/Entity.h"
#include "Scene/Json.h"

namespace Hephaestus::Automation {

namespace {

using namespace Content;
using Construction::ConstructionSystem;
using Construction::Recipe;

ToolResult Failure(ErrorCode code, std::string message)
{
    return ToolResult::Failure(code, std::move(message));
}

Json ParamsSchema()
{
    return {{"type", "object"},
            {"description",
             "Box: width, height, depth, hollow, wallThickness, floor, ceiling. Wall: points ([[x, z], ...] from "
             "the entity's position), closed (a room), height, thickness, floor. Opening: side (+x, -x, +z, -z), "
             "segment (on walls: the path segment, from points[segment] to the next), offset, sill, width, height. "
             "Metres."}};
}

Json FacesSchema()
{
    return {{"type", "array"},
            {"description",
             "Materials painted on faces, replacing the list: [{group, wall?, segment?, material}]. group is top, "
             "bottom, +x, -x, +z, -z, inner (inside walls, with wall), floor, ceiling or reveal; segment picks a "
             "wall path segment; material is a project material path."}};
}

bool ReadVec3(const Json& value, Vec3& out)
{
    if (!value.is_array() || value.size() != 3) return false;
    for (const Json& element : value) {
        if (!element.is_number()) return false;
    }
    out = Vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    return true;
}

// Applies {"params": {...}} (and optional role) onto a shape through the scene-file reader, so automation and files
// accept exactly the same values.
bool ApplyParams(ConstructionShapeComponent& shape, const Json& arguments, std::string& error)
{
    Json data = Json::object();
    data["recipe"] = Construction::RecipeName(shape.recipe);
    if (arguments.contains("role") && arguments["role"].is_string()) data["role"] = arguments["role"];
    if (arguments.contains("operation") && arguments["operation"].is_string()) data["operation"] = arguments["operation"];
    if (arguments.contains("faces")) data["faces"] = arguments["faces"];
    if (arguments.contains("kit")) {
        if (!arguments["kit"].is_string()) {
            error = "kit must be a .hpkit path, or empty to remove the kit";
            return false;
        }
        const std::string kit = arguments["kit"].get<std::string>();
        if (!kit.empty() && (kit.size() < 6 || kit.compare(kit.size() - 6, 6, ".hpkit") != 0)) {
            error = "kit must be a .hpkit path";
            return false;
        }
        if (!kit.empty() && shape.recipe != Recipe::Wall && !(shape.recipe == Recipe::Box && shape.box.hollow)) {
            error = "only walls and hollow rooms can take a kit";
            return false;
        }
        data["kit"] = kit;
    }
    if (arguments.contains("kitSwaps")) data["kitSwaps"] = arguments["kitSwaps"];
    if (arguments.contains("mesh")) {
        if (shape.source != Construction::SourceKind::EditableMesh) {
            error = "mesh is only for editable meshes; use construction.make_editable first";
            return false;
        }
        data["mesh"] = arguments["mesh"];
    }
    if (arguments.contains("params")) {
        if (!arguments["params"].is_object()) {
            error = "params must be an object";
            return false;
        }
        data["params"] = arguments["params"];
    }
    JsonValue parsed;
    if (!JsonParser(data.dump()).Parse(parsed, error)) return false;
    ConstructionShapeComponent updated = shape;
    if (!Construction::ApplyShapeJson(updated, parsed, error)) return false;
    shape = updated;
    return true;
}

Json ShapeJson(ToolContext& context, Scene& scene, entt::entity entity)
{
    const auto& shape = scene.GetRegistry().get<ConstructionShapeComponent>(entity);
    std::string text = Construction::WriteShapeJson(shape, "");
    text = text.substr(text.find('{'));
    Json value = Json::parse(text, nullptr, false);
    value["entity"] = context.Entities().Bind(scene, entity).value;
    if (const auto* tag = scene.GetRegistry().try_get<TagComponent>(entity)) value["name"] = tag->tag;
    const auto& transform = scene.GetRegistry().get<TransformComponent>(entity);
    value["position"] = Json::array({transform.position.x, transform.position.y, transform.position.z});
    if (transform.parent != entt::null && scene.GetRegistry().valid(transform.parent)) {
        value["parent"] = context.Entities().Bind(scene, transform.parent).value;
    }
    if (ConstructionSystem* system = context.Backend().Construction();
        system && Construction::HasOwnGeometry(shape)) {
        Construction::GeometrySummary summary;
        if (system->Summary(entity, summary)) {
            Json openings = Json::array();
            for (entt::entity child : transform.children) {
                const auto* childShape = scene.GetRegistry().try_get<ConstructionShapeComponent>(child);
                if (childShape && childShape->recipe == Recipe::Opening) {
                    openings.push_back(context.Entities().Bind(scene, child).value);
                }
            }
            value["openings"] = std::move(openings);
            value["evaluated"] = {{"triangles", summary.triangles},
                                  {"collisionBoxes", summary.collisionBoxes},
                                  {"inputHash", std::to_string(summary.inputHash)},
                                  {"geometryHash", std::to_string(summary.geometryHash)}};
        }
        Construction::KitSummary kit;
        if (!shape.kit.empty() && system->Kit(entity, kit)) {
            value["kitResult"] = {{"pieces", kit.pieces}, {"fillers", kit.fillers}, {"gaps", kit.gaps},
                                  {"maxStretch", kit.maxStretch}, {"swapped", kit.swapped},
                                  {"staleSwaps", kit.staleSwaps}};
            if (!kit.error.empty()) value["kitResult"]["error"] = kit.error;
        }
    }
    return value;
}

ConstructionSystem* UpToDate(ToolContext& context, Scene& scene)
{
    ConstructionSystem* system = context.Backend().Construction();
    if (system) system->Update(scene);
    return system;
}

} // namespace

void RegisterConstructionAutomation(AutomationService& service)
{
    RegisterOrThrow(service, Definition(
        "construction.create",
        "Create a construction shape: 'box' (solid), 'room' (hollow with a floor), 'stairs' (climbing towards "
        "+Z; params steps, maxRise, and turn ('straight', 'left', 'right', 'uturn-left', 'uturn-right') with flightWidth), "
        "'ramp' (a slope rising towards +Z), 'wall' (params points and closed; a closed wall is a room of any shape), 'plane', "
        "'cylinder' or 'sphere' (params width, height, depth, segments). "
        "Its base sits on position. operation 'subtract' makes a box cut every box it overlaps.",
        ObjectSchema({{"recipe", {{"type", "string"}, {"enum", Json::array({"box", "room", "stairs", "ramp", "wall", "plane", "cylinder", "sphere"})}}},
                      {"operation", {{"type", "string"}, {"enum", Json::array({"add", "subtract"})}}},
                      {"name", {{"type", "string"}}},
                      {"role", {{"type", "string"}}},
                      {"position", {{"type", "array"}, {"minItems", 3}, {"maxItems", 3}}},
                      {"params", ParamsSchema()}},
                     Json::array({"recipe"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            Scene* scene = RequireScene(context);
            if (!scene) return Failure(ErrorCode::NoActiveScene, "No active scene");
            const std::string recipe = arguments["recipe"].get<std::string>();
            ConstructionShapeComponent shape;
            shape.recipe = recipe == "stairs" ? Recipe::Stairs : recipe == "plane" ? Recipe::Plane
                : recipe == "cylinder" ? Recipe::Cylinder : recipe == "sphere" ? Recipe::Sphere
                : recipe == "wall" ? Recipe::Wall : recipe == "ramp" ? Recipe::Ramp : Recipe::Box;
            shape.box.hollow = recipe == "room";
            shape.role = recipe == "box" ? "block" : recipe;
            std::string error;
            if (!ApplyParams(shape, arguments, error)) return Failure(ErrorCode::InvalidArguments, error);
            Vec3 position(0.0f, 0.0f, 0.0f);
            if (arguments.contains("position") && !ReadVec3(arguments["position"], position)) {
                return Failure(ErrorCode::InvalidArguments, "position must be three numbers");
            }
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Create construction shape", mutationFailure)) return mutationFailure;
            const std::string name = arguments.contains("name") && arguments["name"].is_string()
                ? arguments["name"].get<std::string>()
                : Construction::DisplayName(shape);
            Entity entity = scene->CreateEntity(name);
            entity.GetComponent<TransformComponent>().position = position;
            const Construction::Operation operation = shape.operation;
            shape.operation = Construction::Operation::Add;
            entity.AddComponent<ConstructionShapeComponent>(shape);
            ConstructionSystem::SetOperation(*scene, entity, operation);
            context.Backend().CompleteEditorMutation(true);
            UpToDate(context, *scene);
            return ToolResult::Success(ShapeJson(context, *scene, entity));
        }, true));

    RegisterOrThrow(service, Definition(
        "construction.add_opening",
        "Cut a doorway or window into a construction block, room or wall. The opening is a child entity of the host.",
        ObjectSchema({{"host", {{"type", "string"}}},
                      {"name", {{"type", "string"}}},
                      {"role", {{"type", "string"}}},
                      {"params", ParamsSchema()}},
                     Json::array({"host"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            Json hostArguments = {{"entity", arguments["host"]}};
            ToolResult failure;
            const auto host = ResolveEntity(context, hostArguments, failure);
            if (!host) return failure;
            const auto* hostShape = host->scene->GetRegistry().try_get<ConstructionShapeComponent>(host->handle);
            if (!hostShape || !Construction::CanHostOpenings(*hostShape)) {
                return Failure(ErrorCode::InvalidArguments, "host is not a construction block, room or wall");
            }
            ConstructionShapeComponent shape;
            shape.recipe = Recipe::Opening;
            std::string error;
            if (!ApplyParams(shape, arguments, error)) return Failure(ErrorCode::InvalidArguments, error);
            if (shape.role.empty()) shape.role = shape.opening.sill <= 0.25f ? "doorway" : "window";
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Add opening", mutationFailure)) return mutationFailure;
            const std::string name = arguments.contains("name") && arguments["name"].is_string()
                ? arguments["name"].get<std::string>() : (shape.role == "window" ? "Window" : "Doorway");
            Entity opening = host->scene->CreateEntity(name);
            opening.AddComponent<ConstructionShapeComponent>(shape);
            host->scene->SetParent(opening, Entity(host->handle, host->scene), false);
            context.Backend().CompleteEditorMutation(true);
            UpToDate(context, *host->scene);
            return ToolResult::Success({{"opening", ShapeJson(context, *host->scene, opening)},
                                        {"host", ShapeJson(context, *host->scene, host->handle)}});
        }, true));

    RegisterOrThrow(service, Definition(
        "construction.set_params",
        "Change a construction shape's parameters (only the ones given) and re-evaluate it.",
        ObjectSchema({{"entity", {{"type", "string"}}}, {"role", {{"type", "string"}}},
                      {"operation", {{"type", "string"}, {"enum", Json::array({"add", "subtract"})}}},
                      {"faces", FacesSchema()},
                      {"kit", {{"type", "string"},
                               {"description", "a .hpkit that dresses a wall or hollow room with prefab pieces; "
                                               "empty removes it"}}},
                      {"mesh", {{"type", "object"},
                                {"description", "an editable mesh's corners and faces, replacing them: "
                                                "{vertices: [[x, y, z]...], faces: [[i, j, k...]...]}"}}},
                      {"kitSwaps", {{"type", "array"},
                                    {"description", "pieces swapped by hand, replacing the list: [{slot, prefab}]. "
                                                    "slot is a spot such as 0:r0.2 (face 0, run 0, piece 2), 1:c "
                                                    "(corner) or 2:o0 (opening frame); prefab is a kit piece of the "
                                                    "same role and width"}}},
                      {"params", ParamsSchema()}},
                     Json::array({"entity"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = ResolveEntity(context, arguments, failure);
            if (!resolved) return failure;
            auto* shape = resolved->scene->GetRegistry().try_get<ConstructionShapeComponent>(resolved->handle);
            if (!shape) return Failure(ErrorCode::InvalidArguments, "entity has no construction shape");
            ConstructionShapeComponent updated = *shape;
            std::string error;
            if (!ApplyParams(updated, arguments, error)) return Failure(ErrorCode::InvalidArguments, error);
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Edit construction shape", mutationFailure)) return mutationFailure;
            const Construction::Operation operation = updated.operation;
            updated.operation = shape->operation;
            *shape = updated;
            ConstructionSystem::SetOperation(*resolved->scene, resolved->handle, operation);
            context.Backend().CompleteEditorMutation(false);
            ConstructionSystem* system = UpToDate(context, *resolved->scene);
            Json result = ShapeJson(context, *resolved->scene, resolved->handle);
            if (system) result["reevaluated"] = system->Stats().lastEvaluations;
            return ToolResult::Success(std::move(result));
        }, true));

    RegisterOrThrow(service, Definition(
        "construction.get",
        "Read a construction shape: its parameters, openings, and the evaluated triangle count, collision boxes and "
        "hashes.",
        ObjectSchema({{"entity", {{"type", "string"}}}}, Json::array({"entity"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = ResolveEntity(context, arguments, failure);
            if (!resolved) return failure;
            if (!resolved->scene->GetRegistry().all_of<ConstructionShapeComponent>(resolved->handle)) {
                return Failure(ErrorCode::InvalidArguments, "entity has no construction shape");
            }
            ConstructionSystem* system = UpToDate(context, *resolved->scene);
            Json result = ShapeJson(context, *resolved->scene, resolved->handle);
            if (system) {
                const auto& stats = system->Stats();
                result["system"] = {{"evaluations", stats.evaluations}, {"meshUploads", stats.meshUploads},
                                    {"collidersBuilt", stats.collidersBuilt}, {"adopted", stats.adopted},
                                    {"lastUpdateMs", stats.lastUpdateMs}};
            }
            return ToolResult::Success(std::move(result));
        }));

    RegisterOrThrow(service, Definition(
        "level.check",
        "Check the open level against what its characters can do: doorways too narrow or too low, steps too high, "
        "ramps too steep, and floor the characters can't walk to from where they start. Returns the traversal "
        "metrics used and each problem with its check, severity, message, position and entity.",
        ObjectSchema({}, Json::array()),
        [](ToolContext& context, const Json&) -> ToolOutcome {
            Scene* scene = RequireScene(context);
            if (!scene) return Failure(ErrorCode::NoActiveScene, "No active scene");
            UpToDate(context, *scene);
            Hephaestus::AssetManager* assets = context.Backend().Assets();
            const Analysis::LevelCheckReport report =
                Analysis::CheckLevel(*scene, assets ? std::filesystem::path(assets->GetProjectRoot()) : std::filesystem::path());
            Json problems = Json::array();
            for (const Analysis::LevelProblem& problem : report.problems) {
                Json entry = {{"check", problem.check}, {"severity", Analysis::SeverityName(problem.severity)},
                              {"message", problem.message},
                              {"position", Json::array({problem.position.x, problem.position.y, problem.position.z})}};
                if (problem.entity != entt::null && scene->GetRegistry().valid(problem.entity)) {
                    entry["entity"] = context.Entities().Bind(*scene, problem.entity).value;
                }
                problems.push_back(std::move(entry));
            }
            const auto& m = report.metrics;
            return ToolResult::Success(Json{
                {"metrics", {{"height", m.height}, {"radius", m.radius}, {"stepHeight", m.stepHeight},
                             {"maxSlopeDegrees", m.maxSlopeDegrees}, {"jumpHeight", m.jumpHeight},
                             {"fromControllers", m.fromControllers}}},
                {"navPolygons", report.navPolygons},
                {"islands", report.islands},
                {"problems", std::move(problems)}});
        }));

    RegisterOrThrow(service, Definition(
        "construction.add_storey",
        "Stack a copy of a room (hollow box) on top of it, as its child: same footprint, walls, kit and look, its "
        "floor on the room's walls. The room's ceiling is removed. Returns the new storey.",
        ObjectSchema({{"entity", {{"type", "string"}}}}, Json::array({"entity"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = ResolveEntity(context, arguments, failure);
            if (!resolved) return failure;
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Add storey", mutationFailure)) return mutationFailure;
            std::string error;
            const entt::entity storey = Construction::AddStorey(*resolved->scene, resolved->handle, error);
            context.Backend().CompleteEditorMutation(storey != entt::null);
            if (storey == entt::null) return Failure(ErrorCode::InvalidArguments, error);
            UpToDate(context, *resolved->scene);
            return ToolResult::Success(ShapeJson(context, *resolved->scene, storey));
        }, true));

    RegisterOrThrow(service, Definition(
        "construction.add_stairs_up",
        "Put a straight flight of stairs inside a room, along its longer inside wall with floor in front of its foot, "
        "up to the floor of the storey above (construction.add_storey first), and cut the stairwell in that floor. "
        "Steps follow the characters' step height. Returns the stairs.",
        ObjectSchema({{"entity", {{"type", "string"}}}}, Json::array({"entity"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = ResolveEntity(context, arguments, failure);
            if (!resolved) return failure;
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Add stairs up", mutationFailure)) return mutationFailure;
            std::string error;
            const float maxRise = Analysis::MetricsForScene(*resolved->scene).stepHeight * 0.95f;
            const entt::entity stairs = Construction::AddStairsUp(*resolved->scene, resolved->handle, maxRise, error);
            context.Backend().CompleteEditorMutation(stairs != entt::null);
            if (stairs == entt::null) return Failure(ErrorCode::InvalidArguments, error);
            UpToDate(context, *resolved->scene);
            return ToolResult::Success(ShapeJson(context, *resolved->scene, stairs));
        }, true));

    RegisterOrThrow(service, Definition(
        "construction.make_editable",
        "Turn a solid block or a ramp into an editable mesh with the same shape, whose corners can then be moved "
        "(construction.set_params with mesh: {vertices: [[x, y, z]...], faces: [[i, j, k...]...]}, faces wound "
        "anticlockwise seen from outside). Collision becomes the convex hull of the corners.",
        ObjectSchema({{"entity", {{"type", "string"}}}}, Json::array({"entity"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = ResolveEntity(context, arguments, failure);
            if (!resolved) return failure;
            auto* shape = resolved->scene->GetRegistry().try_get<ConstructionShapeComponent>(resolved->handle);
            if (!shape || !Construction::CanMakeEditable(*shape)) {
                return Failure(ErrorCode::InvalidArguments, "only solid blocks and ramps can be made editable");
            }
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Make editable", mutationFailure)) return mutationFailure;
            Construction::MakeEditable(*shape);
            context.Backend().CompleteEditorMutation(false);
            UpToDate(context, *resolved->scene);
            return ToolResult::Success(ShapeJson(context, *resolved->scene, resolved->handle));
        }, true));

    RegisterOrThrow(service, Definition(
        "level.playtest_begin",
        "Start recording a bot playtest. Then start Play, turn realtime off and step frames: game scripts that check "
        "Playtest.isRunning() (the FPS team deathmatch) play it as an all-bot match and record kills, deaths and "
        "positions. Finish with level.playtest_end.",
        ObjectSchema({}, Json::array()),
        [](ToolContext&, const Json&) -> ToolOutcome {
            Analysis::BeginPlaytest();
            return ToolResult::Success(Json{{"recording", true}});
        }));

    RegisterOrThrow(service, Definition(
        "level.playtest_end",
        "Stop recording the bot playtest and summarise it: event counts by kind and team, and the busiest heatmap "
        "cells (x, z centres, cellSize metres) for traffic, kills and deaths.",
        ObjectSchema({{"top", {{"type", "integer"}, {"minimum", 1}}},
                      {"show", {{"type", "string"},
                                {"enum", Json::array({"traffic", "kill", "death", "none"})},
                                {"description", "draw that heatmap over the level in the editor"}}}},
                     Json::array()),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            const size_t top = arguments.contains("top") && arguments["top"].is_number_integer()
                ? static_cast<size_t>(arguments["top"].get<std::int64_t>()) : 5;
            const float seconds = Analysis::PlaytestClock();
            const std::vector<Analysis::PlaytestEvent> events = Analysis::EndPlaytest();
            Json kinds = Json::object();
            for (const char* kind : {"traffic", "kill", "death"}) {
                const Analysis::Heatmap heatmap = Analysis::BuildHeatmap(events, kind, std::string(kind) == "traffic" ? 2.0f : 4.0f);
                Json byTeam = Json::object();
                for (const Analysis::PlaytestEvent& event : events) {
                    if (event.kind != kind) continue;
                    const std::string team = std::to_string(event.team);
                    byTeam[team] = byTeam.value(team, 0) + 1;
                }
                Json cells = Json::array();
                for (size_t n = 0; n < heatmap.cells.size() && n < top; ++n) {
                    const Analysis::HeatmapCell& cell = heatmap.cells[n];
                    cells.push_back({{"x", (cell.x + 0.5f) * heatmap.cellSize}, {"z", (cell.z + 0.5f) * heatmap.cellSize},
                                     {"count", cell.count}});
                }
                kinds[kind] = {{"total", heatmap.total}, {"byTeam", std::move(byTeam)}, {"cellSize", heatmap.cellSize},
                               {"cells", heatmap.cells.size()}, {"busiest", std::move(cells)}};
            }
            if (arguments.contains("show") && arguments["show"].is_string()) {
                const std::string show = arguments["show"].get<std::string>();
                std::vector<EditorDebugLine> lines;
                if (show != "none") {
                    Analysis::AppendHeatmapLines(Analysis::BuildHeatmap(events, show, show == "traffic" ? 2.0f : 4.0f), lines);
                }
                context.Backend().SetLevelOverlay(std::move(lines));
            }
            return ToolResult::Success(Json{{"seconds", seconds}, {"events", kinds}});
        }));

    RegisterOrThrow(service, Definition(
        "level.measure_walk",
        "The walking route between two world points on the open level's navmesh: its corners, length, the straight "
        "distance, the time at the characters' walking speed, vaults on the way, and whether it reaches the end.",
        ObjectSchema({{"from", {{"type", "array"}, {"minItems", 3}, {"maxItems", 3}}},
                      {"to", {{"type", "array"}, {"minItems", 3}, {"maxItems", 3}}}},
                     Json::array({"from", "to"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            Scene* scene = RequireScene(context);
            if (!scene) return Failure(ErrorCode::NoActiveScene, "No active scene");
            Vec3 from;
            Vec3 to;
            if (!ReadVec3(arguments["from"], from) || !ReadVec3(arguments["to"], to)) {
                return Failure(ErrorCode::InvalidArguments, "from and to must be three numbers each");
            }
            UpToDate(context, *scene);
            Hephaestus::AssetManager* assets = context.Backend().Assets();
            Analysis::WalkRoute route;
            std::string error;
            if (!Analysis::MeasureLevelWalk(
                    *scene, assets ? std::filesystem::path(assets->GetProjectRoot()) : std::filesystem::path(), from, to,
                    route, error)) {
                return Failure(ErrorCode::InvalidArguments, "the navmesh could not be built: " + error);
            }
            const Analysis::TraversalMetrics metrics = Analysis::MetricsForScene(*scene);
            Json corners = Json::array();
            for (const Vec3& corner : route.corners) corners.push_back(Json::array({corner.x, corner.y, corner.z}));
            return ToolResult::Success(Json{{"found", route.found}, {"complete", route.complete},
                                            {"length", route.length}, {"straight", route.straight},
                                            {"seconds", route.seconds}, {"walkSpeed", metrics.walkSpeed},
                                            {"vaults", route.links}, {"corners", std::move(corners)}});
        }));
}

} // namespace Hephaestus::Automation
