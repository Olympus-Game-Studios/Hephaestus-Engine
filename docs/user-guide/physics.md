---
title: Physics
description: Rigid bodies, collision shapes, mesh colliders, layers, triggers and the character controller.
order: 8
---

# Physics

Hephaestus uses **Jolt Physics**, the same physics engine behind some very big games, so things fall, stack, slide and collide properly. Physics runs when you press Play and stops when you stop. Nothing moves in the editor itself.

## Making something physical

Add a **Rigid Body** component. It brings a **Physics Shape** along with it, because a body needs a shape to collide with.

Pick the body's **Motion**:

- **Dynamic**: gravity and collisions move it. Crates, barrels, falling debris.
- **Static**: never moves. Floors, walls, rocks. Static bodies are cheap, so use them for everything that stays put.
- **Kinematic**: moved by your scripts (or animation), not by physics, but still pushes dynamic things out of the way. Moving platforms and doors.

Other useful settings: **Mass**, **Friction**, **Restitution** (bounciness), **Linear Damping** and **Angular Damping** (how quickly it slows down), **Use Gravity** and **Freeze Rotation** (stops it tipping over).

The presets at the top (**Preset: Ground/Env** and **Preset: Player**) set sensible values in one click.

## Shapes

A **Physics Shape** is a simple invisible shape that stands in for the object: **Box**, **Sphere**, **Capsule** or **Cylinder**, with an **Offset** if it needs nudging. Simple shapes are fast and stable. Use them wherever you can get away with it.

### Mesh Collider

When a simple shape won't do (terrain, rocks, a staircase you modelled), add a **Mesh Collider**. Tick **Complex Collision** to collide against the actual triangles of the mesh. It's more expensive, so it's best for things that don't move. A Mesh Collider without a Rigid Body acts as a static collider.

## Triggers

Tick **Trigger** on a Rigid Body (or use the **Trigger Volume** option) to turn it into a trigger. Things pass straight through it, but your scripts are told when they enter and leave. Doors that open as you approach, checkpoints, damage zones: that's triggers.

Scripts on either entity receive `onTriggerEnter`, `onTriggerStay` and `onTriggerExit`, and solid bodies get `onCollisionEnter`, `onCollisionStay` and `onCollisionExit`. See [Scripting with Lua](scripting-lua.md#reacting-to-physics).

One quirk worth knowing: a trigger doesn't detect static, non trigger colliders. It's meant for detecting things that move.

## Collision layers

Layers decide what collides with what. Set them up in **Edit > Settings > Project Settings > Physics**, which has a list of named layers and a grid (the collision matrix) where you tick which pairs collide.

New projects start with four layers: **Default**, **World**, **Character** and **Trigger**. Choose a body's layer from **Layer** on its Rigid Body card. Changes to the matrix apply the next time you press Play.

Raycasts in scripts can be filtered by layer too, so a bullet can ignore triggers, or an AI's line of sight can only test against the World.

## Character Controller

For players and NPCs that walk around, don't use a dynamic rigid body; use a **Character Controller**. It's built for exactly this job and handles all the fiddly stuff:

- Walking with **Acceleration**, **Deceleration**, **Air Acceleration** and **Air Control**
- **Jump Speed**, **Gravity Scale** and **Max Fall Speed**
- **Coyote Time**: a jump still works for this many seconds after walking off a ledge, and **Jump Buffer**: a jump pressed this many seconds before landing happens on landing. Both make jumping feel responsive; at 0 they are off.
- Slopes up to **Max Slope Angle** and stairs up to **Step Height**
- Sticking to the ground on the way down slopes (**Ground Snap Distance**)
- Standing on and riding moving platforms
- Crouching (**Crouched Height**), with a check that stops you standing up into a ceiling
- Pushing dynamic objects (**Push Strength**)

Its shape is a capsule set by **Height** and **Radius**. While the game's playing, the **Runtime** section shows live state like Grounded, Ground State and Velocity, which is great for debugging.

You drive it from Lua with the `Character` table:

```lua
function onUpdate(dt)
    local forward = Input.getAxis("MoveForward")
    local right = Input.getAxis("MoveRight")
    Character.move(right, forward)
    if Input.isActionPressed("Jump") and Character.isGrounded() then
        Character.jump()
    end
end
```

`Character.move` takes a world space direction, so for movement relative to where the player is looking, rotate the input by the camera's yaw first. The full list of `Character` functions is in the [Lua API reference](lua-api.md#character). Coyote time and jump buffering aren't built in, so add them in your script if your game wants them.

## Good to know

- Physics steps at a fixed 60 times a second. Moving bodies and characters are drawn smoothly between steps, so it still looks right at high frame rates.
- Tick **Debug Draw Bounds** on a Mesh Collider to see its box as a green outline in the viewport. There's no matching view for Rigid Body shapes yet.
- There are no joints, ragdolls or vehicles yet, and no overlap or shape cast queries; raycasts are the only physics query for now.
