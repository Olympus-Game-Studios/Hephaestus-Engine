# Character Editor

Designer-facing authoring and preview for `.hpcharacter` assets. This is not a gameplay sandbox and does not add animation graphs, blend spaces, IK, or retargeting. Runtime `this:GetAnimator()` / `this:GetCharacter()` live in Play Mode and `HephaestusGame`; see [CharacterControllers.md](./CharacterControllers.md).

Open a character by double-clicking it in the Asset Browser, or **Open**. Empty New Character files show **Import Character Source** until a mesh GLB or FBX is assigned.

## Preview

The window hosts an isolated preview scene (environment, light, character). Playback ticks only the preview character. Play Mode is not started.

Use **Frame** / **Frame All** to fit the camera around **all mesh parts**. **Show Skeleton** draws the one canonical skeleton; **Bone Names** is reserved for a later overlay. Transform gizmos apply to the selected socket. Animation playback deforms every part from the same pose.

## Animations

The **Animations** dock is tabbed with Inspector on the right. It lists clips and folders (they are not in the Character tree). **Import Animation** and **Create Folder** sit at the top of that tab. Select a clip to play, pause, stop, or scrub the timeline; clip fields appear in Inspector.

Inspector fields:

- **Name** — clip id used by playback, `Play()`, and controllers. Rename with F2, double-click, the list context menu, or this field. Duplicate names are refused. Existing scene/prefab instance clip references are not rewritten.
- **Folder** — organization only
- **Source Clip** — the take name inside the GLB/FBX (often `mixamo.com`); this is not the authored clip id
- Loop, playback speed, trim start/end
- **Set as Default** — asset default for *new* placements

**Import Animation** copies a GLB or FBX into `.source/<Character>/` (never overwriting `mesh.glb` / `mesh.fbx`) and adds clip rows. The import dialog has an editable **Name** per take. Generic Mixamo takes (`mixamo.com`, `Take 001`) default to the source file name (`YBot-Walking.fbx` → `YBot-Walking`); `Take 001` is unchecked when a `mixamo.com` take is present. Duplicate take names from different files keep unique clip ids if the authored names collide (`Walk`, `Walk_2`, …). Incompatible skeletons are refused with no metadata writes. Undo removes the clip rows; the copied source stays on disk so Redo can restore metadata.

Duplicate creates a new unique clip id. Delete removes the row only. Both are available from the Animations list context menu.

## Sockets

Add a socket from the Sockets folder or a bone. Parent bone, local position, and local rotation (degrees in the Inspector, radians on disk matching Transform) are authored here. Changing the parent bone keeps the numeric local TRS (it does not recompute world or reset to identity).

Socket markers follow the current preview pose. The selected socket uses the shared editor gizmo.

## Materials and controller

Material slots are grouped by mesh part when a character has more than one skinned mesh. Slot names stay unique per part (`Body / Skin`, `Clothes / Fabric`). Opening a slot still uses the Material Graph editor. The controller section stores enabled + script path on the asset. Exposed Lua values are authored on scene instances, not in the Character Editor. Preview does not run the controller; Play Mode and `HephaestusGame` do.

## Reimport and save

**Reimport Character** uses the Stage E skeleton-compatibility check. Incompatible sources fail with no writes. Compatible reimport refreshes mesh/clip curves and preserves authored clip ids, folders, loop/speed/trim, and material assignments by mesh-part + slot name. Extra compatible skinned meshes are kept; they are not discarded.

Save writes `.hpcharacter`. Live scene instances reload through `CharacterRuntime` without overwriting instance default animation, playback speed, controller enabled/path/variables, or material overrides. Asset defaults apply to **new placements** only.

Trash of an open character prompts to save/close the editor window first. Rename/move retargets the open window.

## Related

- [CharacterAssets.md](./CharacterAssets.md) — import, place, inspect, reimport, and package workflow
- [CharacterControllers.md](./CharacterControllers.md) — Play Mode / HephaestusGame controller Lua and Animator 1.0
