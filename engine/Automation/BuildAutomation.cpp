#include "BuildAutomation.h"

#include "AutomationService.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace Hephaestus::Automation {
namespace {

using BuildProblem = AutomationBackend::BuildProblem;
using BuildRequestSpec = AutomationBackend::BuildRequestSpec;
using BuildStatus = AutomationBackend::BuildStatus;

Json ProblemJson(const BuildProblem& problem)
{
    return {{"severity", problem.severity},
            {"code", problem.code},
            {"message", problem.message},
            {"detail", problem.detail},
            {"assetPath", problem.assetPath}};
}

Json StatusJson(const BuildStatus& status)
{
    Json problems = Json::array();
    for (const BuildProblem& problem : status.problems) problems.push_back(ProblemJson(problem));
    Json log = Json::array();
    for (const std::string& line : status.logSummary) log.push_back(line);
    return {{"state", status.state},
            {"action", status.action},
            {"configuration", status.configuration},
            {"outputPath", status.outputPath},
            {"executablePath", status.executablePath},
            {"packageBytes", status.packageBytes},
            {"problems", std::move(problems)},
            {"logSummary", std::move(log)}};
}

ToolResult BuildFailure(const std::string& reason)
{
    if (reason == "no_project") {
        return ToolResult::Failure(ErrorCode::ToolUnavailable, "An open project is required",
                                   {{"reason", "no_project"}});
    }
    if (reason == "dirty") {
        return ToolResult::Failure(ErrorCode::InvalidArguments, "Save the scene before building",
                                   {{"reason", "dirty"}});
    }
    if (reason == "busy") {
        return ToolResult::Failure(ErrorCode::InvalidArguments, "A build is already running",
                                   {{"reason", "busy"}});
    }
    return ToolResult::Failure(ErrorCode::ToolUnavailable, "Build automation is unavailable",
                               {{"reason", "unavailable"}});
}

ToolResult FinishedBuild(const BuildStatus& status)
{
    if (status.state == "cancelled") {
        return ToolResult::Failure(ErrorCode::Cancelled, "Build was cancelled", StatusJson(status));
    }
    if (status.state == "failed") {
        const std::string message = status.action == "validate" ? "Validation failed" : "Build failed";
        return ToolResult::Failure(ErrorCode::InternalError, message, StatusJson(status));
    }
    return ToolResult::Success(StatusJson(status));
}

bool ReadConfiguration(const Json& arguments, std::string& configuration, ToolResult& error)
{
    configuration = arguments.value("configuration", "Development");
    if (configuration != "Development" && configuration != "Release") {
        error = ToolResult::Failure(ErrorCode::InvalidArguments,
                                    "configuration must be Development or Release");
        return false;
    }
    return true;
}

BuildRequestSpec ReadRequest(const Json& arguments, std::string action, std::string configuration)
{
    BuildRequestSpec request;
    request.action = std::move(action);
    request.configuration = std::move(configuration);
    request.startupScene = arguments.value("startupScene", "");
    request.outputRoot = arguments.value("output", "");
    request.enableRuntimeAutomation = arguments.value("enableRuntimeAutomation", false);
    return request;
}

class BuildOperation final : public DeferredOperation {
public:
    std::optional<ToolResult> Poll(ToolContext& context) override
    {
        const BuildStatus status = context.Backend().GetBuildStatus();
        if (status.state == "running") return std::nullopt;
        return FinishedBuild(status);
    }
};

ToolOutcome Start(ToolContext& context, const BuildRequestSpec& request)
{
    if (request.enableRuntimeAutomation && request.configuration == "Release") {
        return ToolResult::Failure(
            ErrorCode::InvalidArguments,
            "Runtime automation cannot be enabled in a Release package",
            {{"reason", "BUILD_RUNTIME_AUTOMATION_NOT_ALLOWED"}});
    }
    std::string error;
    if (!context.Backend().StartBuild(request, error)) return BuildFailure(error);
    const BuildStatus status = context.Backend().GetBuildStatus();
    if (status.state != "running") return FinishedBuild(status);
    return std::make_unique<BuildOperation>();
}

Json BuildSchema()
{
    return {{"type", "object"},
            {"properties", {{"configuration", {{"type", "string"}}},
                            {"startupScene", {{"type", "string"}}},
                            {"output", {{"type", "string"}}},
                            {"enableRuntimeAutomation", {{"type", "boolean"}}}}},
            {"additionalProperties", false}};
}

void RegisterOne(AutomationService& service, ToolDefinition definition)
{
    std::string error;
    if (!service.Registry().Register(std::move(definition), error)) {
        throw std::runtime_error(error);
    }
}

ToolHandler BuildHandler(std::string action)
{
    return [action = std::move(action)](ToolContext& context, const Json& arguments) -> ToolOutcome {
        std::string configuration;
        ToolResult error = ToolResult::Success();
        if (!ReadConfiguration(arguments, configuration, error)) return error;
        return Start(context, ReadRequest(arguments, action, configuration));
    };
}

} // namespace

void RegisterBuildAutomation(AutomationService& service)
{
    ToolDefinition validate;
    validate.name = "build.validate";
    validate.description = "Validate the open project with the in-process build service.";
    validate.inputSchema = BuildSchema();
    validate.hostMask = HostMask::Editor;
    validate.playRequirement = PlayRequirement::Any;
    validate.mutatesProject = false;
    validate.handler = BuildHandler("validate");
    RegisterOne(service, std::move(validate));

    ToolDefinition package;
    package.name = "build.package";
    package.description =
        "Package the open project. Release packages cannot enable runtime automation.";
    package.inputSchema = BuildSchema();
    package.hostMask = HostMask::Editor;
    package.playRequirement = PlayRequirement::Any;
    package.mutatesProject = false;
    package.handler = BuildHandler("package");
    RegisterOne(service, std::move(package));

    ToolDefinition clean;
    clean.name = "build.clean";
    clean.description = "Remove the last package output for the open project.";
    clean.inputSchema = BuildSchema();
    clean.hostMask = HostMask::Editor;
    clean.playRequirement = PlayRequirement::Any;
    clean.mutatesProject = false;
    clean.handler = BuildHandler("clean");
    RegisterOne(service, std::move(clean));

    ToolDefinition status;
    status.name = "build.get_status";
    status.description = "Report the current or most recent editor build.";
    status.inputSchema = {{"type", "object"}, {"additionalProperties", false}};
    status.hostMask = HostMask::Editor;
    status.playRequirement = PlayRequirement::Any;
    status.mutatesProject = false;
    status.handler = [](ToolContext& context, const Json&) -> ToolOutcome {
        return ToolResult::Success(StatusJson(context.Backend().GetBuildStatus()));
    };
    RegisterOne(service, std::move(status));
}

} // namespace Hephaestus::Automation
