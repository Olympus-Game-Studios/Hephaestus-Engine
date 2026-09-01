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
| `RMB + WASD` | Fly camera |
| `Mouse wheel` | Adjust camera speed |

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
- Lit/Unlit/Wireframe shading modes are not yet wired to the renderer; the viewport shows Lit only.
