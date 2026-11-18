-- Dust kicked up by a slide: put it with a looping Particle Emitter (its Burst is the puff as the slide starts).
-- FPS.SlideEffects sets the rate from the slide's speed and stops it as the slide ends; the entity then removes itself
-- once its last particles are gone.
local stopping = false

function setDustRate(rate)
    if not stopping then
        Particles.setRate(rate)
    end
end

function stopDust()
    stopping = true
    Particles.stop()
end

function onUpdate(dt)
    if stopping and Particles.count() == 0 then
        Scene.destroy(this)
    end
end
