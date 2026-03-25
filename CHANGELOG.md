# Hephaestus Engine Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added

- 2026-03-25: Added right-click context menu to `AssetBrowserWidget` on both the contents view and folder tree. Supports creating new folders, Lua scripts, scenes (`.hpscene`), and materials (`.hematerial`). Also supports rename, delete, and "Show in Explorer" for existing assets.
- 2026-03-24: Expanded `UIStyle` with comprehensive per-widget visual properties: `fontFamily`, `fontSize`, `fontBold`, `fontItalic`, `textAlignment`, `opacity`, `scale`, `backgroundImageId`, `imageScaleMode`, `imageTint`, `textShadow`, `textShadowColor`, `textShadowOffset`.
- 2026-03-24: Added `UIHAlignment`, `UIVAlignment`, and `UIImageScaleMode` enums to `UIStyle.h` for layout alignment and image scaling control.
- 2026-03-24: Added per-element `enabled/disabled` state, `minSize`/`maxSize` constraints, horizontal/vertical alignment, and owned style support to `UIElement`.
- 2026-03-24: Implemented anchor-based layout in `UIElement::UpdateLayout()` — all `UIAnchorPreset` values (TopLeft through Stretch) now compute layout positions correctly.
- 2026-03-24: Expanded `UIButton` with `Focused` and `Disabled` visual states, per-state style getters, `SetFocused(bool)`, and `SetOnHovered` callback.
- 2026-03-24: Added `UITheme` named style registry with `SetStyle`/`GetStyle`/`HasStyle` and `CreateDefaultTheme()` factory providing preset styles (`default_button`, `primary_button`, `danger_button`, `title_text`, `heading_text`, `body_text`, `label_text`, `panel_background`, `menu_container`, `overlay_background`).
- 2026-03-24: Integrated `UITheme` into `UISystem` (initialized with default theme) for centralized style management.
- 2026-03-24: Added `UIGridLayout` container widget with configurable columns, horizontal/vertical spacing, and per-cell alignment.
- 2026-03-24: Added child alignment support to `UIVerticalBox` (`SetChildHAlignment`) and `UIHorizontalBox` (`SetChildVAlignment`) for controlling how children are positioned within the cross-axis.
- 2026-03-24: Expanded `UIDrawTextCommand` with `bold`, `italic`, `shadow`, `shadowColor`, `shadowOffset` fields for richer text rendering.
- 2026-03-24: Expanded `UIDrawRectCommand` with `backgroundImageId`, `imageScaleMode`, `imageTint` for image-backed rectangles.
- 2026-03-24: Expanded `UIDrawImageCommand` with `scaleMode` field.
- 2026-03-24: Added text shadow rendering support in `RuntimeUIOverlayWidget` QPainter backend (draws shadow pass before main text).
- 2026-03-24: Added bold/italic font support in `RuntimeUIOverlayWidget` QPainter text rendering.
- 2026-03-24: Added 30+ new Lua UI bindings: `setStyle`, `setButtonStyle`, `setFocused`, `registerThemeStyle`, `applyThemeStyle`, `setEnabled`, `setPadding`, `setMargin`, `setMinSize`, `setMaxSize`, `setAnchor`, `setAlignment`, `setChildAlignment`, `setBackgroundColor`, `setBorderColor`, `setBorderWidth`, `setCornerRadius`, `setTextColor`, `setFontSize`, `setFontBold`, `setFontItalic`, `setTextAlignment`, `setTextShadow`, `setWordWrap`, `setOnHover`, `createGrid`, `setGridColumns`, `setGridSpacing`, `setImageScaleMode`, `setImageTint`.
- 2026-03-24: Added `ParseStyleTable` helper for Lua table-to-UIStyle conversion, supporting all style properties including colors as `{r,g,b,a}` tables and spacing as uniform float or `{left,top,right,bottom}` tables.
- 2026-03-24: Added Lua enum constant tables: `UIAnchor` (TopLeft..Stretch), `UIAlign` (Left/Center/Right/Top/Bottom/Stretch), `UIScaleMode` (Stretch/Fit/Fill/Center).
- 2026-03-24: Rewrote `scripts/mainMenu.lua` to demonstrate the new styled UI system: centered panel with rounded corners, semi-transparent overlay, styled title with text shadow, per-state button styling (normal/hovered/pressed/focused/disabled), keyboard focus visuals, hover callbacks, and modern visual hierarchy.
- 2026-03-24: Added `CursorMode` enum (`Game` / `UI`) to `UISystem` for high-level cursor/input ownership switching between gameplay and UI interaction.
- 2026-03-24: Added Lua `UI.setCursorMode(mode)` and `UI.getCursorMode()` bindings plus global `CursorMode` constants table (`CursorMode.Game`, `CursorMode.UI`) for script-driven cursor mode control.
- 2026-03-24: Wired cursor mode into Qt editor play mode: editor camera controller is suppressed (mouse press/release/move/wheel) when runtime UI is in `CursorMode::UI` to prevent fly-mode interference with UI interaction.
- 2026-03-24: Wired cursor mode into GLFW game runtime via direct `glfwSetInputMode` calls from the Lua binding, with raw mouse motion toggling.
- 2026-03-24: Cursor mode automatically resets to `Game` when play mode stops in the editor.
- 2026-03-24: Updated `scripts/pauseUI.lua` example to demonstrate `UI.setCursorMode` / `CursorMode` API for pause menu toggle with proper cursor lock/unlock. Menu starts visible in UI mode on play so the UI is immediately shown with a free cursor.
- 2026-03-24: Documented cursor mode API in `LUA_UI_API_REFERENCE.md`, `LUA_SCRIPTING_GUIDE.md`, and `HEPHAESTUS_UI_RUNTIME_GUIDE.md`.
- 2026-03-24: Added `scripts/mainMenu.lua`, a basic Lua-driven runtime main menu test with keyboard navigation, cursor mode switching, start/hide behavior, and `Application.quit()` integration.
- 2026-03-24: Added `UI.setOnClick(buttonHandle, callback)` Lua binding for registering mouse click callbacks on UIButton elements. Callbacks are stored as Lua registry references and invoked when buttons are clicked.

### Fixed

- 2026-03-24: Fixed Lua scripts not receiving mouse input during Qt editor play mode. The `RuntimeUIOverlayWidget` now directly captures mouse events (press/release/move) and forwards them to the `UIContext` via `QtEditorEngine::ForwardOverlayMouseEvent()`, bypassing the viewport's focus-based input filtering.
- 2026-03-24: Fixed Lua scripts not receiving keyboard input during Qt editor play mode when the viewport window didn't have direct focus. Key events are now forwarded from `EditorMainWindow` to the engine's `RuntimeInputState` during play mode, enabling UI menu navigation (W/S/Up/Down/Enter) even when another Qt widget has focus.
- 2026-03-24: Fixed `Input.setCursorMode` and `UI.setCursorMode` calls made during `onAwake` being silently dropped in the Qt editor because `RuntimeInputState.valid` was not yet true. The `cursorModeRequest` is now always written so the host picks it up on the first tick.
- 2026-03-17: Added initial `HephaestusUI` runtime retained-mode UI module with `UISystem`, `UIContext`, `UIScreen`, base `UIElement` tree, layout containers (`UICanvas`, `UIVerticalBox`, `UIHorizontalBox`), widgets (`UIPanel`, `UIText`, `UIImage`, `UIButton`), command-list based `UIRenderer`, `UIInputRouter`, `UIFont` registry scaffolding, `UIStyle`, and a code example for building a simple menu screen.
- 2026-03-17: Connected runtime UI to Lua scripting via a new `UI` binding surface (`isAvailable`, `createSimpleMenu`, `clearScreens`, `getScreenCount`, `setScreenVisible`, `setScreenActive`, `setViewportSize`, `getDrawCommandCount`) and wired `HephaestusGame` to update UI input/layout each frame through `UISystem`/`UIContext`.
- 2026-03-17: Expanded Lua runtime UI API with handle-based widget creation and mutation (`createScreen`, `getRoot`, `createPanel`, `createText`, `createButton`, `createImage`, `createVerticalBox`, `createHorizontalBox`, `setPosition`, `setSize`, `setVisible`, `setOpacity`, `setZOrder`, `setText`, `setImage`, `setSpacing`) plus internal UI handle lifecycle tracking.
- 2026-03-17: Added full HephaestusUI documentation including runtime architecture/lifecycle/render-command integration guide and complete Lua UI API reference, with links added in Features and Scripting documentation indexes.
- 2026-03-17: Added a dedicated script-only guide for creating and displaying custom runtime UI without engine source access (`Documentation/Guides/NO_SOURCE_RUNTIME_UI_GUIDE.md`) and linked it from the Guides index and HephaestusUI runtime guide.
- 2026-03-17: Wired HephaestusUI into Qt editor play mode by binding editor Lua runtime to `UISystem`/`UIContext`/`UIRenderer`, forwarding viewport mouse input to UI routing, ticking UI per frame, and adding a viewport overlay widget to visualize runtime UI draw commands during editor play testing.

#### Multi-Selection Support in Outliner (2026-01-29)

**Feature**: Users can now select multiple actors in the World Outliner and viewport using 
standard multi-selection controls. This enables batch operations on multiple entities.

**Key Features**:

1. **Ctrl+Click (Cmd+Click on macOS)**: Toggle individual selection - add or remove an 
   entity from the current selection without affecting other selected entities.

2. **Shift+Click**: Range selection in the Outliner - select all entities between the 
   last clicked entity and the current entity in the visible tree order.

3. **Viewport Multi-Selection**: Ctrl+Click on objects in the 3D viewport to add/remove 
   them from the selection.

4. **Synchronized Selection**: Selection state is synchronized between the Outliner, 
   viewport, and Details panel. All selected entities show selection outlines.

5. **Multi-Entity Transform Editing**: When multiple entities are selected, transform 
   changes in the Details panel (position, rotation, scale) apply to all selected entities.

6. **Gizmo Multi-Transform**: Using the viewport gizmo to move/rotate/scale affects all 
   selected entities simultaneously, maintaining their relative positions.

**Changes Made**:

1. **HtmlEditorUIHost.cpp**:
   - Added `scene.range_select` handler for Shift+Click range selection
   - Handles comma-separated entity IDs for batch selection

2. **outliner.html**:
   - Added tracking of visible entity order for range selection
   - Implemented Shift+Click range selection logic
   - Enhanced Ctrl+Click to properly toggle selection
   - Updated status bar to show selection feedback

3. **main.cpp**:
   - Viewport picking now supports Ctrl+Click for multi-selection toggle
   - Gizmo transformations apply to all selected entities using delta transformation
   - Selection outline rendering updated to draw outlines for all selected entities
   - ImGui WorldOutliner updated with Ctrl+Click support

4. **Scene.cpp** (existing infrastructure):
   - `GetSelectedEntities()` returns all selected entities
   - `AddSelectedEntity()` adds to multi-selection
   - `ToggleSelectedEntity()` toggles individual selection
   - `ClearSelection()` clears all selections

**Backward Compatibility**: Single-selection behavior remains unchanged. Clicking without 
modifiers still replaces the current selection with the clicked entity.

### Fixed

#### Multi-Material Mesh Rendering in Game Runtime (2026-01-28)

**Problem**: Meshes with multiple material indices (multiple submeshes/sections) were not 
rendering correctly in the exported/standalone game runtime. Only the first material would 
show, or sections would fallback to a default material, even though they rendered correctly 
in the editor.

**Root Cause**: 
1. When loading meshes at runtime, the `MeshRendererComponent::submeshCount` was not being 
   updated from the loaded mesh data, causing a mismatch between the mesh's actual submesh 
   count and the component's tracked count.
2. The runtime was properly storing submesh data in `MeshGPU::submeshes` but the renderer 
   component wasn't synchronized.

**Changes Made**:

1. **GameMain.cpp - Mesh Loading** (`LoadMeshToGPU` integration):
   - After loading a mesh from file, now properly updates `renderer.submeshCount` from the 
     loaded `MeshGPU::submeshes` array.
   - Added debug logging showing mesh name, submesh count, and material slot count.
   - Added warning when submesh count doesn't match material slot count.
   - Same synchronization added for cached mesh reuse path.

2. **GameMain.cpp - Debug Logging** (Debug builds only):
   - Added comprehensive multi-material debug info section before the main render loop.
   - Logs each entity's mesh name, handle, submesh count, and material slot count.
   - For each submesh, logs the material slot index, index range, material path, and 
     whether the material was successfully loaded.
   - Warns when there are more submeshes than material slots.

3. **GameMain.cpp - Runtime Asset Path Resolution**:
    - `NormalizeAssetPath()` now resolves relative material/texture paths under 
       the runtime `assets/` directory if the direct runtime-root path doesn't exist.
    - Fixes cases where editor-stored paths like `materials/rock.hpmat` were 
       not found at runtime (expected under `assets/materials/...`).

4. **SceneExporter.cpp - Export Material Slot Assets**:
    - Export now includes material assets referenced by mesh slot overrides
       (`MeshRendererComponent::appliedMaterialPaths`), ensuring they are copied
       into the runtime `assets/` folder.

5. **GameMain.cpp - World-Aligned Material Graph Support (Runtime)**:
    - Runtime material loader now detects `WorldAlignedTexture` nodes in the
       material graph and enables world-aligned UVs, texture binding, and
       world texture scale without requiring the editor-side graph evaluator.

3. **Test Scene**:
   - Added `assets/scenes/MultiMaterialRuntimeTest.hpscene` to test:
     - Single material meshes (should render normally)
     - Override material tests (material should apply correctly)
     - Missing/fallback material tests (should use entity's MaterialComponent)

**New Debug Warnings** (in debug builds):
- "Mesh 'X' has N submeshes but M material paths assigned. Some sections may use fallback materials."
- Per-submesh material resolution status: "loaded", "NOT FOUND", "default", or "slot missing"

**Acceptance Criteria**:
1. ✅ Multi-material meshes render identically in editor and runtime
2. ✅ Each submesh uses its assigned material slot
3. ✅ If a material slot is missing, only that section falls back to default
4. ✅ Blueprint/scene overrides for per-slot materials work in runtime
5. ✅ Single-material meshes continue to work without regression

---
