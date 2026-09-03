# Asset Importing

How source files become engine assets. Character product import is documented in [CharacterAssets.md](./CharacterAssets.md). Reimport behaviour is in [AssetReimporting.md](./AssetReimporting.md).

## Static meshes

**Import Assets…** copies files into the project. Supported mesh formats:

- `.obj`
- `.gltf` / `.glb` (node TRS is baked into vertices)
- `.fbx` via ufbx (UVs, normals, submeshes, baked node transforms)

A skinned FBX copied through Import Assets is imported as a **static** mesh (skinning ignored) unless you accept the prompt to **Import as Character**.

FBX space conversion for static meshes uses Auto (Blender → Adjust Transforms; Maya/Mixamo/Max/unknown → Modify Geometry).

Materials: FBX 1.0 preserves material **slot identity** only. Assign `.hematerial` assets in the editor. Embedded textures are extracted only when the format is known (PNG/JPEG/BMP/TGA by magic or extension).

## Characters

**Import Character…** (not Import Assets) for skinned GLB or FBX:

- Owned copy is `mesh.glb` or `mesh.fbx` under `.source/<Name>/`
- Canonical bones are deforming skin joints; helper/Null ancestors are folded into rest pose
- Multiple skinned meshes in one file import as one character when they share a compatible skeleton
- Animation stacks are baked to Linear T/R/S (`ufbx_bake_anim`)
- Bone names are preserved (`mixamorig:` stays unless Strip bone namespace is on)

Raw `.gltf` with external `.bin` is not a character source. Export GLB, or use FBX.

## Clone / build

FBX import requires the `extern/ufbx` submodule (pinned v0.23.0):

```
git clone --recurse-submodules
# or
git submodule update --init --recursive
```
