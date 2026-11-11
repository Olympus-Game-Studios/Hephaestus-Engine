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
-- A page with `custom` draws itself instead of a list: custom.show(menu) and custom.hide(menu) when it appears and
-- goes, and custom.update(menu) each frame, returning "back" to leave it. Item actions receive the menu.
-- Every page opened on top of another shows a BACK button in the bottom left corner, after its last item; a custom
-- page selects it with Menu.selectBack. Menus fit the height the player's UI size leaves (Menu.screenHeight): a list
-- longer than the room above BACK scrolls, following the selection and the mouse wheel, with a thin bar showing where
-- it is. Moving the selection to another item plays Menu.tickSound on the "ui" bus (options.tickSound and
-- options.tickVolume override it per menu; "" turns it off).
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
    labelSize = 26,
    titleSize = 34,
    labelPaddingLeft = 18,  -- inside the highlight, before the label's first drawn pixel
    labelPaddingRight = 18, -- after its last
}

local MAX_ROWS = 12
local HOVER_PAD = 12
local EASE_SECONDS = 0.035 -- time constant: the highlight is ~95% of the way there after about 0.1 s

-- True makes selection changes instant (Settings sets it from Reduce Motion).
Menu.reducedMotion = false

Menu.tickSound = "assets/audio/ui/tick.wav"
Menu.tickVolume = 0.5
local TICK_GAP = 0.04 -- seconds between ticks, so a fast scroll doesn't stack them

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

local function hasBack(menu)
    return #menu.pages > 1
end

local function itemsOf(page)
    return page.custom and {} or page.items
end

-- The screen's height in game UI units: 1080 at 100% UI size, less when the UI is made bigger.
function Menu.screenHeight()
    if not UI.getLayoutSize then
        return 1080
    end
    local _, height = UI.getLayoutSize()
    return (height and height > 0) and height or 1080
end

local function clamp(v, lo, hi)
    return math.max(lo, math.min(hi, v))
end

-- Plays the selection tick; custom pages call it when their own selection moves.
function Menu.tick(menu)
    local sound = menu.tickSound or Menu.tickSound
    if sound == "" or not (Audio and Audio.isAvailable and Audio.isAvailable()) then
        return
    end
    if menu.lastTick and menu.clock - menu.lastTick < TICK_GAP then
        return
    end
    menu.lastTick = menu.clock
    Audio.play(sound, { volume = menu.tickVolume or Menu.tickVolume, bus = "ui", priority = 1 })
end

local function selectItem(menu, index)
    if menu.selected ~= index then
        menu.selected = index
        Menu.tick(menu)
    end
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
        item.action(menu)
    end
end

-- options: x (where the labels' and title's text starts; highlights reach out to the left of it by their padding),
-- y (top of the list), width, rowHeight, labelSize (padding and the value column scale with it), backdrop (a dark layer over the whole
-- screen), hints, tiles (each button item sits on its own tile sized to its label).
function Menu.new(name, options)
    options = options or {}
    local menu = {
        screen = UI.createScreen(name),
        x = options.x or 96,
        y = options.y or 300,
        width = options.width or 560,
        rowHeight = options.rowHeight or 56,
        pages = {},
        selected = 1,
        rows = {},
        visible = true,
        tiles = options.tiles or false,
        labelSize = options.labelSize or Menu.theme.labelSize,
        tickSound = options.tickSound,
        tickVolume = options.tickVolume,
        clock = 0,
    }
    local scale = menu.labelSize / Menu.theme.labelSize
    menu.padLeft = math.floor(Menu.theme.labelPaddingLeft * scale + 0.5)
    menu.padRight = math.floor(Menu.theme.labelPaddingRight * scale + 0.5)
    menu.barX = menu.x - menu.padLeft
    local valueSize = math.floor(20 * scale + 0.5)
    local arrowSize = math.floor(22 * scale + 0.5)
    local root = UI.getRoot(menu.screen)
    menu.root = root
    if options.backdrop then
        menu.backdrop = UI.createPanel(root)
        UI.setAnchor(menu.backdrop, "Stretch")
        UI.setBackgroundColor(menu.backdrop, 0.0, 0.0, 0.0, options.backdrop)
    end
    menu.title = text(menu, root, "", menu.x, menu.y - 74, menu.width, 48, Menu.theme.titleSize, true)
    menu.titleRule = panel(root, menu.x, menu.y - 20, 64, 3, Menu.theme.accent)
    for i = 1, MAX_ROWS do
        menu.rows[i] = { tile = panel(root, menu.barX, menu.y + (i - 1) * menu.rowHeight, menu.width,
                                      menu.rowHeight - 8, Menu.theme.tile), amount = 0, target = 0 }
    end
    menu.highlight = panel(root, menu.barX, menu.y, 0, menu.rowHeight - 8, Menu.theme.accent)
    menu.highlightY, menu.highlightWidth = menu.y, 0
    for i = 1, MAX_ROWS do
        local y = menu.y + (i - 1) * menu.rowHeight
        local row = menu.rows[i]
        row.label = text(menu, root, "", menu.x, y, menu.width, menu.rowHeight - 8, menu.labelSize, true)
        row.value = text(menu, root, "", menu.x + menu.width - 250, y, 196, menu.rowHeight - 8, valueSize, false, nil, "Right")
        row.trackBack = panel(root, menu.x + menu.width - 250, y + menu.rowHeight - 18, 196, 3, Menu.theme.track)
        row.trackFill = panel(root, menu.x + menu.width - 250, y + menu.rowHeight - 18, 0, 3, Menu.theme.accent)
        row.button = button(root, "", menu.barX, y, menu.width, menu.rowHeight - 8, 22)
        row.less = button(root, "<", menu.x + menu.width - 290, y, 36, menu.rowHeight - 8, arrowSize, Menu.theme.ink)
        row.more = button(root, ">", menu.x + menu.width - 50, y, 36, menu.rowHeight - 8, arrowSize, Menu.theme.ink)
        UI.setOnHover(row.button, function(hovered)
            if hovered and menu.visible then
                selectItem(menu, menu.first + i - 1)
                Menu.refresh(menu)
            end
        end)
        UI.setOnClick(row.button, function()
            if menu.visible then
                selectItem(menu, menu.first + i - 1)
                activate(menu, menu.selected)
            end
        end)
        UI.setOnClick(row.less, function()
            if menu.visible then
                selectItem(menu, menu.first + i - 1)
                adjust(menu, menu.selected, -1)
            end
        end)
        UI.setOnClick(row.more, function()
            if menu.visible then
                selectItem(menu, menu.first + i - 1)
                adjust(menu, menu.selected, 1)
            end
        end)
    end
    menu.first = 1
    menu.scrollTrack = panel(root, menu.barX + menu.padLeft + menu.width + 14, menu.y, 3, 0, Menu.theme.track)
    menu.scrollThumb = panel(root, menu.barX + menu.padLeft + menu.width + 14, menu.y, 3, 0, Menu.theme.accent)
    menu.backY = Menu.screenHeight() - 46 - 26 - (menu.rowHeight - 8)
    menu.backRow = { amount = 0, target = 0 }
    menu.backRow.label = text(menu, root, "BACK", menu.x, menu.backY, menu.width, menu.rowHeight - 8, menu.labelSize, true)
    menu.backRow.button = button(root, "BACK", menu.barX, menu.backY, menu.width, menu.rowHeight - 8, 22, Menu.theme.clear)
    UI.setOnHover(menu.backRow.button, function(hovered)
        if hovered and menu.visible and hasBack(menu) then
            local page = currentPage(menu)
            if page.custom then
                if not menu.backSelected then
                    Menu.tick(menu)
                end
                Menu.selectBack(menu, true)
            else
                selectItem(menu, #page.items + 1)
                Menu.refresh(menu)
            end
        end
    end)
    UI.setOnClick(menu.backRow.button, function()
        if menu.visible and hasBack(menu) then
            Menu.back(menu)
        end
    end)
    menu.hintBar = UI.createPanel(root)
    UI.setAlignment(menu.hintBar, "Stretch", "Bottom")
    UI.setSize(menu.hintBar, 0, 46)
    UI.setBackgroundColor(menu.hintBar, Menu.theme.bar.r, Menu.theme.bar.g, Menu.theme.bar.b, Menu.theme.bar.a)
    menu.hintRule = UI.createPanel(root)
    UI.setAlignment(menu.hintRule, "Stretch", "Bottom")
    UI.setPosition(menu.hintRule, 0, -46)
    UI.setSize(menu.hintRule, 0, 1)
    UI.setBackgroundColor(menu.hintRule, 1, 1, 1, 0.12)
    menu.hintText = options.hints or ""
    menu.hints = UI.createText(menu.hintBar, menu.hintText)
    UI.setAlignment(menu.hints, "Stretch", "Stretch")
    UI.setPadding(menu.hints, 0, 96, 0, 96)
    UI.setStyle(menu.hints, style(Menu.theme.muted, Menu.theme.clear, 16, true, "Left"))
    UI.setTextAlignment(menu.hints, "left")
    return menu
end

function Menu.setHints(menu, hints)
    menu.hintText = hints or ""
    UI.setText(menu.hints, menu.hintText)
end

-- Replaces every page with this one.
function Menu.setPage(menu, page)
    menu.pages = { page }
    menu.selected = 1
    menu.first = 1
    menu.backSelected = false
    menu.snap = true
    Menu.refresh(menu)
end

function Menu.push(menu, page)
    menu.pages[#menu.pages + 1] = page
    menu.selected = 1
    menu.first = 1
    menu.backSelected = false
    menu.snap = true
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
        menu.first = 1
        menu.backSelected = false
        menu.snap = true
        Menu.refresh(menu)
        return true
    end
    return false
end

function Menu.setVisible(menu, visible)
    menu.visible = visible
    UI.setScreenVisible(menu.screen, visible)
    if visible then
        menu.snap = true
        Menu.refresh(menu)
    end
end

function Menu.refresh(menu)
    local page = currentPage(menu)
    if not page then
        return
    end
    if Menu.screenHeight() ~= menu.screenHeight then
        menu.screenHeight = Menu.screenHeight()
        if menu.onResize then
            menu.onResize(menu)
        end
    end
    UI.setText(menu.title, page.title or "")
    local _, titleInk = UI.measureText(page.title or "", Menu.theme.titleSize, true, Menu.theme.font)
    UI.setPosition(menu.title, menu.x - (titleInk or 0), menu.y - 74)
    UI.setPosition(menu.titleRule, menu.x, menu.y - 20)
    UI.setVisible(menu.title, (page.title or "") ~= "")
    UI.setVisible(menu.titleRule, (page.title or "") ~= "")
    if menu.custom and menu.custom ~= page.custom then
        menu.custom.hide(menu)
        menu.custom = nil
    end
    local theme = Menu.theme
    local items = itemsOf(page)
    menu.backY = menu.screenHeight - 46 - 26 - (menu.rowHeight - 8)
    local room = menu.backY - 12 - menu.y
    -- A list a little too long closes up its rows (to 78% spacing at most) before it scrolls.
    menu.step = menu.rowHeight
    if #items * menu.rowHeight > room and #items * menu.rowHeight * 0.78 <= room then
        menu.step = math.floor(room / #items)
    end
    menu.lines = clamp(math.floor(room / menu.step), 1, MAX_ROWS)
    if menu.selected <= #items then
        if menu.selected < menu.first then
            menu.first = menu.selected
        elseif menu.selected >= menu.first + menu.lines then
            menu.first = menu.selected - menu.lines + 1
        end
    end
    menu.first = clamp(menu.first, 1, math.max(1, #items - menu.lines + 1))
    local height = math.min(menu.rowHeight - 8, menu.step - 4)
    local scrolling = #items > menu.lines
    UI.setVisible(menu.scrollTrack, scrolling)
    UI.setVisible(menu.scrollThumb, scrolling)
    if scrolling then
        local track = menu.lines * menu.step - 8
        UI.setSize(menu.scrollTrack, 3, track)
        UI.setPosition(menu.scrollThumb, menu.barX + menu.padLeft + menu.width + 14,
                       menu.y + track * (menu.first - 1) / #items)
        UI.setSize(menu.scrollThumb, 3, track * menu.lines / #items)
    end
    for i, row in ipairs(menu.rows) do
        local index = menu.first + i - 1
        local item = i <= menu.lines and items[index] or nil
        local selected = item ~= nil and index == menu.selected
        local hasValue = item ~= nil and (item.kind == "slider" or item.kind == "choice")
        UI.setVisible(row.button, item ~= nil)
        UI.setVisible(row.label, item ~= nil)
        local tiled = menu.tiles and item ~= nil and not hasValue
        UI.setVisible(row.tile, tiled and not selected)
        row.target = selected and 1 or 0
        UI.setVisible(row.value, hasValue)
        UI.setVisible(row.trackBack, item ~= nil and item.kind == "slider")
        UI.setVisible(row.trackFill, item ~= nil and item.kind == "slider")
        UI.setVisible(row.less, hasValue and selected)
        UI.setVisible(row.more, hasValue and selected)
        if item then
            UI.setText(row.label, item.label)
            local rowY = menu.y + (i - 1) * menu.step
            local _, inkLeft, inkRight = UI.measureText(item.label, menu.labelSize, true, theme.font)
            row.inkLeft = inkLeft or 0
            local inkWidth = (inkRight or 0) - row.inkLeft
            if inkWidth <= 0 then
                inkWidth = #item.label * menu.labelSize * 0.62
            end
            local barWidth = menu.padLeft + menu.width
            if not hasValue then
                barWidth = math.min(barWidth, menu.padLeft + inkWidth + menu.padRight)
            end
            UI.setPosition(row.tile, menu.barX, rowY)
            UI.setSize(row.tile, barWidth, height)
            row.barWidth = barWidth
            -- The drawn text (not its origin, hence the side bearing) starts on the column; the bar wraps it evenly.
            UI.setPosition(row.label, menu.x - row.inkLeft, rowY)
            UI.setSize(row.label, hasValue and menu.width or barWidth, height)
            UI.setTextAlignment(row.label, "left")
            -- Hover and click only over the item itself (its bar, or the whole row for sliders and choices).
            UI.setPosition(row.button, menu.barX, rowY)
            UI.setSize(row.button, barWidth + HOVER_PAD, height)
            UI.setPosition(row.value, menu.x + menu.width - 250, rowY)
            UI.setSize(row.value, 196, height)
            UI.setPosition(row.trackBack, menu.x + menu.width - 250, rowY + height - 10)
            UI.setPosition(row.trackFill, menu.x + menu.width - 250, rowY + height - 10)
            UI.setPosition(row.less, menu.x + menu.width - 290, rowY)
            UI.setSize(row.less, 36, height)
            UI.setPosition(row.more, menu.x + menu.width - 50, rowY)
            UI.setSize(row.more, 36, height)
            if item.kind == "slider" then
                local value = item.get()
                UI.setText(row.value, item.format and item.format(value) or string.format("%.2f", value))
                local t = (value - item.min) / math.max(item.max - item.min, 1e-6)
                UI.setSize(row.trackFill, 196 * clamp(t, 0, 1), 3)
            elseif item.kind == "choice" then
                UI.setText(row.value, item.options[choiceIndex(item)][2])
            end
        end
    end
    if page.custom and menu.custom ~= page.custom then
        menu.custom = page.custom
        page.custom.show(menu)
    end
    local back = menu.backRow
    local showBack = hasBack(menu)
    local onBack = showBack and (page.custom and menu.backSelected or (not page.custom and menu.selected == #items + 1))
    UI.setVisible(back.label, showBack)
    UI.setVisible(back.button, showBack)
    back.target = onBack and 1 or 0
    if showBack then
        local _, inkLeft, inkRight = UI.measureText("BACK", menu.labelSize, true, theme.font)
        local inkWidth = (inkRight or 0) - (inkLeft or 0)
        back.barWidth = menu.padLeft + (inkWidth > 0 and inkWidth or 4 * menu.labelSize * 0.62) + menu.padRight
        UI.setPosition(back.label, menu.x - (inkLeft or 0), menu.backY)
        UI.setSize(back.label, back.barWidth, menu.rowHeight - 8)
        UI.setPosition(back.button, menu.barX, menu.backY)
        UI.setSize(back.button, back.barWidth + HOVER_PAD, menu.rowHeight - 8)
    end
    local item = items[menu.selected]
    UI.setVisible(menu.highlight, item ~= nil or onBack)
    if onBack then
        menu.targetY = menu.backY
        menu.targetWidth = back.barWidth
        menu.targetHeight = menu.rowHeight - 8
    elseif item then
        menu.targetY = menu.y + (menu.selected - menu.first) * menu.step
        menu.targetHeight = height
        menu.targetWidth = menu.rows[menu.selected - menu.first + 1].barWidth or menu.width
    end
    if menu.snap or Menu.reducedMotion then
        menu.snap = false
        Menu.animate(menu, nil)
    end
end

local function mix(a, b, t)
    return a + (b - a) * t
end

local function mixColor(a, b, t)
    return mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t), mix(a.a, b.a, t)
end

-- Eases the highlight and label colours towards the selection; dt nil jumps there.
function Menu.animate(menu, dt)
    local k = (dt == nil or Menu.reducedMotion) and 1 or (1 - math.exp(-dt / EASE_SECONDS))
    if menu.targetY then
        menu.highlightY = mix(menu.highlightY, menu.targetY, k)
        menu.highlightWidth = mix(menu.highlightWidth, menu.targetWidth, k)
        UI.setPosition(menu.highlight, menu.barX, menu.highlightY)
        UI.setSize(menu.highlight, menu.highlightWidth, menu.targetHeight or (menu.rowHeight - 8))
    end
    local theme = Menu.theme
    for i, row in ipairs(menu.rows) do
        row.amount = mix(row.amount, row.target, k)
        local a = row.amount
        UI.setTextColor(row.label, mixColor(theme.text, theme.ink, a))
        UI.setTextColor(row.value, mixColor(theme.text, theme.ink, a))
        UI.setBackgroundColor(row.trackFill, mixColor(theme.accent, theme.ink, a))
    end
    local back = menu.backRow
    back.amount = mix(back.amount, back.target, k)
    UI.setTextColor(back.label, mixColor(theme.text, theme.ink, back.amount))
end

-- For custom pages: whether their BACK button is the selection.
function Menu.selectBack(menu, selected)
    if menu.backSelected ~= selected then
        menu.backSelected = selected
        Menu.refresh(menu)
    end
end

-- Keyboard and gamepad handling; call every frame while the menu is visible. Returns "back" when MenuBack was
-- pressed on the first page (the owner decides what that means).
function Menu.update(menu)
    if not menu.visible then
        return nil
    end
    menu.clock = menu.clock + Time.unscaledDeltaTime()
    Menu.animate(menu, Time.unscaledDeltaTime())
    local page = currentPage(menu)
    if not page then
        return nil
    end
    if Menu.screenHeight() ~= menu.screenHeight then
        Menu.refresh(menu)
    end
    if page.custom then
        if page.custom.update(menu) == "back" and not Menu.back(menu) then
            return "back"
        end
        return nil
    end
    local items = #page.items
    local count = items + (hasBack(menu) and 1 or 0)
    local wheel = Input.getScrollDelta().y or 0
    if wheel ~= 0 and items > (menu.lines or items) then
        menu.first = clamp(menu.first - (wheel > 0 and 1 or -1), 1, items - menu.lines + 1)
        if menu.selected <= items then
            selectItem(menu, clamp(menu.selected, menu.first, menu.first + menu.lines - 1))
        end
        Menu.refresh(menu)
    end
    local up = Input.isActionPressed("MenuUp")
    local down = Input.isActionPressed("MenuDown")
    local left = Input.isActionPressed("MenuLeft")
    local right = Input.isActionPressed("MenuRight")
    local choose = Input.isActionPressed("MenuSelect")
    local escape = Input.isActionPressed("MenuBack")
    if count > 0 and (up or down) then
        selectItem(menu, (menu.selected - 1 + (down and 1 or -1)) % count + 1)
        Menu.refresh(menu)
    end
    if left then
        adjust(menu, menu.selected, -1)
    elseif right then
        adjust(menu, menu.selected, 1)
    end
    if choose then
        if menu.selected > items then
            Menu.back(menu)
            return nil
        end
        activate(menu, menu.selected)
    end
    if escape and not Menu.back(menu) then
        return "back"
    end
    return nil
end

return Menu
