#include "SceneAutomation.h"

#include "AutomationService.h"
#include "ComponentMetadata.h"
#include "Scene/Components.h"
#include "Scene/PrefabComponentIO.h"
#include "Scene/PrefabOverrideSystem.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Hephaestus::Automation {
namespace {

ToolOutcome Failure(ErrorCode code, std::string message)
{
    return ToolResult::Failure(code, std::move(message));
}

Scene* RequireScene(ToolContext& context)
{
    return context.Backend().ActiveScene().scene;
}

bool BeginMutation(ToolContext& context, std::string_view label, ToolResult& outFailure)
{
    std::string error;
    if (context.Backend().PrepareEditorMutation(label, error)) return true;
    outFailure = ToolResult::Failure(ErrorCode::ToolUnavailable, std::move(error));
    return false;
}

std::optional<ResolvedEntity> Resolve(ToolContext& context, const Json& arguments,
                                      ToolResult& outFailure)
{
    if (!arguments.contains("entity") || !arguments["entity"].is_string()) {
        outFailure = ToolResult::Failure(ErrorCode::InvalidArguments,
                                         "A string entity reference is required");
        return std::nullopt;
    }
    Scene* scene = RequireScene(context);
    if (!scene) {
        outFailure = ToolResult::Failure(ErrorCode::NoActiveScene, "No active scene");
        return std::nullopt;
    }
    ResolvedEntity resolved = context.Entities().Resolve(
        EntityRef{arguments["entity"].get<std::string>()}, scene);
    if (!resolved.IsResolved()) {
        outFailure = ToolResult::Failure(ErrorCode::EntityRefExpired,
                                         "Entity reference is unknown, stale, or destroyed");
        return std::nullopt;
    }
    return resolved;
}

Json Vec3Json(const Vec3& value)
{
    return Json::array({value.x, value.y, value.z});
}

bool ReadVec3(const Json& value, Vec3& out)
{
    if (!value.is_array() || value.size() != 3 ||
        !value[0].is_number() || !value[1].is_number() || !value[2].is_number()) {
        return false;
    }
    out = Vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    return true;
}

Json EntityJson(ToolContext& context, Scene& scene, entt::entity handle)
{
    auto& registry = scene.GetRegistry();
    Json value;
    value["entity"] = context.Entities().Bind(scene, handle).value;
    if (const auto* tag = registry.try_get<TagComponent>(handle)) value["name"] = tag->tag;
    if (const auto* transform = registry.try_get<TransformComponent>(handle)) {
        value["transform"] = {{"position", Vec3Json(transform->position)},
                              {"rotation", Vec3Json(transform->rotation)},
                              {"scale", Vec3Json(transform->scale)}};
        if (transform->parent != entt::null && registry.valid(transform->parent)) {
            value["parent"] = context.Entities().Bind(scene, transform->parent).value;
        }
    }
    return value;
}

Json ToJson(const ::JsonValue& source)
{
    switch (source.type) {
    case ::JsonValue::Type::Null: return nullptr;
    case ::JsonValue::Type::Bool: return source.boolValue;
    case ::JsonValue::Type::Number: return source.numberValue;
    case ::JsonValue::Type::String: return source.stringValue;
    case ::JsonValue::Type::Array: {
        Json result = Json::array();
        for (const auto& value : source.arrayValue) result.push_back(ToJson(value));
        return result;
    }
    case ::JsonValue::Type::Object: {
        Json result = Json::object();
        for (const auto& [key, value] : source.objectValue) result[key] = ToJson(value);
        return result;
    }
    }
    return nullptr;
}

::JsonValue FromJson(const Json& source)
{
    ::JsonValue result;
    if (source.is_null()) return result;
    if (source.is_boolean()) {
        result.type = ::JsonValue::Type::Bool;
        result.boolValue = source.get<bool>();
    } else if (source.is_number()) {
        result.type = ::JsonValue::Type::Number;
        result.numberValue = source.get<double>();
    } else if (source.is_string()) {
        result.type = ::JsonValue::Type::String;
        result.stringValue = source.get<std::string>();
    } else if (source.is_array()) {
        result.type = ::JsonValue::Type::Array;
        for (const auto& value : source) result.arrayValue.push_back(FromJson(value));
    } else if (source.is_object()) {
        result.type = ::JsonValue::Type::Object;
        for (auto it = source.begin(); it != source.end(); ++it) {
            result.objectValue.emplace(it.key(), FromJson(it.value()));
        }
    }
    return result;
}

ToolDefinition Definition(std::string name, std::string description,
                          Json schema, ToolHandler handler, bool mutates = false,
                          PlayRequirement requirement = PlayRequirement::Any)
{
    ToolDefinition definition;
    definition.name = std::move(name);
    definition.description = std::move(description);
    definition.inputSchema = std::move(schema);
    definition.hostMask = HostMask::Editor;
    definition.playRequirement = mutates ? PlayRequirement::EditorOnly : requirement;
    definition.mutatesProject = mutates;
    definition.handler = std::move(handler);
    return definition;
}

Json ObjectSchema(Json properties = Json::object(), Json required = Json::array())
{
    Json schema = {{"type", "object"}, {"properties", std::move(properties)},
                   {"additionalProperties", false}};
    if (!required.empty()) schema["required"] = std::move(required);
    return schema;
}

} // namespace

void RegisterSceneAutomation(AutomationService& service)
{
    std::string error;
    auto registerTool = [&](ToolDefinition definition) {
        if (!service.Registry().Register(std::move(definition), error)) {
            throw std::runtime_error(error);
        }
    };

    registerTool(Definition("project.get_info", "Return the active Hephaestus project information.",
        ObjectSchema(), [](ToolContext& context, const Json&) -> ToolOutcome {
            return ToolResult::Success(context.Backend().GetProjectInfo());
        }));

    registerTool(Definition("scene.get_current", "Return the active live scene.",
        ObjectSchema(), [](ToolContext& context, const Json&) -> ToolOutcome {
            Scene* scene = RequireScene(context);
            if (!scene) return Failure(ErrorCode::NoActiveScene, "No active scene");
            return ToolResult::Success({{"name", scene->GetName()},
                                        {"path", context.Backend().ActiveScenePath()}});
        }));

    registerTool(Definition("scene.new", "Create a new unsaved scene (refuses to discard dirty work).",
        ObjectSchema({{"name", {{"type", "string"}}}}),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            if (arguments.contains("name") && !arguments["name"].is_string())
                return Failure(ErrorCode::InvalidArguments, "Scene name must be a string");
            const std::string name = arguments.value("name", "New Scene");
            if (name.empty()) return Failure(ErrorCode::InvalidArguments, "Scene name cannot be empty");
            std::string error;
            if (!context.Backend().NewScene(name, error)) return Failure(ErrorCode::ToolUnavailable, std::move(error));
            context.Entities().ObserveActiveScene(context.Backend().ActiveScene().scene);
            return ToolResult::Success({{"name", name}, {"path", ""}});
        }, false, PlayRequirement::EditorOnly));

    registerTool(Definition("scene.open", "Open a project-relative scene (refuses to discard dirty work).",
        ObjectSchema({{"path", {{"type", "string"}}}}, Json::array({"path"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            if (!arguments.contains("path") || !arguments["path"].is_string() ||
                arguments["path"].get<std::string>().empty())
                return Failure(ErrorCode::InvalidArguments, "A project-relative scene path is required");
            std::string path, error;
            if (!context.Backend().OpenScene(arguments["path"].get<std::string>(), path, error))
                return Failure(ErrorCode::ToolUnavailable, std::move(error));
            context.Entities().ObserveActiveScene(context.Backend().ActiveScene().scene);
            return ToolResult::Success({{"path", path}, {"opened", true}});
        }, false, PlayRequirement::EditorOnly));

    registerTool(Definition("scene.list_entities", "List entities from the active live scene.",
        ObjectSchema(), [](ToolContext& context, const Json&) -> ToolOutcome {
            Scene* scene = RequireScene(context);
            if (!scene) return Failure(ErrorCode::NoActiveScene, "No active scene");
            std::vector<entt::entity> handles;
            auto view = scene->GetRegistry().view<TagComponent>();
            for (entt::entity handle : view) handles.push_back(handle);
            std::sort(handles.begin(), handles.end(), [&](entt::entity left, entt::entity right) {
                const auto& leftName = view.get<TagComponent>(left).tag;
                const auto& rightName = view.get<TagComponent>(right).tag;
                return leftName == rightName
                    ? static_cast<std::uint32_t>(left) < static_cast<std::uint32_t>(right)
                    : leftName < rightName;
            });
            Json entities = Json::array();
            for (entt::entity handle : handles) entities.push_back(EntityJson(context, *scene, handle));
            return ToolResult::Success({{"entities", std::move(entities)}});
        }));

    registerTool(Definition("scene.save", "Save the active scene through the editor scene subsystem.",
        ObjectSchema(), [](ToolContext& context, const Json&) -> ToolOutcome {
            std::string saveError;
            if (!context.Backend().SaveActiveScene(saveError)) {
                return Failure(ErrorCode::ToolUnavailable, std::move(saveError));
            }
            return ToolResult::Success({{"saved", true},
                                        {"path", context.Backend().ActiveScenePath()}});
        }, false, PlayRequirement::EditorOnly));

    registerTool(Definition("scene.save_as", "Persist the active scene to a project-relative scene path.",
        ObjectSchema({{"path", {{"type", "string"}}}}, Json::array({"path"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            if (!RequireScene(context)) return Failure(ErrorCode::NoActiveScene, "No active scene");
            if (!arguments.contains("path") || !arguments["path"].is_string() ||
                arguments["path"].get<std::string>().empty()) {
                return Failure(ErrorCode::InvalidArguments, "A project-relative scene path is required");
            }
            std::string path, error;
            if (!context.Backend().SaveActiveSceneAs(arguments["path"].get<std::string>(), path, error)) {
                return Failure(ErrorCode::InvalidArguments, std::move(error));
            }
            return ToolResult::Success({{"saved", true}, {"path", path}});
        }, false, PlayRequirement::EditorOnly));

    registerTool(Definition("entity.create", "Create an entity in the active live scene.",
        ObjectSchema({{"name", {{"type", "string"}}}}, Json::array({"name"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            Scene* scene = RequireScene(context);
            if (!scene) return Failure(ErrorCode::NoActiveScene, "No active scene");
            if (!arguments.contains("name") || !arguments["name"].is_string() ||
                arguments["name"].get<std::string>().empty()) {
                return Failure(ErrorCode::InvalidArguments, "A non-empty entity name is required");
            }
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Create Entity", mutationFailure)) return mutationFailure;
            Entity entity = scene->CreateEntity(arguments["name"].get<std::string>());
            context.Backend().CompleteEditorMutation(true);
            return ToolResult::Success(EntityJson(context, *scene, static_cast<entt::entity>(entity)));
        }, true));

    registerTool(Definition("entity.find", "Find all live entities with an exact name.",
        ObjectSchema({{"name", {{"type", "string"}}}}, Json::array({"name"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            Scene* scene = RequireScene(context);
            if (!scene) return Failure(ErrorCode::NoActiveScene, "No active scene");
            if (!arguments.contains("name") || !arguments["name"].is_string()) {
                return Failure(ErrorCode::InvalidArguments, "A name is required");
            }
            const std::string name = arguments["name"].get<std::string>();
            std::vector<entt::entity> matches;
            auto view = scene->GetRegistry().view<TagComponent>();
            for (entt::entity handle : view) {
                if (view.get<TagComponent>(handle).tag == name) matches.push_back(handle);
            }
            std::sort(matches.begin(), matches.end(), [](entt::entity left, entt::entity right) {
                return static_cast<std::uint32_t>(left) < static_cast<std::uint32_t>(right);
            });
            Json entities = Json::array();
            for (entt::entity handle : matches) entities.push_back(EntityJson(context, *scene, handle));
            return ToolResult::Success({{"entities", std::move(entities)}});
        }));

    const Json entityReferenceSchema = ObjectSchema(
        {{"entity", {{"type", "string"}}}}, Json::array({"entity"}));
    registerTool(Definition("entity.get", "Inspect a live entity by opaque reference.",
        entityReferenceSchema, [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            return ToolResult::Success(EntityJson(context, *resolved->scene, resolved->handle));
        }));

    registerTool(Definition("entity.destroy", "Destroy a live entity; children are reparented by Scene.",
        entityReferenceSchema, [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Destroy Entity", mutationFailure)) return mutationFailure;
            resolved->scene->DestroyEntity(Entity(resolved->handle, resolved->scene));
            context.Entities().Erase(EntityRef{arguments["entity"].get<std::string>()});
            context.Backend().CompleteEditorMutation(true);
            return ToolResult::Success({{"destroyed", true}});
        }, true));

    registerTool(Definition("entity.set_transform", "Set local transform values in radians.",
        ObjectSchema({{"entity", {{"type", "string"}}},
                      {"position", {{"type", "array"}, {"minItems", 3}, {"maxItems", 3}}},
                      {"rotation", {{"type", "array"}, {"minItems", 3}, {"maxItems", 3}}},
                      {"scale", {{"type", "array"}, {"minItems", 3}, {"maxItems", 3}}}},
                     Json::array({"entity"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            auto& transform = resolved->scene->GetRegistry().get<TransformComponent>(resolved->handle);
            Vec3 position = transform.position;
            Vec3 rotation = transform.rotation;
            Vec3 scale = transform.scale;
            if ((arguments.contains("position") && !ReadVec3(arguments["position"], position)) ||
                (arguments.contains("rotation") && !ReadVec3(arguments["rotation"], rotation)) ||
                (arguments.contains("scale") && !ReadVec3(arguments["scale"], scale))) {
                return Failure(ErrorCode::InvalidArguments, "Transform values must be three-number arrays");
            }
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Set Transform", mutationFailure)) return mutationFailure;
            transform.position = position;
            transform.rotation = rotation;
            transform.scale = scale;
            PrefabOverrideSystem::SyncEntityOverrides(*resolved->scene,
                                                       Entity(resolved->handle, resolved->scene));
            context.Backend().CompleteEditorMutation(false);
            return ToolResult::Success(EntityJson(context, *resolved->scene, resolved->handle));
        }, true));

    registerTool(Definition("component.list_types", "List component types supported by semantic IO.",
        ObjectSchema(), [](ToolContext&, const Json&) -> ToolOutcome {
            Json components = Json::array();
            for (const auto& descriptor : ComponentMetadataRegistry::Builtins().All()) {
                components.push_back(descriptor.type);
            }
            return ToolResult::Success({{"components", std::move(components)}});
        }));

    registerTool(Definition("component.describe", "Describe a component and its properties.",
        ObjectSchema({{"type", {{"type", "string"}}}}, Json::array({"type"})),
        [](ToolContext&, const Json& arguments) -> ToolOutcome {
            if (!arguments.contains("type") || !arguments["type"].is_string()) {
                return Failure(ErrorCode::InvalidArguments, "A component type is required");
            }
            const auto* descriptor = ComponentMetadataRegistry::Builtins().Find(
                arguments["type"].get<std::string>());
            if (!descriptor) return Failure(ErrorCode::UnknownComponent, "Unknown component type");
            Json properties = Json::array();
            for (const auto& property : descriptor->properties) {
                properties.push_back({{"path", property.path},
                                      {"kind", static_cast<int>(property.kind)},
                                      {"readOnly", property.readOnly}});
            }
            return ToolResult::Success({{"type", descriptor->type},
                                        {"displayName", descriptor->displayName},
                                        {"category", descriptor->category},
                                        {"addable", descriptor->addable},
                                        {"removable", descriptor->removable},
                                        {"properties", std::move(properties)}});
        }));

    registerTool(Definition("component.list", "List components present on a live entity.",
        entityReferenceSchema, [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            Entity entity(resolved->handle, resolved->scene);
            Json components = Json::array({"Transform"});
            for (const std::string& type : PrefabComponentIO::SupportedTypes()) {
                if (PrefabComponentIO::HasComponent(entity, type)) components.push_back(type);
            }
            return ToolResult::Success({{"components", std::move(components)}});
        }));

    const Json componentEntitySchema = ObjectSchema(
        {{"entity", {{"type", "string"}}}, {"type", {{"type", "string"}}}},
        Json::array({"entity", "type"}));
    registerTool(Definition("component.get", "Read component data from a live entity.",
        componentEntitySchema, [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            const std::string type = arguments.value("type", "");
            if (type == "Transform") {
                const auto& transform = resolved->scene->GetRegistry().get<TransformComponent>(resolved->handle);
                return ToolResult::Success({{"type", type}, {"enabled", true},
                    {"data", {{"position", Vec3Json(transform.position)},
                              {"rotation", Vec3Json(transform.rotation)},
                              {"scale", Vec3Json(transform.scale)}}}});
            }
            PrefabComponentEntry entry;
            if (!PrefabComponentIO::CaptureComponent(Entity(resolved->handle, resolved->scene), type, entry)) {
                return Failure(ErrorCode::ComponentMissing, "Entity does not have the requested component");
            }
            return ToolResult::Success({{"type", type}, {"enabled", entry.enabled},
                                        {"data", ToJson(entry.data)}});
        }));

    registerTool(Definition("component.add", "Add a supported component to a live entity.",
        componentEntitySchema, [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            Entity entity(resolved->handle, resolved->scene);
            const std::string type = arguments.value("type", "");
            if (type == "Transform") return Failure(ErrorCode::ComponentAlreadyPresent, "Transform is mandatory");
            const ComponentDescriptor* descriptor = ComponentMetadataRegistry::Builtins().Find(type);
            if (!descriptor || !descriptor->addable) {
                return Failure(ErrorCode::UnknownComponent, "Unsupported component type");
            }
            if (entity.HasComponent<PrefabNodeComponent>()) {
                return Failure(ErrorCode::PrefabStructuralOverrideUnsupported,
                               "Adding components to linked prefab nodes is not supported");
            }
            if (PrefabComponentIO::HasComponent(entity, type)) {
                return Failure(ErrorCode::ComponentAlreadyPresent, "Component is already present");
            }
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Add Component", mutationFailure)) return mutationFailure;
            if (!PrefabComponentIO::AddComponent(entity, type)) {
                context.Backend().AbortEditorMutation();
                return Failure(ErrorCode::UnknownComponent, "Unsupported component type");
            }
            context.Backend().CompleteEditorMutation(true);
            return ToolResult::Success({{"added", true}, {"type", type}});
        }, true));

    registerTool(Definition("component.remove", "Remove a supported component from a live entity.",
        componentEntitySchema, [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            Entity entity(resolved->handle, resolved->scene);
            const std::string type = arguments.value("type", "");
            if (type == "Transform") return Failure(ErrorCode::ReadOnlyProperty, "Transform cannot be removed");
            const ComponentDescriptor* descriptor = ComponentMetadataRegistry::Builtins().Find(type);
            if (!descriptor || !descriptor->removable) {
                return Failure(ErrorCode::UnknownComponent, "Unsupported component type");
            }
            if (entity.HasComponent<PrefabNodeComponent>()) {
                return Failure(ErrorCode::PrefabStructuralOverrideUnsupported,
                               "Removing components from linked prefab nodes is not supported");
            }
            if (!PrefabComponentIO::HasComponent(entity, type)) {
                return Failure(ErrorCode::ComponentMissing, "Component is not present");
            }
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Remove Component", mutationFailure)) return mutationFailure;
            if (!PrefabComponentIO::RemoveComponent(entity, type)) {
                context.Backend().AbortEditorMutation();
                return Failure(ErrorCode::UnknownComponent, "Unsupported component type");
            }
            context.Backend().CompleteEditorMutation(true);
            return ToolResult::Success({{"removed", true}, {"type", type}});
        }, true));

    registerTool(Definition("component.set", "Replace component data through semantic component IO.",
        ObjectSchema({{"entity", {{"type", "string"}}},
                      {"type", {{"type", "string"}}},
                      {"data", {{"type", "object"}}},
                      {"enabled", {{"type", "boolean"}}}},
                     Json::array({"entity", "type", "data"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            const std::string type = arguments.value("type", "");
            if (type == "Transform") {
                Json transformArguments = arguments["data"];
                transformArguments["entity"] = arguments["entity"];
                auto& transform = resolved->scene->GetRegistry().get<TransformComponent>(resolved->handle);
                Vec3 position = transform.position;
                Vec3 rotation = transform.rotation;
                Vec3 scale = transform.scale;
                if ((transformArguments.contains("position") && !ReadVec3(transformArguments["position"], position)) ||
                    (transformArguments.contains("rotation") && !ReadVec3(transformArguments["rotation"], rotation)) ||
                    (transformArguments.contains("scale") && !ReadVec3(transformArguments["scale"], scale))) {
                    return Failure(ErrorCode::InvalidArguments, "Transform values must be three-number arrays");
                }
                ToolResult mutationFailure;
                if (!BeginMutation(context, "Set Component", mutationFailure)) return mutationFailure;
                transform.position = position;
                transform.rotation = rotation;
                transform.scale = scale;
                PrefabOverrideSystem::SyncEntityOverrides(*resolved->scene,
                                                           Entity(resolved->handle, resolved->scene));
                context.Backend().CompleteEditorMutation(false);
                return ToolResult::Success({{"updated", true}, {"type", type}});
            }
            Entity entity(resolved->handle, resolved->scene);
            const ComponentDescriptor* descriptor = ComponentMetadataRegistry::Builtins().Find(type);
            if (!descriptor || !descriptor->writable) {
                return Failure(ErrorCode::UnknownComponent, "Unsupported component type");
            }
            if (!PrefabComponentIO::HasComponent(entity, type)) {
                return Failure(ErrorCode::ComponentMissing, "Component is not present");
            }
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Set Component", mutationFailure)) return mutationFailure;
            const bool enabled = arguments.value("enabled", true);
            if (!PrefabComponentIO::ApplyComponentJson(entity, type, FromJson(arguments["data"]), enabled)) {
                context.Backend().AbortEditorMutation();
                return Failure(ErrorCode::InvalidArguments, "Component data could not be applied");
            }
            PrefabOverrideSystem::SyncEntityOverrides(*resolved->scene, entity);
            context.Backend().CompleteEditorMutation(false);
            return ToolResult::Success({{"updated", true}, {"type", type}});
        }, true));

    const Json prefabEntitySchema = ObjectSchema(
        {{"entity", {{"type", "string"}}},
         {"component", {{"type", "string"}}},
         {"property", {{"type", "string"}}}},
        Json::array({"entity"}));

    registerTool(Definition("prefab.get_info", "Report prefab-instance information for an entity.",
        entityReferenceSchema, [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            Entity entity(resolved->handle, resolved->scene);
            Json info;
            const auto* node = resolved->scene->GetRegistry().try_get<PrefabNodeComponent>(resolved->handle);
            info["isPrefabNode"] = node != nullptr;
            if (node) {
                info["instanceId"] = node->instanceId;
                info["nodeId"] = node->nodeId;
                info["isRoot"] = node->isRoot;
            }
            const auto* instance = PrefabOverrideSystem::TryGetInstance(*resolved->scene, entity);
            info["isPrefabInstance"] = instance != nullptr;
            if (instance) {
                info["prefabPath"] = instance->prefabPath;
                info["instanceId"] = instance->instanceId;
                info["missingPrefab"] = instance->missingPrefab;
                if (instance->missingPrefab) info["missingReason"] = instance->missingReason;
            }
            return ToolResult::Success(std::move(info));
        }));

    registerTool(Definition("prefab.list_overrides", "List overrides on an entity's prefab instance.",
        entityReferenceSchema, [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            Entity entity(resolved->handle, resolved->scene);
            const auto* instance = PrefabOverrideSystem::TryGetInstance(*resolved->scene, entity);
            if (!instance) {
                return Failure(ErrorCode::ToolUnavailable, "Entity is not a prefab instance");
            }
            Json overrides = Json::array();
            for (const auto& ref : PrefabOverrideSystem::ListOverrides(*instance)) {
                overrides.push_back({{"nodeId", ref.nodeId},
                                     {"component", ref.component},
                                     {"property", ref.property}});
            }
            return ToolResult::Success({{"overrides", std::move(overrides)}});
        }));

    registerTool(Definition("prefab.revert", "Revert prefab overrides on an entity.",
        prefabEntitySchema, [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            Entity entity(resolved->handle, resolved->scene);
            if (!entity.HasComponent<PrefabNodeComponent>()) {
                return Failure(ErrorCode::ToolUnavailable, "Entity is not a prefab node");
            }
            const std::string component = arguments.value("component", "");
            const std::string property = arguments.value("property", "");
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Revert Prefab Override", mutationFailure)) return mutationFailure;
            bool reverted = false;
            if (!property.empty()) {
                reverted = PrefabOverrideSystem::RevertProperty(*resolved->scene, entity, component, property);
            } else if (!component.empty()) {
                reverted = PrefabOverrideSystem::RevertComponent(*resolved->scene, entity, component);
            } else {
                reverted = PrefabOverrideSystem::RevertNode(*resolved->scene, entity);
            }
            if (!reverted) {
                context.Backend().AbortEditorMutation();
                return Failure(ErrorCode::InternalError, "Prefab revert failed");
            }
            context.Backend().CompleteEditorMutation(true);
            return ToolResult::Success({{"reverted", true}});
        }, true));

    registerTool(Definition("prefab.unpack", "Unpack a prefab instance into plain entities.",
        entityReferenceSchema, [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            Entity entity(resolved->handle, resolved->scene);
            if (!entity.HasComponent<PrefabInstanceComponent>()) {
                return Failure(ErrorCode::ToolUnavailable, "Entity is not a prefab instance root");
            }
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Unpack Prefab", mutationFailure)) return mutationFailure;
            if (!PrefabOverrideSystem::UnpackInstance(*resolved->scene, entity)) {
                context.Backend().AbortEditorMutation();
                return Failure(ErrorCode::InternalError, "Prefab unpack failed");
            }
            context.Backend().CompleteEditorMutation(true);
            return ToolResult::Success({{"unpacked", true}});
        }, true));

    registerTool(Definition("prefab.apply", "Apply overrides back to the source prefab asset.",
        prefabEntitySchema, [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = Resolve(context, arguments, failure);
            if (!resolved) return failure;
            Entity entity(resolved->handle, resolved->scene);
            if (!entity.HasComponent<PrefabNodeComponent>()) {
                return Failure(ErrorCode::ToolUnavailable, "Entity is not a prefab node");
            }
            const std::string component = arguments.value("component", "");
            const std::string property = arguments.value("property", "");
            ToolResult mutationFailure;
            if (!BeginMutation(context, "Apply To Prefab", mutationFailure)) return mutationFailure;
            std::string error;
            bool applied = false;
            if (!property.empty()) {
                applied = PrefabOverrideSystem::ApplyPropertyToPrefab(*resolved->scene, entity, component, property, error);
            } else if (!component.empty()) {
                applied = PrefabOverrideSystem::ApplyComponentToPrefab(*resolved->scene, entity, component, error);
            } else {
                applied = PrefabOverrideSystem::ApplyNodeToPrefab(*resolved->scene, entity, error);
            }
            if (!applied) {
                context.Backend().AbortEditorMutation();
                return Failure(ErrorCode::ToolUnavailable, std::move(error));
            }
            context.Backend().CompleteEditorMutation(true);
            return ToolResult::Success({{"applied", true}});
        }, true));
}

} // namespace Hephaestus::Automation
