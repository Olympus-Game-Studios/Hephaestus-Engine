# Hephaestus UI Visual Editor — Design Tools

This document describes the Qt-based **HUI Visual Editor** design-tool behaviors: grid, snapping, smart guides, canvas interaction, and how edits map to `.hui` files. Implementation lives primarily in [`engine/UI/QtEditor/HUIVisualEditorDialog.cpp`](../../engine/UI/QtEditor/HUIVisualEditorDialog.cpp).

## Eligibility: who can be moved or resized on the canvas

Freeform **move** and **resize** apply to widgets that are **direct children of a `Canvas` or `Panel`** in the `.hui` tree. Those parents do not run stack/grid layout over their children, so `position` / `size` in the document match what you see (see [`UIElement::UpdateLayout`](../../engine/UI/HephaestusUI/UIElement.cpp) vs [`UILayout::ApplyVerticalLayout`](../../engine/UI/HephaestusUI/UILayout.cpp)).

**Not draggable:** direct children of **`VerticalBox`**, **`HorizontalBox`**, or **`Grid`** — the runtime overwrites their transforms every frame. Use the hierarchy or switch to a `Panel`/`Canvas` parent if you need to drag (e.g. a button on the root `Panel` next to a `VerticalBox` can sit in the panel as a sibling and be positioned freely).

**Resize handles** are shown only when **one** widget is selected and it satisfies `NodeEligibleForFreeformTransform`: anchor **TopLeft** with horizontal alignment **Left** and vertical alignment **Top**. Other anchor/alignment combinations can change how `position` / `size` map to pixels; the editor avoids misleading handles in those cases.

## Coordinate spaces

- **Design / preview space** matches the preview viewport size (`m_previewSize`, e.g. 1280×720). [`CollectPreviewElementInfos`](../../engine/UI/QtEditor/HUIVisualEditorDialog.cpp) stores each widget’s [`UIElement::GetLayoutRect()`](../../engine/UI/HephaestusUI/UIElement.cpp) as `PreviewElementInfo::rect` in that space.
- **Screen space** is the Qt widget; `HUIDesignCanvas` maps design ↔ screen using `previewRect()`, zoom (`m_zoom`), and pan (`m_viewPan`).

## Grid overlay

- Toggle **Grid** on the toolbar. Spacing is controlled by **Grid px** (persisted under `QSettings` group `Hephaestus` / `HUIVisualEditor`, key `gridStep`, default 8).
- The grid is drawn inside the preview clip rect with low opacity; it does not affect runtime layout—only the author’s eye and snap behavior.

## Snapping

### Grid snap

- Toggle **Snap grid**. When enabled, after a **move** (or during resize), `position` / `size` are pulled toward the nearest multiple of `gridStep` using `SnapScalar` with a **soft threshold**: snap only if `|value − nearestMultiple| ≤ threshold`.
- **Threshold** scales with zoom: `max(2, 5 / zoomLevel)` design pixels, so snapping does not feel “sticky” when zoomed in.

### Guide snap (alignment to siblings and canvas)

- Toggle **Snap guides**. Candidate **X** lines include: canvas edges `0`, `width/2`, `width`, and for each non-selected **draggable sibling under the same Canvas parent**, that widget’s `left`, `right`, and horizontal center. **Y** lines are analogous (`top`, `bottom`, vertical center, and canvas horizontal thirds).
- While moving the **selection union** (bounding box of all selected canvas children), the editor tests the union’s `left`, `right`, `center.x`, `top`, `bottom`, and `center.y` against those candidates. The smallest distance under the threshold wins; a **single** delta `bestDx` / `bestDy` is applied to **all** selected widgets’ `position.x` / `position.y` so the group stays rigid.

### Alt bypass

Hold **Alt** during a drag to **disable** grid and guide snapping for that gesture (still allows raw pixel movement).

### Canvas center guides

- Toggle **Canvas center**. Draws faint vertical and horizontal lines through the viewport center. Those positions (`width/2`, `height/2`) are also included in guide snap candidates when **Snap guides** is on.

## Alignment guide visuals

When guide snap adjusts a move, thin **magenta-style** lines are stored in `m_liveGuideLines` (design-space segments, full width or height of the preview). Alpha is raised during drag (`m_liveGuideAlpha` ≈ 0.9) and cleared when the mouse is released. This is a lightweight visual hint, not a full Figma-style multi-edge display.

## Hit testing and stacking

**Selection** (click / hover) uses `hitTestTopmost`: among every preview element whose rect contains the pointer, the editor picks the one with highest `zOrder`, then greatest tree **depth**, then **smallest** layout area. That way a small widget (e.g. a button) wins over a full-screen panel behind it, even when the button lives in a layout container and is not freeform-draggable.

**Freeform drag** still uses the subset of elements marked `draggable` (direct children of `Panel` / `Canvas`) with the same “topmost” rule among that subset. Clicks on layout-managed widgets select them for the inspector; dragging only moves widgets whose parent allows free placement.

## Mouse interaction model

1. **Resize**  
   If one eligible widget is selected, **8 handles** are drawn on its design rect (corners + edge midpoints). Pressing inside a handle starts **Resize**; each move applies `applyResizeStep` (incremental delta in design space) to `HUIWidgetNode::position` / `size` with minimum size `kMinWidgetSize` (8 px).

2. **Move**  
   Pressing inside a draggable widget selects it (or adds to selection with **Shift**). Dragging applies the same design delta to every selected **canvas child** sharing the same parent path.

3. **Marquee**  
   Pressing on empty canvas starts a **rubber-band** rectangle (screen space). On release, the rectangle is converted to design space; every **draggable** widget whose `PreviewElementInfo::rect` intersects it and shares the **same Canvas parent** as the first hit is selected. **Shift** adds to the existing selection instead of replacing it.

4. **Undo for gestures**  
   The first non-trivial **move** or **resize** frame calls `pushUndoSnapshot()` once per gesture (full JSON document via `serializeHUIDocument`).

## Throttled preview rebuild

Dragging schedules `rebuildCanvas()` through a **single-shot `QTimer`** (~14 ms). Multiple mouse moves coalesce into one `HUIBuilder::Build` + `Update` + `Draw` pass per frame burst, reducing cost while keeping the document authoritative.

## Keyboard

| Input | Action |
|--------|--------|
| **Arrow keys** | Nudge selected canvas children by 1 design unit; **Shift** ×10. Respects grid snap unless **Alt** is held. Undo is batched (`m_nudgeUndoTimer`, 400 ms idle). |
| **Ctrl+G** | **Group** selected siblings under one Canvas into a new `Panel` (see below). |
| **Ctrl+Shift+G** | **Ungroup** a selected `Panel` whose parent is a `Canvas`. |
| **Ctrl+D** | Duplicate; with multi-selection, duplicates all selected siblings (same parent). |
| **Delete** | Deletes multi-selection when multiple canvas siblings are selected. |
| **Ctrl+Z / Ctrl+Y** | Undo / redo (document snapshots). |

The dialog uses `Qt::StrongFocus` so arrow keys reach the editor when it is focused.

## Group and Ungroup (.hui structure)

**Group** (requires ≥2 selected nodes, same Canvas parent, each TopLeft/Left/Top):

1. Compute the **union** of preview `rect`s in design space.
2. Compute the Canvas **content origin** as the Canvas widget’s preview rect top-left plus its `padding.left` / `padding.top` from the document node.
3. Remove the selected children from the Canvas (by index, high to low).
4. Insert a new **`Panel`** at the lowest removed index with `position` = union top-left minus content origin, `size` = union size, and `children` = the removed nodes.
5. Each child’s `position` is set to **(child rect origin − union top-left)** so visuals stay stable (panel padding on the new panel is default/zero from `MakeDefaultNode`).

**Ungroup** (selected node is a `Panel` under a `Canvas`):

1. Move each child’s `position` by adding the panel’s `position` (canvas-relative composite).
2. Replace the panel node with its children in the Canvas `children` vector at the same index.

## Z-order

Toolbar **Bring forward** / **Send back** adjust `HUIWidgetNode::zOrder` by ±1. Runtime draw order uses [`GetChildrenSortedByZ`](../../engine/UI/HephaestusUI/UIElement.cpp). The inspector **Transform** section includes a **Z-order** spin box; changes apply to all targets in a multi-selection (canvas children only).

## Mapping to the `.hui` file

All edits mutate the in-memory [`HUIDocument`](../../engine/UI/HephaestusUI/HUIAsset.h) / [`HUIWidgetNode`](../../engine/UI/HephaestusUI/HUIAsset.h) tree:

| Edit | Serialized fields / structure |
|------|-------------------------------|
| Move | `properties.position` (`UIVec2`) |
| Resize | `properties.size` (and sometimes `position` when resizing from left/top) |
| Snap | Same fields; values rounded/snapped in memory before save |
| Z-order | `properties.zOrder` |
| Group / Ungroup | `children` array hierarchy, `type`, `position`, `size` on the inserted/removed `Panel` |
| Undo / redo | Entire document JSON pushed on `m_undoStack` / `m_redoStack` via `serializeHUIDocument` / `HUIParser::ParseString` |

Saving the file writes `serializeHUIDocument(m_document)` to disk. Extra / unknown JSON keys are preserved elsewhere in the parser pipeline (`extraFields` on nodes); the visual editor does not strip them when round-tripping through undo snapshots.

## Settings persistence (`QSettings`)

| Key | Meaning |
|-----|---------|
| `gridVisible` | Toolbar **Grid** checked |
| `snapToGrid` | **Snap grid** |
| `snapToGuides` | **Snap guides** |
| `gridStep` | Grid spacing and primary snap step |
| `centerGuides` | **Canvas center** overlay + snap lines |

Organization: `Hephaestus` / `HUIVisualEditor`.

## Extension notes

- **Animation / constraints** are not implemented here; the interaction layer is localized to `HUIDesignCanvas` + `HUIVisualEditorDialog` and keeps mutations on `HUIWidgetNode` so future constraint data can be added to the same struct and serializer.
- **Anchor-aware resize** that preserves visual bounds for non–TopLeft anchors would require inverting pieces of `UIElement::UpdateLayout`; the editor currently restricts handles to the TopLeft / Left / Top case.
