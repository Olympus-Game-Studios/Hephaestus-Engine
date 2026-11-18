/**
 * @file PluginToolBridge.cpp
 * @brief Lets native plugins add automation (MCP) tools through Plugins::IAutomationToolService.
 */

#include "PluginToolBridge.h"

namespace Hephaestus::Automation {

class PluginToolBridge::PendingOperation final : public DeferredOperation {
public:
    PendingOperation(std::weak_ptr<PluginToolBridge> bridge, std::uint64_t callId)
        : m_Bridge(std::move(bridge)), m_CallId(callId) {}

    std::optional<ToolResult> Poll(ToolContext&) override {
        const auto bridge = m_Bridge.lock();
        if (!bridge) {
            return ToolResult::Failure(ErrorCode::ToolUnavailable, "The plugin that provides this tool was unloaded");
        }
        return bridge->TakeResult(m_CallId);
    }

    void Cancel() noexcept override {
        if (const auto bridge = m_Bridge.lock()) {
            bridge->m_Calls.erase(m_CallId);
        }
    }

private:
    std::weak_ptr<PluginToolBridge> m_Bridge;
    std::uint64_t m_CallId = 0;
};

bool PluginToolBridge::Register(const std::string& owner, const Plugins::AutomationToolDescriptor& descriptor,
                                std::string& outError) {
    if (!descriptor.name || !descriptor.call) {
        outError = "A plugin tool needs a name and a call callback";
        return false;
    }
    const std::string name = descriptor.name;
    if (m_Bindings.contains(name)) {
        outError = "Tool is already provided by another plugin: " + name;
        return false;
    }
    if (m_Registry.Contains(name)) {
        if (!m_PluginToolNames.contains(name)) {
            outError = "Tool name is taken by an engine tool: " + name;
            return false;
        }
        m_Registry.SetEnabled(name, true);
    } else {
        ToolDefinition definition;
        definition.name = name;
        definition.description = descriptor.description ? descriptor.description : "";
        definition.inputSchema = Json::parse(descriptor.inputSchemaJson ? descriptor.inputSchemaJson : "{}",
                                             nullptr, false);
        if (definition.inputSchema.is_discarded()) {
            outError = "Tool input schema is not valid JSON: " + name;
            return false;
        }
        if (!definition.inputSchema.contains("type")) {
            definition.inputSchema["type"] = "object";
        }
        definition.hostMask = HostMask::Editor;
        definition.mutatesProject = descriptor.mutatesProject;
        definition.playRequirement = descriptor.mutatesProject ? PlayRequirement::EditorOnly : PlayRequirement::Any;
        // The handler outlives this bridge inside the registry, so it holds the bridge weakly.
        definition.handler = [bridge = weak_from_this(), name](ToolContext&, const Json& arguments) -> ToolOutcome {
            if (const auto self = bridge.lock()) {
                return self->Invoke(name, arguments);
            }
            return ToolResult::Failure(ErrorCode::ToolUnavailable, "The plugin that provides this tool was unloaded");
        };
        if (!m_Registry.Register(std::move(definition), outError)) {
            return false;
        }
        m_PluginToolNames.insert(name);
    }
    m_Bindings[name] = {owner, descriptor.call, descriptor.userData};
    return true;
}

ToolOutcome PluginToolBridge::Invoke(const std::string& name, const Json& arguments) {
    const auto binding = m_Bindings.find(name);
    if (binding == m_Bindings.end()) {
        return ToolResult::Failure(ErrorCode::ToolUnavailable, "The plugin that provides " + name + " is not loaded");
    }
    const std::uint64_t callId = m_NextCall++;
    m_Calls[callId].owner = binding->second.owner;
    const std::string text = arguments.dump();
    binding->second.call(text.c_str(), callId, binding->second.userData);
    if (std::optional<ToolResult> result = TakeResult(callId)) {
        return std::move(*result);
    }
    return std::make_unique<PendingOperation>(weak_from_this(), callId);
}

std::optional<ToolResult> PluginToolBridge::TakeResult(std::uint64_t callId) {
    const auto call = m_Calls.find(callId);
    if (call == m_Calls.end()) {
        return ToolResult::Failure(ErrorCode::Cancelled, "The plugin call was cancelled");
    }
    if (!call->second.result) {
        return std::nullopt;
    }
    ToolResult result = std::move(*call->second.result);
    m_Calls.erase(call);
    return result;
}

bool PluginToolBridge::Complete(std::uint64_t callId, bool success, const char* resultJson) {
    const auto call = m_Calls.find(callId);
    if (call == m_Calls.end() || call->second.result) {
        return false;
    }
    const std::string text = resultJson ? resultJson : "";
    if (!success) {
        call->second.result = ToolResult::Failure(ErrorCode::InvalidArguments, text.empty() ? "Tool failed" : text);
        return true;
    }
    Json value = Json::parse(text.empty() ? "{}" : text, nullptr, false);
    call->second.result = value.is_object()
        ? ToolResult::Success(std::move(value))
        : ToolResult::Failure(ErrorCode::InternalError, "The plugin returned a result that is not a JSON object");
    return true;
}

void PluginToolBridge::RemoveOwner(const std::string& owner) {
    for (auto binding = m_Bindings.begin(); binding != m_Bindings.end();) {
        if (binding->second.owner == owner) {
            m_Registry.SetEnabled(binding->first, false);
            binding = m_Bindings.erase(binding);
        } else {
            ++binding;
        }
    }
    for (auto& [id, call] : m_Calls) {
        if (call.owner == owner && !call.result) {
            call.result = ToolResult::Failure(ErrorCode::ToolUnavailable,
                                              "The plugin that provides this tool was unloaded");
        }
    }
}

} // namespace Hephaestus::Automation
