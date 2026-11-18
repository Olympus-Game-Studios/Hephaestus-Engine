-- Follows navmesh paths: plans with Nav.findPath, steers through the corners, and plans again when the goal moves
-- or the walker gets stuck. Where a path vaults low cover (a corner marked `link`), the walker stops there and
-- `mover.link` holds the jump's two ends; the caller crosses it and then calls Mover.finishLink.
local Mover = {}

local CORNER_REACHED = 0.35
local STUCK_SECONDS = 1.0
local STUCK_SPEED = 0.25

function Mover.new()
    return { path = nil, index = 1, goal = nil, arrived = true, stuckTime = 0, lastX = nil, lastZ = nil, link = nil }
end

-- Plans a path to goal from position. Returns false when no path exists.
function Mover.setGoal(mover, position, goal)
    mover.goal = { x = goal.x, y = goal.y, z = goal.z }
    mover.path = Nav.findPath(position, goal)
    mover.index = 2
    mover.arrived = mover.path == nil
    mover.stuckTime = 0
    mover.link = nil
    return mover.path ~= nil
end

function Mover.stop(mover)
    mover.path = nil
    mover.goal = nil
    mover.arrived = true
    mover.link = nil
end

-- After crossing mover.link: carries on from the corner it landed on.
function Mover.finishLink(mover)
    if mover.link then
        mover.index = mover.index + 2
        mover.link = nil
        mover.lastX = nil
        mover.stuckTime = 0
    end
end

-- Plans again only when the goal has moved more than tolerance.
function Mover.follow(mover, position, goal, tolerance)
    if mover.goal then
        local dx, dz = goal.x - mover.goal.x, goal.z - mover.goal.z
        if dx * dx + dz * dz <= (tolerance or 1) ^ 2 and not mover.arrived then
            return true
        end
    end
    return Mover.setGoal(mover, position, goal)
end

-- The planar direction to walk this frame (0, 0 when arrived or without a path) and whether the walker has
-- arrived within arriveDistance of the goal.
function Mover.update(mover, position, dt, arriveDistance)
    if not mover.path or mover.arrived then
        return 0, 0, true
    end
    local path = mover.path
    while mover.index <= #path do
        local corner = path[mover.index]
        local dx, dz = corner.x - position.x, corner.z - position.z
        local last = mover.index == #path
        local reach = last and (arriveDistance or CORNER_REACHED) or CORNER_REACHED
        if dx * dx + dz * dz > reach * reach then
            break
        end
        if corner.link and path[mover.index + 1] then
            mover.link = { from = corner, to = path[mover.index + 1] }
            return 0, 0, false
        end
        mover.index = mover.index + 1
    end
    if mover.index > #path then
        mover.arrived = true
        return 0, 0, true
    end
    local corner = path[mover.index]
    local dx, dz = corner.x - position.x, corner.z - position.z
    local length = math.sqrt(dx * dx + dz * dz)

    if mover.lastX then
        local moved = math.sqrt((position.x - mover.lastX) ^ 2 + (position.z - mover.lastZ) ^ 2)
        if dt > 0 and moved / dt < STUCK_SPEED then
            mover.stuckTime = mover.stuckTime + dt
        else
            mover.stuckTime = 0
        end
    end
    mover.lastX, mover.lastZ = position.x, position.z
    if mover.stuckTime > STUCK_SECONDS and mover.goal then
        Mover.setGoal(mover, position, mover.goal)
    end
    if length < 1e-4 then
        return 0, 0, false
    end
    return dx / length, dz / length, false
end

return Mover
