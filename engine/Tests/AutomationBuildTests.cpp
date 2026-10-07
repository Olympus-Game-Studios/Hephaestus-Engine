#include "Automation/AutomationService.h"
#include "Automation/BuildAutomation.h"
#include "Automation/TestAutomation.h"

#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace Automation = Hephaestus::Automation;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

class BuildBackend final : public Automation::AutomationBackend {
public:
    explicit BuildBackend(Automation::HostMask host) : m_host(host) {}

    Automation::HostMask Host() const noexcept override { return m_host; }
    Automation::SceneAccess ActiveScene() noexcept override { return {}; }
    Hephaestus::PlayState GetPlayState() const noexcept override
    {
        return Hephaestus::PlayState::Editor;
    }

    bool StartBuild(const Automation::AutomationBackend::BuildRequestSpec& request,
                    std::string& outError) override
    {
        if (m_build.state == "running") {
            outError = "busy";
            return false;
        }
        ++m_buildStarts;
        m_lastRequest = request;
        m_build.action = request.action;
        m_build.configuration = request.configuration;
        m_build.outputPath = request.outputRoot;
        m_build.state = "running";
        m_buildPollsLeft = 2;
        m_build.problems.clear();
        if (request.action == "validate") {
            m_build.problems.push_back({"warning", "NOTICE", "checked", "", ""});
        }
        return true;
    }

    Automation::AutomationBackend::BuildStatus GetBuildStatus() override
    {
        if (m_build.state == "running" && m_buildPollsLeft > 0 && --m_buildPollsLeft == 0) {
            m_build.state = "succeeded";
            m_build.packageBytes = m_build.action == "package" ? 12 : 0;
        }
        return m_build;
    }

    bool ListTests(const std::string& configuration, std::vector<std::string>& out,
                   std::string&) override
    {
        m_listedConfiguration = configuration;
        out = {"AutomationLog", "AutomationBuild"};
        return true;
    }

    bool StartTest(const std::string& name, const std::string& configuration,
                   std::string& outError) override
    {
        if (m_test.state == "running") {
            outError = "busy";
            return false;
        }
        if (name != "AutomationLog") {
            outError = "not_found";
            return false;
        }
        m_test.state = "running";
        m_test.name = name;
        m_test.configuration = configuration;
        m_test.exitCode = 0;
        m_test.output = "passed";
        m_testPollsLeft = 2;
        return true;
    }

    Automation::AutomationBackend::TestRunStatus GetTestStatus() override
    {
        if (m_test.state == "running" && m_testPollsLeft > 0 && --m_testPollsLeft == 0) {
            m_test.state = "passed";
        }
        return m_test;
    }

    void HoldBuild() { m_build.state = "running"; }
    void ReleaseBuild() { m_build.state = "idle"; }

    int m_buildStarts = 0;
    std::string m_listedConfiguration;
    Automation::AutomationBackend::BuildRequestSpec m_lastRequest;

private:
    Automation::HostMask m_host;
    Automation::AutomationBackend::BuildStatus m_build;
    Automation::AutomationBackend::TestRunStatus m_test;
    int m_buildPollsLeft = 0;
    int m_testPollsLeft = 0;
};

Automation::ToolResult Call(Automation::AutomationService& service, std::string name,
                            Automation::Json arguments = Automation::Json::object())
{
    auto call = service.Post({std::move(name), std::move(arguments)});
    service.Tick();
    if (call.result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        service.Tick();
    }
    return call.result.get();
}

} // namespace

int main()
{
    bool ok = true;

    {
        BuildBackend backend(Automation::HostMask::Runtime);
        Automation::AutomationService service(backend);
        Automation::RegisterBuildAutomation(service);
        Automation::RegisterTestAutomation(service);
        auto blocked = Call(service, "build.validate", {{"configuration", "Development"}});
        ok &= Check(!blocked.Ok() && blocked.error->code == Automation::ErrorCode::ToolUnavailable,
                    "runtime host rejects build.validate");
        auto tests = Call(service, "test.list");
        ok &= Check(!tests.Ok() && tests.error->code == Automation::ErrorCode::ToolUnavailable,
                    "runtime host rejects test.list");
        service.Shutdown();
    }

    BuildBackend backend(Automation::HostMask::Editor);
    Automation::AutomationService service(backend);
    Automation::RegisterBuildAutomation(service);
    Automation::RegisterTestAutomation(service);

    auto idle = Call(service, "build.get_status");
    ok &= Check(idle.Ok() && idle.value.value("state", "") == "idle", "build.get_status starts idle");

    auto rejected = Call(service, "build.package",
                         {{"configuration", "Release"}, {"enableRuntimeAutomation", true}});
    ok &= Check(!rejected.Ok() &&
                    rejected.error->data.value("reason", "") == "BUILD_RUNTIME_AUTOMATION_NOT_ALLOWED",
                "Release package rejects runtime automation");
    ok &= Check(backend.m_buildStarts == 0, "rejected package does not start a build");

    auto validated = Call(service, "build.validate",
                          {{"configuration", "Development"}, {"startupScene", "assets/scenes/A.hpscene"}});
    ok &= Check(validated.Ok() && validated.value.value("state", "") == "succeeded" &&
                    validated.value.value("action", "") == "validate",
                "build.validate completes with the recorded action");
    ok &= Check(backend.m_lastRequest.startupScene == "assets/scenes/A.hpscene",
                "build.validate forwards the startup scene");
    ok &= Check(validated.value["problems"].is_array() && validated.value["problems"].size() == 1 &&
                    validated.value["problems"][0].value("severity", "") == "warning",
                "build.validate returns host problems");

    backend.HoldBuild();
    auto busy = Call(service, "build.clean", {{"configuration", "Development"}});
    ok &= Check(!busy.Ok() && busy.error->data.value("reason", "") == "busy",
                "a second build is rejected while one is running");
    backend.ReleaseBuild();

    auto listed = Call(service, "test.list", {{"configuration", "Debug"}});
    ok &= Check(listed.Ok() && listed.value["tests"].size() == 2 &&
                    backend.m_listedConfiguration == "Debug",
                "test.list returns host test names");

    auto missing = Call(service, "test.run", {{"name", "Missing"}, {"configuration", "Release"}});
    ok &= Check(!missing.Ok() && missing.error->data.value("reason", "") == "not_found",
                "test.run reports an unknown test");

    auto badName = Call(service, "test.run", {{"name", "Has Space"}});
    ok &= Check(!badName.Ok() && badName.error->code == Automation::ErrorCode::InvalidArguments,
                "test.run rejects an unsafe test name");

    auto ran = Call(service, "test.run", {{"name", "AutomationLog"}, {"configuration", "Release"}});
    ok &= Check(ran.Ok() && ran.value.value("state", "") == "passed" &&
                    ran.value.value("name", "") == "AutomationLog",
                "test.run completes a known test");

    auto badConfig = Call(service, "build.validate", {{"configuration", "Shipping"}});
    ok &= Check(!badConfig.Ok() && badConfig.error->code == Automation::ErrorCode::InvalidArguments,
                "build.validate rejects an unknown configuration");

    service.Shutdown();
    if (ok) {
        std::cout << "AutomationBuildTests passed\n";
        return 0;
    }
    return 1;
}
