#include "PhysicsAutomation.h"

#include "AutomationService.h"
#include "Scene/Scene.h"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Hephaestus::Automation {
namespace {

using PhysicsVec3 = AutomationBackend::PhysicsVec3;
using PhysicsBodySnapshot = AutomationBackend::PhysicsBodySnapshot;
using PhysicsRaycastResult = AutomationBackend::PhysicsRaycastResult;

ToolResult PhysicsFailure(const std::string& reason)
{
    if (reason == "no_body") {
        return ToolResult::Failure(ErrorCode::InvalidArguments, "Entity has no physics body",
                                   {{"reason", "no_body"}});
    }
    if (reason == "unreadable") {
        return ToolResult::Failure(ErrorCode::InvalidArguments, "Physics body cannot be queried",
                                   {{"reason", "unreadable"}});
    }
    if (reason == "invalid_direction") {
        return ToolResult::Failure(ErrorCode::InvalidArguments, "Ray direction is too small to cast",
                                   {{"reason", "invalid_direction"}});
    }
    return ToolResult::Failure(ErrorCode::ToolUnavailable, "Physics world is unavailable",
                               {{"reason", "unavailable"}});
}

Json Vec3Json(const PhysicsVec3& value)
{
    return Json::array({value.x, value.y, value.z});
}

bool FiniteNumber(const Json& value, float& out)
{
    if (!value.is_number()) return false;
    out = value.get<float>();
    return std::isfinite(out);
}

bool ReadVec3(const Json& arguments, const char* key, PhysicsVec3& out, std::string& error)
{
    if (!arguments.contains(key) || !arguments[key].is_array() || arguments[key].size() != 3) {
        error = std::string(key) + " must be an array of three finite numbers";
        return false;
    }
    const Json& value = arguments[key];
    if (!FiniteNumber(value[0], out.x) || !FiniteNumber(value[1], out.y) || !FiniteNumber(value[2], out.z)) {
        error = std::string(key) + " must be an array of three finite numbers";
        return false;
    }
    return true;
}

std::optional<ToolResult> WriteEntity(Json& object, ToolContext& context, entt::entity handle, bool entityValid)
{
    if (handle == entt::null) {
        object["entity"] = nullptr;
        object["entityStatus"] = "none";
        return std::nullopt;
    }
    if (!entityValid) {
        object["entity"] = nullptr;
        object["entityStatus"] = "expired";
        return std::nullopt;
    }
    Scene* scene = context.Backend().ActiveScene().scene;
    if (!scene) return ToolResult::Failure(ErrorCode::NoActiveScene, "No active scene");
    const EntityRef ref = context.Entities().Bind(*scene, handle);
    if (!ref) {
        object["entity"] = nullptr;
        object["entityStatus"] = "expired";
        return std::nullopt;
    }
    object["entity"] = ref.value;
    object["entityStatus"] = "resolved";
    return std::nullopt;
}

std::optional<Json> BodyJson(ToolContext& context, const PhysicsBodySnapshot& body, ToolResult& outFailure)
{
    Json object = {{"motionType", body.motionType},
                   {"active", body.active},
                   {"sleeping", body.sleeping},
                   {"sensor", body.sensor},
                   {"position", Vec3Json(body.position)},
                   {"rotation", Json::array({body.rotation.x, body.rotation.y, body.rotation.z, body.rotation.w})},
                   {"linearVelocity", Vec3Json(body.linearVelocity)},
                   {"angularVelocity", Vec3Json(body.angularVelocity)},
                   {"debugBodyIndex", body.debugBodyIndex}};
    if (const auto failure = WriteEntity(object, context, body.entity, body.entityValid)) {
        outFailure = *failure;
        return std::nullopt;
    }
    return object;
}

std::optional<ResolvedEntity> ResolveRequired(ToolContext& context, const Json& arguments, ToolResult& outFailure)
{
    if (!arguments.contains("entity") || !arguments["entity"].is_string()) {
        outFailure = ToolResult::Failure(ErrorCode::InvalidArguments, "A string entity reference is required");
        return std::nullopt;
    }
    Scene* scene = context.Backend().ActiveScene().scene;
    if (!scene) {
        outFailure = ToolResult::Failure(ErrorCode::NoActiveScene, "No active scene");
        return std::nullopt;
    }
    const ResolvedEntity resolved = context.Entities().Resolve(
        EntityRef{arguments["entity"].get<std::string>()}, scene);
    if (!resolved.IsResolved()) {
        outFailure = ToolResult::Failure(ErrorCode::EntityRefExpired,
                                         "Entity reference is unknown, stale, or destroyed");
        return std::nullopt;
    }
    return resolved;
}

void Add(AutomationService& service, std::string name, std::string description, Json schema, ToolHandler handler)
{
    ToolDefinition tool;
    tool.name = std::move(name);
    tool.description = std::move(description);
    tool.inputSchema = std::move(schema);
    tool.hostMask = HostMask::Editor | HostMask::Runtime;
    tool.playRequirement = PlayRequirement::PlayModeActive;
    tool.mutatesProject = false;
    tool.handler = std::move(handler);
    std::string error;
    if (!service.Registry().Register(std::move(tool), error)) throw std::runtime_error(error);
}

} // namespace

void RegisterPhysicsAutomation(AutomationService& service)
{
    const Json vec3 = {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}};
    const Json raySchema = {
        {"type", "object"},
        {"properties", {{"origin", vec3},
                        {"direction", vec3},
                        {"maxDistance", {{"type", "number"}}},
                        {"ignore", {{"type", "string"}}},
                        {"layer", {{"type", "string"}}}}},
        {"required", Json::array({"origin", "direction", "maxDistance"})},
        {"additionalProperties", false}};
    const Json entitySchema = {
        {"type", "object"},
        {"properties", {{"entity", {{"type", "string"}}}}},
        {"required", Json::array({"entity"})},
        {"additionalProperties", false}};
    const Json empty = {{"type", "object"}, {"properties", Json::object()}, {"additionalProperties", false}};

    const char* live =
        " Bodies are created on the first physics update, so a query immediately after play starts can be empty.";

    Add(service, "physics.raycast",
        std::string("Cast a ray in the live physics world. A miss returns hit false. A near-zero direction is an error.") + live,
        raySchema,
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            PhysicsVec3 origin;
            PhysicsVec3 direction;
            std::string error;
            if (!ReadVec3(arguments, "origin", origin, error) ||
                !ReadVec3(arguments, "direction", direction, error)) {
                return ToolResult::Failure(ErrorCode::InvalidArguments, std::move(error));
            }
            if (!arguments.contains("maxDistance")) {
                return ToolResult::Failure(ErrorCode::InvalidArguments,
                                           "maxDistance must be a finite number greater than zero");
            }
            float maxDistance = 0.0f;
            if (!FiniteNumber(arguments["maxDistance"], maxDistance) || maxDistance <= 0.0f) {
                return ToolResult::Failure(ErrorCode::InvalidArguments,
                                           "maxDistance must be a finite number greater than zero");
            }
            const float lengthSq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
            if (lengthSq < 1e-8f) return PhysicsFailure("invalid_direction");

            entt::entity ignore = entt::null;
            if (arguments.contains("ignore")) {
                if (!arguments["ignore"].is_string()) {
                    return ToolResult::Failure(ErrorCode::InvalidArguments, "ignore must be an entity reference");
                }
                Scene* scene = context.Backend().ActiveScene().scene;
                if (!scene) return ToolResult::Failure(ErrorCode::NoActiveScene, "No active scene");
                const ResolvedEntity resolved = context.Entities().Resolve(
                    EntityRef{arguments["ignore"].get<std::string>()}, scene);
                if (!resolved.IsResolved()) {
                    return ToolResult::Failure(ErrorCode::EntityRefExpired,
                                               "Entity reference is unknown, stale, or destroyed");
                }
                ignore = resolved.handle;
            }

            const std::string* layerName = nullptr;
            std::string layerStorage;
            if (arguments.contains("layer")) {
                if (!arguments["layer"].is_string() || arguments["layer"].get<std::string>().empty()) {
                    return ToolResult::Failure(ErrorCode::InvalidArguments, "layer must be a physics layer name");
                }
                layerStorage = arguments["layer"].get<std::string>();
                layerName = &layerStorage;
            }

            PhysicsRaycastResult hit;
            if (!context.Backend().PhysicsRaycast(origin, direction, maxDistance, ignore, hit, error, layerName)) {
                return PhysicsFailure(error);
            }
            if (!hit.hit) return ToolResult::Success({{"hit", false}});

            Json value = {{"hit", true},
                          {"point", Vec3Json(hit.point)},
                          {"normal", Vec3Json(hit.normal)},
                          {"distance", hit.distance}};
            if (hit.entity == entt::null || !hit.entityValid) {
                value["entity"] = nullptr;
            } else {
                Scene* scene = context.Backend().ActiveScene().scene;
                if (!scene) return ToolResult::Failure(ErrorCode::NoActiveScene, "No active scene");
                const EntityRef ref = context.Entities().Bind(*scene, hit.entity);
                value["entity"] = ref ? Json(ref.value) : Json(nullptr);
            }
            return ToolResult::Success(std::move(value));
        });

    Add(service, "physics.list_bodies",
        std::string("List rigid bodies that can currently be read. Unreadable bodies are omitted.") + live,
        empty,
        [](ToolContext& context, const Json&) -> ToolOutcome {
            std::vector<PhysicsBodySnapshot> bodies;
            std::string error;
            if (!context.Backend().ListPhysicsBodies(bodies, error)) return PhysicsFailure(error);
            Json list = Json::array();
            for (const PhysicsBodySnapshot& body : bodies) {
                ToolResult failure;
                auto object = BodyJson(context, body, failure);
                if (!object) return failure;
                list.push_back(std::move(*object));
            }
            return ToolResult::Success({{"bodies", std::move(list)}});
        });

    Add(service, "physics.get_body",
        "Read one rigid body by entity. Fails when that entity has no body or the body cannot be queried.",
        entitySchema,
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = ResolveRequired(context, arguments, failure);
            if (!resolved) return failure;
            PhysicsBodySnapshot body;
            std::string error;
            if (!context.Backend().GetPhysicsBody(resolved->handle, body, error)) return PhysicsFailure(error);
            ToolResult publishFailure;
            auto object = BodyJson(context, body, publishFailure);
            if (!object) return publishFailure;
            return ToolResult::Success(std::move(*object));
        });

    Add(service, "physics.get_character_state",
        "Read position, linear velocity, and the current ground probe for an entity that has a rigid body.",
        entitySchema,
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = ResolveRequired(context, arguments, failure);
            if (!resolved) return failure;
            PhysicsBodySnapshot pose;
            bool grounded = false;
            std::string error;
            if (!context.Backend().GetCharacterState(resolved->handle, pose, grounded, error)) {
                return PhysicsFailure(error);
            }
            Json value = {{"position", Vec3Json(pose.position)},
                          {"linearVelocity", Vec3Json(pose.linearVelocity)},
                          {"grounded", grounded}};
            if (pose.entity == entt::null || !pose.entityValid) {
                value["entity"] = nullptr;
            } else {
                Scene* scene = context.Backend().ActiveScene().scene;
                if (!scene) return ToolResult::Failure(ErrorCode::NoActiveScene, "No active scene");
                const EntityRef ref = context.Entities().Bind(*scene, pose.entity);
                value["entity"] = ref ? Json(ref.value) : Json(nullptr);
            }
            return ToolResult::Success(std::move(value));
        });
}

} // namespace Hephaestus::Automation
