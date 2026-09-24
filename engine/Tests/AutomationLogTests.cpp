#include "Automation/AutomationService.h"
#include "Automation/LogAutomation.h"
#include "Core/EngineLog.h"

#include <iostream>
#include <string>
#include <utility>

namespace Automation = Hephaestus::Automation;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

class LogBackend final : public Automation::AutomationBackend {
public:
    Automation::HostMask Host() const noexcept override { return Automation::HostMask::Editor; }
    Automation::SceneAccess ActiveScene() noexcept override { return {}; }
    Hephaestus::PlayState GetPlayState() const noexcept override
    {
        return Hephaestus::PlayState::Editor;
    }
};

Automation::ToolResult Call(Automation::AutomationService& service, std::string name,
                            Automation::Json arguments = Automation::Json::object())
{
    auto call = service.Post({std::move(name), std::move(arguments)});
    service.Tick();
    return call.result.get();
}

} // namespace

int main()
{
    Hephaestus::EngineLog::Write(Hephaestus::EngineLogLevel::Info, "automation log alpha");
    Hephaestus::EngineLog::Write(Hephaestus::EngineLogLevel::Warning, "automation log beta", "test");

    bool ok = true;
    LogBackend backend;
    Automation::AutomationService service(backend);
    Automation::RegisterLogAutomation(service);

    auto result = Call(service, "log.get_entries", {{"limit", 100}});
    ok &= Check(result.Ok(), "log.get_entries returns successfully");
    ok &= Check(result.value["entries"].is_array() && result.value["entries"].size() >= 2,
                "log.get_entries returns the recorded entries");
    bool foundAlpha = false;
    bool foundBeta = false;
    bool betaHasLevel = false;
    bool betaHasCategory = false;
    for (const auto& entry : result.value["entries"]) {
        const std::string message = entry.value("message", "");
        if (message == "automation log alpha") foundAlpha = true;
        if (message == "automation log beta") {
            foundBeta = true;
            betaHasLevel = entry.value("level", "") == "warning";
            betaHasCategory = entry.value("category", "") == "test";
        }
    }
    ok &= Check(foundAlpha && foundBeta, "log.get_entries returns the expected messages");
    ok &= Check(betaHasLevel && betaHasCategory,
                "log.get_entries maps level and category");

    auto filtered = Call(service, "log.get_entries", {{"after", result.value["nextCursor"]}});
    ok &= Check(filtered.Ok() && filtered.value["entries"].is_array() &&
                    filtered.value["entries"].size() == 0,
                "log.get_entries respects the after-cursor filter");

    service.Shutdown();
    if (ok) {
        std::cout << "AutomationLogTests passed\n";
        return 0;
    }
    return 1;
}
