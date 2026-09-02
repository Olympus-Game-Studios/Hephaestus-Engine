# Assets / import pipeline audit

**Overall: 3 / 5 (Usable path-based pipeline)**  
Runtime 3 · Editor 4 · Tests 4

Authored identity is **portable paths**, not global GUIDs. Prefab `guid` is prefab-asset identity only. This is an explicit design choice (`docs/editor/UX_QOL_AUDIT.md`), not an unfinished migration.

---

## Core pieces

| Piece | Path | Maturity | Notes |
|-------|------|--------:|-------|
| AssetManager | `engine/Assets/AssetManager.*` | 3 | Browse/list/search by path |
| AssetPathPolicy | `AssetPathPolicy.*` | 4 | `assets/`, `.engine/`, `scripts/`, `builtin://` |
| AssetReferenceIndex | `AssetReferenceIndex.*` | 4 | Forward + reverse refs; rewrite JSON fields |
| AssetDatabase | `AssetDatabase.*` | 3 | `.hephaestus/assetdb.json`; importer `assetId` provenance |
| Importers | `AssetImporters.*`, `ModelImporter.*` | 3 | Texture/mesh/audio |
| DDC | `DerivedDataCache.*` | 2 | Store API; **not** runtime load path |
| ReimportManager | `ReimportManager.*` | 3 | Manual jobs + worker |
| FileWatcher | `FileWatcher.*` | 2 | Implemented; **not started** by default |
| AssetOperationService | `AssetOperationService.*` | 4 | Rename/move/trash + rewrite + rollback |
| OpenDocumentRegistry | `OpenDocumentRegistry.*` | 3 | Dirty/open docs for safe ops |
| RecoveryStore | `RecoveryStore.*` | 4 | Editor crash recovery — **not** game saves |
| EditorAssetPipeline | `EditorAssetPipeline.*` | 3 | Owns DB/DDC/reimport; manual |

---

## Import formats

| Type | Extensions | Reality |
|------|------------|---------|
| Textures | png, jpg, jpeg, tga, exr, hdr | stb_image; HDR for sky |
| Meshes | obj, fbx, gltf, glb | Custom importer. **Binary FBX unsupported** (ASCII only). glTF static mesh + submeshes. **No skins/clips.** |
| Audio | wav, ogg (validate) | Playback is WAV/MP3 MCI — ogg import ≠ ogg play |
| Prefab/material/HUI | json assets | Not “imported”; authored |

No GPU texture compression / basis / ktx pipeline.

---

## Runtime loading vs editor metadata

- Runtime loads files from packaged `assets/` / `.engine/` / `shaders/` using portable keys.
- `.hephaestus/` (assetdb, trash, recovery, transactions) is **editor-only** and excluded from game packages (Stage 6).
- DDC is editor derived data, not a required runtime cache.

**Maturity:** 4 distinction after Stage 6

---

## Reimport / source tracking / DDC

- Copy-import registers sources; Asset Browser Reimport / Reimport All Changed for mesh/texture/audio
- Auto-watch deferred (changelog + pipeline comments)
- **Maturity:** 3 manual · 1 auto

---

## Browser / dependencies / trash / missing

- Project-wide search, filters, breadcrumbs, Find References, `AssetDependencyDialog`
- Trash under `.hephaestus/trash/` with manifest; transaction rollback best-effort
- Missing prefabs stub; missing textures log and dummy slot
- **Maturity:** 4 ops

---

## Package inclusion

`PackageStager` copies `assets/`, `scripts/`, `.engine`, shaders. Validator flags broken refs (`ProjectValidationTests`).

---

## Tests

`AssetPathPolicyTests`, `AssetReferenceIndexTests`, `AssetOperationTransactionTests`, `RecoveryStoreTests`.  
No decode goldens for glTF/FBX/audio.

---

## Portability

Path policy + rewrite is the portability story. Absolute paths were a known Boomer-Shooter risk; saves now normalize (changelog).

---

## Recommended next step

Keep path identity. Enable FileWatcher when artists need it. Use DDC as load path when import cost shows up. Document ASCII-FBX + static-only glTF.
