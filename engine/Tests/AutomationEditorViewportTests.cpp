#include "Automation/AutomationService.h"
#include "Automation/EditorAutomation.h"
#include "Automation/ViewportAutomation.h"
#include "Automation/SceneAutomation.h"
#include "Scene/Scene.h"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <limits>

namespace A = Hephaestus::Automation;

class Backend final : public A::AutomationBackend {
public:
    Scene scene{"Observation"};
    std::vector<entt::entity> selected;
    A::Json camera = {{"position", A::Json::array({0, 2, 5})}, {"yawDeg", -90},
                      {"pitchDeg", -15}, {"fovDeg", 60}};
    int selectionCalls = 0, focusCalls = 0;
    Hephaestus::PlayState play = Hephaestus::PlayState::Editor;
    bool available = true;
    std::string projectRoot = std::filesystem::temp_directory_path().string();
    int capturePolls = 0;
    A::HostMask Host() const noexcept override { return A::HostMask::Editor; }
    A::SceneAccess ActiveScene() noexcept override { return {&scene}; }
    Hephaestus::PlayState GetPlayState() const noexcept override { return play; }
    A::Json GetEditorState() const override { return {{"projectLoaded", true}, {"scene", {{"path", ""}, {"dirty", false}}}, {"playState", "Editor"}, {"viewportAvailable", available}}; }
    A::Json GetProjectInfo() const override { return {{"rootDir", projectRoot}}; }
    std::vector<entt::entity> GetSelection() const override { return selected; }
    bool SetSelection(const std::vector<entt::entity>& handles, std::string&) override { selected = handles; ++selectionCalls; return true; }
    A::Json GetViewportState() const override { return {{"available", available}, {"width", 800}, {"height", 600}}; }
    A::Json GetViewportCamera() const override { return camera; }
    bool SetViewportCamera(const A::Json& args, std::string&) override {
        for (auto it = args.begin(); it != args.end(); ++it) camera[it.key()] = it.value();
        return true;
    }
    bool FocusEntities(const std::vector<entt::entity>& handles, std::string&) override {
        ++focusCalls;
        camera["position"] = A::Json::array({static_cast<int>(handles.size()), 0, 0});
        return true;
    }
    bool RequestViewportCapture(std::string&) override { capturePolls = 0; return true; }
    bool PollViewportCapture(ViewportPixels& pixels, std::string&) override {
        if (++capturePolls == 1) return false;
        pixels.width = 2; pixels.height = 2; pixels.frame = 91;
        pixels.rgba = {255, 0, 0, 255, 0, 255, 0, 255,
                       0, 0, 255, 255, 255, 255, 255, 255};
        return true;
    }
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
    A::RegisterEditorAutomation(service);
    A::RegisterViewportAutomation(service);
    auto call = [&](std::string name, A::Json args = A::Json::object()) {
        auto future = service.Post({std::move(name), std::move(args)});
        service.Tick();
        return future.result.get();
    };
    auto& scene = backend.scene;
    const auto one = static_cast<entt::entity>(scene.CreateEntity("One"));
    const auto two = static_cast<entt::entity>(scene.CreateEntity("Two"));
    check(call("editor.get_state").value["projectLoaded"] == true, "Editor state");
    check(call("editor.get_selection").value["selection"].empty(), "Empty selection");
    const auto first = service.Entities().Bind(scene, one).value;
    const auto second = service.Entities().Bind(scene, two).value;
    check(call("editor.select", {{"entities", A::Json::array({first})}}).Ok(), "Select one");
    check(call("editor.select", {{"entities", A::Json::array({second})}, {"mode", "add"}}).value["selection"].size() == 2, "Add selection");
    check(call("editor.select", {{"entities", A::Json::array({first})}}).value["selection"].size() == 1, "Replace selection");
    check(call("viewport.focus_selection").Ok() && backend.focusCalls == 1, "Focus selection");
    check(call("viewport.focus_entity", {{"entity", second}}).Ok(), "Focus entity");
    check(call("viewport.get_state").value["width"] == 800, "Viewport state");
    check(call("viewport.set_camera", {{"position", A::Json::array({4, 5, 6})}, {"fovDeg", 75}}).value["position"][0] == 4, "Set camera");
    check(!call("viewport.set_camera", {{"fovDeg", 180}}).Ok(), "Reject invalid FOV");
    check(!call("viewport.set_camera", {{"position", A::Json::array({std::numeric_limits<double>::infinity(), 0, 0})}}).Ok(), "Reject non-finite position");
    check(!call("viewport.set_camera", {{"yawDeg", 400}}).Ok(), "Reject invalid orientation");
    auto pendingCapture = service.Post({"viewport.capture", A::Json::object()});
    service.Tick();
    check(pendingCapture.result.wait_for(std::chrono::seconds(0)) == std::future_status::timeout,
          "Capture is deferred until a completed frame");
    service.Tick();
    auto capture = pendingCapture.result.get();
    check(capture.Ok(), "Capture completes after readback");
    if (capture.Ok()) {
        const std::filesystem::path png = std::filesystem::path(backend.projectRoot) /
            capture.value["path"].get<std::string>();
        std::ifstream image(png, std::ios::binary);
        char signature[8]{};
        image.read(signature, 8);
        check(image.gcount() == 8 && std::string(signature, 8) == std::string("\x89PNG\r\n\x1a\n", 8),
              "Capture is valid PNG with correct dimensions");
        check(capture.value["width"] == 2 && capture.value["height"] == 2, "Capture dimensions");
        image.close();
        std::filesystem::remove(png);
    }
    check(call("editor.clear_selection").Ok() && backend.selected.empty(), "Clear selection");
    backend.play = Hephaestus::PlayState::Playing;
    check(call("editor.get_state").Ok() && call("viewport.get_state").Ok() &&
          call("viewport.set_camera", {{"yawDeg", 20}}).error->code == A::ErrorCode::PlayModeActive,
          "Editor observation remains available in Play Mode but editor camera mutation is gated");
    backend.play = Hephaestus::PlayState::Editor;
    check(!call("viewport.focus_selection").Ok(), "Empty selection cannot be framed");
    scene.DestroyEntity(Entity(two, &scene));
    check(call("editor.select", {{"entities", A::Json::array({second})}}).error->code == A::ErrorCode::EntityRefExpired, "Expired EntityRef");
    backend.available = false;
    check(!call("viewport.get_camera").Ok(), "Unavailable viewport");
    check(backend.selectionCalls == 4, "Selection writes go through backend");
    return ok ? 0 : 1;
}
