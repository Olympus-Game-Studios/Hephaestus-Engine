# HephaestusUI Runtime UI Guide

This document covers the first production-facing version of Hephaestus Engine's retained-mode runtime UI system.

Scope:
- Runtime/game UI only (HUDs, menus, inventories, overlays)
- Renderer-agnostic command generation
- Engine-native input routing and widget tree management

Out of scope for this version:
- Qt editor UI replacement
- HTML/CSS/WebView UI
- Controller navigation graph
- Timeline/animation system
- JSON screen assets and visual authoring pipeline

## 1. Design Goals

HephaestusUI is built as a retained-mode widget tree:
- UI state lives in C++ objects (`UIElement` hierarchy)
- Layout is resolved each frame from parent to children
- Widgets emit draw commands instead of issuing GPU calls directly
- Input is hit-tested against current layout and dispatched as events

Why retained mode here:
- Better fit for long-lived gameplay UI state
- Easier Lua/runtime mutation (create/modify widgets over time)
- Clear path to theme assets, serialization, and tooling

## 2. Module Layout

Engine source folder:
- `engine/UI/HephaestusUI/`

Core files:
- `UISystem.h/.cpp`: Owns active screens and drives update/render
- `UIScreen.h/.cpp`: Represents one top-level runtime UI tree
- `UIContext.h/.cpp`: Viewport and queued input events for a frame
- `UIElement.h/.cpp`: Base widget node and tree behavior
- `UILayout.h/.cpp`: Vertical/horizontal container layout helpers
- `UIRenderer.h/.cpp`: Draw command buffer
- `UIInputRouter.h/.cpp`: Hover/press/release/click dispatch
- `UIStyle.h`: Primitive UI types and style struct
- `UIFont.h/.cpp`: Font metadata registry scaffold

Widgets:
- `UICanvas`: Absolute layout root/container
- `UIPanel`: Generic styled rectangle container
- `UIText`: Text draw command emitter
- `UIImage`: Image draw command emitter
- `UIButton`: Stateful button with internal text child
- `UIVerticalBox`: Vertical layout container
- `UIHorizontalBox`: Horizontal layout container

## 3. Object Model

### 3.1 UI Tree

Typical hierarchy:

`UISystem -> UIScreen -> RootCanvas (UICanvas) -> Child elements...`

`UIElement` supports:
- Parent pointer and owned children
- Local position and size
- Visibility and opacity
- Padding and margin
- Anchor metadata (`UIAnchor`) for future expansion
- Z-order sorting for draw and hit test
- Optional clipping
- Optional style pointer

### 3.2 Lifecycle

Per frame:
1. Game code queues input in `UIContext`
2. `UISystem::Update(context)`
3. Each active `UIScreen` routes queued input and updates layout
4. `UIRenderer::Reset()`
5. `UISystem::Render(renderer)`
6. Renderer backend consumes `renderer.GetDrawCommands()`

## 4. Layout Behavior

### 4.1 Absolute (Canvas)

`UICanvas` keeps child local position/size as-is (absolute placement).

### 4.2 Vertical/Horizontal Containers

`UIVerticalBox` and `UIHorizontalBox`:
- Iterate visible children in insertion order
- Apply spacing
- Respect child margins
- Auto-fill one axis when child size is `0` on that axis

Notes:
- This v1 is intentionally simple and deterministic
- No flex grow/shrink or min/max constraints yet

## 5. Draw Command Contract

Widgets do not render directly. They emit commands into `UIRenderer`:
- `UIDrawRectCommand`
- `UIDrawImageCommand`
- `UIDrawTextCommand`
- `UIPushClipRectCommand`
- `UIPopClipRectCommand`

Backend responsibility:
- Sort/submit according to command order and z
- Resolve `imageId` to textures
- Resolve font family and glyphs
- Apply clip stack semantics

## 6. Input Model

`UIInputRouter` currently handles:
- Mouse move
- Mouse press
- Mouse release
- Hover enter/exit
- Click detection (press and release on same element)

`UIElement` event hooks:
- `OnHoverEnter`
- `OnHoverExit`
- `OnPressed`
- `OnReleased`
- `OnClicked`

## 7. Cursor Mode (Input Ownership)

`UISystem` tracks a high-level `CursorMode` that governs whether mouse input is owned by the UI or by gameplay:

```cpp
enum class CursorMode {
    Game, // cursor hidden + captured; mouse goes to gameplay (camera, etc.)
    UI    // cursor visible + unlocked; mouse goes to UI widgets
};
```

### C++ API
```cpp
uiSystem.SetCursorMode(HUI::CursorMode::UI);
HUI::CursorMode current = uiSystem.GetCursorMode();
```

### Lua API
```lua
UI.setCursorMode(CursorMode.UI)   -- unlock + show cursor
UI.setCursorMode(CursorMode.Game) -- lock + hide cursor
local mode = UI.getCursorMode()   -- "ui" or "game"
```

### Host Integration

When `CursorMode` changes the host (Qt editor or GLFW runtime) must react:

- **Qt editor**: `populateRuntimeInputState()` already honours the underlying `cursorModeRequest`. The editor camera controller is suppressed when the runtime UI is in `CursorMode::UI` to prevent fly-mode interference.
- **GLFW runtime**: the Lua binding calls `glfwSetInputMode` directly. Script-driven camera controllers should check `UI.getCursorMode()` and skip mouse-look when in UI mode.

Cursor mode resets to `Game` when play mode stops or `UISystem::ClearScreens()` is called.

## 8. Style and Font

### 8.1 UIStyle

`UIStyle` fields:
- `backgroundColor`
- `borderColor`
- `borderWidth`
- `cornerRadius`
- `textColor`
- `padding`

Widgets may use local style or inherited/default behavior.

### 8.2 UIFont

`UIFont` and `UIFontRegistry` provide metadata storage:
- Family name
- Point size
- Bold/italic flags

This is prepared for future FreeType integration.

## 9. C++ Usage Example

Simple runtime setup pattern:

```cpp
#include "../UI/HephaestusUI/UISystem.h"
#include "../UI/HephaestusUI/UIContext.h"
#include "../UI/HephaestusUI/UIRenderer.h"
#include "../UI/HephaestusUI/UISimpleMenuExample.h"

namespace HUI = Hephaestus::HephaestusUI;

HUI::UISystem uiSystem;
HUI::UIContext uiContext;
HUI::UIRenderer uiRenderer;

uiContext.SetViewportSize({1920.0f, 1080.0f});
uiSystem.AddScreen(HUI::CreateSimpleMenuScreen());

// Per frame:
uiContext.QueueMouseMove(mousePos);
uiSystem.Update(uiContext);

uiRenderer.Reset();
uiSystem.Render(uiRenderer);

const auto& commands = uiRenderer.GetDrawCommands();
// Feed commands into render backend.
```

## 10. Lua Bridge Summary

HephaestusUI is exposed to Lua through the global `UI` table in `LuaScriptEngine`.

Two levels are available:
- Screen/system control (`createScreen`, visibility, active state, viewport)
- Dynamic widget API (create panel/text/button/image/boxes and mutate properties)

Full Lua API reference is documented in:
- `Documentation/Scripting/LUA_UI_API_REFERENCE.md`

Script-only workflow (no engine source access):
- `Documentation/Guides/NO_SOURCE_RUNTIME_UI_GUIDE.md`

## 11. Current Limitations

Known v1 constraints:
- No keyboard/controller focus navigation
- No built-in animation/tweening
- No serialized UI asset format (JSON/binary)
- No data binding framework
- No direct Lua button callback registration yet

## 12. Extension Plan

Designed extension points:
- Add focus manager for controller/keyboard navigation
- Add animation subsystem over `UIElement` properties
- Add theme/style asset loading
- Add screen/widget serialization pipeline
- Add Lua callback/event subscription API
- Add editor-side visual UI authoring that exports runtime assets

## 13. Integration Checklist

When integrating into a runtime target:
- Create `UISystem`, `UIContext`, `UIRenderer`
- Set viewport size each frame
- Queue mouse events into `UIContext`
- Update `UISystem` before render
- Reset and render `UIRenderer`
- Consume draw command list in renderer backend
- If Lua is enabled, call `LuaScriptEngine::SetUIBindings(...)`
