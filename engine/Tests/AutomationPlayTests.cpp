#include "Automation/AutomationService.h"
#include "Automation/PlayAutomation.h"
#include "Core/SimulationExecutionMode.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>

namespace Automation = Hephaestus::Automation;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

class PlayBackend final : public Automation::AutomationBackend {
public:
    Hephaestus::PlayState state = Hephaestus::PlayState::Editor;
    Automation::HostMask host = Automation::HostMask::Editor;
    Hephaestus::SimulationExecutionMode executionMode;
    int startCalls = 0;
    int stopCalls = 0;
    int toggleCalls = 0;
    int stepCalls = 0;
    std::uint32_t lastStepCount = 0;
    float lastStepDt = 0.0f;
    std::unordered_map<std::string, float> actions;
    std::unordered_map<std::string, float> axes;
    double mouseDeltaX = 0.0;
    double mouseDeltaY = 0.0;

    Automation::HostMask Host() const noexcept override { return host; }
    Automation::SceneAccess ActiveScene() noexcept override { return {}; }
    Hephaestus::PlayState GetPlayState() const noexcept override { return state; }
    bool IsRealtime() const noexcept override { return executionMode.IsRealtime(); }

    bool StartPlayMode(std::string& outError) override
    {
        ++startCalls;
        state = Hephaestus::PlayState::Playing;
        executionMode.Reset();
        return true;
    }
    bool StopPlayMode(std::string& outError) override
    {
        ++stopCalls;
        state = Hephaestus::PlayState::Editor;
        executionMode.Reset();
        return true;
    }
    bool TogglePause(std::string& outError) override
    {
        ++toggleCalls;
        state = state == Hephaestus::PlayState::Playing ? Hephaestus::PlayState::Paused
                                                        : Hephaestus::PlayState::Playing;
        return true;
    }
    bool StepFrames(std::uint32_t count, float fixedDt, std::string& outError) override
    {
        if (state == Hephaestus::PlayState::Editor) {
            outError = "Play Mode is not active";
            return false;
        }
        ++stepCalls;
        lastStepCount = count;
        lastStepDt = fixedDt;
        return true;
    }
    bool SetRealtime(bool enabled, std::string&) override
    {
        executionMode.SetRealtime(enabled);
        return true;
    }
    bool SetActionOverride(std::string_view action, float value, std::string& outError) override
    {
        actions[std::string(action)] = value;
        return true;
    }
    bool SetAxisOverride(std::string_view axis, float value, std::string& outError) override
    {
        axes[std::string(axis)] = value;
        return true;
    }
    bool AddMouseDelta(double x, double y, std::string& outError) override
    {
        mouseDeltaX += x;
        mouseDeltaY += y;
        return true;
    }
    bool ClearInputOverrides(std::string& outError) override
    {
        actions.clear();
        axes.clear();
        return true;
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
    bool ok = true;
    Hephaestus::SimulationExecutionMode executionMode;
    ok &= Check(executionMode.IsRealtime() && executionMode.ShouldAdvanceRealtime(),
                "Simulation execution mode defaults to realtime host advancement");
    int simulatedHostFrames = 0;
    const auto hostUpdate = [&]() {
        if (executionMode.ShouldAdvanceRealtime()) ++simulatedHostFrames;
    };
    hostUpdate();
    executionMode.SetRealtime(false);
    ok &= Check(!executionMode.IsRealtime() && !executionMode.ShouldAdvanceRealtime(),
                "Manual execution mode closes the normal host-update gate");
    for (int hostFrame = 0; hostFrame < 20; ++hostFrame) hostUpdate();
    ok &= Check(simulatedHostFrames == 1,
                "normal host updates do not tick simulation while manual mode is active");
    for (int stepFrame = 0; stepFrame < 3; ++stepFrame) ++simulatedHostFrames;
    ok &= Check(simulatedHostFrames == 4,
                "explicit stepped frames advance simulation while the host update gate is closed");
    executionMode.SetRealtime(true);
    hostUpdate();
    ok &= Check(executionMode.ShouldAdvanceRealtime() && simulatedHostFrames == 5,
                "restoring realtime resumes ordinary host-driven simulation");
    executionMode.Reset();
    ok &= Check(executionMode.IsRealtime(), "Reset restores realtime host advancement");

    PlayBackend backend;
    Automation::AutomationService service(backend);
    Automation::RegisterPlayAutomation(service);

    auto initial = Call(service, "play.get_state");
    ok &= Check(initial.Ok() && initial.value["state"] == "editor",
                "Initial play state is editor");
    ok &= Check(initial.value["realtime"] == true && initial.value["executionMode"] == "realtime",
                "Initial execution mode is realtime");

    auto stepWhileEditor = Call(service, "play.step_frames", {{"count", 3}});
    ok &= Check(!stepWhileEditor.Ok() &&
                    stepWhileEditor.error->code == Automation::ErrorCode::PlayModeRequired,
                "Stepping outside Play Mode is rejected");

    auto inputWhileEditor = Call(service, "input.set_action", {{"action", "Jump"}, {"value", 1.0}});
    ok &= Check(!inputWhileEditor.Ok() &&
                    inputWhileEditor.error->code == Automation::ErrorCode::PlayModeRequired,
                "Input injection outside Play Mode is rejected");

    auto start = Call(service, "play.start");
    ok &= Check(start.Ok() && backend.startCalls == 1 && backend.state == Hephaestus::PlayState::Playing,
                "play.start enters Play Mode");

    auto manual = Call(service, "play.set_realtime", {{"enabled", false}});
    ok &= Check(manual.Ok() && manual.value["realtime"] == false && !backend.IsRealtime(),
                "play.set_realtime(false) enables manual simulation mode");
    auto manualState = Call(service, "play.get_state");
    ok &= Check(manualState.Ok() && manualState.value["executionMode"] == "manual" &&
                    manualState.value["realtime"] == false,
                "play.get_state reports manual simulation mode");
    auto invalidRealtime = Call(service, "play.set_realtime", {{"enabled", "false"}});
    ok &= Check(!invalidRealtime.Ok() &&
                    invalidRealtime.error->code == Automation::ErrorCode::InvalidArguments,
                "play.set_realtime rejects non-boolean values");

    auto startAgain = Call(service, "play.start");
    ok &= Check(!startAgain.Ok() && startAgain.error->code == Automation::ErrorCode::PlayModeActive,
                "play.start is unavailable while already playing");

    auto badCount = Call(service, "play.step_frames", {{"count", 0}});
    ok &= Check(!badCount.Ok() && badCount.error->code == Automation::ErrorCode::InvalidArguments,
                "Non-positive step count is rejected");

    auto step = Call(service, "play.step_frames", {{"count", 4}, {"fixedDt", 1.0 / 30.0}});
    ok &= Check(step.Ok() && step.value["advanced"] == 4 && backend.stepCalls == 1 &&
                    backend.lastStepCount == 4 &&
                    std::abs(backend.lastStepDt - 1.0 / 30.0) < 1e-6,
                "play.step_frames advances the requested frame count with fixed dt");

    auto restoreRealtime = Call(service, "play.set_realtime", {{"enabled", true}});
    ok &= Check(restoreRealtime.Ok() && backend.IsRealtime(),
                "play.set_realtime(true) restores wall-clock-driven simulation");
    auto manualAgain = Call(service, "play.set_realtime", {{"enabled", false}});
    ok &= Check(manualAgain.Ok() && !backend.IsRealtime(),
                "manual execution mode may be enabled again before Stop");

    backend.host = Automation::HostMask::Runtime;
    const auto runtimeTools = service.Registry().Available(Automation::HostMask::Runtime,
                                                            Hephaestus::PlayState::Playing);
    bool runtimeHasManualControl = false;
    bool runtimeHasState = false;
    bool runtimeHasStepping = false;
    for (const auto* definition : runtimeTools) {
        if (definition->name == "play.set_realtime") runtimeHasManualControl = true;
        if (definition->name == "play.get_state") runtimeHasState = true;
        if (definition->name == "play.step_frames") runtimeHasStepping = true;
    }
    ok &= Check(runtimeHasManualControl,
                "play.set_realtime is registered for the packaged runtime host");
    ok &= Check(runtimeHasState && runtimeHasStepping,
                "play.get_state and play.step_frames are registered for the packaged runtime host");
    backend.host = Automation::HostMask::Editor;

    auto pause = Call(service, "play.pause");
    ok &= Check(pause.Ok() && backend.toggleCalls == 1 && backend.state == Hephaestus::PlayState::Paused,
                "play.pause pauses Play Mode");

    auto pauseAgain = Call(service, "play.pause");
    ok &= Check(pauseAgain.Ok() && backend.toggleCalls == 1,
                "play.pause is idempotent when already paused");

    auto resume = Call(service, "play.resume");
    ok &= Check(resume.Ok() && backend.toggleCalls == 2 && backend.state == Hephaestus::PlayState::Playing,
                "play.resume resumes Play Mode");

    auto action = Call(service, "input.set_action", {{"action", "Jump"}, {"value", 0.75}});
    ok &= Check(action.Ok() && backend.actions["Jump"] == 0.75f,
                "input.set_action injects an action override");

    auto axis = Call(service, "input.set_axis", {{"axis", "MoveX"}, {"value", -0.5}});
    ok &= Check(axis.Ok() && backend.axes["MoveX"] == -0.5f,
                "input.set_axis injects an axis override");

    auto mouse = Call(service, "input.add_mouse_delta", {{"x", 40.0}, {"y", -80.0}});
    ok &= Check(mouse.Ok() && backend.mouseDeltaX == 40.0 && backend.mouseDeltaY == -80.0,
                "input.add_mouse_delta injects unclamped pixel delta");

    auto clear = Call(service, "input.clear_overrides");
    ok &= Check(clear.Ok() && backend.actions.empty() && backend.axes.empty(),
                "input.clear_overrides clears injected input");

    auto stop = Call(service, "play.stop");
    ok &= Check(stop.Ok() && backend.stopCalls == 1 && backend.state == Hephaestus::PlayState::Editor,
                "play.stop exits Play Mode");
    ok &= Check(backend.IsRealtime(), "play.stop restores realtime mode for the next Play session");

    service.Shutdown();
    if (ok) {
        std::cout << "AutomationPlayTests passed\n";
        return 0;
    }
    return 1;
}
