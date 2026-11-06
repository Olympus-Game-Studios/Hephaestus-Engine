---
title: Characters and animation
description: Importing rigged characters, the Character Editor, adding animations and playing them from Lua.
order: 9
---

# Characters and animation

Anything that moves with a skeleton (a hero, an enemy, a dog) is a **character** in Hephaestus. A character is a `.hpcharacter` asset that bundles a skinned mesh, its skeleton, its animations, its materials and optionally a little Lua controller that decides which animation plays when.

## Importing a character

In the Content Browser, use **Import Character...** (right click a folder or use the import menu) and pick a `.glb` or `.fbx` file with a skeleton. Mixamo characters work nicely.

The import dialog shows you what it found (bones, mesh parts, animation clips, material slots and any warnings) before anything is written:

- **FBX** files are converted to Hephaestus's axes automatically. Leave the conversion on **Auto** unless your character comes in lying down or the wrong size.
- **Strip bone namespace** tidies names like `mixamorig:Hips` into `Hips`.
- A character made of several skinned meshes (a body and separate armour, say) comes in as one character, as long as they share a skeleton. Don't delete the extra parts in Blender first.
- Give it a name and a folder. Hephaestus won't overwrite an existing character with the same name.

A `.gltf` file with separate `.bin` and image files won't import as a character. Export a single `.glb` instead, or use FBX.

If an animation called **Idle** exists, it becomes the default.

## The Character Editor

Double click a character to open the **Character Editor**. It has its own preview viewport and:

- An **animation library**. **Import Animation** adds clips from other GLB or FBX files (extra Mixamo animations work fine). Name them as you import, and rename them later with `F2`. Organise them into folders.
- A **timeline** for scrubbing through a clip.
- A **skeleton overlay**.
- **Sockets**: named attachment points on bones, for things like a weapon in the right hand. Move them with the gizmo.
- **Materials** grouped by mesh part.
- The character's **controller** script.

`Ctrl+S` saves and the usual undo and redo work.

## Placing a character

Drag the `.hpcharacter` into the viewport. In the Inspector's **Character** card you can choose the **Default Animation**, the **Playback Speed**, override materials per slot for just this copy and set the controller script.

The character stays in its rest pose in the editor. Animations play when you press Play.

## Controllers and gameplay scripts

There are two kinds of script on a character, and keeping them apart keeps things tidy:

- **The controller** (set on the Character card, switched on with **Run Controller**) is purely about animation: blending from idle to walk based on speed, playing a hit reaction, that kind of thing. Its Inspector variables show under **Controller Parameters**.
- **Gameplay scripts** are ordinary Lua Script components that decide what the character actually does: chasing the player, taking damage, dying.

They talk to each other with `this:CallController("functionName", ...)`, and both can reach the animator:

```lua
function onAwake()
    local animator = this:GetAnimator()
    animator:Play("Idle")
end

function startWalking()
    this:GetAnimator():CrossFade("Walk", 0.25)
end

function onAnimationFinished(clipId)
    if clipId == "Attack" then
        this:GetAnimator():CrossFade("Idle", 0.2)
    end
end
```

The animator also has `Pause`, `Resume`, `Stop`, `SetSpeed`, `IsPlaying`, `GetCurrentAnimation`, `GetTime` and `GetDuration`, and characters have `GetSocket` and `GetSocketLocal` for attaching things to sockets. See the [Lua API reference](lua-api.md#characters-and-animation).

For movement, pair the character with a [Character Controller](physics.md#character-controller) component.

## What's not there yet

Animation today is clip playback with crossfades between two clips. There's no animation graph or state machine editor, no blend spaces, no root motion and no inverse kinematics yet. Your controller script is the state machine for now, which honestly works pretty well for a lot of games.
