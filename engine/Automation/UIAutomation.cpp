#include "UIAutomation.h"

#include "AutomationService.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace Hephaestus::Automation {
namespace {

using UIBounds = AutomationBackend::UIBounds;
using UINode = AutomationBackend::UINode;
using UIScreenNode = AutomationBackend::UIScreenNode;
using UISelector = AutomationBackend::UISelector;

constexpr std::uint64_t kClickTimeoutTicks = 300;

const char* kWidgetTypes[] = {
    "Panel", "Canvas", "VerticalBox", "HorizontalBox", "Grid", "Text", "Button", "Image",
};

ToolResult UIFailure(const std::string& reason)
{
    if (reason == "not_found") {
        return ToolResult::Failure(ErrorCode::InvalidArguments, "No live UI element matches the selector",
                                   {{"reason", "not_found"}});
    }
    if (reason == "ambiguous") {
        return ToolResult::Failure(ErrorCode::InvalidArguments, "UI element selector matches more than one element",
                                   {{"reason", "ambiguous"}});
    }
    if (reason == "not_hittable") {
        return ToolResult::Failure(ErrorCode::InvalidArguments,
                                   "UI element is hidden, disabled, inactive, or not the hit target",
                                   {{"reason", "not_hittable"}});
    }
    if (reason == "no_bounds") {
        return ToolResult::Failure(ErrorCode::InvalidArguments, "UI element has no layout bounds",
                                   {{"reason", "no_bounds"}});
    }
    return ToolResult::Failure(ErrorCode::ToolUnavailable, "Runtime UI is unavailable",
                               {{"reason", "unavailable"}});
}

Json NodeJson(const UINode& node)
{
    Json children = Json::array();
    for (const UINode& child : node.children) children.push_back(NodeJson(child));
    return {{"id", node.id},
            {"type", node.type},
            {"text", node.text},
            {"visible", node.visible},
            {"enabled", node.enabled},
            {"interactable", node.interactable},
            {"bounds", {{"x", node.bounds.x}, {"y", node.bounds.y},
                        {"width", node.bounds.width}, {"height", node.bounds.height}}},
            {"screen", node.screen},
            {"children", std::move(children)}};
}

Json NodeSummaryJson(const UINode& node)
{
    Json summary = NodeJson(node);
    summary.erase("children");
    return summary;
}

Json SelectorSchema()
{
    return {{"type", "object"},
            {"properties", {{"id", {{"type", "string"}}},
                            {"type", {{"type", "string"}}},
                            {"text", {{"type", "string"}}},
                            {"screen", {{"type", "string"}}}}},
            {"additionalProperties", false}};
}

bool ReadSelector(const Json& arguments, UISelector& selector, std::string& error, bool requireFilter)
{
    selector = {};
    auto read = [&](const char* key, std::string& destination) {
        if (!arguments.contains(key)) return true;
        if (!arguments[key].is_string()) {
            error = std::string(key) + " must be a string";
            return false;
        }
        destination = arguments[key].get<std::string>();
        return true;
    };
    if (!read("id", selector.id) || !read("type", selector.type) ||
        !read("text", selector.text) || !read("screen", selector.screen)) {
        return false;
    }
    if (!selector.type.empty()) {
        bool known = false;
        for (const char* type : kWidgetTypes) {
            if (selector.type == type) known = true;
        }
        if (!known) {
            error = "type is not a live HUI widget type";
            return false;
        }
    }
    if (requireFilter && selector.id.empty() && selector.type.empty() &&
        selector.text.empty() && selector.screen.empty()) {
        error = "At least one of id, type, text, or screen is required";
        return false;
    }
    return true;
}

class ClickOperation final : public DeferredOperation {
public:
    ClickOperation(AutomationBackend& backend, std::uint64_t serial, std::uint64_t token, Json value)
        : m_Backend(backend), m_Serial(serial), m_Token(token), m_Value(std::move(value))
    {
    }

    std::optional<ToolResult> Poll(ToolContext& context) override
    {
        if (m_Token == 0) {
            return ToolResult::Failure(ErrorCode::Cancelled, "UI click was cancelled");
        }
        if (m_FirstFrame == 0) m_FirstFrame = context.FrameIndex();
        if (context.Backend().RuntimeUIUpdateSerial() > m_Serial) {
            m_Token = 0;
            return ToolResult::Success(m_Value);
        }
        if (context.FrameIndex() - m_FirstFrame <= kClickTimeoutTicks) return std::nullopt;
        context.Backend().CancelRuntimeUIClick(m_Token);
        m_Token = 0;
        return ToolResult::Failure(ErrorCode::ToolUnavailable,
                                   "Runtime UI did not update within 300 automation ticks");
    }

    void Cancel() noexcept override
    {
        if (m_Token == 0) return;
        m_Backend.CancelRuntimeUIClick(m_Token);
        m_Token = 0;
    }

private:
    AutomationBackend& m_Backend;
    std::uint64_t m_Serial = 0;
    std::uint64_t m_Token = 0;
    std::uint64_t m_FirstFrame = 0;
    Json m_Value;
};

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

void RegisterUIAutomation(AutomationService& service)
{
    const Json empty = {{"type", "object"}, {"properties", Json::object()}, {"additionalProperties", false}};
    Add(service, "ui.get_tree", "List the live runtime HUI screens and elements.", empty,
        [](ToolContext& context, const Json&) -> ToolOutcome {
            std::vector<UIScreenNode> screens;
            std::string error;
            if (!context.Backend().CopyRuntimeUI(screens, error)) return UIFailure(error);
            Json tree = Json::array();
            for (const UIScreenNode& screen : screens) {
                Json children = Json::array();
                for (const UINode& child : screen.children) children.push_back(NodeJson(child));
                tree.push_back({{"name", screen.name},
                                {"active", screen.active},
                                {"visible", screen.visible},
                                {"children", std::move(children)}});
            }
            return ToolResult::Success({{"screens", std::move(tree)}});
        });

    Add(service, "ui.find", "Find live HUI elements by id, type, text, or screen.", SelectorSchema(),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            UISelector selector;
            std::string error;
            if (!ReadSelector(arguments, selector, error, true)) {
                return ToolResult::Failure(ErrorCode::InvalidArguments, std::move(error));
            }
            std::vector<UIScreenNode> screens;
            if (!context.Backend().CopyRuntimeUI(screens, error)) return UIFailure(error);
            Json matches = Json::array();
            auto visit = [&](auto&& self, const UINode& node) -> void {
                const bool idOk = selector.id.empty() || selector.id == node.id;
                const bool typeOk = selector.type.empty() || selector.type == node.type;
                const bool textOk = selector.text.empty() || selector.text == node.text;
                const bool screenOk = selector.screen.empty() || selector.screen == node.screen;
                if (idOk && typeOk && textOk && screenOk) matches.push_back(NodeSummaryJson(node));
                for (const UINode& child : node.children) self(self, child);
            };
            for (const UIScreenNode& screen : screens) {
                for (const UINode& child : screen.children) visit(visit, child);
            }
            return ToolResult::Success({{"matches", std::move(matches)}});
        });

    Add(service, "ui.get", "Return one live HUI element.", SelectorSchema(),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            UISelector selector;
            std::string error;
            if (!ReadSelector(arguments, selector, error, true)) {
                return ToolResult::Failure(ErrorCode::InvalidArguments, std::move(error));
            }
            UINode node;
            if (!context.Backend().ResolveRuntimeUI(selector, node, error)) return UIFailure(error);
            return ToolResult::Success(NodeJson(node));
        });

    Add(service, "ui.click",
        "Click a live HUI element by queueing press and release into UIContext. "
        "The click is dispatched by the next UISystem update.",
        SelectorSchema(),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            UISelector selector;
            std::string error;
            if (!ReadSelector(arguments, selector, error, true)) {
                return ToolResult::Failure(ErrorCode::InvalidArguments, std::move(error));
            }
            UIBounds bounds;
            float x = 0.0f;
            float y = 0.0f;
            std::uint64_t serial = 0;
            std::uint64_t token = 0;
            std::string id;
            std::string screen;
            if (!context.Backend().QueueRuntimeUIClick(selector, bounds, x, y, serial, token, id, screen, error)) {
                return UIFailure(error);
            }
            Json value = {{"dispatched", true},
                          {"id", std::move(id)},
                          {"screen", std::move(screen)},
                          {"point", Json::array({x, y})},
                          {"button", 0}};
            return std::make_unique<ClickOperation>(context.Backend(), serial, token, std::move(value));
        });
}

} // namespace Hephaestus::Automation
