---
title: Input
description: Actions and axes, binding keys, mouse and gamepads, and reading input from Lua.
order: 13
---

# Input

Rather than checking for the W key all over your scripts, Hephaestus lets you name what the player is *doing*. "Jump" is an **action**: it's either pressed or it isn't. "MoveForward" is an **axis**: a number between -1 and 1. You bind keys, mouse movement and gamepad controls to those names once, and your scripts only ever ask about the names.

The upside is huge. Want to support a gamepad, or let players rebind keys later? Your gameplay code doesn't change.

## The input map

Your project's input lives in `assets/input/Default.hpinput`. Edit it in **Edit > Settings > Project Settings > Input**, where you can **Add Action**, **Add Axis** and **Add Binding** for each one. Axes also have a **Deadzone** (to ignore a slightly drifting thumbstick) and **Clamp**, and each binding has a **Scale** (use -1 for the "backwards" key on an axis).

New projects start with a sensible set:

| Name | Kind | Keyboard and mouse | Gamepad |
| --- | --- | --- | --- |
| `Jump` | Action | Space | South button (A on Xbox) |
| `Interact` | Action | E | West button (X on Xbox) |
| `Sprint` | Action | Left Shift | Left stick click |
| `MoveForward` | Axis | W and S | Left stick up and down |
| `MoveRight` | Axis | D and A | Left stick left and right |
| `LookX` | Axis | Mouse X | Right stick left and right |
| `LookY` | Axis | Mouse Y | Right stick up and down |

## Reading input in Lua

```lua
function onUpdate(dt)
    local forward = Input.getAxis("MoveForward")   -- -1 to 1

    if Input.isActionPressed("Jump") then          -- only on the frame it was pressed
        print("Jump!")
    end

    if Input.isActionDown("Sprint") then           -- every frame it's held
        speed = runSpeed
    end

    if Input.isActionReleased("Interact") then     -- only on the frame it was let go
        print("Let go of interact")
    end
end
```

For raw mouse input there's `Input.getMouseDelta()`, `Input.getMousePosition()`, `Input.getScrollDelta()` and `Input.isMouseButtonPressed(button)`. `Input.isGamepadConnected()` tells you whether a pad is plugged in. The rest is in the [Lua API reference](lua-api.md#input).

## Capturing the mouse

First person games want the mouse locked and hidden; menus want it free. Switch between the two with:

```lua
UI.setCursorMode(CursorMode.Game)   -- mouse captured, for looking around
UI.setCursorMode(CursorMode.UI)     -- mouse free, for clicking buttons
```

While you're playing in the editor, `Esc` always stops the game and releases the mouse, so you can never get stuck.

## Limitations

- **Gamepads only work in packaged games**, not while playing inside the editor. Test pad controls with **Package & Run**.
- One gamepad (player one) for now, so no local multiplayer yet.
- Players can't rebind controls in your game yet, and there are no input contexts (separate maps for driving and walking, say). You can still switch behaviour in your scripts.
