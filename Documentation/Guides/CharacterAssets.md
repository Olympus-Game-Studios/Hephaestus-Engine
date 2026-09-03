# Character Assets

Stage E product workflow for `.hpcharacter` files. This is not the Character Editor (timeline, clip folders, play preview) and not the gameplay `GetAnimator()` Lua API.

## Import

In the Asset Browser:

1. **Import → Import Character…** (or the folder context menu)
2. Choose a **GLB** (`*.glb` only). Raw `.gltf` with external `.bin` / images is refused; export a GLB from the DCC tool.
3. Review detected skin, bone count, clips, and warnings.
4. Set the character name and destination folder. Duplicate names fail; nothing is overwritten.

Output:

```
<dest>/<Name>.hpcharacter
<dest>/.source/<Name>/mesh.glb
<dest>/.source/<Name>/<Name>Character.lua   # if “Create character controller” is on
```

Owned `.source/` folders are hidden in the Asset Browser (dot-directories). Use the character Inspector to pick the controller script; the picker still lists those Lua files.

**Import Assets…** is unchanged copy-import for meshes, textures, audio, and so on. Do not copy a `.hpcharacter` JSON without its owned source folder.

Default animation:

1. A clip named `Idle` (case-insensitive, including `/Idle`)
2. Else the only clip, if there is exactly one
3. Else unset (`(None)` in the Inspector)

## Place

Drag a `.hpcharacter` into the viewport, or **Spawn in Scene**. The entity is Tag + Transform + CharacterComponent. A skinned MeshRenderer is created at bind time and is **runtime-owned**: it is not serialized, not captured into prefabs, not copied as designer clipboard state, and not shown in the Inspector.

Gameplay AI is a separate `LuaScriptComponent`. Character spawn does not attach one.

## Inspect

The Character Inspector edits instance data:

- Character asset
- Default animation and playback speed (edit mode stays at rest pose)
- Per-slot `.hematerial` overrides (empty override uses the asset slot default)
- Controller enabled / script / scanned `Expose.*` parameters

The first time an asset is assigned (including spawn), asset defaults copy onto the component. Later Inspector edits stick even if the `.hpcharacter` file’s defaults change on disk. Replacing the asset keeps the current clip if that id still exists, materials by slot name, and a controller path the instance had overridden.

Controller scripts are authored here. They do not run until a later animation-scripting stage.

## Reimport

Right-click the `.hpcharacter` → **Reimport Character…** and pick a GLB.

- If the owned `mesh.glb` is still the previous source, the importer compares skeletons (`SkeletonBuilder::Compare`) and signatures. Incompatible sources fail with **no writes**.
- If `mesh.glb` was already overwritten, only the stored signature is compared. A detailed bone diff may be unavailable.

Compatible reimport updates mesh/clip curves and slot names, preserves authored clip ids/folders/loop/speed/trim when `sourceClip` still exists, and preserves material assignments by slot name.

This path is not the static-mesh Reimport pipeline. Owned character GLBs must not be baked as static meshes.

## Prefabs and packaging

Create Prefab From Selection captures Character (and independently LuaScript). Runtime MeshRenderer is omitted. Apply/Revert of Character fields does not clobber LuaScript fields.

Packaging copies `assets/`, including owned `.source/` files the character JSON references.

## Runtime

Editor play mode and `HephaestusGame` both bind through `CharacterRuntime`: load `.hpcharacter`, import the owned GLB, `BindRuntime`, stamp the runtime MeshRenderer, upload a skinned GPU mesh. Missing sources set `lastError` and leave the entity selectable.
