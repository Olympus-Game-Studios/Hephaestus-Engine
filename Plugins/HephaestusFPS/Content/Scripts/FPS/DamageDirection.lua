-- Where recent hits on the player came from, for the damage direction indicator.
local GrenadeWarning = require("FPS.GrenadeWarning")

local DamageDirection = {}

-- hits: list of { x, y, z, time } (Registry.playerHits). Returns the newest `limit` hits younger than `duration`
-- seconds as { angle, alpha }: angle in radians clockwise from straight ahead, alpha fading from 1 to 0 with age.
function DamageDirection.compute(feet, yaw, hits, now, duration, limit)
    local out = {}
    for i = #hits, 1, -1 do
        local hit = hits[i]
        local age = now - hit.time
        if age >= 0 and age < duration then
            out[#out + 1] = { angle = GrenadeWarning.bearing(feet, yaw, hit.x, hit.z), alpha = 1 - age / duration }
            if #out >= limit then
                break
            end
        end
    end
    return out
end

return DamageDirection
