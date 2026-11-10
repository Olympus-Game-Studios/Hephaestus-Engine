-- Menus built from code: a vertical list of items on the left, the selected one on an accent bar, sliders and
-- choices with their value on the right, and a hint bar along the bottom. The MenuUp, MenuDown, MenuLeft,
-- MenuRight, MenuSelect and MenuBack input actions (arrows or WASD, Enter or Space, Escape, and the gamepad) and
-- the mouse (hover to select, click to choose or step a value) both work. A menu shows one page at a time; Menu.push opens a page on top and
-- Menu.back returns to the previous one.
--
-- A page is { title = "SETTINGS", items = { item, ... }, onBack = function }. Items:
--   { label = "PLAY", action = function }
--   { label = "FIELD OF VIEW", kind = "slider", min, max, step, get = function, set = function(v), format = function(v) }
--   { label = "DIFFICULTY", kind = "choice", options = { { value, text }, ... }, get, set }
local Menu = {}

Menu.theme = {
    accent = { r = 1.0, g = 0.42, b = 0.06, a = 1.0 },
    text = { r = 0.94, g = 0.92, b = 0.89, a = 1.0 },
    muted = { r = 0.6, g = 0.6, b = 0.6, a = 1.0 },
    ink = { r = 0.06, g = 0.05, b = 0.04, a = 1.0 },
    clear = { r = 0, g = 0, b = 0, a = 0 },
    bar = { r = 0.0, g = 0.0, b = 0.0, a = 0.6 },
    tile = { r = 0.04, g = 0.035, b = 0.03, a = 0.72 },
    track = { r = 1.0, g = 1.0, b = 1.0, a = 0.18 },
    font = "Roboto",
}

local MAX_ROWS = 12

local function style(textColor, background, size, bold, align)
    return { textColor = textColor, backgroundColor = background, fontSize = size, fontBold = bold,
             fontFamily = Menu.theme.font, textAlignment = (align or "left"):lower(), cornerRadius = 0 }
end

local function place(widget, x, y, w, h)
    UI.setAnchor(widget, "TopLeft")
    UI.setPosition(widget, x, y)
    UI.setSize(widget, w, h)
end

local function text(menu, parent, content, x, y, w, h, size, bold, color, align)
    local widget = UI.createText(parent, content)
    place(widget, x, y, w, h)
    UI.setStyle(widget, style(color or Menu.theme.text, Menu.theme.clear, size, bold, align))
    UI.setTextAlignment(widget, (align or "Left"):lower())
    return widget
end

local function panel(parent, x, y, w, h, color)
    local widget = UI.createPanel(parent)
    place(widget, x, y, w, h)
    UI.setBackgroundColor(widget, color.r, color.g, color.b, color.a)
    return widget
end

-- An invisible click and hover area, or a centred symbol in `color` (buttons always centre their label).
local function button(parent, label, x, y, w, h, size, color)
    local widget = UI.createButton(parent, label)
    place(widget, x, y, w, h)
    for _, state in ipairs({ "normal", "hovered", "pressed", "disabled" }) do
        UI.setButtonStyle(widget, state, style(color or Menu.theme.text, Menu.theme.clear, size, true, "Center"))
    end
    return widget
end

local function currentPage(menu)
    return menu.pages[#menu.pages]
end

local function clamp(v, lo, hi)
    return math.max(lo, math.min(hi, v))
end

local function choiceIndex(item)
    local value = item.get()
    for i, option in ipairs(item.options) do
        if option[1] == value then
            return i
        end
    end
    return 1
end

-- Changes a slider or choice by `direction` steps.
local function adjust(menu, index, direction)
    local item = currentPage(menu).items[index]
    if not item then
        return
    end
    if item.kind == "slider" then
        local step = item.step or 0.1
        local value = clamp(item.get() + direction * step, item.min, item.max)
        item.set(math.floor(value / step + 0.5) * step)
    elseif item.kind == "choice" then
        local i = (choiceIndex(item) - 1 + direction) % #item.options + 1
        item.set(item.options[i][1])
    end
    Menu.refresh(menu)
end

local function activate(menu, index)
    local item = currentPage(menu).items[index]
    if not item then
        return
    end
    if item.kind == "slider" or item.kind == "choice" then
        adjust(menu, index, 1)
    elseif item.action then
        item.action()
    end
end

-- options: x, y (top of the list), width, rowHeight, backdrop (a dark layer over the whole screen), hints, tiles
-- (each button item sits on its own tile sized to its label instead of a full-width bar).
function Menu.new(name, options)
    options = options or {}
    local menu = {
        screen = UI.createScreen(name),
        x = options.x or 96,
        y = options.y or 300,
        width = options.width or 560,
        rowHeight = options.rowHeight or 50,
        pages = {},
        selected = 1,
        rows = {},
        visible = true,
        tiles = options.tiles or false,
    }
    local root = UI.getRoot(menu.screen)
    menu.root = root
    if options.backdrop then
        menu.backdrop = UI.createPanel(root)
        UI.setAnchor(menu.backdrop, "Stretch")
        UI.setBackgroundColor(menu.backdrop, 0.0, 0.0, 0.0, options.backdrop)
    end
    menu.title = text(menu, root, "", menu.x, menu.y - 74, menu.width, 48, 30, true)
    menu.titleRule = panel(root, menu.x, menu.y - 20, 64, 3, Menu.theme.accent)
    for i = 1, MAX_ROWS do
        local y = menu.y + (i - 1) * menu.rowHeight
        local row = {}
        row.tile = panel(root, menu.x, y, menu.width, menu.rowHeight - 8, Menu.theme.tile)
        row.bar = panel(root, menu.x, y, menu.width, menu.rowHeight - 8, Menu.theme.accent)
        row.label = text(menu, root, "", menu.x + 18, y, menu.width - 36, menu.rowHeight - 8, 22, true)
        row.value = text(menu, root, "", menu.x + menu.width - 250, y, 196, menu.rowHeight - 8, 20, false, nil, "Right")
        row.trackBack = panel(root, menu.x + menu.width - 250, y + menu.rowHeight - 18, 196, 3, Menu.theme.track)
        row.trackFill = panel(root, menu.x + menu.width - 250, y + menu.rowHeight - 18, 0, 3, Menu.theme.accent)
        row.button = button(root, "", menu.x, y, menu.width, menu.rowHeight - 8, 22)
        row.less = button(root, "<", menu.x + menu.width - 290, y, 36, menu.rowHeight - 8, 22, Menu.theme.ink)
        row.more = button(root, ">", menu.x + menu.width - 50, y, 36, menu.rowHeight - 8, 22, Menu.theme.ink)
        UI.setOnHover(row.button, function(hovered)
            if hovered and menu.visible then
                menu.selected = i
                Menu.refresh(menu)
            end
        end)
        UI.setOnClick(row.button, function()
            if menu.visible then
                menu.selected = i
                activate(menu, i)
            end
        end)
        UI.setOnClick(row.less, function()
            if menu.visible then
                menu.selected = i
                adjust(menu, i, -1)
            end
        end)
        UI.setOnClick(row.more, function()
            if menu.visible then
                menu.selected = i
                adjust(menu, i, 1)
            end
        end)
        menu.rows[i] = row
    end
    menu.hintBar = UI.createPanel(root)
    UI.setAlignment(menu.hintBar, "Stretch", "Bottom")
    UI.setSize(menu.hintBar, 0, 46)
    UI.setBackgroundColor(menu.hintBar, Menu.theme.bar.r, Menu.theme.bar.g, Menu.theme.bar.b, Menu.theme.bar.a)
    menu.hintRule = UI.createPanel(root)
    UI.setAlignment(menu.hintRule, "Stretch", "Bottom")
    UI.setPosition(menu.hintRule, 0, -46)
    UI.setSize(menu.hintRule, 0, 1)
    UI.setBackgroundColor(menu.hintRule, 1, 1, 1, 0.12)
    menu.hints = UI.createText(menu.hintBar, options.hints or "")
    UI.setAlignment(menu.hints, "Stretch", "Stretch")
    UI.setPadding(menu.hints, 0, 96, 0, 96)
    UI.setStyle(menu.hints, style(Menu.theme.muted, Menu.theme.clear, 16, true, "Left"))
    UI.setTextAlignment(menu.hints, "left")
    return menu
end

function Menu.setHints(menu, hints)
    UI.setText(menu.hints, hints or "")
end

-- Replaces every page with this one.
function Menu.setPage(menu, page)
    menu.pages = { page }
    menu.selected = 1
    Menu.refresh(menu)
end

function Menu.push(menu, page)
    menu.pages[#menu.pages + 1] = page
    menu.selected = 1
    Menu.refresh(menu)
end

-- Back one page (calling the page's onBack first); false when already on the first page.
function Menu.back(menu)
    local page = currentPage(menu)
    if page and page.onBack then
        page.onBack()
    end
    if #menu.pages > 1 then
        table.remove(menu.pages)
        menu.selected = 1
        Menu.refresh(menu)
        return true
    end
    return false
end

function Menu.setVisible(menu, visible)
    menu.visible = visible
    UI.setScreenVisible(menu.screen, visible)
    if visible then
        Menu.refresh(menu)
    end
end

function Menu.refresh(menu)
    local page = currentPage(menu)
    if not page then
        return
    end
    UI.setText(menu.title, page.title or "")
    UI.setVisible(menu.title, (page.title or "") ~= "")
    UI.setVisible(menu.titleRule, (page.title or "") ~= "")
    local theme = Menu.theme
    for i, row in ipairs(menu.rows) do
        local item = page.items[i]
        local selected = item ~= nil and i == menu.selected
        local hasValue = item ~= nil and (item.kind == "slider" or item.kind == "choice")
        UI.setVisible(row.button, item ~= nil)
        UI.setVisible(row.label, item ~= nil)
        local tiled = menu.tiles and item ~= nil and not hasValue
        UI.setVisible(row.tile, tiled and not selected)
        UI.setVisible(row.bar, selected)
        UI.setVisible(row.value, hasValue)
        UI.setVisible(row.trackBack, item ~= nil and item.kind == "slider")
        UI.setVisible(row.trackFill, item ~= nil and item.kind == "slider")
        UI.setVisible(row.less, hasValue and selected)
        UI.setVisible(row.more, hasValue and selected)
        if item then
            UI.setText(row.label, item.label)
            local barWidth = menu.width
            if tiled then
                barWidth = math.min(menu.width, 40 + #item.label * 17)
            end
            UI.setSize(row.tile, barWidth, menu.rowHeight - 8)
            UI.setSize(row.bar, barWidth, menu.rowHeight - 8)
            local color = selected and theme.ink or theme.text
            UI.setTextColor(row.label, color.r, color.g, color.b, color.a)
            UI.setTextColor(row.value, color.r, color.g, color.b, color.a)
            if item.kind == "slider" then
                local value = item.get()
                UI.setText(row.value, item.format and item.format(value) or string.format("%.2f", value))
                local t = (value - item.min) / math.max(item.max - item.min, 1e-6)
                UI.setSize(row.trackFill, 196 * clamp(t, 0, 1), 3)
                local fill = selected and theme.ink or theme.accent
                UI.setBackgroundColor(row.trackFill, fill.r, fill.g, fill.b, fill.a)
            elseif item.kind == "choice" then
                UI.setText(row.value, item.options[choiceIndex(item)][2])
            end
        end
    end
end

-- Keyboard and gamepad handling; call every frame while the menu is visible. Returns "back" when MenuBack was
-- pressed on the first page (the owner decides what that means).
function Menu.update(menu)
    if not menu.visible then
        return nil
    end
    local page = currentPage(menu)
    if not page then
        return nil
    end
    local count = #page.items
    local up = Input.isActionPressed("MenuUp")
    local down = Input.isActionPressed("MenuDown")
    local left = Input.isActionPressed("MenuLeft")
    local right = Input.isActionPressed("MenuRight")
    local choose = Input.isActionPressed("MenuSelect")
    local escape = Input.isActionPressed("MenuBack")
    if count > 0 and (up or down) then
        menu.selected = (menu.selected - 1 + (down and 1 or -1)) % count + 1
        Menu.refresh(menu)
    end
    if left then
        adjust(menu, menu.selected, -1)
    elseif right then
        adjust(menu, menu.selected, 1)
    end
    if choose then
        activate(menu, menu.selected)
    end
    if escape and not Menu.back(menu) then
        return "back"
    end
    return nil
end

return Menu
