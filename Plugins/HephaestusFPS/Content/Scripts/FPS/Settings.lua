-- Player settings: look sensitivity, field of view, comfort options, UI size, sound volumes, the last match and
-- Survival set up from the menu (with their difficulties), the loadout (FPS.Loadout) and the controls (FPS.Controls). Kept in a commented INI file (Config "Settings": Settings.ini in a packaged game,
-- EditorSettings.ini in Editor Play) that players can edit by hand; a missing or unreadable value keeps its default
-- and is logged. The player script reads Settings.values every frame, and Settings.apply pushes the rest (bus
-- volumes, UI size, texture quality) to the engine. Difficulty is chosen per mode when it starts, not here.
local Controls = require("FPS.Controls")
local Difficulty = require("FPS.AI.Difficulty")
local Menu = require("FPS.Menu")

local Settings = {}

Settings.defaults = {
    sensitivity = 1.0,  -- multiplies the player's mouse and stick look speed
    fieldOfView = 0,    -- 0 keeps the game's own field of view (Settings.gameFieldOfView)
    viewBob = 1.0,      -- 0..1, multiplies the player's viewBobScale
    cameraShake = 1.0,  -- 0..1, multiplies the player's cameraShakeScale
    uiScale = 1.0,      -- game UI size on top of its scaling with resolution
    masterVolume = 1.0,
    sfxVolume = 1.0,
    weaponsVolume = 1.0,
    musicVolume = 1.0,
    reducedMotion = false,
    textureQuality = "medium", -- low, medium or high (Settings.TEXTURE_SIZES)
    -- The last team deathmatch and Survival set up from the menu (FPS.TeamDeathmatch reads the match it starts from
    -- Registry.matchSetup; Survival's enemies take the AI difficulty level set as it starts).
    matchMap = "",
    matchFriendlyBots = 5,
    matchEnemyBots = 6,
    matchFriendlyDifficulty = "regular",
    matchEnemyDifficulty = "regular",
    matchScoreLimit = 50,
    matchTimeLimitMinutes = 10,
    matchRespawnSeconds = 5,
    matchFriendlyFire = false,
    survivalDifficulty = "regular",
    -- The player's loadout (FPS.Loadout): weapon ids from the game's loadout list.
    loadoutPrimary = "Rifle",
    loadoutSecondary = "DesertEagle",
}

Settings.values = {}
for key, value in pairs(Settings.defaults) do
    Settings.values[key] = value
end
Settings.gameFieldOfView = 80

-- The longest side material textures load at for each Texture Quality (0 = the file's own size).
Settings.TEXTURE_SIZES = { low = 1024, medium = 2048, high = 0 }

-- The field of view in use: the player's choice, or the game's own until they choose one.
function Settings.fieldOfView()
    local chosen = Settings.values.fieldOfView
    return chosen > 0 and chosen or Settings.gameFieldOfView
end

local BUSES = { masterVolume = "master", sfxVolume = "sfx", weaponsVolume = "weapons", musicVolume = "music" }

-- The file's layout: sections, keys and the comments written above them. min and max bound hand-edited numbers.
Settings.file = {
    { name = "Gameplay", keys = {
        { key = "LookSensitivity", field = "sensitivity", min = 0.2, max = 3, comment = "Mouse and stick look speed, 0.2 to 3" },
        { key = "ViewBob", field = "viewBob", min = 0, max = 1, comment = "Head bob while moving, 0 (off) to 1" },
        { key = "CameraShake", field = "cameraShake", min = 0, max = 1, comment = "Shake from blasts and landings, 0 (off) to 1" },
    } },
    { name = "Video", keys = {
        { key = "FieldOfView", field = "fieldOfView", min = 0, max = 110,
          comment = "Degrees, 60 to 110; 0 uses the game's own" },
        { key = "UIScale", field = "uiScale", min = 0.75, max = 1.5, comment = "Game UI size, 0.75 to 1.5" },
        { key = "TextureQuality", field = "textureQuality", choices = { "low", "medium", "high" },
          comment = "low (1024), medium (2048) or high (full size) textures" },
        { key = "ReduceMotion", field = "reducedMotion", comment = "true makes menu highlights jump instead of slide" },
    } },
    { name = "Audio", keys = {
        { key = "MasterVolume", field = "masterVolume", min = 0, max = 1, comment = "Volumes, 0 to 1" },
        { key = "EffectsVolume", field = "sfxVolume", min = 0, max = 1 },
        { key = "WeaponsVolume", field = "weaponsVolume", min = 0, max = 1 },
        { key = "MusicVolume", field = "musicVolume", min = 0, max = 1 },
    } },
    { name = "Match", keys = {
        { key = "Map", field = "matchMap", comment = "Last team deathmatch set up against bots" },
        { key = "FriendlyBots", field = "matchFriendlyBots", min = 0, max = 7 },
        { key = "EnemyBots", field = "matchEnemyBots", min = 1, max = 8 },
        { key = "FriendlyDifficulty", field = "matchFriendlyDifficulty", levels = true,
          comment = "recruit, regular, hardened or veteran" },
        { key = "EnemyDifficulty", field = "matchEnemyDifficulty", levels = true },
        { key = "ScoreLimit", field = "matchScoreLimit", min = 10, max = 200 },
        { key = "TimeLimitMinutes", field = "matchTimeLimitMinutes", min = 1, max = 60 },
        { key = "RespawnSeconds", field = "matchRespawnSeconds", min = 1, max = 30 },
        { key = "FriendlyFire", field = "matchFriendlyFire" },
    } },
    { name = "Survival", keys = {
        { key = "Difficulty", field = "survivalDifficulty", levels = true,
          comment = "Last Survival difficulty: recruit, regular, hardened or veteran" },
    } },
    { name = "Loadout", keys = {
        { key = "Primary", field = "loadoutPrimary", comment = "Weapons taken into a match, by id (such as Rifle, Sniper, Shotgun)" },
        { key = "Secondary", field = "loadoutSecondary" },
    } },
}

local CONTROLS_COMMENT = "Each action lists its bindings as kind:CONTROL, separated by commas.\n"
    .. "Kinds: key, mouse (LEFT, RIGHT, MIDDLE, 3 to 7), pad (SOUTH, EAST, WEST, NORTH, LEFT_BUMPER, ...),\n"
    .. "stick (LEFT_X, LEFT_Y, ...). *-1 reverses one, as for the backward half of a movement axis.\n"
    .. "Delete a line to go back to the game's default for it."

local function log(text)
    engine_log("[FPS.Settings] " .. text)
end

-- The value of a setting read from the file, or nil (and a log line) when it does not suit the setting.
local function parse(entry, text)
    local default = Settings.defaults[entry.field]
    if type(default) == "number" then
        local n = tonumber(text)
        if n and n == n then
            return math.max(entry.min or n, math.min(entry.max or n, n))
        end
    elseif type(default) == "boolean" then
        local lower = tostring(text):lower()
        if lower == "true" or lower == "1" or lower == "yes" or lower == "on" then
            return true
        elseif lower == "false" or lower == "0" or lower == "no" or lower == "off" then
            return false
        end
    elseif entry.choices then
        for _, choice in ipairs(entry.choices) do
            if choice == tostring(text):lower() then
                return choice
            end
        end
    elseif entry.levels then
        for _, level in ipairs(Difficulty.levels()) do
            if level == tostring(text):lower() then
                return level
            end
        end
    else
        return text
    end
    log(string.format("%s = %s is not a valid value; using %s", entry.key, tostring(text), tostring(default)))
    return nil
end

local function readFile(document)
    -- Older files kept one Difficulty under [Gameplay]; it starts each per-mode difficulty not saved yet.
    local old = document.Gameplay and document.Gameplay.Difficulty
    for _, section in ipairs(Settings.file) do
        local values = document[section.name] or {}
        for _, entry in ipairs(section.keys) do
            local saved = values[entry.key]
            if saved == nil and entry.levels then
                saved = old
            end
            if saved ~= nil then
                local value = parse(entry, saved)
                if value ~= nil then
                    Settings.values[entry.field] = value
                end
            end
        end
    end
end

-- Settings saved by older versions (Storage "settings" and "controls"), read once into the new file.
local function migrate()
    local saved = Storage and Storage.load and Storage.load("settings")
    if type(saved) == "table" then
        for key, default in pairs(Settings.defaults) do
            if type(saved[key]) == type(default) then
                Settings.values[key] = saved[key]
            end
        end
    end
    local controls = Storage and Storage.load and Storage.load("controls")
    if type(controls) == "table" then
        for name, list in pairs(controls) do
            if type(list) == "table" and Input.getBindings(name) then
                Input.setBindings(name, list)
            end
        end
    end
    if saved or controls then
        log("moved the saved settings into " .. tostring(Config.path("Settings")))
    end
end

-- Reads the settings file (every change is saved at once, so it always matches the game); with no file yet, older
-- saved settings move into a new one. Called by each level's player and menu, since a level load can reload the
-- input map and the file may have been edited by hand.
function Settings.load()
    for key, value in pairs(Settings.defaults) do
        Settings.values[key] = value
    end
    local document = Config and Config.load and Config.load("Settings")
    if document then
        Controls.apply(document.Controls, tostring(Config.path("Settings")):match("[^/\\]+$"))
        readFile(document)
    elseif Config and Config.save then
        migrate()
        Settings.save()
    end
    Settings.apply()
    return Settings.values
end

local function text(value)
    if type(value) == "number" then
        return string.format("%g", math.floor(value * 1000 + 0.5) / 1000)
    end
    return tostring(value)
end

function Settings.save()
    if not (Config and Config.save) then
        return
    end
    local sections = {}
    for _, section in ipairs(Settings.file) do
        local keys = {}
        for _, entry in ipairs(section.keys) do
            keys[#keys + 1] = { key = entry.key, value = text(Settings.values[entry.field]), comment = entry.comment }
        end
        sections[#sections + 1] = { name = section.name, keys = keys }
    end
    sections[#sections + 1] = { name = "Controls", comment = CONTROLS_COMMENT, keys = Controls.serialize() }
    Config.save("Settings", sections, "Game settings. Edit with the game closed; the game rewrites this file when "
        .. "settings change.")
end

Controls.changed = Settings.save

function Settings.apply()
    if Audio and Audio.setBusVolume then
        for key, bus in pairs(BUSES) do
            Audio.setBusVolume(bus, Settings.values[key])
        end
    end
    if UI and UI.setScale then
        UI.setScale(Settings.values.uiScale)
    end
    Menu.reducedMotion = Settings.values.reducedMotion
    if Graphics and Graphics.setMaxTextureSize then
        Graphics.setMaxTextureSize(Settings.TEXTURE_SIZES[Settings.values.textureQuality] or 0)
    end
end

function Settings.set(key, value)
    Settings.values[key] = value
    Settings.apply()
end

local function percent(v)
    return string.format("%d%%", math.floor(v * 100 + 0.5))
end

local function slider(label, key, min, max, step, format)
    return { label = label, kind = "slider", min = min, max = max, step = step,
             get = function() return Settings.values[key] end,
             set = function(v) Settings.set(key, v) end,
             format = format or percent }
end

-- The settings page for FPS.Menu; leaving it saves.
function Settings.page()
    return {
        title = "SETTINGS",
        items = {
            { label = "CONTROLS", action = function(menu) Menu.push(menu, Controls.page()) end },
            slider("LOOK SENSITIVITY", "sensitivity", 0.2, 3.0, 0.05, function(v) return string.format("%.2fx", v) end),
            { label = "FIELD OF VIEW", kind = "slider", min = 60, max = 110, step = 1,
              get = Settings.fieldOfView, set = function(v) Settings.set("fieldOfView", v) end,
              format = function(v) return string.format("%d", v) end },
            slider("VIEW BOB", "viewBob", 0, 1, 0.1),
            slider("CAMERA SHAKE", "cameraShake", 0, 1, 0.1),
            slider("UI SIZE", "uiScale", 0.75, 1.5, 0.05),
            { label = "TEXTURE QUALITY", kind = "choice",
              options = { { "low", "LOW" }, { "medium", "MEDIUM" }, { "high", "HIGH" } },
              get = function() return Settings.values.textureQuality end,
              set = function(v) Settings.set("textureQuality", v) end },
            slider("MASTER VOLUME", "masterVolume", 0, 1, 0.05),
            slider("EFFECTS VOLUME", "sfxVolume", 0, 1, 0.05),
            slider("WEAPONS VOLUME", "weaponsVolume", 0, 1, 0.05),
            slider("MUSIC VOLUME", "musicVolume", 0, 1, 0.05),
            { label = "REDUCE MOTION", kind = "choice", options = { { false, "OFF" }, { true, "ON" } },
              get = function() return Settings.values.reducedMotion end,
              set = function(v) Settings.set("reducedMotion", v) end },
        },
        onBack = Settings.save,
    }
end

return Settings
