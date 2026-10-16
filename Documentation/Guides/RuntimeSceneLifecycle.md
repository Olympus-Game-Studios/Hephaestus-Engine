# Runtime Scene Lifecycle 1.0

`Scene.reload()` reconstructs the current runtime scene from the authored scene
asset that started it. This is a gameplay restart primitive, not save/load,
checkpoint, additive-scene, or streaming functionality.

```lua
local accepted = Scene.reload()
if not accepted then
    engine_log("The current scene cannot be restarted")
end
```

The call returns `true` when the host accepts the request and `false` if there
is no active reloadable authored scene, Play Mode is unavailable, or another
reload is already pending. Acceptance means scheduled; reconstruction happens
at the next safe host boundary.

## Scene source identity

The runtime `Scene` retains its authored source asset path separately from its
display name. Filesystem paths are normalized to absolute paths; paths carrying
a URI scheme such as `plugin://` are retained verbatim rather than passed
through filesystem normalization. A scene created only in memory has no source
and cannot be restarted with this API.

Editor Play uses the scene loaded/saved by the editor. Packaged runtime uses the
same resolved startup scene asset it loaded at launch. The current scene object
is reconstructed in place, preserving host references to the scene while
replacing its entities and runtime state.

## Safe point and ordering

Reload is a request, not an immediate mutation. The host processes it after the
requesting Lua callback returns and before another gameplay frame proceeds:

1. Stop dispatching further old-scene script/contact callbacks for that frame.
2. Validate the source asset by deserializing it into a temporary scene.
3. Run entity-script `onDisable` / `onDestroy` while old entities still exist.
4. Destroy C++ script instances, character controllers, rigid bodies, and
   physics contact/trigger caches; clear scene-owned audio and runtime UI.
5. Clear transient UI events and mouse/action edges while preserving held
   physical state and current automation override levels.
6. Deserialize the authored source into the live scene, giving reconstructed
   entities fresh runtime EnTT handles and advancing the scene generation.
7. Rebind character runtime data, prepare physics bodies, reselect the active
   camera, and initialize fresh script instances. New Lua `onAwake` callbacks
   run on the next gameplay script frame.

The old frame is not advanced through physics, animation, late-update, or UI
callbacks after a reload request has been accepted. A request from a physics
contact callback is handled after that dispatch returns; a request from
`onLateUpdate` is handled after late-update returns.

## Lua, references, physics, camera, and UI

All per-entity Lua instances are destroyed and recreated, so entity-local
variables and `onAwake` initialization are fresh and `onDestroy` is delivered
before teardown. Shared Lua modules in `package.loaded` are intentionally not
cleared; module-level state remains process-owned and modules should key any
scene/session registries by lifecycle as appropriate. Serialized entity IDs
are scene-local link keys, not reused as live entity handles. Lua entity,
transform, camera, animator, and character userdata carry scene identity and
generation; references retained by a shared module become invalid after reload.
Automation `EntityRef`s also capture scene identity and generation, so
prior-scene references remain expired even if a handle value is encountered
again.

Jolt rigid bodies and `CharacterVirtual` instances are removed before the new
scene is deserialized. Contact/overlap and supporting-entity bookkeeping is
cleared; recreated controllers start from authored transforms. Lua-managed HUI
screens, direct runtime screens, widget handles, callbacks, and queued UI mouse
events are cleared before new scene scripts build their HUD; newly allocated
screen/widget handles do not reuse the previous scene's numeric IDs. The camera
authority and runtime camera pose are reset from the new scene's active camera
(or the host editor-camera fallback).

Reload clears queued one-shot mouse deltas, scroll deltas, automation action
edges, and queued action changes. Held physical input and explicit current
automation action/axis override levels persist; edge history is synchronized
so a held action does not become a phantom `Pressed` or `Released` event on the
new scene's first frame. Clear automation overrides explicitly when a test or
game needs a neutral input state.

## Execution modes and availability

Reload does not change the simulation execution mode. In manual automation
mode, request processing occurs only during an explicitly stepped gameplay
frame, and wall-clock time does not advance simulation. Editor Stop continues
to restore the editor's existing realtime-on-next-Play behavior.

`Scene.reload()` is a gameplay API and is available in Editor Play and both
packaged Development and Release builds when Lua scripting is enabled. It is
independent of runtime automation/MCP compilation. Runtime automation remains
explicitly enabled for Development and compiled out of Release.

## Failure behavior and scope

An invalid, missing, or malformed source is checked before teardown, so the
existing runtime scene remains available. If the second load fails after
teardown despite successful validation, the host enters a controlled paused
or fatal-error state rather than advancing a partially reconstructed world.
Failure is logged with the source path and loader error.

This API always returns to authored scene defaults. It does not preserve
runtime changes, serialize progress, restore checkpoints, or load multiple
scenes concurrently.
