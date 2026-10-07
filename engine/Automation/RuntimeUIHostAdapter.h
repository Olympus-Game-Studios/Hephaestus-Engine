#pragma once

/**
 * Host-side translation between live HUI trees and automation DTOs.
 * Not compiled into HephaestusAutomationCore. Automation library sources must
 * not include this header.
 */

#include "AutomationBackend.h"
#include "UI/HephaestusUI/RuntimeUIInspection.h"

#include <string>
#include <vector>

namespace Hephaestus::Automation {

inline AutomationBackend::UIBounds ToAutomationBounds(const HephaestusUI::RuntimeUIBounds& bounds)
{
    return AutomationBackend::UIBounds{bounds.x, bounds.y, bounds.width, bounds.height};
}

inline AutomationBackend::UINode ToAutomationNode(const HephaestusUI::RuntimeUINode& node)
{
    AutomationBackend::UINode out;
    out.id = node.id;
    out.type = node.type;
    out.text = node.text;
    out.screen = node.screen;
    out.visible = node.visible;
    out.enabled = node.enabled;
    out.interactable = node.interactable;
    out.bounds = ToAutomationBounds(node.bounds);
    out.children.reserve(node.children.size());
    for (const HephaestusUI::RuntimeUINode& child : node.children) {
        out.children.push_back(ToAutomationNode(child));
    }
    return out;
}

inline HephaestusUI::RuntimeUISelector ToHUISelector(const AutomationBackend::UISelector& selector)
{
    return HephaestusUI::RuntimeUISelector{selector.id, selector.type, selector.text, selector.screen};
}

inline const char* RuntimeUIReasonToken(HephaestusUI::RuntimeUIQueryReason reason)
{
    switch (reason) {
    case HephaestusUI::RuntimeUIQueryReason::Ok: return "";
    case HephaestusUI::RuntimeUIQueryReason::NotFound: return "not_found";
    case HephaestusUI::RuntimeUIQueryReason::Ambiguous: return "ambiguous";
    case HephaestusUI::RuntimeUIQueryReason::NotHittable: return "not_hittable";
    case HephaestusUI::RuntimeUIQueryReason::NoBounds: return "no_bounds";
    }
    return "unavailable";
}

inline bool CopyRuntimeUIFrom(HephaestusUI::UISystem& system, const HephaestusUI::UIContext& context,
                              std::vector<AutomationBackend::UIScreenNode>& out, std::string& outError)
{
    std::vector<HephaestusUI::RuntimeUIScreenNode> screens;
    HephaestusUI::CopyRuntimeUI(system, context, screens);
    out.clear();
    out.reserve(screens.size());
    for (const HephaestusUI::RuntimeUIScreenNode& screen : screens) {
        AutomationBackend::UIScreenNode node;
        node.name = screen.name;
        node.active = screen.active;
        node.visible = screen.visible;
        node.children.reserve(screen.children.size());
        for (const HephaestusUI::RuntimeUINode& child : screen.children) {
            node.children.push_back(ToAutomationNode(child));
        }
        out.push_back(std::move(node));
    }
    outError.clear();
    return true;
}

inline bool ResolveRuntimeUIFrom(HephaestusUI::UISystem& system, const HephaestusUI::UIContext& context,
                                 const AutomationBackend::UISelector& selector, AutomationBackend::UINode& out,
                                 std::string& outError)
{
    HephaestusUI::RuntimeUINode node;
    const HephaestusUI::RuntimeUIQueryReason reason =
        HephaestusUI::ResolveRuntimeUI(system, context, ToHUISelector(selector), node);
    if (reason != HephaestusUI::RuntimeUIQueryReason::Ok) {
        out = {};
        outError = RuntimeUIReasonToken(reason);
        return false;
    }
    out = ToAutomationNode(node);
    outError.clear();
    return true;
}

inline bool QueueRuntimeUIClickFrom(HephaestusUI::UISystem& system, HephaestusUI::UIContext& context,
                                    const AutomationBackend::UISelector& selector,
                                    AutomationBackend::UIBounds& outBounds, float& outX, float& outY,
                                    std::uint64_t& outClickToken, std::string& outId, std::string& outScreen,
                                    std::string& outError)
{
    HephaestusUI::RuntimeUIQueuedClick queued;
    const HephaestusUI::RuntimeUIQueryReason reason =
        HephaestusUI::QueueRuntimeUIClick(system, context, ToHUISelector(selector), queued);
    if (reason != HephaestusUI::RuntimeUIQueryReason::Ok) {
        outBounds = {};
        outX = 0.0f;
        outY = 0.0f;
        outClickToken = 0;
        outId.clear();
        outScreen.clear();
        outError = RuntimeUIReasonToken(reason);
        return false;
    }
    outBounds = ToAutomationBounds(queued.bounds);
    outX = queued.x;
    outY = queued.y;
    outClickToken = queued.tag;
    outId = queued.id;
    outScreen = queued.screen;
    outError.clear();
    return true;
}

} // namespace Hephaestus::Automation
