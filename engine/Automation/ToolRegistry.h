#pragma once

#include "AutomationTypes.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Hephaestus {
enum class PlayState;
}

namespace Hephaestus::Automation {

class AutomationBackend;
class ToolContext;

class DeferredOperation {
public:
    virtual ~DeferredOperation() = default;
    virtual std::optional<ToolResult> Poll(ToolContext& context) = 0;
    virtual void Cancel() noexcept {}
};

using ToolOutcome = std::variant<ToolResult, std::unique_ptr<DeferredOperation>>;
using ToolHandler = std::function<ToolOutcome(ToolContext&, const Json&)>;

struct ToolDefinition {
    std::string name;
    std::string title;
    std::string description;
    Json inputSchema = {{"type", "object"}};
    std::optional<Json> outputSchema;
    HostMask hostMask = HostMask::None;
    PlayRequirement playRequirement = PlayRequirement::Any;
    std::vector<std::string> requiredClientCapabilities;
    bool mutatesProject = false;
    ToolHandler handler;
};

class ToolRegistry {
public:
    bool Register(ToolDefinition definition, std::string& outError);
    [[nodiscard]] const ToolDefinition* Find(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<const ToolDefinition*> Available(HostMask host,
                                                               PlayState state) const;
    [[nodiscard]] std::size_t Size() const noexcept { return m_Tools.size(); }

private:
    std::map<std::string, ToolDefinition, std::less<>> m_Tools;
};

} // namespace Hephaestus::Automation
