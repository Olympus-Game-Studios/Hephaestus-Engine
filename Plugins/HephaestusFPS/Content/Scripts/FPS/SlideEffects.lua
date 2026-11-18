-- The sound and dust of a slide, for the player (2D sound) and bots (3D sound at the bot). Keep one state table per
-- slider and call SlideEffects.update(state, sliding, options) every frame; SlideEffects.stop(state) when it goes away.
-- options: entity (the slider, whose child the dust is), sound, volume, loopStart and loopEnd (the stretch the sound
-- repeats from once it reaches loopEnd while the slide goes on), fadeSeconds (how fast it fades when the slide ends),
-- spatial (play at position), position {x, y, z}, minDistance, maxDistance, dustPrefab (a looping Particle Emitter
-- with FPS/SlideDust.lua), dustOffset {x, y, z} (in the slider's own space: +X forward, +Z right), dustRotation
-- {x, y, z} (degrees; the emitter sprays along its +Y), dustRate (particles
-- a second at fullSpeed), fullSpeed (metres a second) and speed (the slider's current speed).
local SlideEffects = {}

local function audioReady()
    return Audio and Audio.isAvailable and Audio.isAvailable()
end

function SlideEffects.stop(state, fadeSeconds)
    if state.sound and audioReady() then
        Audio.stop(state.sound, fadeSeconds or 0)
    end
    state.sound = nil
    if state.dust and state.dust:IsValid() then
        state.dust:Call("stopDust")
    end
    state.dust = nil
    state.sliding = false
end

local function start(state, options)
    if options.sound and options.sound ~= "" and audioReady() then
        local play = { volume = options.volume or 1, loop = true, loopStart = options.loopStart or 0,
                       loopEnd = options.loopEnd or 0, bus = "sfx", priority = 2 }
        if options.spatial and options.position then
            play.position = options.position
            play.minDistance = options.minDistance or 2
            play.maxDistance = options.maxDistance or 30
        end
        local handle = Audio.play(options.sound, play)
        state.sound = handle and handle ~= 0 and handle or nil
    end
    if options.dustPrefab and options.dustPrefab ~= "" and options.entity then
        local offset = options.dustOffset or { x = 0, y = 0, z = 0 }
        state.dust = Scene.instantiate(options.dustPrefab, { parent = options.entity, position = offset,
                                                            rotation = options.dustRotation })
    end
    state.sliding = true
end

function SlideEffects.update(state, sliding, options)
    if sliding and not state.sliding then
        start(state, options)
    elseif not sliding and state.sliding then
        SlideEffects.stop(state, options.fadeSeconds or 0.2)
        return
    end
    if not state.sliding then
        return
    end
    if options.spatial and state.sound and options.position then
        local p = options.position
        Audio.setPosition(state.sound, p.x, p.y, p.z)
    end
    if state.dust and state.dust:IsValid() then
        local full = math.max(options.fullSpeed or 6, 0.1)
        local amount = math.max(0, math.min(1, (options.speed or full) / full))
        state.dust:Call("setDustRate", (options.dustRate or 40) * amount)
    end
end

return SlideEffects
