# Character Controllers

Runtime Lua that drives `.hpcharacter` playback. This is not an animation graph, blend space, IK, retargeting, or Character Editor sandbox.

Gameplay AI stays on `LuaScriptComponent`. The controller stays on `CharacterComponent` (`runController`, `controllerScriptPath`, `controllerVariables`). One Lua VM, two script slots on the same entity.

## Slots

| Slot | Storage | Typical script |
| --- | --- | --- |
| Gameplay | `LuaScriptComponent` | `CultistAI.lua` |
| CharacterController | `CharacterComponent` | `CultistCharacter.lua` |

`this:Call("fn", ...)` invokes **Gameplay only**. It never falls through to the controller.

`this:CallController("fn", ...)` invokes **CharacterController only**. Returns `false` if the controller is missing or the function is absent.

`runController = false` or an empty controller path destroys only the controller instance. Gameplay Lua keeps running.

## Tick order

Play Mode and `HephaestusGame`:

```text
Physics
C++ scripts (editor only)
Gameplay onAwake / onEnable / onUpdate
Controller onAwake / onEnable / onUpdate
CharacterAnimationSystem::Tick
onAnimationFinished (controller, then gameplay)
Gameplay onLateUpdate
Controller onLateUpdate
UI + render
```

First frame awakens gameplay, then the controller, then both `onUpdate`s. Cache `this:GetAnimator()` in controller `onAwake` so AI `CallController` in the same frame’s `onUpdate` can `Play`.

`Play` / `CrossFade` issued from `onUpdate` commit in this frame’s Tick. Commands from `onAnimationFinished` stay pending until the **next** Tick (no recursive pose eval).

Sockets in `onUpdate` are the previous pose. Sockets in `onLateUpdate` are this frame.

## Animator 1.0

`this:GetAnimator()` returns `nil` without `CharacterComponent`. Methods:

- `Play(clipId)` / `CrossFade(clipId, duration)` / `Pause()` / `Resume()` / `Stop()`
- `SetSpeed(speed)` — runtime multiplier, default `1.0`, must be `> 0`. Use `Pause()` to freeze. Rejected values leave the previous speed.
- `IsPlaying()` / `IsPlaying(clipId)`
- `GetCurrentAnimation()` / `GetTime()` / `GetDuration()` / `GetDuration(clipId)`

Clip lookup uses existing `clip.id` (then name / sourceClip / unique suffix). Not `displayName` or folder.

During `CrossFade`, queries use the **destination** clip. The fade source is internal.

Repeated `Play` of the currently playing, unpaused, non-fading clip is a no-op. `Play` of a **finished** clip restarts. Repeated `CrossFade` to the current destination (including mid-fade) is a no-op and does not reset fade time.

`SetSpeed` does not write Inspector `playbackSpeed`. It persists across Play/CrossFade/Stop and resets to `1.0` on `BindRuntime`. Effective rate is `dt * player.speed * playbackSpeed * clip.speed`.

Missing clips return `false`, do not play a fallback, and log once.

## One-shot actions vs locomotion

Locomotion may `CrossFade("Idle"|"Walk"|"Run", blend)` every frame.

Shoot / Hurt / Death must start **once on state entry**. A finished clip `Play`s again if called every frame, so a buggy `Play("Death")` in `onUpdate` will loop death. Do not `Stop()` on death (that resets time to 0); leave `currentClip` as Death.

See `assets/scripts/CultistCharacter.lua` and `assets/scripts/CultistAI.lua`.

## Sockets

`this:GetCharacter()` returns `nil` without `CharacterComponent`.

- `GetSocket(name)` — animated **world** space (`entity world *` skeleton/model socket)
- `GetSocketLocal(name)` — animated **character/model** space (current pose), **not** the raw authored bone-local offset

Both return `{ position = {x,y,z}, rotation = {x,y,z} }` in the same units as `GetTransform()`, or `nil` if the socket is missing.

Add sockets in the Character Editor. Preview gizmos stay bone-local authoring; they are not this Lua API.

## Expose, hot reload, errors

Controller `Expose.*` declarations are scanned in the Character Inspector. Instance values live in `controllerVariables` (not inside the `.hpcharacter` JSON). They seed the controller `_ENV` the same way gameplay `variables` seed AI.

Hot reload uses the same file mtime / source-hash path as gameplay. Reloading one slot does not destroy the other.

A controller syntax error does not stop gameplay Lua. Runtime `onUpdate` failures log once per instance until reload.

## Cultist / YBot

Sources live at `assets/Character/YBot` (FBX). Convert to GLB, then Import Character / Import Animation. Set clip ids to `Idle`, `Walk`, `Run`, `Shoot`, `Hurt`, `Death`. Assign `CultistCharacter.lua` on the Character component and `CultistAI.lua` on LuaScript. Details: [YBot README](../../assets/Character/YBot/README.md).

## Limitations

No animation graphs, state machines, blend spaces, layers, masks, additive, root motion, IK, retargeting, montages, ragdolls, or general animation events. Character Editor preview does not run the controller; Play Mode and `HephaestusGame` do.
