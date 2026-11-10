-- AI difficulty: every value an enemy's skill depends on, in named presets. A settings menu can switch the global
-- level (Difficulty.setLevel) or change single values (Difficulty.setOverride); an enemy can name its own level and
-- override values too. Times in seconds, angles in degrees, distances in metres.
local Difficulty = {}

local ORDER = { "recruit", "regular", "hardened", "veteran" }

local PRESETS = {
    recruit = {
        reactionSeconds = 0.9,          -- delay between spotting the player and the first shot
        aimErrorDegrees = 9,            -- aim error when the target is first seen
        aimErrorMinDegrees = 2.5,       -- aim error after the aim has settled
        aimSettleSeconds = 3,           -- time to go from the first to the settled error while the target stays visible
        movingTargetErrorDegrees = 0.8, -- extra error per m/s the target is moving
        burstMin = 2,
        burstMax = 3,
        roundsPerSecond = 6,
        burstPauseSeconds = 1.4,
        damageScale = 0.5,              -- multiplies the enemy weapon's damage
        sightRange = 35,
        fovDegrees = 100,
        awarenessPerSecond = 1.0,       -- how fast an enemy notices a visible player at close range
        awarenessDecayPerSecond = 0.3,
        hearingScale = 0.6,             -- multiplies how far noises carry to this enemy
        coverChance = 0.4,              -- chance to take cover when engaging
        peekSeconds = 1.6,
        hideSeconds = 2.2,
        lostTargetSeconds = 6,          -- time without seeing the player before searching
        aggression = 0.2,               -- chance to push towards the player instead of waiting
        grenadeAwareness = false,       -- moves away from live grenades
        squadAlerts = false,            -- tells nearby allies where the player is
        allyAlertRadius = 15,
    },
    regular = {
        reactionSeconds = 0.6, aimErrorDegrees = 6, aimErrorMinDegrees = 1.5, aimSettleSeconds = 2,
        movingTargetErrorDegrees = 0.6, burstMin = 3, burstMax = 5, roundsPerSecond = 8, burstPauseSeconds = 1.0,
        damageScale = 0.8, sightRange = 50, fovDegrees = 120, awarenessPerSecond = 1.6, awarenessDecayPerSecond = 0.25,
        hearingScale = 1.0, coverChance = 0.7, peekSeconds = 2.0, hideSeconds = 1.6, lostTargetSeconds = 5,
        aggression = 0.4, grenadeAwareness = true, squadAlerts = true, allyAlertRadius = 20,
    },
    hardened = {
        reactionSeconds = 0.4, aimErrorDegrees = 4, aimErrorMinDegrees = 0.8, aimSettleSeconds = 1.5,
        movingTargetErrorDegrees = 0.4, burstMin = 3, burstMax = 6, roundsPerSecond = 9, burstPauseSeconds = 0.8,
        damageScale = 1.0, sightRange = 65, fovDegrees = 130, awarenessPerSecond = 2.4, awarenessDecayPerSecond = 0.2,
        hearingScale = 1.2, coverChance = 0.9, peekSeconds = 2.4, hideSeconds = 1.2, lostTargetSeconds = 4,
        aggression = 0.6, grenadeAwareness = true, squadAlerts = true, allyAlertRadius = 25,
    },
    veteran = {
        reactionSeconds = 0.25, aimErrorDegrees = 2.5, aimErrorMinDegrees = 0.4, aimSettleSeconds = 1.0,
        movingTargetErrorDegrees = 0.25, burstMin = 4, burstMax = 8, roundsPerSecond = 10, burstPauseSeconds = 0.6,
        damageScale = 1.25, sightRange = 80, fovDegrees = 140, awarenessPerSecond = 3.5,
        awarenessDecayPerSecond = 0.15, hearingScale = 1.5, coverChance = 1.0, peekSeconds = 2.8, hideSeconds = 0.9,
        lostTargetSeconds = 3, aggression = 0.8, grenadeAwareness = true, squadAlerts = true, allyAlertRadius = 30,
    },
}

local level = "regular"
local overrides = {}

function Difficulty.levels()
    return ORDER
end

function Difficulty.level()
    return level
end

function Difficulty.setLevel(name)
    if PRESETS[name] then
        level = name
        return true
    end
    return false
end

-- A global change to one value, kept across level changes until cleared; nil removes it.
function Difficulty.setOverride(field, value)
    overrides[field] = value
end

function Difficulty.clearOverrides()
    overrides = {}
end

-- The settings for one enemy: its own level's preset (or the global level's), then the global overrides, then
-- the enemy's own overrides.
function Difficulty.resolve(enemyLevel, enemyOverrides)
    local preset = PRESETS[enemyLevel ~= nil and enemyLevel ~= "" and enemyLevel or level] or PRESETS.regular
    local settings = {}
    for key, value in pairs(PRESETS.recruit) do
        settings[key] = value
    end
    for key, value in pairs(preset) do
        settings[key] = value
    end
    for key, value in pairs(overrides) do
        settings[key] = value
    end
    for key, value in pairs(enemyOverrides or {}) do
        settings[key] = value
    end
    return settings
end

return Difficulty
