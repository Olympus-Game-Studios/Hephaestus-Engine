local Hitscan = {}

function Hitscan.trace(ox, oy, oz, dx, dy, dz, range, owner)
    return Physics.raycast(ox, oy, oz, dx, dy, dz, range or 100, "Character", true, owner)
end

return Hitscan
