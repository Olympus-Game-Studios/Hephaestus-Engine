# HUI Format Reference

## Overview

### What is .hui?

`.hui` (**H**ephaestus **UI**) is the engine-native file format for defining UI layouts in the Hephaestus Engine. It is a declarative, JSON-based asset format designed to describe the structure, hierarchy, styling, and properties of runtime UI widget trees.

### Why does it exist?

Previously, all runtime UI in Hephaestus was built procedurally via Lua scripts using the `UI.*` API. While flexible, this approach mixed layout concerns with behavioral logic, making UIs harder to iterate on, share between designers and programmers, and tool-author for.

`.hui` separates **what the UI looks like** from **what the UI does**:

| Concern | Owned by |
|---|---|
| Widget hierarchy, types, layout, sizing, styling, text, images | `.hui` file |
| Click handlers, state changes, animations, game logic | `.lua` controller script |

This separation makes the UI workflow more declarative, easier to visually inspect, and ready for a future visual editor.

### How it replaces Lua-based UI layout

**Old workflow:**
```lua
-- Everything in Lua
local screen = UI.createScreen("menu")
local root = UI.getRoot(screen)
local panel = UI.createPanel(root)
UI.setSize(panel, 200, 50)
UI.setBackgroundColor(panel, 0.1, 0.1, 0.15, 1)
local text = UI.createText(panel, "Hello")
UI.setFontSize(text, 24)
-- ... dozens more lines for layout ...
UI.setOnClick(button, function() ... end) -- buried in layout code
```

**New workflow:**
```json
// MainMenu.hui — layout only
{
    "version": 1,
    "controller": "MainMenu.lua",
    "root": {
        "type": "Panel",
        "id": "root",
        "children": [
            { "type": "Text", "id": "title", "properties": { "text": "Hello" } },
            { "type": "Button", "id": "ok_btn", "properties": { "text": "OK" } }
        ]
    }
}
```
```lua
-- MainMenu.lua — logic only
function setupMainMenu(screen)
    local btn = UI.getWidget(screen, "ok_btn")
    UI.setOnClick(btn, function()
        print("OK clicked!")
    end)
end
```

---

## File Structure

A `.hui` file is a single JSON object with three top-level fields:

```json
{
    "version": 1,
    "controller": "path/to/Controller.lua",
    "root": { ... }
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `version` | integer | Yes | Format version. Currently `1`. |
| `controller` | string | No | Relative path to the Lua controller script. Empty string or omitted if no controller. |
| `root` | object | Yes | The root widget node of the UI hierarchy. |

### Widget node format

Every widget (including the root) follows this schema:

```json
{
    "type": "WidgetType",
    "id": "unique_identifier",
    "class": "style_class_name",
    "properties": { ... },
    "style": { ... },
    "normalStyle": { ... },
    "hoveredStyle": { ... },
    "pressedStyle": { ... },
    "disabledStyle": { ... },
    "children": [ ... ]
}
```

| Field | Required | Description |
|---|---|---|
| `type` | Yes | Widget type name (see Supported Widgets below). |
| `id` | Recommended | Unique identifier for the widget. Used for Lua lookups. Must be unique within the document. |
| `class` | No | Style class name (reserved for future theming). |
| `properties` | No | Layout, content, and behavior properties. |
| `style` | No | Visual style properties (colors, fonts, borders, etc.). |
| `normalStyle` | No | Button-specific: style when in normal state. |
| `hoveredStyle` | No | Button-specific: style when hovered. |
| `pressedStyle` | No | Button-specific: style when pressed. |
| `disabledStyle` | No | Button-specific: style when disabled. |
| `children` | No | Array of child widget nodes (for containers). |

### Hierarchy rules

- The `root` must be a single widget node (typically a `Panel` or `Canvas`).
- Only container widgets (`Panel`, `Canvas`, `VerticalBox`, `HorizontalBox`, `Grid`) should have `children`.
- Widget IDs must be unique across the entire document.
- Nesting depth is unlimited but keep it reasonable for performance.

---

## Supported Widgets

### Panel

A rectangular container with optional background styling.

```json
{
    "type": "Panel",
    "id": "my_panel",
    "properties": { "anchor": "Stretch" },
    "style": { "backgroundColor": [0.1, 0.1, 0.15, 1.0], "cornerRadius": 8 },
    "children": [ ... ]
}
```

### Canvas

A free-positioning container. Children are placed by absolute position.

```json
{
    "type": "Canvas",
    "id": "my_canvas",
    "children": [
        { "type": "Text", "id": "label", "properties": { "position": [100, 50], "text": "Hello" } }
    ]
}
```

### VerticalBox

Stacks children vertically with configurable spacing.

| Property | Type | Default | Description |
|---|---|---|---|
| `spacing` | float | 8 | Space between children (pixels). |
| `childHAlignment` | string | "Stretch" | Horizontal alignment of children: Left, Center, Right, Stretch. |

```json
{
    "type": "VerticalBox",
    "id": "vbox",
    "properties": { "spacing": 16, "childHAlignment": "Center" },
    "children": [ ... ]
}
```

### HorizontalBox

Stacks children horizontally with configurable spacing.

| Property | Type | Default | Description |
|---|---|---|---|
| `spacing` | float | 8 | Space between children (pixels). |
| `childVAlignment` | string | "Stretch" | Vertical alignment of children: Top, Center, Bottom, Stretch. |

### Grid

Arranges children in a grid layout.

| Property | Type | Default | Description |
|---|---|---|---|
| `columns` | int | 2 | Number of columns. |
| `hSpacing` | float | 8 | Horizontal spacing between cells. |
| `vSpacing` | float | 8 | Vertical spacing between cells. |

### Text

Displays a text string.

| Property | Type | Default | Description |
|---|---|---|---|
| `text` | string | "" | The text content to display. |
| `textAlignment` | string | "Left" | Text alignment: Left, Center, Right. |
| `wordWrap` | bool | false | Enable word wrapping. |

Style properties control font, size, color, and shadow (see Styling section).

### Button

An interactive button with text label and state-based styling.

| Property | Type | Default | Description |
|---|---|---|---|
| `text` | string | "Button" | Button label text. |
| `size` | [x, y] | [0, 0] | Explicit button size. |

Buttons support separate style blocks for each state: `normalStyle`, `hoveredStyle`, `pressedStyle`, `disabledStyle`.

### Image

Displays an image asset.

| Property | Type | Default | Description |
|---|---|---|---|
| `imageId` | string | "" | Asset identifier for the image. |
| `imageScaleMode` | string | "Stretch" | Scale mode: Stretch, Fit, Fill, Center. |
| `imageTint` | color | white | Tint color applied to the image. |

---

## Layout System

### Common properties (all widgets)

| Property | Type | Default | Description |
|---|---|---|---|
| `position` | [x, y] | [0, 0] | Position relative to parent (for Canvas) or ignored (for auto-layout). |
| `size` | [x, y] | [0, 0] | Explicit size. 0 = auto/parent-determined. |
| `minSize` | [x, y] | [0, 0] | Minimum size constraint. |
| `maxSize` | [x, y] | [0, 0] | Maximum size constraint. |
| `anchor` | string | "TopLeft" | Anchor preset (see below). |
| `hAlignment` | string | "Left" | Horizontal alignment within parent. |
| `vAlignment` | string | "Top" | Vertical alignment within parent. |
| `padding` | spacing | 0 | Inner padding (inside the element). |
| `margin` | spacing | 0 | Outer margin (outside the element). |
| `visible` | bool | true | Whether the widget is rendered. |
| `enabled` | bool | true | Whether the widget accepts interaction. |
| `opacity` | float | 1.0 | Opacity (0 = invisible, 1 = opaque). |
| `zOrder` | int | 0 | Render order among siblings. |
| `interactable` | bool | true | Whether hit-testing includes this widget. |
| `clippingEnabled` | bool | false | Whether children are clipped to bounds. |

### Anchor presets

| Value | Description |
|---|---|
| `TopLeft` | Positioned from parent's top-left corner. |
| `TopCenter` | Centered at top of parent. |
| `TopRight` | Positioned from parent's top-right corner. |
| `CenterLeft` | Centered vertically on left side. |
| `Center` | Centered in parent. |
| `CenterRight` | Centered vertically on right side. |
| `BottomLeft` | Positioned from parent's bottom-left corner. |
| `BottomCenter` | Centered at bottom of parent. |
| `BottomRight` | Positioned from parent's bottom-right corner. |
| `Stretch` | Fills parent (respects padding/margin). |

### Spacing format

Spacing values (padding, margin) accept two formats:

- **Uniform:** A single number applies to all four sides: `"padding": 10`
- **Per-side:** An array of `[left, top, right, bottom]`: `"padding": [10, 20, 10, 20]`

### Vec2 format

Two-component values (position, size, etc.) use arrays: `"size": [200, 50]`

---

## Styling

### Style block properties

Style blocks can appear as `style`, `normalStyle`, `hoveredStyle`, `pressedStyle`, or `disabledStyle`.

| Property | Type | Default | Description |
|---|---|---|---|
| `backgroundColor` | color | transparent | Background fill color. |
| `backgroundImageId` | string | "" | Background image asset ID. |
| `imageScaleMode` | string | "Stretch" | Background image scale mode. |
| `imageTint` | color | white | Background image tint. |
| `borderColor` | color | transparent | Border color. |
| `borderWidth` | float | 0 | Border width in pixels. |
| `cornerRadius` | float | 0 | Rounded corner radius in pixels. |
| `textColor` | color | white | Text rendering color. |
| `fontFamily` | string | "Default" | Font family name. |
| `fontSize` | float | 0 | Font size in points (0 = default). |
| `fontBold` | bool | false | Bold text. |
| `fontItalic` | bool | false | Italic text. |
| `textAlignment` | string | "Left" | Text alignment within the widget. |
| `padding` | spacing | 0 | Style-level padding override. |
| `opacity` | float | 1.0 | Style-level opacity. |
| `scale` | float | 1.0 | Scale factor. |
| `textShadow` | bool | false | Enable text drop shadow. |
| `textShadowColor` | color | [0,0,0,0.5] | Shadow color. |
| `textShadowOffset` | [x, y] | [1, 1] | Shadow offset in pixels. |

### Color format

Colors accept two formats:

- **Array:** `[r, g, b, a]` where each component is 0.0–1.0. Alpha is optional (defaults to 1.0):
  - `[1, 0, 0]` = red
  - `[0.5, 0.5, 0.5, 0.8]` = semi-transparent gray
- **Named:** `"white"`, `"black"`, `"transparent"`

---

## Lua Integration

### Displaying and destroying UI (recommended)

The `display` and `destroy` globals provide managed lifecycle for `.hui` assets. This is the recommended API for loading and unloading UI.

**Display a .hui file:**

```lua
local MAIN_MENU = "assets/ui/MainMenu.hui"

local ui, err = display.ui(MAIN_MENU)
if not ui then
    print("Error: " .. tostring(err))
    return
end
```

`display.ui` returns two values:
1. **handle** (integer) on success, or `nil` on failure.
2. **error string** (`nil` on success, descriptive error on failure).

If the `.hui` file specifies a `controller` script, it is loaded automatically. Displaying the same path twice replaces the previous instance (no duplicates).

**Destroy a UI:**

```lua
destroy.ui(ui)          -- destroy by handle
destroy.ui("current")   -- destroy the most recently displayed UI
destroy.ui("all")       -- destroy every active UI
```

Returns `true` if something was destroyed, `false` otherwise. Destroying an already-destroyed handle is a safe no-op.

All managed UI is automatically cleaned up when play mode stops.

> For full details, see [`Documentation/Scripting/LUA_DISPLAY_DESTROY_API.md`](../Scripting/LUA_DISPLAY_DESTROY_API.md).

### Low-level loading (UI.loadHUI)

For advanced use cases, `UI.loadHUI` provides direct loading without managed lifecycle:

```lua
local screen, err = UI.loadHUI("ui/MainMenu.hui")
```

This adds the screen but does not track it for duplicate prevention or `destroy.ui("current")` support. Prefer `display.ui()` for typical usage.

### Accessing widgets by ID

Use `UI.getWidget(handle, widgetId)` to get a widget's element handle:

```lua
local title = UI.getWidget(ui, "title")
local btn = UI.getWidget(ui, "btn_start")
```

Returns an element handle (integer) or `nil` if not found.

### Binding events

Once you have a widget handle, use the standard `UI.*` functions:

```lua
UI.setOnClick(btn, function()
    print("Button clicked!")
end)

UI.setOnHover(btn, function(isHovered)
    if isHovered then
        print("Hovering over button")
    end
end)
```

### Modifying widgets at runtime

All standard `UI.*` mutation functions work with handles from `.hui` widgets:

```lua
UI.setText(title, "Welcome Back!")
UI.setVisible(panel, false)
UI.setBackgroundColor(panel, 0.2, 0.3, 0.4, 1.0)
UI.setFontSize(title, 36)
```

### Full controller example

```lua
-- MainMenu.lua — controller for MainMenu.hui

local MAIN_MENU = "assets/ui/MainMenu.hui"
local menuHandle = nil

function showMainMenu()
    local handle, err = display.ui(MAIN_MENU)
    if not handle then
        print("Failed: " .. tostring(err))
        return
    end
    menuHandle = handle
    UI.setCursorMode(CursorMode.UI)

    local btnStart = UI.getWidget(handle, "btn_start")
    if btnStart then
        UI.setOnClick(btnStart, function()
            destroy.ui(menuHandle)
            menuHandle = nil
            UI.setCursorMode(CursorMode.Game)
        end)
    end

    local btnQuit = UI.getWidget(handle, "btn_quit")
    if btnQuit then
        UI.setOnClick(btnQuit, function()
            Application.quit()
        end)
    end
end
```

---

## Asset Workflow

### Creating .hui files in the editor

1. Open the **Asset Browser**.
2. Navigate to the desired directory (or right-click a folder).
3. Right-click to open the context menu.
4. Select **New UI File**.
5. Enter a name (the `.hui` extension is added automatically).
6. A starter template is created with a root panel, title label, and two buttons.

### Project directory structure

Recommended layout:

```
assets/
├── ui/
│   ├── MainMenu.hui
│   ├── MainMenu.lua
│   ├── PauseMenu.hui
│   ├── PauseMenu.lua
│   ├── HUD.hui
│   └── HUD.lua
├── textures/
├── meshes/
└── ...
```

### Loading at runtime

From any Lua script (e.g. in `onAwake`):

```lua
local MAIN_MENU = "assets/ui/MainMenu.hui"

function onAwake()
    local ui, err = display.ui(MAIN_MENU)
    if ui then
        -- bind events via UI.getWidget / UI.setOnClick
    end
end
```

---

## Full Working Example

### MainMenu.hui

```json
{
    "version": 1,
    "controller": "MainMenu.lua",
    "root": {
        "type": "Panel",
        "id": "root",
        "properties": {
            "anchor": "Stretch"
        },
        "style": {
            "backgroundColor": [0.05, 0.05, 0.1, 0.95]
        },
        "children": [
            {
                "type": "VerticalBox",
                "id": "main_layout",
                "properties": {
                    "spacing": 24,
                    "padding": [60, 80, 60, 60],
                    "childHAlignment": "Center",
                    "anchor": "Stretch"
                },
                "children": [
                    {
                        "type": "Text",
                        "id": "title",
                        "properties": {
                            "text": "My Game",
                            "textAlignment": "Center"
                        },
                        "style": {
                            "fontSize": 48,
                            "fontBold": true,
                            "textColor": [0.9, 0.75, 0.3, 1.0],
                            "textShadow": true,
                            "textShadowColor": [0, 0, 0, 0.7],
                            "textShadowOffset": [3, 3]
                        }
                    },
                    {
                        "type": "Button",
                        "id": "btn_start",
                        "properties": {
                            "text": "Start Game",
                            "size": [280, 55]
                        },
                        "normalStyle": {
                            "backgroundColor": [0.2, 0.55, 0.85, 1.0],
                            "cornerRadius": 10,
                            "textColor": [1, 1, 1, 1],
                            "fontSize": 20,
                            "fontBold": true
                        },
                        "hoveredStyle": {
                            "backgroundColor": [0.3, 0.65, 0.95, 1.0],
                            "cornerRadius": 10,
                            "textColor": [1, 1, 1, 1],
                            "fontSize": 20,
                            "fontBold": true
                        }
                    },
                    {
                        "type": "Button",
                        "id": "btn_quit",
                        "properties": {
                            "text": "Quit",
                            "size": [280, 55]
                        },
                        "normalStyle": {
                            "backgroundColor": [0.6, 0.15, 0.15, 1.0],
                            "cornerRadius": 10,
                            "textColor": [1, 1, 1, 1],
                            "fontSize": 20
                        },
                        "hoveredStyle": {
                            "backgroundColor": [0.75, 0.2, 0.2, 1.0],
                            "cornerRadius": 10,
                            "textColor": [1, 1, 1, 1],
                            "fontSize": 20
                        }
                    }
                ]
            }
        ]
    }
}
```

### MainMenu.lua (controller)

```lua
local MAIN_MENU = "assets/ui/MainMenu.hui"
local menuHandle = nil

function showMainMenu()
    local handle, err = display.ui(MAIN_MENU)
    if not handle then
        print("Error: " .. tostring(err))
        return
    end
    menuHandle = handle
    UI.setCursorMode(CursorMode.UI)

    local btnStart = UI.getWidget(handle, "btn_start")
    if btnStart then
        UI.setOnClick(btnStart, function()
            destroy.ui(menuHandle)
            menuHandle = nil
            UI.setCursorMode(CursorMode.Game)
        end)
    end

    local btnQuit = UI.getWidget(handle, "btn_quit")
    if btnQuit then
        UI.setOnClick(btnQuit, function()
            Application.quit()
        end)
    end
end
```

### Loading script (entity onAwake)

```lua
function onAwake()
    showMainMenu()
end
```

---

## Best Practices

### Widget ID naming

- Use `snake_case` for widget IDs: `btn_start`, `title_label`, `health_bar`.
- Prefix buttons with `btn_`, text with `txt_` or `lbl_`, panels with `pnl_` for clarity.
- Keep IDs short but descriptive.

### Separation of concerns

- **Do** put all layout, sizing, spacing, and visual properties in `.hui`.
- **Do** put all event handlers, state management, and game logic in `.lua`.
- **Don't** use `.hui` for runtime-dynamic content that changes every frame. Use Lua to update text/visibility.

### Structure recommendations

- Use `VerticalBox` and `HorizontalBox` for linear layouts instead of manual positioning.
- Use `Panel` as a grouping container when you need background styling.
- Set `anchor: "Stretch"` on root panels for responsive layouts.
- Keep widget hierarchies shallow for performance.

### Script variable pattern

Define UI paths as local constants at the top of scripts. Store handles in local variables:

```lua
local MAIN_MENU = "assets/ui/MainMenu.hui"
local PAUSE_MENU = "assets/ui/PauseMenu.hui"

local menuHandle = nil
```

This avoids hard-coded strings scattered through logic and makes refactoring trivial.

### File organization

- Keep `.hui` and its `.lua` controller in the same directory with matching names.
- Group related UI files in a `ui/` subdirectory.

---

## Error Handling

### Common errors and fixes

| Error | Cause | Fix |
|---|---|---|
| `JSON parse error: ...` | Malformed JSON syntax | Check for missing commas, brackets, or quotes. |
| `Missing or invalid 'root' widget definition` | No `root` field in the document | Add a `root` widget object to the top level. |
| `Widget missing 'type' field` | A widget node lacks a `type` | Add `"type": "Panel"` (or appropriate type) to every widget. |
| `Unknown widget type: 'X'` | Unrecognized type name | Use one of: Panel, Canvas, VerticalBox, HorizontalBox, Grid, Text, Button, Image. |
| `Duplicate widget id: 'X'` | Two widgets share the same `id` | Rename one of the widgets to have a unique ID. |
| `Failed to open file: X` | File path is wrong or file doesn't exist | Check the path relative to the project root. |
| `Controller script not found: X` | The `controller` path doesn't resolve | Ensure the Lua file exists relative to the `.hui` file's directory. |

### Debugging tips

- Check the console output for `[HUI]` prefixed messages.
- Use `UI.loadHUI` with error checking: `local screen, err = UI.loadHUI(path)`.
- Start with the generated template and modify incrementally.

---

## Future Expansion Notes

### Visual editor

The `.hui` format is designed with visual editor round-tripping in mind:

- **Deterministic serialization**: The serializer outputs properties in a consistent order, making diffs predictable.
- **Structured hierarchy**: The tree-based JSON structure maps directly to a visual widget tree.
- **ID-based references**: Widgets are addressable by stable IDs, allowing the editor to track modifications.
- **No executable logic**: The format is purely declarative — no Lua code or side effects in `.hui` files.

A future visual editor would:
1. Parse `.hui` to build an editable widget tree.
2. Allow drag-and-drop placement, property editing, and hierarchy reorganization.
3. Serialize back to `.hui` with clean round-tripping.

### Themes

The `class` field on widgets is reserved for future theme/style class support, where named style classes could be defined in a theme file and applied across multiple `.hui` files.

### Animations

Future versions may add an `animations` block to widget nodes for declarative transition and animation definitions.

### Reusable components

A future `"ref"` or `"template"` mechanism could allow .hui files to reference shared widget subtrees defined in other `.hui` files, enabling component reuse across screens.

### Data binding

Future integration with a data-binding system could allow `.hui` properties to reference runtime variables, reducing the need for Lua mutation calls.
