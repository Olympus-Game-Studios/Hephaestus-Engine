#include "EditorAutomation.h"
#include "AutomationService.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"

#include <algorithm>
#include <stdexcept>

namespace Hephaestus::Automation {
namespace {
Json Selection(ToolContext& context)
{
    Json selected = Json::array();
    Scene* scene = context.Backend().ActiveScene().scene;
    if (!scene) return selected;
    for (entt::entity handle : context.Backend().GetSelection()) {
        if (!scene->GetRegistry().valid(handle)) continue;
        Json item = {{"entity", context.Entities().Bind(*scene, handle).value}};
        if (auto* tag = scene->GetRegistry().try_get<TagComponent>(handle)) item["name"] = tag->tag;
        selected.push_back(std::move(item));
    }
    return selected;
}

void Add(AutomationService& service, std::string name, Json schema, ToolHandler handler)
{
    ToolDefinition definition;
    definition.name = std::move(name);
    definition.description = "Inspect or change live editor selection and state.";
    definition.inputSchema = std::move(schema);
    definition.hostMask = HostMask::Editor;
    definition.handler = std::move(handler);
    std::string error;
    if (!service.Registry().Register(std::move(definition), error)) throw std::runtime_error(error);
}
}

void RegisterEditorAutomation(AutomationService& service)
{
    const Json empty = {{"type", "object"}, {"properties", Json::object()}, {"additionalProperties", false}};
    Add(service, "editor.get_state", empty, [](ToolContext& context, const Json&) -> ToolOutcome {
        Json state = context.Backend().GetEditorState();
        state["selection"] = Selection(context);
        return ToolResult::Success(std::move(state));
    });
    Add(service, "editor.get_selection", empty, [](ToolContext& context, const Json&) -> ToolOutcome {
        return ToolResult::Success({{"selection", Selection(context)}});
    });
    Add(service, "editor.select", {{"type", "object"}, {"properties", {
        {"entities", {{"type", "array"}, {"items", {{"type", "string"}}}}},
        {"mode", {{"type", "string"}, {"enum", Json::array({"replace", "add"})}}}}},
        {"required", Json::array({"entities"})}, {"additionalProperties", false}},
        [](ToolContext& context, const Json& args) -> ToolOutcome {
            Scene* scene = context.Backend().ActiveScene().scene;
            if (!scene) return ToolResult::Failure(ErrorCode::NoActiveScene, "No active scene");
            if (!args.contains("entities") || !args["entities"].is_array())
                return ToolResult::Failure(ErrorCode::InvalidArguments, "entities must be an array of EntityRefs");
            const std::string mode = args.value("mode", "replace");
            if (mode != "replace" && mode != "add")
                return ToolResult::Failure(ErrorCode::InvalidArguments, "mode must be replace or add");
            std::vector<entt::entity> selection = mode == "add" ? context.Backend().GetSelection() : std::vector<entt::entity>{};
            for (const Json& ref : args["entities"]) {
                if (!ref.is_string()) return ToolResult::Failure(ErrorCode::InvalidArguments, "EntityRef must be a string");
                const auto resolved = context.Entities().Resolve(EntityRef{ref.get<std::string>()}, scene);
                if (!resolved.IsResolved()) return ToolResult::Failure(ErrorCode::EntityRefExpired, "Entity reference is unknown, stale, or destroyed");
                if (std::find(selection.begin(), selection.end(), resolved.handle) == selection.end()) selection.push_back(resolved.handle);
            }
            std::string error;
            if (!context.Backend().SetSelection(selection, error)) return ToolResult::Failure(ErrorCode::ToolUnavailable, std::move(error));
            return ToolResult::Success({{"selection", Selection(context)}});
        });
    Add(service, "editor.clear_selection", empty, [](ToolContext& context, const Json&) -> ToolOutcome {
        std::string error;
        if (!context.Backend().SetSelection({}, error)) return ToolResult::Failure(ErrorCode::ToolUnavailable, std::move(error));
        return ToolResult::Success({{"selection", Json::array()}});
    });
}
} // namespace Hephaestus::Automation
