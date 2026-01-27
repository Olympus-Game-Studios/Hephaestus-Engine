# Hephaestus Engine Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Fixed

#### Multi-Material Mesh Rendering in Game Runtime (2026-01-28)

**Problem**: Meshes with multiple material indices (multiple submeshes/sections) were not 
rendering correctly in the exported/standalone game runtime. Only the first material would 
show, or sections would fallback to a default material, even though they rendered correctly 
in the editor.

**Root Cause**: 
1. When loading meshes at runtime, the `MeshRendererComponent::submeshCount` was not being 
   updated from the loaded mesh data, causing a mismatch between the mesh's actual submesh 
   count and the component's tracked count.
2. The runtime was properly storing submesh data in `MeshGPU::submeshes` but the renderer 
   component wasn't synchronized.

**Changes Made**:

1. **GameMain.cpp - Mesh Loading** (`LoadMeshToGPU` integration):
   - After loading a mesh from file, now properly updates `renderer.submeshCount` from the 
     loaded `MeshGPU::submeshes` array.
   - Added debug logging showing mesh name, submesh count, and material slot count.
   - Added warning when submesh count doesn't match material slot count.
   - Same synchronization added for cached mesh reuse path.

2. **GameMain.cpp - Debug Logging** (Debug builds only):
   - Added comprehensive multi-material debug info section before the main render loop.
   - Logs each entity's mesh name, handle, submesh count, and material slot count.
   - For each submesh, logs the material slot index, index range, material path, and 
     whether the material was successfully loaded.
   - Warns when there are more submeshes than material slots.

3. **GameMain.cpp - Runtime Asset Path Resolution**:
    - `NormalizeAssetPath()` now resolves relative material/texture paths under 
       the runtime `assets/` directory if the direct runtime-root path doesn't exist.
    - Fixes cases where editor-stored paths like `materials/rock.hpmat` were 
       not found at runtime (expected under `assets/materials/...`).

4. **SceneExporter.cpp - Export Material Slot Assets**:
    - Export now includes material assets referenced by mesh slot overrides
       (`MeshRendererComponent::appliedMaterialPaths`), ensuring they are copied
       into the runtime `assets/` folder.

3. **Test Scene**:
   - Added `assets/scenes/MultiMaterialRuntimeTest.hpscene` to test:
     - Single material meshes (should render normally)
     - Override material tests (material should apply correctly)
     - Missing/fallback material tests (should use entity's MaterialComponent)

**New Debug Warnings** (in debug builds):
- "Mesh 'X' has N submeshes but M material paths assigned. Some sections may use fallback materials."
- Per-submesh material resolution status: "loaded", "NOT FOUND", "default", or "slot missing"

**Acceptance Criteria**:
1. ✅ Multi-material meshes render identically in editor and runtime
2. ✅ Each submesh uses its assigned material slot
3. ✅ If a material slot is missing, only that section falls back to default
4. ✅ Blueprint/scene overrides for per-slot materials work in runtime
5. ✅ Single-material meshes continue to work without regression

---
