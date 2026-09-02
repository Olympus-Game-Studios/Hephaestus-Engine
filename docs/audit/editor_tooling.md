# Editor tooling audit

**Overall: 4 / 5 (Strongest product surface)**  
After UX Phases 1–4 and Stage 6, the Qt editor is practical for assembling and packaging small 3D projects. Remaining gaps are **systems**, not icon padding.

Primary UI is **Qt Widgets** (`engine/UI/QtEditor`). README’s ImGui editor is **stale**. Comments still mention “legacy ImGui”; no ImGui editor sources in `engine/`.

---

## Surfaces

| Surface | Maturity | Notes |
|---------|--------:|-------|
| Launcher | 3 | Recent projects, open-last, New Project via `ProjectCreationService` |
| Scene viewport | 4 | Vulkan in `QtViewportWindow` (Win32 HWND), gizmos, marquee, snap, ortho views, Lit/Unlit/Wireframe, placement preview |
| Outliner | 4 | Shortcuts, filters (component/prefab/hidden/locked), expansion persist |
| Inspector | 4 | Multi-select mixed values, Add Component popup, asset refs, clipboard, registry |
| Asset Browser | 4 | Search, import, duplicate, trash, references, filters, MIME drags |
| Prefab Editor | 3 | Live component authoring |
| Material Editor | 3 | Graph bake to PBR |
| HUI Editor | 3 | Visual `.hui` |
| Texture Viewer | 3 | Checkerboard, channels, zoom |
| Build window | 4 | Stage 6 |
| Settings | 3 | Real settings (changelog); some render keys unwired |
| Play mode | 3 | Snapshot + Lua; HUD overlay + cursor capture are delicate |

---

## Cross-cutting

| Capability | Status |
|------------|--------|
| Undo/redo | Scene + prefab; coalesced transforms |
| Search | Asset Browser project-wide |
| Snapping | Translate/rotate/scale; Ctrl bypass |
| Multi-select | Viewport + outliner + inspector |
| Placement | Surface-aware preview, repeat/cancel |
| Recovery | `RecoveryStore` autosave ~30s |
| Notifications | `EditorNotificationService` |
| Dock layout | Persisted |
| Keyboard | Outliner + build shortcut; not a full keymap product |

---

## Remaining **major** gaps (not polish)

1. Unwired inspector fields (env yaw, render distance, exposure, physics debug draw, cylinder).
2. Lua exposed-variable Inspector.
3. No animation / particle / nav editors (systems missing).
4. Add Actors empty categories: Audio, Volumes, Environment, FX, UI, Gameplay (`AddActorsPanel.cpp` placeholders).
5. Auto-reimport off.
6. Qt tests opt-in and trivial (`EditorQtSmokeTests`).
7. Linux/macOS viewport.
8. World-space TRS display / universal enabled flag (called out in prior UX audit).
9. Play-in-editor vs packaged parity (dual renderer + input hosts).

---

## Designer vs programmer

Designers can build levels, prefabs, materials (PBR), menus, and Windows packages without C++. They **cannot** author animation, VFX, input maps, or tune Lua vars in the Inspector.

---

## Recommended next step

Fix lying controls (unwired fields). Lua var Inspector. Do not add new specialized editors until those runtimes exist.
