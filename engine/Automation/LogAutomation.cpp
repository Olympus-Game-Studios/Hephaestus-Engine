#include "LogAutomation.h"

#include "AutomationService.h"
#include "Core/EngineLog.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace Hephaestus::Automation {
namespace {

const char* LevelName(EngineLogLevel level)
{
    switch (level) {
    case EngineLogLevel::Debug: return "debug";
    case EngineLogLevel::Warning: return "warning";
    case EngineLogLevel::Error: return "error";
    case EngineLogLevel::Fatal: return "fatal";
    case EngineLogLevel::Info: return "info";
    }
    return "info";
}

} // namespace

void RegisterLogAutomation(AutomationService& service)
{
    std::string error;
    ToolDefinition definition;
    definition.name = "log.get_entries";
    definition.description = "Read recent structured engine log entries.";
    definition.inputSchema = {
        {"type", "object"},
        {"properties", {{"after", {{"type", "integer"}}}, {"limit", {{"type", "integer"}}}}},
        {"additionalProperties", false},
    };
    definition.hostMask = HostMask::Editor | HostMask::Runtime;
    definition.playRequirement = PlayRequirement::Any;
    definition.mutatesProject = false;
    definition.handler = [](ToolContext&, const Json& arguments) -> ToolOutcome {
        const std::uint64_t after = arguments.value("after", std::uint64_t{0});
        const std::size_t limit = arguments.value("limit", std::size_t{200});
        EngineLogSnapshot snapshot = EngineLog::Snapshot(after, limit);
        Json entries = Json::array();
        for (const auto& entry : snapshot.entries) {
            entries.push_back({{"cursor", entry.cursor},
                               {"timestamp", entry.timestamp},
                               {"level", LevelName(entry.level)},
                               {"category", entry.category},
                               {"message", entry.message}});
        }
        return ToolResult::Success({{"entries", std::move(entries)},
                                    {"nextCursor", snapshot.nextCursor}});
    };
    if (!service.Registry().Register(std::move(definition), error)) {
        throw std::runtime_error(error);
    }
}

} // namespace Hephaestus::Automation
