#include "Automation/AutomationService.h"
#include "Automation/ComponentMetadata.h"

#include <chrono>
#include <iostream>
#include <string>

namespace Automation = Hephaestus::Automation;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

class FakeBackend final : public Automation::AutomationBackend {
public:
    Automation::HostMask host = Automation::HostMask::Editor;
    Hephaestus::PlayState state = Hephaestus::PlayState::Editor;
    Scene* scene = nullptr;

    Automation::HostMask Host() const noexcept override { return host; }
    Automation::SceneAccess ActiveScene() noexcept override { return {scene}; }
    Hephaestus::PlayState GetPlayState() const noexcept override { return state; }
};

class CompleteAfterFrame final : public Automation::DeferredOperation {
public:
    explicit CompleteAfterFrame(std::uint64_t target) : m_Target(target) {}

    std::optional<Automation::ToolResult> Poll(Automation::ToolContext& context) override
    {
        if (context.FrameIndex() < m_Target) return std::nullopt;
        return Automation::ToolResult::Success({{"frame", context.FrameIndex()}});
    }

private:
    std::uint64_t m_Target = 0;
};

Automation::ToolDefinition EchoTool(std::string name, Automation::HostMask host,
                                    Automation::PlayRequirement play = Automation::PlayRequirement::Any)
{
    Automation::ToolDefinition definition;
    definition.name = std::move(name);
    definition.hostMask = host;
    definition.playRequirement = play;
    definition.handler = [](Automation::ToolContext&, const Automation::Json& arguments) {
        return Automation::ToolResult::Success(arguments);
    };
    return definition;
}

} // namespace

int main()
{
    bool ok = true;
    FakeBackend backend;
    Automation::AutomationService service(backend);
    std::string error;

    ok &= Check(service.Registry().Register(
                    EchoTool("z.last", Automation::HostMask::Editor), error),
                "Register editor tool: " + error);
    ok &= Check(service.Registry().Register(
                    EchoTool("a.first", Automation::HostMask::Any), error),
                "Register shared tool: " + error);
    ok &= Check(service.Registry().Register(
                    EchoTool("runtime.only", Automation::HostMask::Runtime), error),
                "Register runtime tool: " + error);
    ok &= Check(service.Registry().Register(
                    EchoTool("play.active", Automation::HostMask::Editor,
                             Automation::PlayRequirement::PlayModeActive), error),
                "Register play tool: " + error);
    ok &= Check(!service.Registry().Register(
                    EchoTool("a.first", Automation::HostMask::Editor), error),
                "Duplicate tool is rejected");
    ok &= Check(!service.Registry().Register(
                    EchoTool("bad name", Automation::HostMask::Editor), error),
                "Invalid tool name is rejected");

    const auto editorTools = service.Registry().Available(
        Automation::HostMask::Editor, Hephaestus::PlayState::Editor);
    ok &= Check(editorTools.size() == 2, "Editor sees shared and editor-state tools only");
    ok &= Check(editorTools.size() == 2 && editorTools[0]->name == "a.first" &&
                    editorTools[1]->name == "z.last",
                "Available tools are deterministic");

    auto echo = service.Post({"a.first", {{"value", 42}}});
    ok &= Check(echo.result.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout,
                "Posted calls wait for the main-thread tick");
    service.Tick();
    const Automation::ToolResult echoResult = echo.result.get();
    ok &= Check(echoResult.Ok() && echoResult.value.value("value", 0) == 42,
                "Tick dispatches tool arguments and result");

    auto unavailable = service.Post({"runtime.only", Automation::Json::object()});
    service.Tick();
    ok &= Check(unavailable.result.get().error->code == Automation::ErrorCode::ToolUnavailable,
                "Host-incompatible tool cannot execute");

    auto gated = service.Post({"play.active", Automation::Json::object()});
    service.Tick();
    ok &= Check(gated.result.get().error->code == Automation::ErrorCode::PlayModeRequired,
                "Play-state requirement is enforced");

    Automation::ToolDefinition deferred;
    deferred.name = "wait.frame";
    deferred.hostMask = Automation::HostMask::Any;
    deferred.handler = [](Automation::ToolContext& context, const Automation::Json&) -> Automation::ToolOutcome {
        return std::make_unique<CompleteAfterFrame>(context.FrameIndex() + 2);
    };
    ok &= Check(service.Registry().Register(std::move(deferred), error),
                "Register deferred tool: " + error);
    auto wait = service.Post({"wait.frame", Automation::Json::object()});
    service.Tick();
    const auto firstWait = wait.result.wait_for(std::chrono::milliseconds(0));
    ok &= Check(firstWait == std::future_status::timeout, "Deferred call remains pending");
    if (firstWait == std::future_status::timeout) {
        service.Tick();
        ok &= Check(wait.result.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout,
                    "Deferred call waits until target frame");
        service.Tick();
    }
    const Automation::ToolResult waitResult = wait.result.get();
    if (!waitResult.Ok()) {
        std::cerr << "[INFO] Deferred result error: " << waitResult.error->message << '\n';
    }
    ok &= Check(waitResult.Ok(), "Deferred call completes on target frame");

    const auto& metadata = Automation::ComponentMetadataRegistry::Builtins();
    ok &= Check(metadata.Find("Transform") != nullptr, "Built-in component metadata is available");
    ok &= Check(metadata.FindProperty("Transform", "position") != nullptr,
                "Transform property metadata is available");

    auto cancelled = service.Post({"wait.frame", Automation::Json::object()});
    service.Tick();
    service.Shutdown();
    ok &= Check(cancelled.result.get().error->code == Automation::ErrorCode::Cancelled,
                "Shutdown cancels deferred requests");
    auto rejected = service.Post({"a.first", Automation::Json::object()});
    ok &= Check(rejected.result.get().error->code == Automation::ErrorCode::ServiceShuttingDown,
                "Posting after shutdown fails immediately");

    if (ok) {
        std::cout << "AutomationServiceTests passed\n";
        return 0;
    }
    return 1;
}
