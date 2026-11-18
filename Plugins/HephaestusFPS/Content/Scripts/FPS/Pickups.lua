-- What a pickup would give the player, worked out without touching the scene so it can be tested. Kinds:
-- health (points, up to max health), ammo (magazines for every weapon carried, up to each weapon's starting
-- reserve), frag, smoke and grenades (that many of both, up to the player's carry limits).
local Pickups = {}

Pickups.colors = {
    health = { 0.35, 0.9, 0.4 },
    ammo = { 1.0, 0.8, 0.35 },
    frag = { 0.95, 0.25, 0.22 },
    smoke = { 0.7, 0.8, 0.9 },
    grenades = { 0.95, 0.25, 0.22 },
}

-- How much of amount fits between current and max (never negative).
function Pickups.room(current, max, amount)
    return math.max(0, math.min(amount, (max or 0) - (current or 0)))
end

-- What taking the pickup changes, or nil when it would do nothing. state holds health and maxHealth, weapons (a
-- list of { reserve, maxReserve, magazineSize }), activeWeapon (an index into weapons, or nil), grenades
-- ({ frag, smoke }) and maxGrenades. The result has `health`, `ammo` (gain per weapon index), `frag` and `smoke`.
function Pickups.plan(kind, amount, state)
    local plan = {}
    local any = false
    if kind == "health" then
        local gain = Pickups.room(state.health, state.maxHealth, amount)
        if gain > 0 then
            plan.health, any = gain, true
        end
    elseif kind == "ammo" then
        plan.ammo = {}
        for i, weapon in ipairs(state.weapons or {}) do
            local gain = Pickups.room(weapon.reserve, weapon.maxReserve, amount * (weapon.magazineSize or 0))
            plan.ammo[i] = gain
            any = any or gain > 0
        end
    elseif kind == "frag" or kind == "smoke" or kind == "grenades" then
        local grenades, limits = state.grenades or {}, state.maxGrenades or {}
        for _, which in ipairs({ "frag", "smoke" }) do
            if kind == which or kind == "grenades" then
                local gain = Pickups.room(grenades[which], limits[which], amount)
                if gain > 0 then
                    plan[which], any = gain, true
                end
            end
        end
    end
    return any and plan or nil
end

-- The HUD message for a plan, such as "+50 Health" or "+60 Ammo" (the rounds the weapon in hand got, or the most
-- any weapon got). tacticalLabel names the tactical grenade ("Smoke" when nil).
function Pickups.describe(plan, activeWeapon, tacticalLabel)
    if plan.health then
        return string.format("+%d Health", math.floor(plan.health + 0.5))
    end
    if plan.ammo then
        local rounds = activeWeapon and plan.ammo[activeWeapon] or 0
        if rounds <= 0 then
            for _, gain in pairs(plan.ammo) do
                rounds = math.max(rounds, gain)
            end
        end
        return string.format("+%d Ammo", rounds)
    end
    local parts = {}
    if plan.frag then
        parts[#parts + 1] = string.format("+%d Frag", plan.frag)
    end
    if plan.smoke then
        parts[#parts + 1] = string.format("+%d %s", plan.smoke, tacticalLabel or "Smoke")
    end
    return table.concat(parts, "   ")
end

return Pickups
