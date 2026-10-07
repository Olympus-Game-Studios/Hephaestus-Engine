#include "Automation/AutomationService.h"
#include "Automation/ViewportAutomation.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace A = Hephaestus::Automation;

class Backend final : public A::AutomationBackend {
public:
    Hephaestus::PlayState play = Hephaestus::PlayState::Editor;
    A::HostMask host = A::HostMask::Editor;
    std::string projectRoot = std::filesystem::temp_directory_path().string();
    bool hasRoot = true;
    bool requestOk = true;
    std::string requestError = "NoRenderedFrame";
    std::string pollError;
    bool slotBusy = false;
    int capturePolls = 0;

    A::HostMask Host() const noexcept override { return host; }
    A::SceneAccess ActiveScene() noexcept override { return {}; }
    Hephaestus::PlayState GetPlayState() const noexcept override { return play; }
    A::Json GetProjectInfo() const override
    {
        if (!hasRoot) return {{"name", "Runtime"}, {"host", host == A::HostMask::Runtime ? "runtime" : "editor"}};
        return {{"rootDir", projectRoot}, {"name", "Runtime"}};
    }
    bool RequestViewportCapture(std::string& outError) override
    {
        if (!requestOk) {
            outError = requestError;
            return false;
        }
        if (slotBusy) {
            outError = "CaptureInProgress";
            return false;
        }
        slotBusy = true;
        capturePolls = 0;
        return true;
    }
    bool PollViewportCapture(ViewportPixels& pixels, std::string& outError) override
    {
        if (++capturePolls == 1) return false;
        if (!pollError.empty()) {
            outError = pollError;
            slotBusy = false;
            return true;
        }
        pixels.width = 2;
        pixels.height = 2;
        pixels.frame = 91;
        pixels.rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        slotBusy = false;
        return true;
    }
    void CancelViewportCapture() override { slotBusy = false; }
};

int main()
{
    bool ok = true;
    auto check = [&](bool value, const char* message) {
        if (!value) std::cerr << "[FAIL] " << message << '\n';
        ok &= value;
    };

    Backend backend;
    A::AutomationService service(backend);
    A::RegisterRuntimeCapture(service);
    A::RegisterViewportAutomation(service);
    auto call = [&](std::string name, A::Json args = A::Json::object()) {
        auto future = service.Post({std::move(name), std::move(args)});
        service.Tick();
        return future.result.get();
    };

    const auto gated = call("runtime.capture");
    check(!gated.Ok() && gated.error->code == A::ErrorCode::PlayModeRequired,
          "Editor not playing rejects runtime.capture");

    backend.play = Hephaestus::PlayState::Playing;
    auto pending = service.Post({"runtime.capture", A::Json::object()});
    service.Tick();
    check(pending.result.wait_for(std::chrono::seconds(0)) == std::future_status::timeout,
          "Capture stays pending until the framebuffer is ready");
    service.Tick();
    const auto captured = pending.result.get();
    check(captured.Ok(), "Capture completes on the next tick");
    if (captured.Ok()) {
        const std::string relative = captured.value["path"].get<std::string>();
        check(relative.find(".hephaestus/automation/captures/runtime-") == 0, "Runtime capture uses the runtime prefix");
        check(captured.value["width"] == 2 && captured.value["height"] == 2 && captured.value["frame"] == 91 &&
                  captured.value["format"] == "png",
              "Capture reports dimensions, frame, and format");
        const std::filesystem::path png = std::filesystem::path(backend.projectRoot) / relative;
        std::ifstream image(png, std::ios::binary);
        char signature[8]{};
        image.read(signature, 8);
        check(image.gcount() == 8 && std::string(signature, 8) == std::string("\x89PNG\r\n\x1a\n", 8),
              "Runtime capture is a PNG");
        image.close();
        std::filesystem::remove(png);
    }

    backend.requestOk = false;
    const auto noFrame = call("runtime.capture");
    check(!noFrame.Ok() && noFrame.error->code == A::ErrorCode::ToolUnavailable &&
              noFrame.error->message == "NoRenderedFrame",
          "NoRenderedFrame fails immediately");
    backend.requestOk = true;

    backend.pollError = "ViewportUnavailable: viewport resized during capture";
    auto resizing = service.Post({"runtime.capture", A::Json::object()});
    service.Tick();
    service.Tick();
    const auto resized = resizing.result.get();
    check(!resized.Ok() && resized.error->code == A::ErrorCode::ToolUnavailable &&
              resized.error->message == backend.pollError,
          "Resize during capture fails the deferred capture");
    backend.pollError.clear();

    backend.host = A::HostMask::Runtime;
    auto runtimePending = service.Post({"runtime.capture", A::Json::object()});
    service.Tick();
    service.Tick();
    const auto runtimeCaptured = runtimePending.result.get();
    check(runtimeCaptured.Ok() &&
              runtimeCaptured.value["path"].get<std::string>().find(".hephaestus/automation/captures/runtime-") == 0,
          "Runtime host writes the same relative capture path");
    if (runtimeCaptured.Ok()) {
        std::filesystem::remove(std::filesystem::path(backend.projectRoot) /
                                runtimeCaptured.value["path"].get<std::string>());
    }

    backend.hasRoot = false;
    auto missingRoot = service.Post({"runtime.capture", A::Json::object()});
    service.Tick();
    service.Tick();
    const auto noRoot = missingRoot.result.get();
    check(!noRoot.Ok() && noRoot.error->code == A::ErrorCode::ToolUnavailable &&
              noRoot.error->message == "An open project is required for captures",
          "Runtime host without rootDir is unavailable");
    backend.hasRoot = true;

    auto first = service.Post({"runtime.capture", A::Json::object()});
    service.Tick();
    const auto second = call("runtime.capture");
    check(!second.Ok() && second.error->message == "CaptureInProgress",
          "A second capture fails while the slot is busy");
    service.Tick();
    if (first.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const auto finished = first.result.get();
        if (finished.Ok()) {
            std::filesystem::remove(std::filesystem::path(backend.projectRoot) /
                                    finished.value["path"].get<std::string>());
        }
    }

    if (!ok) return 1;
    std::cout << "AutomationRuntimeCaptureTests passed\n";
    return 0;
}
