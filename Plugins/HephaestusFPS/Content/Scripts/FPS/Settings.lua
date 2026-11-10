-- Player settings: look sensitivity, field of view, comfort options, sound volumes and difficulty. Loaded from and
-- saved to Storage ("settings"); the player script reads Settings.values every frame, and Settings.apply pushes
-- the rest (bus volumes, difficulty) to the engine.
local Difficulty = require("FPS.AI.Difficulty")
local Menu = require("FPS.Menu")

local Settings = {}

Settings.defaults = {
    sensitivity = 1.0,  -- multiplies the player's mouse and stick look speed
    fieldOfView = 0,    -- 0 keeps the game's own field of view (Settings.gameFieldOfView)
    viewBob = 1.0,      -- 0..1, multiplies the player's viewBobScale
    cameraShake = 1.0,  -- 0..1, multiplies the player's cameraShakeScale
    masterVolume = 1.0,
    sfxVolume = 1.0,
    weaponsVolume = 1.0,
    musicVolume = 1.0,
    difficulty = "regular",
    reducedMotion = false,
}

Settings.values = {}
for key, value in pairs(Settings.defaults) do
    Settings.values[key] = value
end
Settings.loaded = false
Settings.gameFieldOfView = 80

-- The field of view in use: the player's choice, or the game's own until they choose one.
function Settings.fieldOfView()
    local chosen = Settings.values.fieldOfView
    return chosen > 0 and chosen or Settings.gameFieldOfView
end

local BUSES = { masterVolume = "master", sfxVolume = "sfx", weaponsVolume = "weapons", musicVolume = "music" }

-- Loads the saved settings once per game session (later calls keep the values in memory).
function Settings.load()
    if Settings.loaded then
        return Settings.values
    end
    Settings.loaded = true
    local saved = Storage and Storage.load and Storage.load("settings")
    if type(saved) == "table" then
        for key, default in pairs(Settings.defaults) do
            if type(saved[key]) == type(default) then
                Settings.values[key] = saved[key]
            end
        end
    end
    Settings.apply()
    return Settings.values
end

function Settings.save()
    if Storage and Storage.save then
        Storage.save("settings", Settings.values)
    end
end

function Settings.apply()
    if Audio and Audio.setBusVolume then
        for key, bus in pairs(BUSES) do
            Audio.setBusVolume(bus, Settings.values[key])
        end
    end
    Difficulty.setLevel(Settings.values.difficulty)
    Menu.reducedMotion = Settings.values.reducedMotion
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
    local levels = {}
    for _, name in ipairs(Difficulty.levels()) do
        levels[#levels + 1] = { name, name:upper() }
    end
    return {
        title = "SETTINGS",
        items = {
            slider("LOOK SENSITIVITY", "sensitivity", 0.2, 3.0, 0.05, function(v) return string.format("%.2fx", v) end),
            { label = "FIELD OF VIEW", kind = "slider", min = 60, max = 110, step = 1,
              get = Settings.fieldOfView, set = function(v) Settings.set("fieldOfView", v) end,
              format = function(v) return string.format("%d", v) end },
            slider("VIEW BOB", "viewBob", 0, 1, 0.1),
            slider("CAMERA SHAKE", "cameraShake", 0, 1, 0.1),
            slider("MASTER VOLUME", "masterVolume", 0, 1, 0.05),
            slider("EFFECTS VOLUME", "sfxVolume", 0, 1, 0.05),
            slider("WEAPONS VOLUME", "weaponsVolume", 0, 1, 0.05),
            slider("MUSIC VOLUME", "musicVolume", 0, 1, 0.05),
            { label = "DIFFICULTY", kind = "choice", options = levels,
              get = function() return Settings.values.difficulty end,
              set = function(v) Settings.set("difficulty", v) end },
            { label = "REDUCE MOTION", kind = "choice", options = { { false, "OFF" }, { true, "ON" } },
              get = function() return Settings.values.reducedMotion end,
              set = function(v) Settings.set("reducedMotion", v) end },
        },
        onBack = Settings.save,
    }
end

return Settings
