#include "TestAutomation.h"

#include "AutomationService.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace Hephaestus::Automation {
namespace {

using TestRunStatus = AutomationBackend::TestRunStatus;

Json StatusJson(const TestRunStatus& status)
{
    return {{"state", status.state},
            {"name", status.name},
            {"configuration", status.configuration},
            {"exitCode", status.exitCode},
            {"output", status.output}};
}

bool ValidConfig(const std::string& configuration)
{
    return configuration == "Debug" || configuration == "Release" ||
           configuration == "RelWithDebInfo" || configuration == "MinSizeRel";
}

bool ValidName(const std::string& name)
{
    if (name.empty() || name.size() > 128) return false;
    for (const char ch : name) {
        const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                        (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-';
        if (!ok) return false;
    }
    return true;
}

ToolResult TestFailure(const std::string& reason)
{
    if (reason == "busy") {
        return ToolResult::Failure(ErrorCode::InvalidArguments, "A test is already running",
                                   {{"reason", "busy"}});
    }
    if (reason == "not_found") {
        return ToolResult::Failure(ErrorCode::InvalidArguments, "Test was not found",
                                   {{"reason", "not_found"}});
    }
    return ToolResult::Failure(ErrorCode::ToolUnavailable, "Test automation is unavailable",
                               {{"reason", "unavailable"}});
}

ToolResult FinishedTest(const TestRunStatus& status)
{
    if (status.state == "failed") {
        return ToolResult::Failure(ErrorCode::InternalError, "Test failed", StatusJson(status));
    }
    return ToolResult::Success(StatusJson(status));
}

class TestOperation final : public DeferredOperation {
public:
    std::optional<ToolResult> Poll(ToolContext& context) override
    {
        const TestRunStatus status = context.Backend().GetTestStatus();
        if (status.state == "running") return std::nullopt;
        return FinishedTest(status);
    }
};

void RegisterOne(AutomationService& service, ToolDefinition definition)
{
    std::string error;
    if (!service.Registry().Register(std::move(definition), error)) {
        throw std::runtime_error(error);
    }
}

} // namespace

void RegisterTestAutomation(AutomationService& service)
{
    ToolDefinition list;
    list.name = "test.list";
    list.description = "List CTest names in the engine build tree.";
    list.inputSchema = {{"type", "object"},
                        {"properties", {{"configuration", {{"type", "string"}}}}},
                        {"additionalProperties", false}};
    list.hostMask = HostMask::Editor;
    list.playRequirement = PlayRequirement::Any;
    list.mutatesProject = false;
    list.handler = [](ToolContext& context, const Json& arguments) -> ToolOutcome {
        const std::string configuration = arguments.value("configuration", "Release");
        if (!ValidConfig(configuration)) {
            return ToolResult::Failure(ErrorCode::InvalidArguments,
                                       "configuration must be a CMake config name");
        }
        std::vector<std::string> names;
        std::string error;
        if (!context.Backend().ListTests(configuration, names, error)) return TestFailure(error);
        Json tests = Json::array();
        for (const std::string& name : names) tests.push_back(name);
        return ToolResult::Success({{"configuration", configuration}, {"tests", std::move(tests)}});
    };
    RegisterOne(service, std::move(list));

    ToolDefinition run;
    run.name = "test.run";
    run.description = "Run one CTest from the engine build tree by exact name.";
    run.inputSchema = {{"type", "object"},
                       {"properties", {{"name", {{"type", "string"}}},
                                       {"configuration", {{"type", "string"}}}}},
                       {"required", {"name"}},
                       {"additionalProperties", false}};
    run.hostMask = HostMask::Editor;
    run.playRequirement = PlayRequirement::Any;
    run.mutatesProject = false;
    run.handler = [](ToolContext& context, const Json& arguments) -> ToolOutcome {
        if (!arguments.contains("name") || !arguments["name"].is_string()) {
            return ToolResult::Failure(ErrorCode::InvalidArguments, "name is required");
        }
        const std::string name = arguments["name"].get<std::string>();
        const std::string configuration = arguments.value("configuration", "Release");
        if (!ValidName(name)) {
            return ToolResult::Failure(ErrorCode::InvalidArguments, "name is not a CTest identifier");
        }
        if (!ValidConfig(configuration)) {
            return ToolResult::Failure(ErrorCode::InvalidArguments,
                                       "configuration must be a CMake config name");
        }
        std::string error;
        if (!context.Backend().StartTest(name, configuration, error)) return TestFailure(error);
        const TestRunStatus status = context.Backend().GetTestStatus();
        if (status.state != "running") return FinishedTest(status);
        return std::make_unique<TestOperation>();
    };
    RegisterOne(service, std::move(run));

    ToolDefinition status;
    status.name = "test.get_status";
    status.description = "Report the current or most recent CTest run.";
    status.inputSchema = {{"type", "object"}, {"additionalProperties", false}};
    status.hostMask = HostMask::Editor;
    status.playRequirement = PlayRequirement::Any;
    status.mutatesProject = false;
    status.handler = [](ToolContext& context, const Json&) -> ToolOutcome {
        return ToolResult::Success(StatusJson(context.Backend().GetTestStatus()));
    };
    RegisterOne(service, std::move(status));
}

} // namespace Hephaestus::Automation
