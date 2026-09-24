#include "RuntimeAutomation.h"

#include "AutomationService.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace Hephaestus::Automation {
namespace {

std::string PlayStateName(PlayState state)
{
    switch (state) {
    case PlayState::Playing: return "playing";
    case PlayState::Paused: return "paused";
    case PlayState::Editor: return "editor";
    }
    return "editor";
}

} // namespace

void RegisterRuntimeAutomation(AutomationService& service)
{
    std::string error;
    ToolDefinition status;
    status.name = "runtime.get_status";
    status.description = "Report the packaged runtime's current state.";
    status.inputSchema = {{"type", "object"}};
    status.hostMask = HostMask::Runtime;
    status.playRequirement = PlayRequirement::Any;
    status.mutatesProject = false;
    status.handler = [](ToolContext& context, const Json&) -> ToolOutcome {
        return ToolResult::Success({{"state", PlayStateName(context.Backend().GetPlayState())},
                                    {"frame", context.FrameIndex()},
                                    {"project", context.Backend().GetProjectInfo()}});
    };
    if (!service.Registry().Register(std::move(status), error)) {
        throw std::runtime_error(error);
    }
}

} // namespace Hephaestus::Automation
