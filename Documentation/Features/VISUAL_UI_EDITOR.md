# Visual UI Editor

## Overview

The Visual UI Editor is Hephaestus Engine's dedicated Qt-based authoring tool for `.hui` assets.

It is designed for:

- beginners building their first menu without writing layout code
- UI/UX artists who want a visual workflow
- technical designers mixing `.hui` layouts with Lua controller logic
- developers who still want direct text access when needed

The Visual UI Editor is now the default editor for `.hui` files inside the Hephaestus editor.

## Opening `.hui` Files

### Default behavior

In the Asset Browser:

- double-clicking a `.hui` file opens the Visual UI Editor
- right-clicking a `.hui` file and choosing `Open` also opens the Visual UI Editor

`.hui` files are no longer opened in the external IDE by default.

### Open in IDE

The Visual UI Editor includes an `Open in IDE` action in its toolbar.

This opens the real `.hui` file on disk in the user's configured external editor when available. If no configured editor path is available, the engine falls back to the operating system's default file handler.

The Asset Browser also exposes `Open in IDE` for `.hui` assets.

## Main Layout

The first version of the Visual UI Editor uses a dedicated internal editor window with a familiar visual-tool layout:

- `Hierarchy`: widget tree for the current `.hui`
- `Palette`: available widget types that can be added visually
- `Canvas`: central design surface and live preview
- `Details`: inspector for the selected widget
- `Toolbar`: save, undo, redo, duplicate, delete, preview toggle, resolution preview, zoom, and `Open in IDE`

## Supported Widget Types

The editor is structured around the current `.hui` runtime widget set:

- `Panel`
- `Canvas`
- `VerticalBox`
- `HorizontalBox`
- `Grid`
- `Text`
- `Button`
- `Image`

These match the widget types supported by the runtime `.hui` parser and builder.

## Core Workflow

### 1. Create or open a UI

You can:

- create a new `.hui` file from the Asset Browser using `New UI File`
- open an existing `.hui` file from the Asset Browser

New `.hui` files use the shared default generator so starter layouts stay consistent across tools.

### 2. Add widgets

Use the `Palette` panel and double-click a widget type to add it.

The editor inserts the new widget into:

- the selected container, if the selection can contain children
- the selected widget's parent, if the selected widget is not a container
- the root, if nothing suitable is selected

New widgets receive sensible starter defaults and generated unique IDs.

### 3. Organize hierarchy

Use the `Hierarchy` panel to:

- select widgets
- inspect the parent/child structure
- drag and drop widgets to reorder or reparent them

This makes it easy to build menu stacks, grouped panels, HUD overlays, and more structured layouts.

### 4. Edit properties

Use the `Details` panel to edit the selected widget.

The editor exposes a first-pass set of grouped properties:

- `Identity`: ID, class, widget type
- `Layout`: position, size, opacity, anchor, alignment, visibility, enabled state, hit-testing, clipping
- `Content`: text, image reference, stack spacing, grid columns, and other widget-specific fields
- `Primary Style`: background color, text color, corner radius, font size

Changes update the canvas immediately.

### 5. Work on the canvas

The center canvas gives a visual preview of the active `.hui`.

Current v1 canvas features:

- live visual preview of the hierarchy
- selection outlines
- preview resolution switching
- zoom controls
- drag-to-move for widgets inside `Canvas` containers
- preview mode toggle to hide editing overlays

The canvas is intended for authoring, not just read-only preview.

## Layout Workflows

The Visual UI Editor supports both common `.hui` layout styles:

### Layout-container workflow

Use:

- `VerticalBox`
- `HorizontalBox`
- `Grid`
- `Panel`

This is best for:

- main menus
- pause menus
- settings screens
- dialog layouts
- inventory panels

### Absolute-placement workflow

Use `Canvas` for free-positioned widgets.

This is best for:

- HUD overlays
- title-screen compositions
- floating indicators
- custom screen-space placements

Widgets inside a `Canvas` can be repositioned directly on the design surface.

## Save, Dirty State, and Reopen

The Visual UI Editor behaves like a real asset editor:

- tracks unsaved changes
- updates the window title with a dirty marker
- supports `Save`
- supports `Save As`
- prompts before closing if changes are unsaved
- supports undo and redo via document snapshots

Saved files remain plain-text `.hui` assets.

## `.hui` Round-Tripping

The Visual UI Editor saves back to `.hui` deterministically while preserving unsupported JSON data when possible.

Current behavior:

- known `.hui` properties are serialized in a stable order
- parser-supported fields missing from older serializer output are preserved on save
- unknown/custom JSON fields are carried through the document model instead of being silently discarded

This keeps `.hui` usable as a source-controlled, human-readable asset.

## `.hui` and Lua Workflow

The Visual UI Editor focuses on structure, layout, and styling.

Behavior should still live in Lua.

Typical workflow:

1. Build the screen visually in the Visual UI Editor.
2. Set or edit the optional `controller` path.
3. Use Lua to bind events and runtime behavior by widget ID.

Example:

- `.hui`: declares the button and label structure
- `.lua`: uses `UI.getWidget(...)` and `UI.setOnClick(...)`

This keeps layout and behavior cleanly separated.

## Beginner Quick Start

1. In the Asset Browser, create `New UI File`.
2. Double-click the new `.hui`.
3. In `Palette`, add a `VerticalBox`.
4. Add a `Text` widget for the title.
5. Add one or more `Button` widgets.
6. Select each widget and edit text, size, and style in `Details`.
7. Use the preview resolution and zoom controls to check composition.
8. Save the file.
9. Optionally add a Lua controller and wire up button behavior.

## Example Menu Workflow

To create a pause menu:

1. Open `PauseMenu.hui` or create a new `.hui`.
2. Use a root `Panel` or `Canvas`.
3. Add a centered `VerticalBox`.
4. Add a title `Text`.
5. Add `Resume`, `Settings`, and `Quit` buttons.
6. Tune spacing and sizing in `Details`.
7. Set button colors and font size in `Primary Style`.
8. Save the `.hui`.
9. In Lua, bind each button by ID and attach click handlers.

## Limitations In This Version

The first version intentionally focuses on a solid editing foundation.

Current limitations:

- no multi-select yet
- no box-resize gizmos yet
- no copy/paste workflow yet
- no per-state visual editor for all button states yet
- no reusable widget/prefab system yet
- no animation or responsive-layout authoring tools yet
- preview mode is visual cleanup first, not full runtime interaction simulation

## Future Expansion

The editor is structured so future additions can be layered on without replacing the core workflow:

- richer visual manipulation tools
- copy/paste and multi-select
- alignment guides and snapping
- reusable components and prefabs
- themes and style classes
- animation timelines
- responsive device previews
- localization previews
- richer button/input state previews

## Summary

The Visual UI Editor makes `.hui` a first-class visual workflow in Hephaestus while keeping `.hui` text assets and Lua controllers intact.

Use it when you want fast, visual UI iteration.
Use `Open in IDE` when you want to inspect or hand-edit the source.
