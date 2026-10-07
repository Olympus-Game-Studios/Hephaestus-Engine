#include "RuntimeUIInspection.h"

#include "UIButton.h"
#include "UICanvas.h"
#include "UIElement.h"
#include "UIGridLayout.h"
#include "UIHorizontalBox.h"
#include "UIImage.h"
#include "UIPanel.h"
#include "UIScreen.h"
#include "UISystem.h"
#include "UIText.h"
#include "UIVerticalBox.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace Hephaestus::HephaestusUI {
namespace {

std::string ElementType(const UIElement& element)
{
    if (dynamic_cast<const UIButton*>(&element)) return "Button";
    if (dynamic_cast<const UIText*>(&element)) return "Text";
    if (dynamic_cast<const UIImage*>(&element)) return "Image";
    if (dynamic_cast<const UIVerticalBox*>(&element)) return "VerticalBox";
    if (dynamic_cast<const UIHorizontalBox*>(&element)) return "HorizontalBox";
    if (dynamic_cast<const UIGridLayout*>(&element)) return "Grid";
    if (dynamic_cast<const UICanvas*>(&element)) return "Canvas";
    if (dynamic_cast<const UIPanel*>(&element)) return "Panel";
    return "Element";
}

std::string ElementText(const UIElement& element)
{
    if (const auto* text = dynamic_cast<const UIText*>(&element)) return text->GetText();
    if (dynamic_cast<const UIButton*>(&element)) {
        for (const std::unique_ptr<UIElement>& child : element.GetChildren()) {
            if (!child) continue;
            if (const auto* label = dynamic_cast<const UIText*>(child.get())) return label->GetText();
        }
    }
    return {};
}

std::vector<UIElement*> ChildrenFrontToBack(UIElement& element)
{
    std::vector<UIElement*> ordered;
    ordered.reserve(element.GetChildren().size());
    for (const std::unique_ptr<UIElement>& child : element.GetChildren()) {
        if (child) ordered.push_back(child.get());
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const UIElement* left, const UIElement* right) {
        return left->GetZOrder() < right->GetZOrder();
    });
    std::reverse(ordered.begin(), ordered.end());
    return ordered;
}

RuntimeUINode MakeNode(UIElement& element, const std::string& screen)
{
    const UIRect& rect = element.GetLayoutRect();
    RuntimeUINode node;
    node.id = element.GetId();
    node.type = ElementType(element);
    node.text = ElementText(element);
    node.screen = screen;
    node.visible = element.IsVisible();
    node.enabled = element.IsEnabled();
    node.interactable = element.IsInteractable();
    node.bounds = {rect.x, rect.y, rect.width, rect.height};
    for (UIElement* child : ChildrenFrontToBack(element)) {
        node.children.push_back(MakeNode(*child, screen));
    }
    return node;
}

bool Matches(const UIScreen& screen, const UIElement& element, const RuntimeUISelector& selector)
{
    if (!selector.screen.empty() && selector.screen != screen.GetName()) return false;
    if (!selector.id.empty() && selector.id != element.GetId()) return false;
    if (!selector.type.empty() && selector.type != ElementType(element)) return false;
    if (!selector.text.empty() && selector.text != ElementText(element)) return false;
    return true;
}

struct Match {
    UIScreen* screen = nullptr;
    UIElement* element = nullptr;
};

void Collect(UIScreen& screen, UIElement& element, const RuntimeUISelector& selector,
             std::vector<Match>& matches)
{
    if (Matches(screen, element, selector)) matches.push_back(Match{&screen, &element});
    for (UIElement* child : ChildrenFrontToBack(element)) {
        Collect(screen, *child, selector, matches);
    }
}

std::vector<Match> FindMatches(UISystem& system, const RuntimeUISelector& selector)
{
    std::vector<Match> matches;
    for (const std::shared_ptr<UIScreen>& screen : system.GetScreens()) {
        if (!screen || !screen->GetRootCanvas()) continue;
        Collect(*screen, *screen->GetRootCanvas(), selector, matches);
    }
    return matches;
}

} // namespace

void RefreshRuntimeUILayout(UISystem& system, const UIContext& context)
{
    const UIVec2 size = context.GetViewportSize();
    for (const std::shared_ptr<UIScreen>& screen : system.GetScreens()) {
        if (!screen) continue;
        screen->SetViewportSize(size);
        if (UICanvas* root = screen->GetRootCanvas()) {
            root->SetSize(size);
            root->UpdateLayout();
        }
    }
}

void CopyRuntimeUI(UISystem& system, const UIContext& context, std::vector<RuntimeUIScreenNode>& out)
{
    RefreshRuntimeUILayout(system, context);
    out.clear();
    for (const std::shared_ptr<UIScreen>& screen : system.GetScreens()) {
        if (!screen) continue;
        RuntimeUIScreenNode node;
        node.name = screen->GetName();
        node.active = screen->IsActive();
        node.visible = screen->IsVisible();
        if (UICanvas* root = screen->GetRootCanvas()) {
            node.children.push_back(MakeNode(*root, screen->GetName()));
        }
        out.push_back(std::move(node));
    }
}

RuntimeUIQueryReason ResolveRuntimeUI(UISystem& system, const UIContext& context,
                                      const RuntimeUISelector& selector, RuntimeUINode& out)
{
    RefreshRuntimeUILayout(system, context);
    const std::vector<Match> matches = FindMatches(system, selector);
    if (matches.empty()) return RuntimeUIQueryReason::NotFound;
    if (matches.size() != 1) return RuntimeUIQueryReason::Ambiguous;
    out = MakeNode(*matches.front().element, matches.front().screen->GetName());
    return RuntimeUIQueryReason::Ok;
}

RuntimeUIQueryReason QueueRuntimeUIClick(UISystem& system, UIContext& context,
                                         const RuntimeUISelector& selector,
                                         RuntimeUIQueuedClick& out)
{
    RefreshRuntimeUILayout(system, context);
    const std::vector<Match> matches = FindMatches(system, selector);
    if (matches.empty()) return RuntimeUIQueryReason::NotFound;
    if (matches.size() != 1) return RuntimeUIQueryReason::Ambiguous;

    UIScreen& screen = *matches.front().screen;
    UIElement& element = *matches.front().element;
    if (!screen.IsActive()) return RuntimeUIQueryReason::NotHittable;

    const UIRect& rect = element.GetLayoutRect();
    if (rect.width <= 0.0f || rect.height <= 0.0f) return RuntimeUIQueryReason::NoBounds;

    const UIVec2 point{rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f};
    if (screen.HitTest(point) != &element) return RuntimeUIQueryReason::NotHittable;

    out.bounds = {rect.x, rect.y, rect.width, rect.height};
    out.x = point.x;
    out.y = point.y;
    out.id = element.GetId();
    out.screen = screen.GetName();
    out.tag = context.QueueSyntheticClick(point, 0);
    return RuntimeUIQueryReason::Ok;
}

} // namespace Hephaestus::HephaestusUI
