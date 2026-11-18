#include "PlayAutomation.h"

#include "AutomationService.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace Hephaestus::Automation {
namespace {

constexpr float kDefaultFixedDt = 1.0f / 60.0f;

ToolOutcome Failure(ErrorCode code, std::string message)
{
    return ToolResult::Failure(code, std::move(message));
}

std::string PlayStateName(PlayState state)
{
    switch (state) {
    case PlayState::Playing: return "playing";
    case PlayState::Paused: return "paused";
    case PlayState::Editor: return "editor";
    }
    return "editor";
}

ToolDefinition Definition(std::string name, std::string description, Json schema,
                          ToolHandler handler, PlayRequirement requirement,
                          HostMask hostMask = HostMask::Editor)
{
    ToolDefinition definition;
    definition.name = std::move(name);
    definition.description = std::move(description);
    definition.inputSchema = std::move(schema);
    definition.hostMask = hostMask;
    definition.playRequirement = requirement;
    definition.mutatesProject = false;
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

bool RequireFloat(const Json& arguments, std::string_view key, float& out)
{
    if (!arguments.contains(key) || !arguments[key].is_number()) return false;
    out = arguments[key].get<float>();
    return true;
}

} // namespace

void RegisterPlayAutomation(AutomationService& service)
{
    std::string error;
    auto registerTool = [&](ToolDefinition definition) {
        if (!service.Registry().Register(std::move(definition), error)) {
            throw std::runtime_error(error);
        }
    };

    registerTool(Definition(
        "play.get_state", "Report the current Play Mode state.",
        ObjectSchema(), [](ToolContext& context, const Json&) -> ToolOutcome {
            const bool realtime = context.Backend().IsRealtime();
            return ToolResult::Success({{"state", PlayStateName(context.Backend().GetPlayState())},
                                        {"realtime", realtime},
                                        {"executionMode", realtime ? "realtime" : "manual"}});
        }, PlayRequirement::Any, HostMask::Editor | HostMask::Runtime));

    registerTool(Definition(
        "play.start",
        "Start Play Mode (snapshots the scene and runs scripts). fromHere puts the player at the editor camera.",
        ObjectSchema({{"fromHere", {{"type", "boolean"}}}}), [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            std::string error;
            const bool fromHere = arguments.contains("fromHere") && arguments["fromHere"].is_boolean() &&
                                  arguments["fromHere"].get<bool>();
            if (!(fromHere ? context.Backend().StartPlayModeFromHere(error) : context.Backend().StartPlayMode(error))) {
                return Failure(ErrorCode::ToolUnavailable, std::move(error));
            }
            return ToolResult::Success({{"state", "playing"}});
        }, PlayRequirement::EditorOnly));

    registerTool(Definition(
        "play.stop", "Stop Play Mode and restore the editor scene.",
        ObjectSchema(), [](ToolContext& context, const Json&) -> ToolOutcome {
            std::string error;
            if (!context.Backend().StopPlayMode(error)) {
                return Failure(ErrorCode::ToolUnavailable, std::move(error));
            }
            return ToolResult::Success({{"state", "editor"}});
        }, PlayRequirement::PlayModeActive));

    registerTool(Definition(
        "play.pause", "Pause Play Mode (scripts stop ticking).",
        ObjectSchema(), [](ToolContext& context, const Json&) -> ToolOutcome {
            if (context.Backend().GetPlayState() != PlayState::Playing) {
                return ToolResult::Success({{"state", PlayStateName(context.Backend().GetPlayState())}});
            }
            std::string error;
            if (!context.Backend().TogglePause(error)) {
                return Failure(ErrorCode::ToolUnavailable, std::move(error));
            }
            return ToolResult::Success({{"state", "paused"}});
        }, PlayRequirement::PlayModeActive));

    registerTool(Definition(
        "play.resume", "Resume Play Mode from a paused state.",
        ObjectSchema(), [](ToolContext& context, const Json&) -> ToolOutcome {
            if (context.Backend().GetPlayState() != PlayState::Paused) {
                return ToolResult::Success({{"state", PlayStateName(context.Backend().GetPlayState())}});
            }
            std::string error;
            if (!context.Backend().TogglePause(error)) {
                return Failure(ErrorCode::ToolUnavailable, std::move(error));
            }
            return ToolResult::Success({{"state", "playing"}});
        }, PlayRequirement::PlayModeActive));

    registerTool(Definition(
        "play.step_frames",
        "Advance exactly N simulation frames with a fixed dt while Play Mode is active.",
        ObjectSchema({{"count", {{"type", "integer"}, {"minimum", 1}}},
                      {"fixedDt", {{"type", "number"}, {"minimum", 0.0}}}},
                     Json::array({"count"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            if (!arguments.contains("count") || !arguments["count"].is_number_integer() ||
                arguments["count"].get<std::int64_t>() < 1) {
                return Failure(ErrorCode::InvalidArguments, "count must be a positive integer");
            }
            float fixedDt = kDefaultFixedDt;
            if (arguments.contains("fixedDt") &&
                (!arguments["fixedDt"].is_number() || arguments["fixedDt"].get<float>() <= 0.0f)) {
                return Failure(ErrorCode::InvalidArguments, "fixedDt must be a positive number");
            }
            if (arguments.contains("fixedDt")) fixedDt = arguments["fixedDt"].get<float>();
            std::string error;
            const std::uint32_t count = static_cast<std::uint32_t>(arguments["count"].get<std::int64_t>());
            if (!context.Backend().StepFrames(count, fixedDt, error)) {
                return Failure(ErrorCode::ToolUnavailable, std::move(error));
            }
            return ToolResult::Success({{"advanced", count}, {"fixedDt", fixedDt}});
        }, PlayRequirement::PlayModeActive, HostMask::Editor | HostMask::Runtime));

    registerTool(Definition(
        "play.set_realtime",
        "Enable or disable wall-clock-driven gameplay simulation while Play Mode is active.",
        ObjectSchema({{"enabled", {{"type", "boolean"}}}}, Json::array({"enabled"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            if (!arguments.contains("enabled") || !arguments["enabled"].is_boolean()) {
                return Failure(ErrorCode::InvalidArguments, "enabled must be a boolean");
            }
            const bool enabled = arguments["enabled"].get<bool>();
            std::string error;
            if (!context.Backend().SetRealtime(enabled, error)) {
                return Failure(ErrorCode::ToolUnavailable, std::move(error));
            }
            return ToolResult::Success({{"realtime", enabled},
                                        {"executionMode", enabled ? "realtime" : "manual"}});
        }, PlayRequirement::PlayModeActive, HostMask::Editor | HostMask::Runtime));

    registerTool(Definition(
        "input.set_action",
        "Override a gameplay action value (0..1) while Play Mode is active.",
        ObjectSchema({{"action", {{"type", "string"}}},
                      {"value", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}}},
                     Json::array({"action", "value"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            if (!arguments.contains("action") || !arguments["action"].is_string() ||
                arguments["action"].get<std::string>().empty()) {
                return Failure(ErrorCode::InvalidArguments, "A non-empty action name is required");
            }
            float value = 0.0f;
            if (!RequireFloat(arguments, "value", value)) {
                return Failure(ErrorCode::InvalidArguments, "A numeric action value is required");
            }
            std::string error;
            if (!context.Backend().SetActionOverride(arguments["action"].get<std::string>(), value, error)) {
                return Failure(ErrorCode::ToolUnavailable, std::move(error));
            }
            return ToolResult::Success({{"action", arguments["action"]}, {"value", value}});
        }, PlayRequirement::PlayModeActive, HostMask::Editor | HostMask::Runtime));

    registerTool(Definition(
        "input.set_axis",
        "Override a gameplay axis value (-1..1) while Play Mode is active.",
        ObjectSchema({{"axis", {{"type", "string"}}},
                      {"value", {{"type", "number"}, {"minimum", -1.0}, {"maximum", 1.0}}}},
                     Json::array({"axis", "value"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            if (!arguments.contains("axis") || !arguments["axis"].is_string() ||
                arguments["axis"].get<std::string>().empty()) {
                return Failure(ErrorCode::InvalidArguments, "A non-empty axis name is required");
            }
            float value = 0.0f;
            if (!RequireFloat(arguments, "value", value)) {
                return Failure(ErrorCode::InvalidArguments, "A numeric axis value is required");
            }
            std::string error;
            if (!context.Backend().SetAxisOverride(arguments["axis"].get<std::string>(), value, error)) {
                return Failure(ErrorCode::ToolUnavailable, std::move(error));
            }
            return ToolResult::Success({{"axis", arguments["axis"]}, {"value", value}});
        }, PlayRequirement::PlayModeActive, HostMask::Editor | HostMask::Runtime));

    registerTool(Definition(
        "input.add_mouse_delta",
        "Add unclamped mouse pixel delta for the next gameplay frame while Play Mode is active.",
        ObjectSchema({{"x", {{"type", "number"}}}, {"y", {{"type", "number"}}}},
                     Json::array({"x", "y"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            float x = 0.0f;
            float y = 0.0f;
            if (!RequireFloat(arguments, "x", x) || !RequireFloat(arguments, "y", y)) {
                return Failure(ErrorCode::InvalidArguments, "Numeric x and y mouse delta values are required");
            }
            std::string error;
            if (!context.Backend().AddMouseDelta(static_cast<double>(x), static_cast<double>(y), error)) {
                return Failure(ErrorCode::ToolUnavailable, std::move(error));
            }
            return ToolResult::Success({{"x", x}, {"y", y}});
        }, PlayRequirement::PlayModeActive, HostMask::Editor | HostMask::Runtime));

    registerTool(Definition(
        "input.clear_overrides", "Clear all injected action and axis overrides.",
        ObjectSchema(), [](ToolContext& context, const Json&) -> ToolOutcome {
            std::string error;
            if (!context.Backend().ClearInputOverrides(error)) {
                return Failure(ErrorCode::ToolUnavailable, std::move(error));
            }
            return ToolResult::Success({{"cleared", true}});
        }, PlayRequirement::PlayModeActive, HostMask::Editor | HostMask::Runtime));
}

} // namespace Hephaestus::Automation
