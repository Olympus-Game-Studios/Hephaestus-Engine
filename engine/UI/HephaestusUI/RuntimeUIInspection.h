#pragma once

#include "UIContext.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Hephaestus::HephaestusUI {

class UIElement;
class UISystem;

enum class RuntimeUIQueryReason {
    Ok,
    NotFound,
    Ambiguous,
    NotHittable,
    NoBounds,
};

struct RuntimeUIBounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct RuntimeUINode {
    std::string id;
    std::string type;
    std::string text;
    std::string screen;
    bool visible = false;
    bool enabled = false;
    bool interactable = false;
    RuntimeUIBounds bounds;
    std::vector<RuntimeUINode> children;
};

struct RuntimeUIScreenNode {
    std::string name;
    bool active = false;
    bool visible = false;
    std::vector<RuntimeUINode> children;
};

struct RuntimeUISelector {
    std::string id;
    std::string type;
    std::string text;
    std::string screen;
};

struct RuntimeUIQueuedClick {
    RuntimeUIBounds bounds;
    float x = 0.0f;
    float y = 0.0f;
    std::uint64_t tag = 0;
    std::string id;
    std::string screen;
};

void RefreshRuntimeUILayout(UISystem& system, const UIContext& context);

void CopyRuntimeUI(UISystem& system, const UIContext& context, std::vector<RuntimeUIScreenNode>& out);

RuntimeUIQueryReason ResolveRuntimeUI(UISystem& system, const UIContext& context,
                                      const RuntimeUISelector& selector, RuntimeUINode& out);

RuntimeUIQueryReason QueueRuntimeUIClick(UISystem& system, UIContext& context,
                                         const RuntimeUISelector& selector,
                                         RuntimeUIQueuedClick& out);

} // namespace Hephaestus::HephaestusUI
