---
title: Scripting with Lua
description: Writing gameplay in Lua, the lifecycle callbacks, Inspector variables, the code editor and the debugger.
order: 11
---

# Scripting with Lua

Gameplay in Hephaestus is written in **Lua** (5.4). Lua is small, fast to learn and doesn't need compiling, so you can change a script while the game is running and see the result straight away. If you've never programmed before, it's a friendly first language. If you have, you'll be productive in an afternoon.

Lua is built in and is the main language. Other languages can be added with plugins; see [Other languages through plugins](other-languages.md).

## Your first script

1. In the Content Browser, go to `assets/scripts`, right click and choose **New Lua Script**. Call it `Spinner`.
2. Open it and replace the contents with:

    ```lua
    Expose.number("speed", 90)

    function onUpdate(dt)
        this:GetTransform():Rotate(0, speed * dt, 0)
    end
    ```

3. Select a cube, **Add Component > Lua Script** and set its script to `Spinner.lua`.
4. Press **Play**. It spins!
5. While it's still playing, change the script (try spinning on a different axis) and save. The script restarts with your change without you stopping the game.
6. Stop, and look at the Lua Script card in the Inspector. **speed** is there, so every spinner in your scene can have its own.

## How a script works

A script is attached to an entity, and `this` is always that entity. The engine calls functions in your script at the right moments. Define the ones you need and ignore the rest:

| Function | When it's called |
| --- | --- |
| `onAwake()` | Once, when the script first starts. Set things up here. |
| `onEnable()` / `onDisable()` | When the script is switched on or off. |
| `onUpdate(dt)` | Every frame. `dt` is the time since the last frame in seconds, so multiply speeds by it. |
| `onLateUpdate(dt)` | Every frame, after every script's `onUpdate`. Good for cameras that follow things. |
| `onDestroy()` | When the entity is removed or the game stops. |
| `onTriggerEnter(other, contact)` and friends | Physics events, [below](#reacting-to-physics). |
| `onAnimationFinished(clipId)` | A non looping animation has finished. |

You can attach several scripts to the same entity, and each gets its own copy of its variables.

## Inspector variables with `Expose`

`Expose` puts a variable in the Inspector so you (or a designer) can tweak it per entity without touching code:

```lua
Expose.number("moveSpeed", 5.0)
Expose.integer("ammo", 30)
Expose.bool("canSprint", true)
Expose.string("doorId", "main_door")
Expose.vec2("screenOffset", 0.0, 0.0)
Expose.vec3("spawnOffset", 0.0, 1.0, 0.0)
Expose.color("tint", 1.0, 0.8, 0.6, 1.0)

function onUpdate(dt)
    -- moveSpeed, ammo and the rest are ordinary variables here
end
```

Put `Expose` lines at the top of the file, and give them plain values (not calculations). The editor reads them straight from the file to build the Inspector, so it can't run your code to work out a value. Each name must be unique in the script. Values set in the Inspector are saved with the scene and with prefabs, and packaged games use them too.

## Talking to other entities

```lua
-- Find things by name
local spawns = Scene.findAllWithPrefix("EnemySpawn")

-- Call a function in another entity's script
local hit = Physics.raycast(x, y, z, dirX, dirY, dirZ, 100)
if hit and hit.entity then
    hit.entity:Call("onDamage", 25)
end

-- Spawn and remove prefabs
local enemy = Scene.instantiate("assets/prefabs/Enemy.hpprefab", {
    position = { x = 0, y = 1, z = 0 },
    vars = { health = 40 },          -- overrides the prefab script's Expose values
})
Scene.destroy(enemy)
```

`Call` runs a function by name in the other entity's scripts, which is the simplest way for objects to talk to each other. The [Lua API reference](lua-api.md) lists everything.

### Sharing code between scripts

`require` finds modules in `assets/scripts`, so a file called `assets/scripts/Utils.lua` that ends in `return Utils` can be loaded with `local Utils = require("Utils")`.

## Reacting to physics

```lua
function onTriggerEnter(other)
    if other:GetName() == "Player" then
        print("Player reached the checkpoint")
    end
end

function onCollisionEnter(other, contact)
    if contact then
        print("Hit at " .. contact.point.x .. ", " .. contact.point.y .. ", " .. contact.point.z)
    end
end
```

The full set is `onTriggerEnter`, `onTriggerStay`, `onTriggerExit`, `onCollisionEnter`, `onCollisionStay` and `onCollisionExit`. They run just after each physics step. `contact` (with `point`, `normal`, `relativeVelocity` and `penetration`) is sometimes `nil`, and the exit callbacks never get one, so always check it. `other` is `nil` if the other entity was destroyed.

## The code editor

In **Programmer** mode, scripts open in the editor's own **Code** panel. It's a proper code editor:

- Tabs, line numbers, a minimap, bracket matching and Lua highlighting
- **Autocomplete** and hover docs for Lua keywords, the engine's API and your own names
- Find and replace (`Ctrl+F`, `Ctrl+H`) with regex support, go to line (`Ctrl+G`) and toggle comment (`Ctrl+/`)
- Move and duplicate lines (`Alt+Up/Down`, `Alt+Shift+Up/Down`)
- `Ctrl+S` saves and `Ctrl+W` closes a tab

Script errors appear in the Console as `file:line: message`. Double click one to jump to the line, which stays underlined until you save. `Ctrl+Shift+F` searches every script in the project.

In the Default and Artist modes, scripts open in whatever app Windows uses for `.lua` files (VS Code is a great choice). Open the **Code** panel from View if you'd rather stay in the editor.

## Debugging

Here's where the built in editor really earns its keep. **Click in the gutter** next to a line number to set a breakpoint. When a script reaches it during Play, the game freezes on that frame, the Code panel shows the line and the **Debugger** panel shows the call stack, local variables and any watch expressions you've added.

| Key | Does |
| --- | --- |
| `F5` | Continue |
| `F10` | Step over |
| `F11` | Step into |
| `Shift+F11` | Step out |
| `Shift+F5` | Stop |

While the game is playing (or stopped at a breakpoint), the Console has a `lua>` prompt. Type any Lua and it runs right there in the game, so you can poke at variables or try a fix before writing it into the script. Up and Down scroll through what you've typed.

And of course `print("...")` still works, with its output in the Console.

## Things to know

- **Scripts can do anything Lua can do**, including reading and writing files. Only run scripts you trust, the same as any other program.
- Saving a script while the game is playing restarts that script: `onDestroy` runs, then the new version starts with `onAwake`.
- `Scene.reload()` restarts the current scene from its saved file, which is handy for a "try again" button. It doesn't save progress, though; there's no save game system yet.
