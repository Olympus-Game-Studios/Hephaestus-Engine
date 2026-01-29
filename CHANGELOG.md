# Hephaestus Engine Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added

#### Multi-Selection Support in Outliner (2026-01-29)

**Feature**: Users can now select multiple actors in the World Outliner and viewport using 
standard multi-selection controls. This enables batch operations on multiple entities.

**Key Features**:

1. **Ctrl+Click (Cmd+Click on macOS)**: Toggle individual selection - add or remove an 
   entity from the current selection without affecting other selected entities.

2. **Shift+Click**: Range selection in the Outliner - select all entities between the 
   last clicked entity and the current entity in the visible tree order.

3. **Viewport Multi-Selection**: Ctrl+Click on objects in the 3D viewport to add/remove 
   them from the selection.

4. **Synchronized Selection**: Selection state is synchronized between the Outliner, 
   viewport, and Details panel. All selected entities show selection outlines.

5. **Multi-Entity Transform Editing**: When multiple entities are selected, transform 
   changes in the Details panel (position, rotation, scale) apply to all selected entities.

6. **Gizmo Multi-Transform**: Using the viewport gizmo to move/rotate/scale affects all 
   selected entities simultaneously, maintaining their relative positions.

**Changes Made**:

1. **HtmlEditorUIHost.cpp**:
   - Added `scene.range_select` handler for Shift+Click range selection
   - Handles comma-separated entity IDs for batch selection

2. **outliner.html**:
   - Added tracking of visible entity order for range selection
   - Implemented Shift+Click range selection logic
   - Enhanced Ctrl+Click to properly toggle selection
   - Updated status bar to show selection feedback

3. **main.cpp**:
   - Viewport picking now supports Ctrl+Click for multi-selection toggle
   - Gizmo transformations apply to all selected entities using delta transformation
   - Selection outline rendering updated to draw outlines for all selected entities
   - ImGui WorldOutliner updated with Ctrl+Click support

4. **Scene.cpp** (existing infrastructure):
   - `GetSelectedEntities()` returns all selected entities
   - `AddSelectedEntity()` adds to multi-selection
   - `ToggleSelectedEntity()` toggles individual selection
   - `ClearSelection()` clears all selections

**Backward Compatibility**: Single-selection behavior remains unchanged. Clicking without 
modifiers still replaces the current selection with the clicked entity.

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

5. **GameMain.cpp - World-Aligned Material Graph Support (Runtime)**:
    - Runtime material loader now detects `WorldAlignedTexture` nodes in the
       material graph and enables world-aligned UVs, texture binding, and
       world texture scale without requiring the editor-side graph evaluator.

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
