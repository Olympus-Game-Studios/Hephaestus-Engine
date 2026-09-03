# Asset Reimporting (Phase 1)

## Overview
Hephaestus now supports safe, transactional asset reimporting for:
- Textures: `.png`, `.jpg`, `.jpeg`, `.tga`, `.exr`, `.hdr`
- Static meshes: `.fbx`, `.gltf`, `.glb`, `.obj`
- Audio: `.wav`, `.ogg`

Reimporting is pipeline-driven and does **not** invalidate existing assets if the import fails.

## Asset Identity
Each asset is tracked by a stable `assetId` in the AssetDB, stored in:

- `.hephaestus/assetdb.json`

Assets are identified by `assetId` (not filenames), and the AssetDB stores:
- `sourcePath`
- `assetType`
- `importerId` + `importerVersion`
- `importSettingsHash`
- `derivedDataKey`
- `fingerprint` (size, mtime, optional content hash)
- `lastImportTime` + `lastImportResult`

## Transactional Reimport (No Corruption)
Reimport uses an atomic swap:
1. Import to staging (derived data keyed by fingerprint)
2. Validate output (mesh geometry, texture dimensions, audio header)
3. Update AssetDB in a single commit
4. Replace runtime resources on the render thread

If any step fails, the existing asset remains active.

## Auto Reimport
A background file watcher monitors AssetDB `sourcePath` entries.
When a change is detected, it:
- Debounces events (500ms)
- Waits for file stability
- Queues a low-priority reimport job

## Manual Reimport
From the Asset Browser:
- Right click → **Reimport**
- **Reimport All Changed** button

Character assets (`.hpcharacter`) use a separate path: right-click → **Reimport Character…** and pick a GLB. That pipeline validates skeleton compatibility before writing. Do not use static-mesh Reimport on owned `.source/<Stem>/mesh.glb` files.

See [CharacterAssets.md](./CharacterAssets.md).

## Adding New Importers
Implement `IAssetImporter`:
- `CanImport()`
- `Import()`

Register it in `ReimportManager` during startup.

## Test Plan (Manual)
1. **Texture hot reload**
   - Import a PNG, overwrite the file, verify the material updates.
2. **Mesh slot preservation**
   - Import an FBX with 2 slots, overwrite with same slot names, verify materials persist.
3. **Failure safety**
   - Replace a mesh with a corrupt file, ensure existing mesh remains active.
4. **Debounce**
   - Copy a large FBX into place (multiple events), verify only one reimport occurs.
5. **Replace-in-place**
   - Overwrite a file without renaming; verify reimport triggers.
