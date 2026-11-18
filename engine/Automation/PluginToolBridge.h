#pragma once

/**
 * @file PluginToolBridge.h
 * @brief Lets native plugins add automation (MCP) tools through Plugins::IAutomationToolService.
 */

#include "ToolRegistry.h"
#include "PluginAPI/PluginAPI.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace Hephaestus::Automation {

class PluginToolBridge : public std::enable_shared_from_this<PluginToolBridge> {
public:
    explicit PluginToolBridge(ToolRegistry& registry) : m_Registry(registry) {}

    /// Registers (or brings back after its plugin reloads) a tool owned by one plugin.
    bool Register(const std::string& owner, const Plugins::AutomationToolDescriptor& descriptor,
                  std::string& outError);
    /// Answers a call; false when the call is unknown, already answered or cancelled.
    bool Complete(std::uint64_t callId, bool success, const char* resultJson);
    /// Hides the owner's tools and fails its unanswered calls. Call before the plugin's code is unloaded.
    void RemoveOwner(const std::string& owner);

private:
    struct Binding {
        std::string owner;
        Plugins::AutomationToolCallback call = nullptr;
        void* userData = nullptr;
    };
    struct Call {
        std::string owner;
        std::optional<ToolResult> result;
    };
    class PendingOperation;

    ToolOutcome Invoke(const std::string& name, const Json& arguments);
    std::optional<ToolResult> TakeResult(std::uint64_t callId);

    ToolRegistry& m_Registry;
    std::map<std::string, Binding, std::less<>> m_Bindings;
    std::set<std::string, std::less<>> m_PluginToolNames;
    std::map<std::uint64_t, Call> m_Calls;
    std::uint64_t m_NextCall = 1;
};

} // namespace Hephaestus::Automation
