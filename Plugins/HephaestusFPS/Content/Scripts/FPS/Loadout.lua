-- The player's loadout: a primary and a secondary weapon and a tactical grenade, chosen on a LOADOUT page and kept
-- in the settings file ([Loadout]). The game lists what can be chosen in a module named Loadout.listModule (default
-- "LoadoutWeapons", under its assets/scripts) that returns entries { id, definition, slot }: id is the weapon's entity
-- name under the player, definition its weapon data module (as in Weapon.lua) and slot "primary" or "secondary". The
-- page shows each weapon's displayName and description from its definition, with stat bars worked out from the same
-- values. Tactical entries are { id, slot = "tactical", name, description, grenade }: grenade "gas" makes the player's
-- tactical throw its gasGrenadePrefab (tear gas) instead of its smokeGrenadePrefab.
-- A level whose mode uses loadouts sets Registry.loadoutActive (FPS.TeamDeathmatch and FPS.WaveDirector do); the
-- player then carries only the chosen two, picking up a changed loadout when it next respawns or the level restarts.
-- Elsewhere (a firing range) the player keeps every weapon.
local Menu = require("FPS.Menu")
local Settings = require("FPS.Settings")

local Loadout = { listModule = "LoadoutWeapons" }

local SLOTS = { "primary", "secondary", "tactical" }
local FIELD = { primary = "loadoutPrimary", secondary = "loadoutSecondary", tactical = "loadoutTactical" }
local SLOT_LABEL = { primary = "PRIMARY", secondary = "SECONDARY", tactical = "TACTICAL" }

-- Weapon.lua's defaults, for definitions that leave a value out.
local DEFAULTS = {
    displayName = "Weapon", damage = 20, pellets = 1, range = 100, falloffStart = 25, falloffEnd = 60,
    roundsPerSecond = 5, adsMoveMultiplier = 0.65, sprintToFireSeconds = 0.22, raiseSeconds = 0.45,
    adsSeconds = 0.22, hipSpread = 2.0, recoilPitch = 1.0,
}

local function load(name)
    local ok, value = pcall(require, name)
    if ok and type(value) == "table" then
        return value
    end
    return nil
end

local function clamp01(v)
    return math.max(0, math.min(1, v))
end

-- The first shot's upward kick in degrees: the recoil pattern's first entry, else recoilPitch.
local function firstKick(d)
    local pitch = tostring(d.recoilPattern or ""):match("^%s*([%d%.%-]+)")
    return tonumber(pitch) or d.recoilPitch or DEFAULTS.recoilPitch
end

-- Bars from 0 to 1. Damage is a body hit with every pellet, range the distance where falloff is halfway (on a log
-- scale from 5 to 400 m), fire rate the square root of rounds a second against 15, mobility how little the weapon
-- slows aiming, sprinting to fire and raising, and handling how quickly it aims, how tight it is from the hip and how
-- little the first shot kicks.
function Loadout.stats(d)
    local function v(name)
        if d[name] ~= nil then
            return d[name]
        end
        return DEFAULTS[name]
    end
    local effective = math.min(v("range"), (v("falloffStart") + v("falloffEnd")) * 0.5)
    return {
        { label = "DAMAGE", value = clamp01(v("damage") * math.max(1, v("pellets")) / 150) },
        { label = "RANGE", value = clamp01(math.log(math.max(effective, 5) / 5) / math.log(400 / 5)) },
        { label = "FIRE RATE", value = clamp01(math.sqrt(v("roundsPerSecond") / 15)) },
        { label = "MOBILITY", value = clamp01((v("adsMoveMultiplier") + (1 - v("sprintToFireSeconds") / 0.6)
                                              + (1 - v("raiseSeconds") / 1.0)) / 3) },
        { label = "HANDLING", value = clamp01(((1 - v("adsSeconds") / 0.5) + (1 - v("hipSpread") / 10)
                                              + (1 - firstKick(d) / 8)) / 3) },
    }
end

-- The weapons the game offers, each with its definition table loaded, or {} when the game lists none.
function Loadout.list()
    local entries = load(Loadout.listModule) or {}
    local out = {}
    for _, entry in ipairs(entries) do
        local d = entry.definition and load(entry.definition) or {}
        out[#out + 1] = { id = entry.id, slot = entry.slot, definition = d, hasStats = entry.definition ~= nil,
                          grenade = entry.grenade,
                          name = string.upper(d.displayName or entry.name or entry.id),
                          description = d.description or entry.description or "" }
    end
    return out
end

local function inSlot(list, slot)
    local out = {}
    for _, weapon in ipairs(list) do
        if weapon.slot == slot then
            out[#out + 1] = weapon
        end
    end
    return out
end

-- The chosen weapon ids { primary, secondary }: the saved ones when the game still offers them, else the first of
-- each slot.
function Loadout.current(list)
    list = list or Loadout.list()
    local chosen = {}
    for _, slot in ipairs(SLOTS) do
        local options = inSlot(list, slot)
        local saved = Settings.values[FIELD[slot]]
        for _, weapon in ipairs(options) do
            if weapon.id == saved then
                chosen[slot] = saved
            end
        end
        chosen[slot] = chosen[slot] or (options[1] and options[1].id)
    end
    return chosen
end

-- The chosen entry for a slot, or nil when the game offers none for it.
function Loadout.entry(slot, list)
    list = list or Loadout.list()
    local id = Loadout.current(list)[slot]
    for _, item in ipairs(list) do
        if item.slot == slot and item.id == id then
            return item
        end
    end
    return nil
end

-- Stat panel beside the list -------------------------------------------------------------------------------------
local PANEL_WIDTH = 520
local BAR_WIDTH = 300

local function textWidget(parent, size, bold, color)
    local widget = UI.createText(parent, "")
    UI.setAnchor(widget, "TopLeft")
    UI.setStyle(widget, { textColor = color, backgroundColor = Menu.theme.clear, fontSize = size, fontBold = bold,
                          fontFamily = Menu.theme.font, textAlignment = "left", cornerRadius = 0 })
    UI.setTextAlignment(widget, "left")
    return widget
end

local function panelWidget(parent, color)
    local widget = UI.createPanel(parent)
    UI.setAnchor(widget, "TopLeft")
    UI.setBackgroundColor(widget, color.r, color.g, color.b, color.a)
    return widget
end

local function build(menu)
    local root = menu.root
    local ui = { rows = {} }
    ui.back = panelWidget(root, Menu.theme.tile)
    ui.name = textWidget(root, 30, true, Menu.theme.text)
    ui.slot = textWidget(root, 16, true, Menu.theme.accent)
    ui.lines = {}
    for i = 1, 4 do
        ui.lines[i] = textWidget(root, 18, false, Menu.theme.muted)
    end
    for i = 1, 5 do
        ui.rows[i] = { label = textWidget(root, 16, true, Menu.theme.muted),
                       track = panelWidget(root, Menu.theme.track), fill = panelWidget(root, Menu.theme.accent) }
    end
    return ui
end

local function setShown(ui, shown)
    for _, widget in ipairs({ ui.back, ui.name, ui.slot }) do
        UI.setVisible(widget, shown)
    end
    for _, widget in ipairs(ui.lines) do
        UI.setVisible(widget, shown)
    end
    for _, row in ipairs(ui.rows) do
        UI.setVisible(row.label, shown)
        UI.setVisible(row.track, shown)
        UI.setVisible(row.fill, shown)
    end
end

-- Splits text into lines no wider than width, at most `count` of them.
local function wrap(text, width, size, count)
    local out, line = {}, nil
    for word in tostring(text):gmatch("%S+") do
        local candidate = line and (line .. " " .. word) or word
        if line and (UI.measureText(candidate, size, false, Menu.theme.font) or 0) > width then
            out[#out + 1] = line
            line = word
        else
            line = candidate
        end
    end
    out[#out + 1] = line
    while #out > count do
        table.remove(out)
    end
    return out
end

local function draw(menu, ui, weapon, slot)
    local layoutWidth = UI.getLayoutSize and UI.getLayoutSize() or 1920
    local x = math.min(menu.x + menu.width + 120, (layoutWidth or 1920) - PANEL_WIDTH - 48)
    local y = menu.y
    local inner = PANEL_WIDTH - 48
    UI.setPosition(ui.back, x - 24, y - 16)
    UI.setText(ui.slot, SLOT_LABEL[slot] or "")
    UI.setPosition(ui.slot, x, y)
    UI.setSize(ui.slot, inner, 22)
    UI.setText(ui.name, weapon.name)
    UI.setPosition(ui.name, x, y + 24)
    UI.setSize(ui.name, inner, 40)
    local lines = wrap(weapon.description, inner, 18, #ui.lines)
    local lineY = y + 76
    for i, widget in ipairs(ui.lines) do
        UI.setText(widget, lines[i] or "")
        UI.setPosition(widget, x, lineY + (i - 1) * 26)
        UI.setSize(widget, inner, 26)
        UI.setVisible(widget, lines[i] ~= nil)
    end
    local rowY = lineY + math.max(1, #lines) * 26 + 24
    local stats = weapon.hasStats and Loadout.stats(weapon.definition) or {}
    for i, row in ipairs(ui.rows) do
        UI.setVisible(row.label, stats[i] ~= nil)
        UI.setVisible(row.track, stats[i] ~= nil)
        UI.setVisible(row.fill, stats[i] ~= nil)
    end
    for i, stat in ipairs(stats) do
        local row = ui.rows[i]
        UI.setText(row.label, stat.label)
        UI.setPosition(row.label, x, rowY)
        UI.setSize(row.label, inner - BAR_WIDTH - 16, 24)
        UI.setPosition(row.track, x + inner - BAR_WIDTH, rowY + 10)
        UI.setSize(row.track, BAR_WIDTH, 4)
        UI.setPosition(row.fill, x + inner - BAR_WIDTH, rowY + 10)
        UI.setSize(row.fill, math.max(2, BAR_WIDTH * stat.value), 4)
        rowY = rowY + 36
    end
    UI.setSize(ui.back, PANEL_WIDTH, (#stats > 0 and rowY or rowY - 24) - y + 16)
end

-- The LOADOUT page. options: confirmLabel and onConfirm (an item after the choices, such as START MATCH; without it
-- the page only saves the choice) and hint (shown while a weapon is selected).
function Loadout.page(options)
    options = options or {}
    local list = Loadout.list()
    local function choiceFor(slot)
        local entries = {}
        for _, weapon in ipairs(inSlot(list, slot)) do
            entries[#entries + 1] = { weapon.id, weapon.name }
        end
        return {
            label = SLOT_LABEL[slot],
            kind = "choice",
            options = entries,
            loadoutSlot = slot,
            get = function() return Loadout.current(list)[slot] end,
            set = function(value)
                Settings.set(FIELD[slot], value)
            end,
            hint = options.hint,
        }
    end
    local items = { choiceFor("primary"), choiceFor("secondary") }
    if #inSlot(list, "tactical") > 0 then
        items[#items + 1] = choiceFor("tactical")
    end
    if options.onConfirm then
        items[#items + 1] = { label = options.confirmLabel or "START", action = function(menu)
            Settings.save()
            options.onConfirm(menu)
        end }
    end
    local view = {}
    local side = {}
    function side.show(menu)
        menu.loadoutUi = menu.loadoutUi or build(menu)
        view.ui = menu.loadoutUi
        setShown(view.ui, true)
    end
    function side.hide()
        if view.ui then
            setShown(view.ui, false)
        end
    end
    function side.refresh(menu, item)
        local slot = item and item.loadoutSlot or "primary"
        local id = Loadout.current(list)[slot]
        for _, weapon in ipairs(list) do
            if weapon.id == id then
                draw(menu, view.ui, weapon, slot)
                return
            end
        end
    end
    return { title = "LOADOUT", items = items, side = #list > 0 and side or nil, onBack = Settings.save }
end

return Loadout
