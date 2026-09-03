# Character Editor

Designer-facing authoring and preview for `.hpcharacter` assets. This is not a gameplay sandbox and does not add animation graphs, blend spaces, IK, retargeting, or Lua `GetAnimator` APIs.

Open a character by double-clicking it in the Asset Browser, or **Open**. Empty New Character files show **Import Character Source** until a mesh GLB is assigned.

## Preview

The window hosts an isolated preview scene (environment, light, character). Playback ticks only the preview character. Play Mode is not started.

Use **Frame** / **Frame All** to fit the camera. **Show Skeleton** draws bone lines; **Bone Names** is reserved for a later overlay. Transform gizmos apply to the selected socket.

## Animations

The Character tree lists clips and folders. Select a clip to play, pause, stop, or scrub the timeline.

Inspector fields:

- **Name** and **Folder** — organization only
- **Clip ID** — read-only. Scene and prefab references use this id. Stage F does not rewrite clip ids.
- Loop, playback speed, trim start/end
- **Set as Default** — asset default for *new* placements

**Import Animation** copies a GLB into `.source/<Character>/` (never overwriting `mesh.glb`) and adds clip rows. Incompatible skeletons are refused with no metadata writes. Undo removes the clip rows; the copied GLB stays on disk so Redo can restore metadata.

Duplicate creates a new unique clip id. Delete removes the row only.

## Sockets

Add a socket from the Sockets folder or a bone. Parent bone, local position, and local rotation (degrees in the Inspector, radians on disk matching Transform) are authored here. Changing the parent bone keeps the numeric local TRS (it does not recompute world or reset to identity).

Socket markers follow the current preview pose. The selected socket uses the shared editor gizmo.

## Materials and controller

Material slots open the Material Graph editor. The controller section stores enabled + script path on the asset. Exposed Lua values are authored on scene instances, not in the Character Editor. Preview does not run the controller.

## Reimport and save

**Reimport Character** uses the Stage E skeleton-compatibility check. Incompatible sources fail with no writes. Compatible reimport refreshes mesh/clip curves and preserves authored clip ids, folders, loop/speed/trim, and material assignments by slot name.

Save writes `.hpcharacter`. Live scene instances reload through `CharacterRuntime` without overwriting instance default animation, playback speed, controller enabled/path/variables, or material overrides. Asset defaults apply to **new placements** only.

Trash of an open character prompts to save/close the editor window first. Rename/move retargets the open window.

## Related

- [CharacterAssets.md](./CharacterAssets.md) — import, place, inspect, reimport, and package workflow
