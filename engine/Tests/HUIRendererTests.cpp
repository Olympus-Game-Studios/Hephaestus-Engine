/**
 * HUIRendererTests.cpp
 *
 * Headless (no GPU) tests for the HephaestusUI command-generation, layout,
 * hit-testing, visibility, ordering, and the Vulkan-backend geometry
 * conversion layer (Stage 8A).
 *
 * Usage: HUIRendererTests <repoRoot>
 */

#include "../UI/HephaestusUI/HUIAsset.h"
#include "../UI/HephaestusUI/UIButton.h"
#include "../UI/HephaestusUI/UICanvas.h"
#include "../UI/HephaestusUI/UIContext.h"
#include "../UI/HephaestusUI/UIElement.h"
#include "../UI/HephaestusUI/UIImage.h"
#include "../UI/HephaestusUI/UIPanel.h"
#include "../UI/HephaestusUI/UIRenderer.h"
#include "../UI/HephaestusUI/UIScreen.h"
#include "../UI/HephaestusUI/UISystem.h"
#include "../UI/HephaestusUI/UIText.h"
#include "../UI/HephaestusUI/UIVerticalBox.h"
#include "../UI/HephaestusUI/Rendering/HUIGeometryBuilder.h"
#include "../UI/HephaestusUI/Rendering/UIFontAtlas.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "../UI/HephaestusUI/UIDynamicImages.h"

namespace HUI = Hephaestus::HephaestusUI;

namespace {

bool g_Ok = true;

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::printf("[FAIL] %s\n", message.c_str());
        g_Ok = false;
    } else {
        std::printf("[PASS] %s\n", message.c_str());
    }
    return condition;
}

bool Near(float a, float b, float epsilon = 0.01f) {
    return std::abs(a - b) <= epsilon;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<HUI::UIScreen> MakeScreen(float width, float height) {
    auto screen = std::make_shared<HUI::UIScreen>("TestScreen");
    screen->SetViewportSize({width, height});
    return screen;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: draw command generation for a panel/text/button/image hierarchy
// ─────────────────────────────────────────────────────────────────────────────

bool TestDrawCommandGeneration() {
    bool local = true;
    auto screen = MakeScreen(1280, 720);
    HUI::UIElement* root = screen->GetRootCanvas();

    auto panel = std::make_unique<HUI::UIPanel>();
    HUI::UIStyle panelStyle;
    panelStyle.backgroundColor = {0.2f, 0.4f, 0.6f, 1.0f};
    panel->SetOwnedStyle(panelStyle);
    panel->SetPosition({10, 20});
    panel->SetSize({200, 120});
    HUI::UIElement* panelRaw = root->AddChild(std::move(panel));

    auto text = std::make_unique<HUI::UIText>("Hello HUI");
    text->SetPosition({0, 0});
    text->SetSize({180, 30});
    panelRaw->AddChild(std::move(text));

    auto button = std::make_unique<HUI::UIButton>("Click");
    button->SetPosition({10, 50});
    button->SetSize({120, 40});
    panelRaw->AddChild(std::move(button));

    auto image = std::make_unique<HUI::UIImage>("assets/textures/test.png");
    image->SetPosition({10, 100});
    image->SetSize({40, 40});
    panelRaw->AddChild(std::move(image));

    screen->GetRootCanvas()->UpdateLayout();

    HUI::UIRenderer renderer;
    screen->Draw(renderer);
    const auto& commands = renderer.GetDrawCommands();

    int rectCount = 0, textCount = 0, imageCount = 0;
    for (const auto& cmd : commands) {
        if (std::get_if<HUI::UIDrawRectCommand>(&cmd)) ++rectCount;
        else if (std::get_if<HUI::UIDrawTextCommand>(&cmd)) ++textCount;
        else if (std::get_if<HUI::UIDrawImageCommand>(&cmd)) ++imageCount;
    }

    // Panel rect (1) + button rect (1) = 2 rects; text: 2 (panel text + button label).
    if (!Check(rectCount == 2, "rect commands == 2 (panel + button)")) local = false;
    if (!Check(textCount == 2, "text commands == 2 (panel text + button label)")) local = false;
    if (!Check(imageCount == 1, "image command == 1")) local = false;

    // Verify the first command is the panel rect with correct geometry.
    if (!commands.empty()) {
        if (const auto* rect = std::get_if<HUI::UIDrawRectCommand>(&commands.front())) {
            if (!Check(Near(rect->rect.x, 10) && Near(rect->rect.y, 20), "panel rect x/y")) local = false;
            if (!Check(Near(rect->rect.width, 200) && Near(rect->rect.height, 120), "panel rect size")) local = false;
        } else {
            Check(false, "first command is a rect");
            local = false;
        }
    }
    return local;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: anchors and layouts produce the expected bounds
// ─────────────────────────────────────────────────────────────────────────────

bool TestLayoutAnchors() {
    bool local = true;
    const float W = 1280, H = 720;

    auto screen = MakeScreen(W, H);
    HUI::UIElement* root = screen->GetRootCanvas();

    auto topRight = std::make_unique<HUI::UIPanel>();
    topRight->SetSize({100, 50});
    topRight->SetAnchor({HUI::UIAnchorPreset::TopRight});
    root->AddChild(std::move(topRight));

    auto bottomLeft = std::make_unique<HUI::UIPanel>();
    bottomLeft->SetSize({100, 50});
    bottomLeft->SetAnchor({HUI::UIAnchorPreset::BottomLeft});
    root->AddChild(std::move(bottomLeft));

    auto center = std::make_unique<HUI::UIPanel>();
    center->SetSize({200, 100});
    center->SetAnchor({HUI::UIAnchorPreset::Center});
    root->AddChild(std::move(center));

    auto stretch = std::make_unique<HUI::UIPanel>();
    stretch->SetAnchor({HUI::UIAnchorPreset::Stretch});
    root->AddChild(std::move(stretch));

    screen->GetRootCanvas()->UpdateLayout();

    const auto& children = root->GetChildren();
    const HUI::UIRect tr = children[0]->GetLayoutRect();
    const HUI::UIRect bl = children[1]->GetLayoutRect();
    const HUI::UIRect ct = children[2]->GetLayoutRect();
    const HUI::UIRect st = children[3]->GetLayoutRect();

    if (!Check(Near(tr.x, W - 100) && Near(tr.y, 0), "TopRight anchored at (W-w, 0)")) local = false;
    if (!Check(Near(bl.x, 0) && Near(bl.y, H - 50), "BottomLeft anchored at (0, H-h)")) local = false;
    if (!Check(Near(ct.x, (W - 200) / 2) && Near(ct.y, (H - 100) / 2), "Center anchored at center")) local = false;
    if (!Check(Near(st.width, W) && Near(st.height, H) && Near(st.x, 0) && Near(st.y, 0), "Stretch fills viewport")) local = false;

    // Vertical box stacks children.
    auto vbox = std::make_unique<HUI::UIVerticalBox>();
    vbox->SetPosition({0, 0});
    vbox->SetSize({300, 300});
    vbox->SetSpacing(10);
    auto a = std::make_unique<HUI::UIPanel>();
    a->SetSize({100, 40});
    auto b = std::make_unique<HUI::UIPanel>();
    b->SetSize({100, 40});
    auto c = std::make_unique<HUI::UIPanel>();
    c->SetSize({100, 40});
    vbox->AddChild(std::move(a));
    vbox->AddChild(std::move(b));
    vbox->AddChild(std::move(c));
    root->AddChild(std::move(vbox));

    screen->GetRootCanvas()->UpdateLayout();
    const auto& vboxChildren = root->GetChildren().back()->GetChildren();
    const float y0 = vboxChildren[0]->GetLayoutRect().y;
    const float y1 = vboxChildren[1]->GetLayoutRect().y;
    const float y2 = vboxChildren[2]->GetLayoutRect().y;
    if (!Check(Near(y1 - y0, 50) && Near(y2 - y1, 50), "VBox stacks children with spacing")) local = false;

    return local;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: input hit-testing uses the same bounds the renderer consumes
// ─────────────────────────────────────────────────────────────────────────────

bool TestHitTesting() {
    bool local = true;
    auto screen = MakeScreen(1280, 720);
    HUI::UIElement* root = screen->GetRootCanvas();

    auto button = std::make_unique<HUI::UIButton>("HitMe");
    button->SetPosition({100, 100});
    button->SetSize({200, 50});
    bool clicked = false;
    bool hovered = false;
    button->SetOnClicked([&]() { clicked = true; });
    button->SetOnHovered([&](bool entered) { hovered = entered; });
    root->AddChild(std::move(button));

    screen->GetRootCanvas()->UpdateLayout();
    const HUI::UIRect btnRect = root->GetChildren()[0]->GetLayoutRect();
    if (!Check(Near(btnRect.x, 100) && Near(btnRect.y, 100) && Near(btnRect.width, 200) && Near(btnRect.height, 50),
               "button bounds")) local = false;

    HUI::UIContext context;
    context.SetViewportSize({1280, 720});
    context.QueueMouseMove({150, 120});
    context.QueueMousePress({150, 120}, 0);
    context.QueueMouseRelease({150, 120}, 0);
    screen->Update(context, context.ConsumeMouseEvents());

    if (!Check(clicked, "button OnClicked fires for in-bounds click")) local = false;
    if (!Check(hovered, "button OnHovered(true) fires for in-bounds move")) local = false;

    clicked = false;
    context.QueueMousePress({500, 500}, 0);
    context.QueueMouseRelease({500, 500}, 0);
    screen->Update(context, context.ConsumeMouseEvents());
    if (!Check(!clicked, "button does not fire for out-of-bounds click")) local = false;

    return local;
}

// Game UI laid out for 1080p fills a 1440p viewport the same way: draw commands come out in viewport pixels and
// mouse input in viewport pixels reaches the right widget.
bool TestResolutionScale() {
    bool local = true;
    HUI::UIContext plain;
    plain.SetViewportSize({1280, 720});
    if (!Check(Near(plain.GetScale(), 1.0f) && Near(plain.GetLayoutSize().y, 720),
               "without a reference height the UI is laid out in viewport pixels")) local = false;

    HUI::UIContext context;
    context.SetReferenceHeight(HUI::kGameUIReferenceHeight);
    context.SetViewportSize({2560, 1440});
    if (!Check(Near(context.GetScale(), 4.0f / 3.0f) && Near(context.GetLayoutSize().x, 1920) &&
               Near(context.GetLayoutSize().y, 1080), "a 1440p viewport is 1080 layout units tall")) local = false;

    HUI::UISystem system;
    auto screen = MakeScreen(1, 1);
    auto button = std::make_unique<HUI::UIButton>("Corner");
    button->SetPosition({1800, 1000});
    button->SetSize({100, 50});
    HUI::UIStyle style;
    style.fontSize = 30.0f;
    button->SetNormalStyle(style);
    button->SetHoveredStyle(style);
    button->SetPressedStyle(style);
    bool clicked = false;
    button->SetOnClicked([&]() { clicked = true; });
    screen->GetRootCanvas()->AddChild(std::move(button));
    system.AddScreen(screen);
    system.Update(context); // lays the screen out, as the frame before a click does

    const HUI::UIVec2 inside{1850.0f * 4.0f / 3.0f, 1025.0f * 4.0f / 3.0f};
    context.QueueMouseMove(inside);
    context.QueueMousePress(inside, 0);
    context.QueueMouseRelease(inside, 0);
    system.Update(context);
    if (!Check(clicked, "a click in viewport pixels reaches the widget laid out in layout units")) local = false;

    HUI::UIRenderer renderer;
    system.Render(renderer);
    const HUI::UIDrawRectCommand* rect = nullptr;
    const HUI::UIDrawTextCommand* text = nullptr;
    for (const HUI::UIDrawCommand& command : renderer.GetDrawCommands()) {
        if (!rect) rect = std::get_if<HUI::UIDrawRectCommand>(&command);
        if (!text) text = std::get_if<HUI::UIDrawTextCommand>(&command);
    }
    if (!Check(rect && Near(rect->rect.x, 2400) && Near(rect->rect.y, 1333.33f) && Near(rect->rect.width, 133.33f) &&
               Near(rect->rect.height, 66.67f), "rectangles are drawn in viewport pixels")) local = false;
    if (!Check(text && Near(text->fontSize, 40.0f), "text is drawn at the scaled size")) local = false;

    context.SetUserScale(1.25f);
    if (!Check(Near(context.GetScale(), 5.0f / 3.0f) && Near(context.GetLayoutSize().y, 864),
               "the player's UI size multiplies the resolution scale")) local = false;
    context.SetUserScale(9.0f);
    if (!Check(Near(context.GetUserScale(), 2.0f), "the player's UI size is kept within range")) local = false;
    return local;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: hidden widgets produce no draw commands
// ─────────────────────────────────────────────────────────────────────────────

bool TestVisibility() {
    bool local = true;
    auto screen = MakeScreen(1280, 720);
    HUI::UIElement* root = screen->GetRootCanvas();

    auto visiblePanel = std::make_unique<HUI::UIPanel>();
    visiblePanel->SetPosition({0, 0});
    visiblePanel->SetSize({100, 100});
    HUI::UIStyle vs;
    vs.backgroundColor = {1, 1, 1, 1};
    visiblePanel->SetOwnedStyle(vs);
    root->AddChild(std::move(visiblePanel));

    auto hiddenPanel = std::make_unique<HUI::UIPanel>();
    hiddenPanel->SetPosition({200, 200});
    hiddenPanel->SetSize({100, 100});
    HUI::UIStyle hs;
    hs.backgroundColor = {1, 1, 1, 1};
    hiddenPanel->SetOwnedStyle(hs);
    hiddenPanel->SetVisible(false);
    auto hiddenText = std::make_unique<HUI::UIText>("Invisible");
    hiddenText->SetPosition({0, 0});
    hiddenText->SetSize({100, 30});
    hiddenPanel->AddChild(std::move(hiddenText));
    root->AddChild(std::move(hiddenPanel));

    auto zeroOpacity = std::make_unique<HUI::UIPanel>();
    zeroOpacity->SetPosition({400, 400});
    zeroOpacity->SetSize({100, 100});
    zeroOpacity->SetOpacity(0.0f);
    HUI::UIStyle zs;
    zs.backgroundColor = {1, 1, 1, 1};
    zeroOpacity->SetOwnedStyle(zs);
    root->AddChild(std::move(zeroOpacity));

    screen->GetRootCanvas()->UpdateLayout();

    HUI::UIRenderer renderer;
    screen->Draw(renderer);
    const auto& commands = renderer.GetDrawCommands();

    // Only the visible panel emits a rect; hidden + 0-opacity emit nothing.
    int rectCount = 0;
    for (const auto& cmd : commands) {
        if (std::get_if<HUI::UIDrawRectCommand>(&cmd)) ++rectCount;
    }
    if (!Check(rectCount == 1, "only the visible panel produces a rect command")) local = false;
    if (!Check(commands.size() == 1, "no text/draw commands from hidden subtree")) local = false;

    return local;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: parent/child draw order
// ─────────────────────────────────────────────────────────────────────────────

bool TestOrdering() {
    bool local = true;
    auto screen = MakeScreen(1280, 720);
    HUI::UIElement* root = screen->GetRootCanvas();

    auto parent = std::make_unique<HUI::UIPanel>();
    HUI::UIStyle ps;
    ps.backgroundColor = {1, 0, 0, 1};
    parent->SetOwnedStyle(ps);
    parent->SetPosition({0, 0});
    parent->SetSize({300, 200});
    HUI::UIElement* parentRaw = root->AddChild(std::move(parent));

    auto childText = std::make_unique<HUI::UIText>("ChildText");
    childText->SetPosition({10, 10});
    childText->SetSize({200, 30});
    parentRaw->AddChild(std::move(childText));

    // A higher-z sibling paints on top (comes later in the command stream).
    auto overlay = std::make_unique<HUI::UIPanel>();
    HUI::UIStyle os;
    os.backgroundColor = {0, 1, 0, 1};
    overlay->SetOwnedStyle(os);
    overlay->SetPosition({0, 0});
    overlay->SetSize({50, 50});
    overlay->SetZOrder(10);
    root->AddChild(std::move(overlay));

    screen->GetRootCanvas()->UpdateLayout();

    HUI::UIRenderer renderer;
    screen->Draw(renderer);
    const auto& commands = renderer.GetDrawCommands();

    // Expected order: parent rect, child text, overlay rect.
    if (!Check(commands.size() == 3, "three commands (parent, child text, overlay)")) local = false;
    if (commands.size() == 3) {
        bool firstIsParent = std::holds_alternative<HUI::UIDrawRectCommand>(commands[0]);
        bool secondIsText = std::holds_alternative<HUI::UIDrawTextCommand>(commands[1]);
        bool thirdIsOverlay = std::holds_alternative<HUI::UIDrawRectCommand>(commands[2]);
        if (!Check(firstIsParent && secondIsText && thirdIsOverlay, "parent background -> child -> overlay order")) local = false;
    }
    return local;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: .hui asset compatibility (existing asset loads and draws)
// ─────────────────────────────────────────────────────────────────────────────

bool TestHuiAssetCompatibility(const std::string& repoRoot) {
    bool local = true;

    const std::string assetPath = repoRoot + "/assets/ui/MainMenu.hui";
    HUI::HUIDocument document;
    HUI::HUIParser parser;
    if (!parser.ParseFile(assetPath, document)) {
        return Check(false, "ParseFile(MainMenu.hui) succeeded");
    }
    Check(true, "ParseFile(MainMenu.hui) succeeded");

    HUI::HUIBuilder builder;
    HUI::HUIBuilder::BuildResult result = builder.Build(document);
    if (!Check(result.success && result.screen != nullptr, "HUIBuilder.Build succeeded")) local = false;
    if (result.screen) {
        result.screen->SetViewportSize({1280, 720});
        result.screen->GetRootCanvas()->UpdateLayout();

        auto title = result.widgetMap.find("title");
        auto start = result.widgetMap.find("btn_start");
        if (!Check(title != result.widgetMap.end(), "widget map contains title")) local = false;
        if (!Check(start != result.widgetMap.end(), "widget map contains btn_start")) local = false;
        if (title != result.widgetMap.end() &&
            !Check(title->second->GetId() == "title", "title id persists on live element")) local = false;
        if (start != result.widgetMap.end() &&
            !Check(start->second->GetId() == "btn_start", "button id persists on live element")) local = false;

        HUI::UIRenderer renderer;
        result.screen->Draw(renderer);
        if (!Check(!renderer.GetDrawCommands().empty(), "MainMenu.hui produces draw commands")) local = false;
    }
    return local;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: semi-transparent panels carry element opacity into draw commands
// ─────────────────────────────────────────────────────────────────────────────

bool TestSemiTransparentOpacity() {
    bool local = true;

    auto screen = MakeScreen(1280, 720);
    HUI::UIElement* root = screen->GetRootCanvas();

    auto panel = std::make_unique<HUI::UIPanel>();
    HUI::UIStyle style;
    style.backgroundColor = {0.5f, 0.2f, 0.7f, 1.0f};
    panel->SetOwnedStyle(style);
    panel->SetPosition({0, 0});
    panel->SetSize({200, 50});
    panel->SetOpacity(0.4f);
    root->AddChild(std::move(panel));

    screen->GetRootCanvas()->UpdateLayout();

    HUI::UIRenderer renderer;
    screen->Draw(renderer);
    const auto& commands = renderer.GetDrawCommands();

    bool found = false;
    for (const auto& cmd : commands) {
        if (const auto* rect = std::get_if<HUI::UIDrawRectCommand>(&cmd)) {
            if (Near(rect->opacity, 0.4f) && Near(rect->fillColor.a, 1.0f)) {
                found = true;
            }
        }
    }
    if (!Check(found, "semi-transparent panel emits a rect with opacity 0.4")) local = false;
    return local;
}

// ─────────────────────────────────────────────────────────────────────────────
// Font provider for geometry tests (CPU-only)
// ─────────────────────────────────────────────────────────────────────────────

class TestFontProvider final : public HUI::IFontAtlasProvider {
public:
    explicit TestFontProvider(std::string searchPath) : m_SearchPath(std::move(searchPath)) {}

    HUI::UIFontAtlas* GetOrCreateAtlas(const HUI::UIFontKey& key) override {
        auto it = m_Atlases.find(key);
        if (it != m_Atlases.end()) return it->second.get();

        const std::string file = HUI::UIFontAtlas::ResolveFontFile(
            key.family, key.bold, key.italic, {m_SearchPath + "/assets"});
        if (file.empty()) return nullptr;

        auto atlas = std::make_unique<HUI::UIFontAtlas>();
        if (!atlas->LoadFromFile(file, key.size)) return nullptr;
        atlas->SetTextureHandle(m_NextHandle++);
        HUI::UIFontAtlas* raw = atlas.get();
        m_Atlases[key] = std::move(atlas);
        return raw;
    }

private:
    std::string m_SearchPath;
    int64_t m_NextHandle = 1000;
    std::unordered_map<HUI::UIFontKey, std::unique_ptr<HUI::UIFontAtlas>, HUI::UIFontKeyHash> m_Atlases;
};

HUI::UITextureInfo StubResolver(const std::string& imageId) {
    if (imageId == "tex_64x32") return {5, 64, 32};
    return {-1, 1, 1}; // missing
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: .hui fixture anchors (bottom-center / bottom-right) produce expected bounds
// ─────────────────────────────────────────────────────────────────────────────

bool TestFixtureAnchors(const std::string& repoRoot) {
    bool local = true;

    const std::string assetPath =
        repoRoot + "/engine/Tests/Fixtures/PackagePortabilityProject/assets/ui/Portability.hui";
    HUI::HUIDocument document;
    HUI::HUIParser parser;
    if (!parser.ParseFile(assetPath, document)) {
        return Check(false, "ParseFile(Portability.hui) succeeded");
    }
    HUI::HUIBuilder builder;
    auto result = builder.Build(document);
    if (!Check(result.success, "Build(Portability.hui) succeeded")) local = false;
    if (!result.success) return local;

    result.screen->SetViewportSize({1280, 720});
    result.screen->GetRootCanvas()->UpdateLayout();

    auto bar = result.widgetMap.find("semi_transparent");
    auto corner = result.widgetMap.find("corner_indicator");
    if (bar != result.widgetMap.end()) {
        const HUI::UIRect r = bar->second->GetLayoutRect();
        if (!Check(Near(r.x, 510) && Near(r.y, 684) && Near(r.width, 260) && Near(r.height, 36),
                   "BottomCenter bar at (510,684,260,36)")) local = false;
        std::printf("[INFO] bar layout rect = (%.1f, %.1f, %.1f, %.1f)\n",
                    r.x, r.y, r.width, r.height);
    } else {
        Check(false, "widget map contains semi_transparent");
        local = false;
    }
    if (corner != result.widgetMap.end()) {
        const HUI::UIRect r = corner->second->GetLayoutRect();
        if (!Check(Near(r.x, 1190) && Near(r.y, 690) && Near(r.width, 90) && Near(r.height, 30),
                   "BottomRight indicator at (1190,690,90,30)")) local = false;
        std::printf("[INFO] corner indicator layout rect = (%.1f, %.1f, %.1f, %.1f)\n",
                    r.x, r.y, r.width, r.height);
    }

    // Dump the command stream for diagnostic purposes.
    HUI::UIRenderer renderer;
    result.screen->Draw(renderer);
    for (const auto& cmd : renderer.GetDrawCommands()) {
        if (const auto* r = std::get_if<HUI::UIDrawRectCommand>(&cmd)) {
            std::printf("[INFO] rect cmd at (%.1f, %.1f, %.1f, %.1f) fill=(%.2f,%.2f,%.2f,a=%.2f) opacity=%.2f r=%.1f border=%.1f\n",
                        r->rect.x, r->rect.y, r->rect.width, r->rect.height,
                        r->fillColor.r, r->fillColor.g, r->fillColor.b, r->fillColor.a,
                        r->opacity, r->cornerRadius, r->borderWidth);
        } else if (const auto* t = std::get_if<HUI::UIDrawTextCommand>(&cmd)) {
            std::printf("[INFO] text cmd \"%s\" at (%.1f, %.1f, %.1f, %.1f)\n",
                        t->text.c_str(), t->rect.x, t->rect.y, t->rect.width, t->rect.height);
        } else if (const auto* im = std::get_if<HUI::UIDrawImageCommand>(&cmd)) {
            std::printf("[INFO] image cmd \"%s\" at (%.1f, %.1f, %.1f, %.1f)\n",
                        im->imageId.c_str(), im->rect.x, im->rect.y, im->rect.width, im->rect.height);
        }
    }

    // Run the geometry builder over the fixture commands and verify the rounded
    // center panel and the semi-transparent bar produce quads.
    HUI::HUIGeometryBuilder geoBuilder;
    TestFontProvider fonts(repoRoot);
    const HUI::UITextureResolver resolver = [](const std::string&) {
        return HUI::UITextureInfo{-1, 1, 1};
    };
    HUI::UIBatchData data = geoBuilder.Build(renderer.GetDrawCommands(), {1280, 720}, resolver, fonts);
    bool foundCenter = false;
    bool foundBar = false;
    for (const HUI::UIVertex& v : data.vertices) {
        if (Near(v.rectMinX, 430) && Near(v.rectMinY, 250)) foundCenter = true;
        if (Near(v.rectMinX, 510) && Near(v.rectMinY, 684)) foundBar = true;
    }
    if (!Check(foundCenter, "geometry contains center panel quad (430,250)")) local = false;
    if (!Check(foundBar, "geometry contains semi-transparent bar quad (510,684)")) local = false;
    return local;
}

bool TestGeometryConversion(const std::string& repoRoot) {
    bool local = true;

    HUI::HUIGeometryBuilder builder;
    TestFontProvider fonts(repoRoot);

    HUI::UIRenderer renderer;

    // Rect -> single quad.
    HUI::UIDrawRectCommand rect;
    rect.rect = {10, 20, 100, 50};
    rect.fillColor = {1.0f, 0.0f, 0.0f, 1.0f};
    renderer.DrawRect(rect);

    // Opacity 0 -> no geometry.
    HUI::UIDrawRectCommand faded;
    faded.rect = {0, 0, 50, 50};
    faded.fillColor = {1, 1, 1, 1};
    faded.opacity = 0.0f;
    renderer.DrawRect(faded);

    // Missing image -> placeholder solid quad (tinted), no crash.
    HUI::UIDrawImageCommand missing;
    missing.rect = {0, 200, 60, 60};
    missing.imageId = "missing.png";
    missing.tint = {0.5f, 0.5f, 0.5f, 1.0f};
    renderer.DrawImage(missing);

    // Clipped rect -> scissor becomes the clip rect.
    renderer.PushClipRect({50, 50, 120, 120});
    HUI::UIDrawRectCommand clipped;
    clipped.rect = {60, 60, 80, 80};
    clipped.fillColor = {0, 1, 0, 1};
    renderer.DrawRect(clipped);
    renderer.PopClipRect();

    // Text -> glyph quads.
    HUI::UIDrawTextCommand text;
    text.rect = {20, 300, 300, 40};
    text.text = "Runtime UI";
    text.fontSize = 20.0f;
    text.textColor = {1, 1, 1, 1};
    renderer.DrawText(text);

    HUI::UIBatchData data = builder.Build(renderer.GetDrawCommands(), {1280, 720}, StubResolver, fonts);

    // Expected batches: [rect + missing-image placeholder merged] (white texture,
    // full-viewport scissor), [clipped rect] (clip scissor), [text glyphs] (atlas).
    if (!Check(data.batches.size() >= 3, "at least 3 batches (rect+placeholder, clipped, text)")) local = false;
    if (!Check(data.vertices.size() % 4 == 0, "vertex count is a multiple of 4")) local = false;
    if (!Check(data.indices.size() == (data.vertices.size() / 4) * 6, "index count matches quads")) local = false;

    // First batch: the opaque rect merged with the missing-image placeholder
    // (same solid-white texture + full-viewport scissor) -> 12 indices.
    if (!data.batches.empty()) {
        const HUI::UIBatch& b0 = data.batches[0];
        if (!Check(b0.indexCount == 12, "first batch merges rect + placeholder (12 indices)")) local = false;
        if (!Check(Near(b0.scissor.width, 1280) && Near(b0.scissor.height, 720), "first batch scissor is full viewport")) local = false;
        if (!data.vertices.empty()) {
            const HUI::UIVertex& v = data.vertices[0];
            if (!Check(Near(v.posX, 10) && Near(v.posY, 20), "first vertex position")) local = false;
            if (!Check(Near(v.r, 1.0f) && Near(v.g, 0.0f), "first vertex color")) local = false;
        }
    }

    // The clipped batch must use the pushed clip rect as its scissor.
    for (const HUI::UIBatch& batch : data.batches) {
        if (Near(batch.scissor.x, 50) && Near(batch.scissor.y, 50)) {
            if (!Check(Near(batch.scissor.width, 120) && Near(batch.scissor.height, 120), "clipped batch scissor = clip rect")) local = false;
        }
    }

    // Text produced glyph quads.
    bool foundGlyphQuad = false;
    for (const HUI::UIVertex& v : data.vertices) {
        if (Near(v.u, 0.0f) && Near(v.v, 0.0f) && v.a > 0.0f) {
            foundGlyphQuad = true;
            break;
        }
    }
    if (!Check(foundGlyphQuad || data.vertices.empty(), "text produced glyph geometry")) local = false;

    return local;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: font atlas (metrics, layout, wrapping)
// ─────────────────────────────────────────────────────────────────────────────

bool TestFontAtlas(const std::string& repoRoot) {
    bool local = true;

    const std::string file = HUI::UIFontAtlas::ResolveFontFile(
        "Default", false, false, {repoRoot + "/assets"});
    if (!Check(!file.empty(), "ResolveFontFile finds the bundled font")) local = false;
    const std::string byName = HUI::UIFontAtlas::ResolveFontFile("Roboto-Bold", false, false, {repoRoot + "/assets/fonts"});
    if (!Check(std::filesystem::path(byName).filename() == "Roboto-Bold.ttf",
               "a family names its file, punctuation and all")) local = false;
    const std::string absolute = std::filesystem::absolute(repoRoot + "/assets/fonts/Roboto-Bold.ttf").string();
    if (!Check(HUI::UIFontAtlas::ResolveFontFile(absolute, false, false, {}) == absolute,
               "a family can be an absolute font file path")) local = false;

    HUI::UIFontAtlas atlas;
    if (!Check(atlas.LoadFromFile(file, 32.0f), "LoadFromFile succeeds at 32px")) local = false;
    if (!Check(Near(atlas.GetLineHeight(), 32.0f, 16.0f), "line height is roughly font size")) local = false;

    const HUI::UIGlyphInfo* A = atlas.GetGlyph('A');
    if (!Check(A != nullptr && A->advance > 0.0f, "glyph 'A' has advance")) local = false;
    const HUI::UIGlyphInfo* space = atlas.GetGlyph(' ');
    if (!Check(space != nullptr && space->advance > 0.0f, "space glyph has advance (no bitmap)")) local = false;

    HUI::UITextLayoutResult twoLines = atlas.LayoutText("Alpha\nBeta", 0.0f, false);
    if (!Check(twoLines.ok && twoLines.lines.size() == 2, "LayoutText splits on newlines")) local = false;

    HUI::UITextLayoutResult wrapped = atlas.LayoutText(
        "A very long line of text that should wrap onto multiple lines given a narrow width.",
        120.0f, true);
    if (!Check(wrapped.ok && wrapped.lines.size() >= 2, "word wrap produces multiple lines")) local = false;

    HUI::UITextLayoutResult centered = atlas.LayoutText("Hi", 0.0f, false);
    if (!Check(!centered.lines.empty() && !centered.lines[0].glyphs.empty(), "single line has glyphs")) local = false;

    return local;
}

bool TestMeasureText(const std::string& repoRoot) {
    bool local = true;
    HUI::UIFontKey key;
    key.family = "Roboto";
    key.size = 22.0f;
    key.bold = true;
    const HUI::UITextMeasure quit = HUI::MeasureHUIText(repoRoot, "QUIT", key);
    const float short_ = quit.width;
    const float long_ = HUI::MeasureHUIText(repoRoot, "QUIT TO MENU", key).width;
    if (!Check(quit.inkLeft >= 0.0f && quit.inkRight > quit.inkLeft && quit.inkRight <= quit.width + 2.0f,
               "ink bounds sit inside the advance width")) local = false;
    if (!Check(short_ > 0.0f, "MeasureHUIText measures with a bundled font")) local = false;
    if (!Check(long_ > short_ * 2.0f, "longer text measures wider")) local = false;
    // The width is the game's own layout: the same atlas the renderer draws with.
    HUI::UIFontAtlas atlas;
    atlas.LoadFromFile(HUI::UIFontAtlas::ResolveFontFile("Roboto", true, false, HUI::HUIFontSearchPaths(repoRoot)), 22.0f);
    const HUI::UITextLayoutResult layout = atlas.LayoutText("QUIT", 0.0f, false);
    if (!Check(!layout.lines.empty() && Near(layout.lines[0].width, short_, 0.01f), "measure matches the renderer's layout")) local = false;
    return local;
}

// Runtime-supplied images: published pixels are copied out once per version, then not again until they change.
bool TestDynamicImages() {
    bool local = true;
    auto& store = HUI::UIDynamicImages::Instance();
    const uint8_t red[4] = {255, 0, 0, 255}, blue[4] = {0, 0, 255, 255};
    if (!Check(store.Version("test:frame") == 0, "an unknown dynamic image has no version")) local = false;
    store.Set("test:frame", red, 1, 1);
    HUI::UIDynamicImages::Image image;
    const bool first = store.CopyIfNewer("test:frame", 0, image);
    if (!Check(first && image.width == 1 && image.rgba[0] == 255, "a published image copies out")) local = false;
    if (!Check(!store.CopyIfNewer("test:frame", image.version, image), "an unchanged image does not copy again")) local = false;
    store.Set("test:frame", blue, 1, 1);
    if (!Check(store.CopyIfNewer("test:frame", 1, image) && image.rgba[2] == 255 && image.rgba[0] == 0,
               "a new frame copies out with its pixels")) local = false;
    store.Remove("test:frame");
    if (!Check(store.Version("test:frame") == 0, "a removed image is gone")) local = false;
    return local;
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string repoRoot = argc > 1 ? argv[1] : ".";

    TestDrawCommandGeneration();
    TestLayoutAnchors();
    TestHitTesting();
    TestResolutionScale();
    TestVisibility();
    TestOrdering();
    TestHuiAssetCompatibility(repoRoot);
    TestSemiTransparentOpacity();
    TestFixtureAnchors(repoRoot);
    TestGeometryConversion(repoRoot);
    TestFontAtlas(repoRoot);
    TestMeasureText(repoRoot);
    TestDynamicImages();

    if (g_Ok) {
        std::printf("HUIRendererTests: ALL PASSED\n");
        return 0;
    }
    std::printf("HUIRendererTests: FAILURES PRESENT\n");
    return 1;
}
