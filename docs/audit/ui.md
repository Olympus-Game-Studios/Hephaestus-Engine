# UI audit (HephaestusUI / `.hui`)

**Overall: 3 / 5 (Usable menus and simple HUD)**  
Runtime 3 · Editor 3 · Lua 3 · Tests 0

HephaestusUI is a **retained widget tree** that emits a **draw-command list**, composited by the host (Qt overlay in editor; game host in `GameMain`). It is not Dear ImGui and not a full GPU UI toolkit.

---

## File format / runtime / editor

| Piece | Evidence |
|-------|----------|
| Format | `HUIAsset.h` — JSON `.hui`, widget tree + styles + optional controller script |
| Runtime | `UISystem`, `UIScreen`, `UIElement`, `UIRenderer`, `UIInputRouter`, `UITheme` |
| Editor | `HUIVisualEditorDialog` (~3k LOC): canvas, hierarchy, drag/resize, styles |
| Lua | `UI.*`, `UI.loadHUI`, `display.ui` / `destroy.ui` |

**Maturity:** 3

---

## Widgets

`HUIWidgetType`: Panel, Canvas, VerticalBox, HorizontalBox, Grid, Text, Button, Image.

**Missing:** Text field, slider, checkbox, dropdown, scroll view, progress bar, list virtualization.

---

## Layout / positioning

- Anchors (`UIAnchorPreset`), alignment, padding, margin, min/max size, spacing, z-order, opacity, clipping
- **Maturity:** 3 for menus
- **Responsive:** Anchor-based; no constraint solver / percentage layout beyond that
- **Resolution scaling:** Viewport size APIs (`setViewportSize`); not a full DPI/UI scale system

---

## Text / fonts / images

- `UIText`, `UIFont`, word wrap, alignment, shadow, bold/italic flags
- `UIImage` + tint + scale mode
- Overlay may placeholder missing images (Qt overlay code)
- **Maturity:** 2–3

---

## Interaction

- Mouse hit-test, hover, press, click (`UIInputRouter`)
- Lua `setOnClick` / `setOnHover`
- `CursorMode` Game vs UI — wired Qt + GLFW (important for FPS + HUD)
- `setFocused` exists in Lua bindings; **no keyboard navigation / text caret** in HephaestusUI sources
- Gamepad UI navigation: **no**
- **Maturity:** 3 mouse · 1 focus/keyboard

---

## Animation / themes

- Button state styles (normal/hover/pressed/disabled)
- Theme register/apply from Lua
- No timeline/UI animation system
- **Maturity:** 2 themes · 0 UI animation

---

## Multiple documents / layering / show-hide

- Multiple screens; visibility/active; z-order
- `display.ui` / `destroy.ui` / `clearScreens`
- **Maturity:** 3

---

## Accessibility / localization

- **None** — no names for AT, no string tables
- **Maturity:** 0

---

## Debugging

- Lua `getDrawCommandCount`
- Editor visual editor
- No runtime widget picker / wireframe overlay in game
- **Maturity:** 2

---

## Suitability

| Use | Fit |
|-----|-----|
| Main menu / pause | **Good** — `MainMenu.hui` / `PauseMenu.hui` + Lua controllers exist under `assets/ui/` |
| Simple HUD (health text, crosshair panel) | **Possible** |
| Settings with sliders/rebind | **Poor** until form widgets exist |
| Inventory / RPG character sheet | **Poor** (no scrolling lists) |
| Complex app-like UI | **Poor** |

Designer workflow: visual editor is real. Programmer workflow: Lua can build UI procedurally **or** load `.hui`.

---

## Recommended next step

Text field + slider + scroll view if settings/inventory matter. Keyboard/gamepad focus after input maps exist. Do not replace with Qt QML at runtime.
