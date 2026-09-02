# Editor Keyboard Shortcuts (Qt)

This document describes the keyboard shortcuts in the Qt Hephaestus Engine editor.
Shortcuts are context-aware: they do not override text-field editing when focus is in a line edit or similar widget.

## Global / Main Window

| Shortcut | Action |
|----------|--------|
| `Ctrl+S` | Save Level |
| `Ctrl+Shift+S` | Save Level As |
| `Ctrl+N` | New Level |
| `Ctrl+O` | Open Level |
| `Ctrl+Shift+O` | Open Project |
| `Ctrl+Z` | Undo |
| `Ctrl+Y` / `Ctrl+Shift+Z` | Redo |
| `Ctrl+C` | Copy Entity |
| `Ctrl+V` | Paste Entity |
| `Ctrl+D` | Duplicate Entity |
| `H` | Hide selected entities in the editor |
| `Shift+H` | Isolate selected / exit isolation |
| `Ctrl+H` | Show all / exit isolation |
| `End` | Drop selected entities to ground |
| `Alt+1` … `Alt+9` | Recall camera bookmark |
| `Ctrl+Alt+1` … `Ctrl+Alt+9` | Save camera bookmark |
| `Ctrl+,` | Settings |
| `Esc` | Stop Play Mode (while playing) |

## Outliner (Scene Hierarchy)

Active when the Outliner tree has focus (not the search box):

| Shortcut | Action |
|----------|--------|
| `Delete` | Delete selected entity |
| `F2` | Rename selected entity |
| `Ctrl+D` | Duplicate selected entity |
| `Ctrl+A` | Select all entities |
| `Esc` | Clear selection |

## Details (Inspector)

Active when a component inspector has focus (does not steal `Ctrl+C` entity copy):

| Shortcut | Action |
|----------|--------|
| `Ctrl+Shift+C` | Copy component |
| `Ctrl+Shift+V` | Paste component values onto the selection |
| `Ctrl+Shift+R` | Reset component to constructor defaults |

Numeric Inspector fields remain **local** transform values. The Details World/Local control is labelled **Gizmo space** and only syncs the viewport gizmo.

Component headers offer Copy / Paste Values / Reset to Default / Remove. Prefab instances also show Revert; Apply to Prefab is hidden for multi-select or mixed prefab sources.

## Viewport

Active when the viewport has focus / mouse interaction:

| Shortcut | Action |
|----------|--------|
| `W` | Translate gizmo |
| `E` | Rotate gizmo |
| `R` | Scale gizmo |
| `Q` | Hide gizmo / selection tool |
| `X` | Toggle World / Local space |
| `F` | Focus selected |
| `Delete` | Delete selection |
| `Alt` + gizmo drag | Duplicate and transform selection |
| `Ctrl` during gizmo drag | Temporarily bypass snapping |
| `Shift` during translate drag | Surface snap |
| `` ` `` | Cycle to the next object under the cursor |
| `RMB + WASD` | Fly camera |
| `RMB + Mouse wheel` | Adjust fly-camera speed |
| Drag empty viewport | Marquee-select fully enclosed objects |

Dragging a mesh, prefab, or audio asset into the viewport starts a placement
preview. Click to commit, `Shift+click` to place another copy, or `Esc` to
cancel.

## Prefab Editor

| Shortcut | Action |
|----------|--------|
| `Ctrl+S` | Save prefab |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| `F` | Frame selected |

## Material Graph Editor

| Shortcut | Action |
|----------|--------|
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| `Ctrl+D` | Duplicate selected nodes |
| `Delete` | Remove selected nodes/links |
| `F` | Frame selection |
| `Tab` | Node search |

## HephaestusUI (`.hui`) Visual Editor

| Shortcut | Action |
|----------|--------|
| `Ctrl+S` | Save |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| `Ctrl+D` | Duplicate |
| `Delete` | Delete selection |
| `Ctrl+G` / `Ctrl+Shift+G` | Group / Ungroup |
| Arrow keys | Nudge selection |

## Asset Browser

| Shortcut / Action | Notes |
|-------------------|-------|
| Context menu Rename / Duplicate / Delete / Copy Path | Right-click assets |
| Import toolbar button | Copies files into the current folder |
| Clickable breadcrumbs | Navigate ancestor folders |

## Notes

- Scene and Prefab undo are snapshot-based with a bounded redo stack.
- Continuous transform scrubbing and gizmo drags coalesce into a single undo entry where possible.
- Lit/Unlit/Wireframe and perspective/orthographic viewport modes are live.
- Editor hide/lock state and camera bookmarks are stored under
  `.hephaestus/editor/` and do not alter runtime scene visibility.
