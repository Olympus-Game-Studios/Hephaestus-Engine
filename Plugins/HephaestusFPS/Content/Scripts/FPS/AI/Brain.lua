-- A small state machine for AI. Each state is a table with optional enter(owner, brain, ...), update(owner, brain,
-- dt) and exit(owner, brain) functions; update returns the name of the next state (and arguments for its enter) or
-- nil to stay. New behaviours are added by putting more states in the table, so the same brain can drive other
-- kinds of enemies or be swapped for a behaviour tree later.
local Brain = {}

function Brain.new(owner, states, initial, ...)
    local brain = { owner = owner, states = states, current = nil, time = 0, previous = nil }
    Brain.change(brain, initial, ...)
    return brain
end

function Brain.change(brain, name, ...)
    local state = brain.states[name]
    assert(state, "unknown AI state " .. tostring(name))
    local old = brain.current and brain.states[brain.current]
    if old and old.exit then
        old.exit(brain.owner, brain)
    end
    brain.previous = brain.current
    brain.current = name
    brain.time = 0
    if state.enter then
        state.enter(brain.owner, brain, ...)
    end
end

function Brain.update(brain, dt)
    brain.time = brain.time + dt
    local state = brain.states[brain.current]
    if state and state.update then
        local result = table.pack(state.update(brain.owner, brain, dt))
        if result[1] and result[1] ~= brain.current then
            Brain.change(brain, table.unpack(result, 1, result.n))
        end
    end
end

return Brain
