# HephaestusUI Runtime UI Guide

This document covers the production-facing version of Hephaestus Engine's retained-mode runtime UI system (HephaestusUI / HUI), including the packaged Vulkan renderer backend introduced in Stage 8A.

Scope:
- Runtime/game UI only (HUDs, menus, inventories, overlays)
- Renderer-agnostic command generation
- Engine-native input routing and widget tree management

Out of scope:
- Qt editor UI replacement
- HTML/CSS/WebView UI
- Controller navigation graph
- Timeline/animation system
- Scroll lists / sliders / text inputs (separate milestones)

## 1. Design Goals

HephaestusUI is built as a retained-mode widget tree:
- UI state lives in C++ objects (`UIElement` hierarchy)
- Layout is resolved each frame from parent to children
- Widgets emit draw commands instead of issuing GPU calls directly
- Input is hit-tested against current layout and dispatched as events
- Rendering is backend-agnostic: the same `.hui`/Lua-authored screen renders through
  the Qt editor overlay (Editor Play) and the Vulkan packaged-game backend

Why retained mode:
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
- `UIRenderer.h/.cpp`: Draw command buffer (renderer-independent)
- `UIInputRouter.h/.cpp`: Hover/press/release/click dispatch
- `UIStyle.h`: Primitive UI types and style struct
- `UIFont.h/.cpp`: Font metadata registry scaffold
- `HUIAsset.h/.cpp`: `.hui` JSON asset parsing, building, serialization

Widgets:
- `UICanvas`: Absolute layout root/container
- `UIPanel`: Generic styled rectangle container
- `UIText`: Text draw command emitter
- `UIImage`: Image draw command emitter
- `UIButton`: Stateful button with internal text child
- `UIVerticalBox`: Vertical layout container
- `UIHorizontalBox`: Horizontal layout container
- `UIGridLayout`: Grid layout container

Rendering backends (`engine/UI/HephaestusUI/Rendering/`):
- `HUIRenderBackend.h`: Renderer-independent `IHUIRenderBackend` interface
- `HUICommon.h`: Backend-agnostic vertex/batch geometry types (`UIVertex`, `UIBatch`)
- `HUIGeometryBuilder.h/.cpp`: Converts the draw command list into batched quads (CPU)
- `UIFontAtlas.h/.cpp`: CPU glyph rasterization (stb_truetype), atlas packing, text layout
- `VulkanHUIRenderer.h/.cpp`: Vulkan backend used by packaged `HephaestusGame`

## 3. Object Model

### 3.1 UI Tree

Typical hierarchy:

`UISystem -> UIScreen -> RootCanvas (UICanvas) -> Child elements...`

`UIElement` supports:
- Parent pointer and owned children
- Local position and size
- Visibility and opacity (inherited multiplicatively down the tree)
- Padding and margin
- Anchor presets (`UIAnchorPreset`: TopLeft/TopCenter/TopRight/CenterLeft/Center/CenterRight/BottomLeft/BottomCenter/BottomRight/Stretch)
- Z-order sorting for draw and hit test
- Optional clipping (pushed as clip rects)
- Optional style pointer / owned style

### 3.2 Lifecycle

Per frame:
1. Game code queues input in `UIContext`
2. `UISystem::Update(context)`
3. Each active `UIScreen` routes queued input and updates layout
4. `UIRenderer::Reset()`
5. `UISystem::Render(renderer)`
6. The active render backend consumes `renderer.GetDrawCommands()`

## 4. Layout Behavior

- Coordinates are **viewport pixels** with a **top-left origin, y growing downward**.
- `UICanvas` keeps child local position/size as-is (absolute placement).
- `UIVerticalBox` / `UIHorizontalBox`: iterate visible children, apply spacing and
  margins, auto-fill one axis when child size is `0` on that axis.
- `UIGridLayout`: fixed-column grid with per-cell alignment.
- Anchors resolve child position against the parent's content rect; `Stretch` fills
  the available space.
- Layout is resolution-independent: screens relayout automatically when the
  viewport size changes (window resize, fullscreen, different display resolution).

## 5. Draw Command Contract

Widgets do not render directly. They emit commands into `UIRenderer`:
- `UIDrawRectCommand` (fill, border, corner radius, optional background image)
- `UIDrawImageCommand` (image + tint + scale mode)
- `UIDrawTextCommand` (text, font family/size/weight, alignment, wrap, shadow)
- `UIPushClipRectCommand` / `UIPopClipRectCommand` (nested clipping)

The command stream is already in paint order (parent before children, siblings
sorted by ascending z-order). Backend responsibility:
- Convert commands into batched textured quads
- Resolve `imageId` to textures
- Resolve font family and glyphs (atlas caching)
- Apply clip stack semantics (scissor rectangles)
- Blend correctly (premultiplied alpha)

## 6. Input Model

`UIInputRouter` handles:
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

Hit-testing uses the same layout rects that the renderer draws, so a button
visually and functionally occupies the same region.

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
- **Qt editor**: `populateGameplayInput()` honours the shared `InputSystem` cursor request. The editor camera controller is suppressed when the runtime UI is in `CursorMode::UI`.
- **GLFW runtime / packaged game**: the Lua binding calls `glfwSetInputMode` directly. The packaged runtime routes mouse events to the UI at swapchain pixel coordinates (the same coordinate space the Vulkan backend renders into).

Cursor mode resets to `Game` when play mode stops or `UISystem::ClearScreens()` is called.

## 8. Style and Font

### 8.1 UIStyle

`UIStyle` fields:
- `backgroundColor`
- `backgroundImageId`, `imageScaleMode`, `imageTint`
- `borderColor`, `borderWidth`, `cornerRadius`
- `textColor`, `fontFamily`, `fontSize`, `fontBold`, `fontItalic`, `textAlignment`
- `textShadow`, `textShadowColor`, `textShadowOffset`
- `padding`, `opacity`, `scale`

### 8.2 Font Rendering

Runtime text is rendered with a glyph atlas:
- **Rasterizer:** `stb_truetype` (vendored at `extern/stb_truetype.h`)
- **Atlas:** one RGBA8 atlas per (family, size, bold, italic) combination, packed
  lazily as glyphs are requested (`UIFontAtlas`)
- **Metrics:** advance, bearing, baseline, line height, basic kerning, UTF-8 decoding
- **Font discovery:** bundled fonts in `assets/fonts/` (Roboto is shipped with the
  engine), falling back to platform system font directories
- Glyphs are cached; the atlas is only re-uploaded when new glyphs are rasterized

## 9. Renderer Backends

Both backends consume the exact same draw command list. Layout, input, and widget
behaviour are identical; only the final presentation differs.

### 9.1 Editor Play (Qt/QPainter)

- `RuntimeUIOverlayWidget` paints the HUI command list over the viewport while
  playing in the editor.
- Fonts use Qt's `QFont`; images resolve through the editor asset manager.

### 9.2 Packaged Game (Vulkan)

- `VulkanHUIRenderer` renders HUI inside the shared swapchain render pass, after
  the 3D scene, as an overlay.
- One lightweight `hui.vert`/`hui.frag` pipeline with dynamic vertex/index buffers
  per swapchain image.
- **Batching:** consecutive quads sharing a texture and scissor are submitted with
  one draw call.
- **Alpha:** premultiplied alpha blending (correct text edges, PNG alpha, and
  semi-transparent panels).
- **Depth:** test and write disabled (UI is an overlay; ordering follows widget order).
- **Clipping:** dynamic scissor rectangles support nested clip rects.
- **Textures:** reuse the engine `VulkanTexture` path (PNG/JPG/HDR/PPM via stb_image).
- **Resolution:** converts layout pixels to clip space centrally in the backend,
  so screens work across 1280x720, 1920x1080, 2560x1440, and 3840x2160.

### 9.3 Wiring in a packaged game

`HephaestusGame` owns the Vulkan overlay through `EngineFrameRenderer`:

```cpp
// per frame, after uiSystem.Render(uiRenderer):
frameRenderer.SubmitHUICommands(uiRenderer.GetDrawCommands());
frameRenderer.RenderFrame(&scene, camera, config, deltaTime);
```

The frame renderer prepares the overlay geometry, records it into the swapchain
pass after the scene, and recreates the UI pipeline/frame buffers on swapchain
resize. `EngineFrameRenderer::SetProjectRoot(...)` also seeds the UI font/image
search paths.

## 10. Lua Bridge Summary

HephaestusUI is exposed to Lua through the global `UI` table in `LuaScriptEngine`,
plus the managed `display.ui` / `destroy.ui` lifecycle API.

Two levels are available:
- Screen/system control (`createScreen`, visibility, active state, viewport)
- Dynamic widget API (create panel/text/button/image/boxes and mutate properties)
- Managed `.hui` loading with controller auto-load (`display.ui(path)`)

Full Lua API reference:
- `Documentation/Scripting/LUA_UI_API_REFERENCE.md`
- `Documentation/Scripting/LUA_DISPLAY_DESTROY_API.md`

Game maker walkthrough (UI Editor, `display.ui`, widgets by ID):
- `docs/user-guide/game-ui.md`

`.hui` asset format:
- `Documentation/Features/HUI_FORMAT_REFERENCE.md`

## 11. Supported Image Formats

Runtime UI images reuse the engine texture pipeline (stb_image):
- PNG, JPG, BMP, TGA, PSD, GIF, HDR, PIC, PNM/PPM

Scale modes (`UIImageScaleMode`): `Stretch`, `Fit`, `Fill`, `Center`.
Tint and opacity are applied per image. Missing images render as a tinted solid
placeholder and log the missing asset once (no crash).

## 12. Current Limitations

Known constraints:
- No keyboard/controller focus navigation
- No built-in animation/tweening
- No scroll lists / sliders / text-edit boxes / tooltips yet
- Word-wrap is greedy (no hyphenation); kerning is basic
- `UIStyle::scale` is parsed but not yet applied
- Rounded corners use a fragment-shader SDF (uniform quad; no tessellation)

## 13. Extension Plan

Designed extension points:
- Add focus manager for controller/keyboard navigation
- Add animation subsystem over `UIElement` properties
- Add scroll lists, sliders, and text input
- Reuse the Vulkan HUI backend for Editor Play (Stage 8B) in place of the QPainter overlay

## 14. Integration Checklist

When integrating into a runtime target:
- Create `UISystem`, `UIContext`, `UIRenderer`
- Set viewport size each frame
- Queue mouse events into `UIContext`
- Update `UISystem` before render
- Reset and render `UIRenderer`
- Submit the draw command list to the active backend
- If Lua is enabled, call `LuaScriptEngine::SetUIBindings(...)`