#include "UISimpleMenuExample.h"

#include "UIButton.h"
#include "UICanvas.h"
#include "UIPanel.h"
#include "UIScreen.h"
#include "UIText.h"
#include "UIVerticalBox.h"

#include <memory>

namespace Hephaestus::HephaestusUI {

std::shared_ptr<UIScreen> CreateSimpleMenuScreen() {
    auto screen = std::make_shared<UIScreen>("MainMenu");
    screen->SetViewportSize({1920.0f, 1080.0f});

    UICanvas* canvas = screen->GetRootCanvas();
    if (!canvas) {
        return screen;
    }

    auto panel = std::make_unique<UIPanel>();
    panel->SetPosition({680.0f, 220.0f});
    panel->SetSize({560.0f, 640.0f});
    panel->SetPadding(UISpacing::Uniform(24.0f));
    UIPanel* panelRef = panel.get();
    canvas->AddChild(std::move(panel));

    auto title = std::make_unique<UIText>("Hephaestus Engine");
    title->SetPosition({0.0f, 0.0f});
    title->SetSize({512.0f, 64.0f});
    title->SetAlignment(UITextAlignment::Center);
    panelRef->AddChild(std::move(title));

    auto buttonColumn = std::make_unique<UIVerticalBox>();
    buttonColumn->SetPosition({0.0f, 90.0f});
    buttonColumn->SetSize({512.0f, 420.0f});
    buttonColumn->SetSpacing(12.0f);
    UIVerticalBox* columnRef = buttonColumn.get();
    panelRef->AddChild(std::move(buttonColumn));

    auto playButton = std::make_unique<UIButton>("Play");
    playButton->SetSize({512.0f, 64.0f});
    playButton->SetOnClicked([] {
        // Hook this callback into the runtime scene loading flow.
    });
    columnRef->AddChild(std::move(playButton));

    auto settingsButton = std::make_unique<UIButton>("Settings");
    settingsButton->SetSize({512.0f, 64.0f});
    columnRef->AddChild(std::move(settingsButton));

    auto exitButton = std::make_unique<UIButton>("Exit");
    exitButton->SetSize({512.0f, 64.0f});
    columnRef->AddChild(std::move(exitButton));

    return screen;
}

} // namespace Hephaestus::HephaestusUI
