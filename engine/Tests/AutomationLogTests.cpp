#include "Automation/AutomationService.h"
#include "Automation/LogAutomation.h"
#include "Core/EngineLog.h"
#include "Core/RuntimeBootstrap.h"

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

    const std::uint64_t pageStart = result.value["nextCursor"].get<std::uint64_t>();
    for (int index = 0; index < 5; ++index) {
        Hephaestus::EngineLog::Write(Hephaestus::EngineLogLevel::Info,
                                     "page-" + std::to_string(index));
    }
    std::vector<std::string> paged;
    std::uint64_t cursor = pageStart;
    for (int page = 0; page < 4; ++page) {
        auto slice = Call(service, "log.get_entries", {{"after", cursor}, {"limit", 2}});
        ok &= Check(slice.Ok(), "paged log.get_entries succeeds");
        if (!slice.Ok()) break;
        for (const auto& entry : slice.value["entries"]) {
            paged.push_back(entry.value("message", ""));
        }
        const std::uint64_t next = slice.value["nextCursor"].get<std::uint64_t>();
        if (next == cursor && slice.value["entries"].empty()) break;
        cursor = next;
    }
    ok &= Check(paged.size() == 5 && paged.front() == "page-0" && paged.back() == "page-4",
                "log.get_entries pages through a limited window without skipping");

    Hephaestus::EngineLog::RecordOnly(Hephaestus::EngineLogLevel::Info,
                                      "HUI_RUNTIME_TEST_PLAY_CLICKED", "Lua");
    Hephaestus::Runtime::Bootstrap::Log(Hephaestus::Runtime::LogLevel::Info,
                                        "packaged runtime marker");
    auto bridged = Call(service, "log.get_entries", {{"after", cursor}, {"limit", 20}});
    bool foundLua = false;
    bool foundRuntime = false;
    if (bridged.Ok()) {
        for (const auto& entry : bridged.value["entries"]) {
            if (entry.value("message", "") == "HUI_RUNTIME_TEST_PLAY_CLICKED" &&
                entry.value("category", "") == "Lua") {
                foundLua = true;
            }
            if (entry.value("message", "") == "packaged runtime marker" &&
                entry.value("category", "") == "Runtime" &&
                entry.value("level", "") == "info") {
                foundRuntime = true;
            }
        }
    }
    ok &= Check(foundLua, "Lua engine_log records a structured Lua entry");
    ok &= Check(foundRuntime, "runtime bootstrap logs record a structured Runtime entry");

    service.Shutdown();
    if (ok) {
        std::cout << "AutomationLogTests passed\n";
        return 0;
    }
    return 1;
}
