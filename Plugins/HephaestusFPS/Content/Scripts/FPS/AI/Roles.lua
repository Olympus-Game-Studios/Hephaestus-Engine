-- Bot roles: what each kind of soldier carries and how it fights, on top of its difficulty (FPS.AI.Difficulty).
-- An enemy picks its role from its `role` variable; FPS.TeamDeathmatch hands some bots "sniper". Change the tables
-- here (or replace a role) to tune them. Distances in metres, times in seconds, angles in degrees.
local Roles = {}

-- The default: the rifle and behaviour set on the enemy itself.
Roles.rifleman = { name = "rifleman" }

-- Hangs back on overwatch: a cover spot overwatchMin to overwatchMax from the enemy with a peek spot that sees them,
-- hides to work the bolt, peeks to shoot, moves on after relocateAfterShots shots or when hit, falls back when anyone
-- comes within keepAwayDistance, and fires on what teammates are fighting when it has no target of its own.
Roles.sniper = {
    name = "sniper",
    damage = 110,               -- times the difficulty's damageScale: 55 recruit, 88 regular, a body kill from hardened
    range = 250,
    magazineSize = 5,
    reloadSeconds = 3.6,
    shotInterval = 1.6,         -- a shot, then the bolt
    sightRangeScale = 2.2,      -- sees this much further than a rifleman of the same difficulty
    aimErrorScale = 0.45,       -- tighter settled aim...
    aimSettleScale = 1.5,       -- ...that takes longer to settle, so the first shot is the dangerous one less often
    noiseRadius = 70,
    overwatchMin = 20,
    overwatchMax = 50,
    overwatchSearchRadius = 16,
    keepAwayDistance = 12,
    relocateAfterShots = 3,
    peekSeconds = 4,
    hideSeconds = 1.6,
    rescanSeconds = 15,         -- with nothing to shoot for this long, finds a new spot facing where the enemy is
    coverFireInterval = 2.2,
    coverFireSpreadDegrees = 2.5,
}

-- Fire at a bot pins it: worse aim and a dash for cover.
Roles.suppression = {
    perShot = 0.15,             -- per shot fired at it (cover fire and snipers count double)
    max = 1,
    decayPerSecond = 0.3,
    aimErrorDegrees = 5,        -- at full suppression
    coverAbove = 0.5,           -- above this a bot in the open takes cover
}

function Roles.get(name)
    local role = name and Roles[string.lower(name)]
    return type(role) == "table" and role.name and role or Roles.rifleman
end

-- How many of a team's `count` bots are snipers: one in sniperEvery, and none in teams under sniperMinTeam.
Roles.sniperEvery = 4
Roles.sniperMinTeam = 3
function Roles.sniperCount(count)
    if count < Roles.sniperMinTeam then
        return 0
    end
    return math.max(1, math.floor(count / Roles.sniperEvery))
end

return Roles
