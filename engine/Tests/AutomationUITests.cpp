#include "Automation/AutomationService.h"
#include "Automation/RuntimeUIHostAdapter.h"
#include "Automation/UIAutomation.h"
#include "UI/HephaestusUI/UIButton.h"
#include "UI/HephaestusUI/UICanvas.h"
#include "UI/HephaestusUI/UIContext.h"
#include "UI/HephaestusUI/UIScreen.h"
#include "UI/HephaestusUI/UISystem.h"
#include "UI/HephaestusUI/UIText.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

namespace A = Hephaestus::Automation;
namespace UI = Hephaestus::HephaestusUI;

class Backend final : public A::AutomationBackend {
public:
    UI::UISystem system;
    UI::UIContext context;
    Hephaestus::PlayState play = Hephaestus::PlayState::Playing;
    A::HostMask host = A::HostMask::Editor;
    bool uiReady = true;
    std::uint64_t serial = 0;

    A::HostMask Host() const noexcept override { return host; }
    A::SceneAccess ActiveScene() noexcept override { return {}; }
    Hephaestus::PlayState GetPlayState() const noexcept override { return play; }

    bool CopyRuntimeUI(std::vector<A::AutomationBackend::UIScreenNode>& out, std::string& outError) override
    {
        if (!uiReady) { outError = "unavailable"; return false; }
        return A::CopyRuntimeUIFrom(system, context, out, outError);
    }
    bool ResolveRuntimeUI(const A::AutomationBackend::UISelector& selector,
                          A::AutomationBackend::UINode& out, std::string& outError) override
    {
        if (!uiReady) { outError = "unavailable"; return false; }
        return A::ResolveRuntimeUIFrom(system, context, selector, out, outError);
    }
    bool QueueRuntimeUIClick(const A::AutomationBackend::UISelector& selector,
                             A::AutomationBackend::UIBounds& outBounds, float& outX, float& outY,
                             std::uint64_t& outSerial, std::uint64_t& outClickToken,
                             std::string& outId, std::string& outScreen, std::string& outError) override
    {
        if (!uiReady) { outError = "unavailable"; return false; }
        if (!A::QueueRuntimeUIClickFrom(system, context, selector, outBounds, outX, outY,
                                        outClickToken, outId, outScreen, outError)) {
            return false;
        }
        outSerial = serial;
        return true;
    }
    void CancelRuntimeUIClick(std::uint64_t token) override { context.CancelTaggedMouseEvents(token); }
    std::uint64_t RuntimeUIUpdateSerial() const override { return serial; }
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
    A::RegisterUIAutomation(service);
    auto call = [&](std::string name, A::Json args = A::Json::object()) {
        auto future = service.Post({std::move(name), std::move(args)});
        service.Tick();
        return future.result.get();
    };

    backend.context.SetViewportSize({800.0f, 600.0f});
    auto screen = std::make_shared<UI::UIScreen>("root");
    auto button = std::make_unique<UI::UIButton>("Play");
    button->SetId("btn_play");
    button->SetPosition({10.0f, 20.0f});
    button->SetSize({100.0f, 40.0f});
    int clicks = 0;
    button->SetOnClicked([&] { ++clicks; });
    screen->GetRootCanvas()->AddChild(std::move(button));

    auto hidden = std::make_unique<UI::UIButton>("Hidden");
    hidden->SetId("hidden");
    hidden->SetSize({80.0f, 30.0f});
    hidden->SetVisible(false);
    int hiddenClicks = 0;
    hidden->SetOnClicked([&] { ++hiddenClicks; });
    screen->GetRootCanvas()->AddChild(std::move(hidden));

    auto disabled = std::make_unique<UI::UIButton>("Disabled");
    disabled->SetId("disabled");
    disabled->SetPosition({0.0f, 80.0f});
    disabled->SetSize({80.0f, 30.0f});
    disabled->SetEnabled(false);
    screen->GetRootCanvas()->AddChild(std::move(disabled));

    auto empty = std::make_unique<UI::UIButton>("Empty");
    empty->SetId("empty");
    empty->SetPosition({0.0f, 120.0f});
    empty->SetSize({0.0f, 0.0f});
    screen->GetRootCanvas()->AddChild(std::move(empty));

    auto title = std::make_unique<UI::UIText>("Title");
    title->SetId("title");
    title->SetPosition({200.0f, 10.0f});
    title->SetSize({120.0f, 24.0f});
    screen->GetRootCanvas()->AddChild(std::move(title));

    auto sameA = std::make_unique<UI::UIText>("Same");
    sameA->SetId("same_a");
    sameA->SetPosition({200.0f, 40.0f});
    sameA->SetSize({40.0f, 16.0f});
    screen->GetRootCanvas()->AddChild(std::move(sameA));
    auto sameB = std::make_unique<UI::UIText>("Same");
    sameB->SetId("same_b");
    sameB->SetPosition({260.0f, 40.0f});
    sameB->SetSize({40.0f, 16.0f});
    screen->GetRootCanvas()->AddChild(std::move(sameB));

    backend.system.AddScreen(screen);

    backend.play = Hephaestus::PlayState::Editor;
    auto gated = call("ui.get_tree");
    check(!gated.Ok() && gated.error->code == A::ErrorCode::PlayModeRequired, "Editor not playing rejects ui.get_tree");
    backend.play = Hephaestus::PlayState::Playing;

    auto tree = call("ui.get_tree");
    check(tree.Ok() && tree.value["screens"].size() == 1, "Tree has one screen");
    check(tree.value["screens"][0]["name"] == "root", "Screen name");
    const auto& canvas = tree.value["screens"][0]["children"][0];
    check(canvas["type"] == "Canvas", "Root canvas is reported");
    bool sawButton = false;
    bool sawLabel = false;
    for (const auto& child : canvas["children"]) {
        if (child["id"] == "btn_play") {
            sawButton = child["type"] == "Button" && child["text"] == "Play" && child["visible"] == true;
            sawLabel = !child["children"].empty() && child["children"][0]["type"] == "Text";
        }
    }
    check(sawButton && sawLabel, "Button text comes from its label child");

    auto found = call("ui.find", {{"id", "btn_play"}});
    check(found.Ok() && found.value["matches"].size() == 1, "Find by id");
    auto byType = call("ui.find", {{"type", "Text"}, {"screen", "root"}});
    check(byType.Ok() && byType.value["matches"].size() >= 3, "Find text widgets on the screen");
    auto byText = call("ui.find", {{"text", "Title"}});
    check(byText.Ok() && byText.value["matches"].size() == 1 && byText.value["matches"][0]["id"] == "title",
          "Find by text");
    auto missing = call("ui.find", {{"id", "missing"}});
    check(missing.Ok() && missing.value["matches"].empty(), "Missing id is an empty match list");

    auto got = call("ui.get", {{"id", "btn_play"}});
    check(got.Ok() && got.value["enabled"] == true && got.value["bounds"]["width"].get<float>() == 100.0f,
          "Get returns enabled state and layout bounds");
    auto ambiguous = call("ui.get", {{"text", "Same"}});
    check(!ambiguous.Ok() && ambiguous.error->data.value("reason", "") == "ambiguous", "Ambiguous get");
    auto notFound = call("ui.get", {{"id", "missing"}});
    check(!notFound.Ok() && notFound.error->data.value("reason", "") == "not_found", "Missing get");

    auto hiddenClick = call("ui.click", {{"id", "hidden"}});
    check(!hiddenClick.Ok() && hiddenClick.error->data.value("reason", "") == "not_hittable" && hiddenClicks == 0,
          "Hidden click is not queued");
    auto disabledClick = call("ui.click", {{"id", "disabled"}});
    check(!disabledClick.Ok() && disabledClick.error->data.value("reason", "") == "not_hittable",
          "Disabled click is not queued");
    auto emptyClick = call("ui.click", {{"id", "empty"}});
    check(!emptyClick.Ok() && emptyClick.error->data.value("reason", "") == "no_bounds", "Zero bounds are rejected");

    auto pending = service.Post({"ui.click", {{"id", "btn_play"}}});
    service.Tick();
    check(clicks == 0, "Click does not invoke OnClicked before UISystem::Update");
    check(pending.result.wait_for(std::chrono::seconds(0)) == std::future_status::timeout,
          "Click stays deferred until a UI update");
    backend.system.Update(backend.context);
    ++backend.serial;
    service.Tick();
    auto clicked = pending.result.get();
    check(clicked.Ok() && clicked.value["dispatched"] == true && clicked.value["id"] == "btn_play" &&
              clicked.value["screen"] == "root" && clicks == 1,
          "Click is dispatched by UIInputRouter on the next update");

    backend.context.QueueMousePress({3.0f, 4.0f}, 1);
    auto timed = service.Post({"ui.click", {{"id", "btn_play"}}});
    for (int tick = 0; tick < 400 && timed.result.wait_for(std::chrono::seconds(0)) != std::future_status::ready; ++tick) {
        service.Tick();
    }
    auto timedOut = timed.result.get();
    check(!timedOut.Ok() && timedOut.error->code == A::ErrorCode::ToolUnavailable && clicks == 1,
          "Click times out without firing");
    const auto leftover = backend.context.ConsumeMouseEvents();
    check(leftover.size() == 1 && leftover[0].tag == 0 && leftover[0].position.x == 3.0f,
          "Timeout drops only the synthetic click and keeps player input");

    backend.host = A::HostMask::Runtime;
    check(call("ui.get_tree").Ok(), "Runtime host can read the live tree");
    backend.uiReady = false;
    auto unavailable = call("ui.get_tree");
    check(!unavailable.Ok() && unavailable.error->code == A::ErrorCode::ToolUnavailable, "Missing UI is unavailable");

    return ok ? 0 : 1;
}
