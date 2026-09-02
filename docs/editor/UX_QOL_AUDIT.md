# Hephaestus Editor UX / QOL Audit

Date: 2026-09-01  
Scope: Prioritized polish pass across the Qt editor, launcher, asset browser, specialized editors, and common authoring workflows.  
Approach: Evidence-based audit of existing code, then implement high-impact low-risk fixes while preserving architecture and asset formats.

## Severity Legend

- **Critical friction** — Blocks work, causes data loss, or silently breaks expected behaviour
- **Major usability issue** — Common workflows are awkward, incomplete, or misleading
- **Minor usability issue** — Discoverability / convenience gaps
- **Polish** — Visual or consistency issues
- **Future enhancement** — Valuable but intentionally deferred

---

## Problems Discovered

### Critical friction

| Issue | Why it matters | Status |
|-------|----------------|--------|
| Closing main window did not reliably prompt for unsaved scene work | Easy to lose edits | Fixed in this pass |
| New Lua scripts used `OnStart`/`OnUpdate` instead of `onAwake`/`onUpdate` | Scripts silently do nothing | Fixed in this pass |
| Play-mode scene camera applied degrees→radians twice | Camera orientation wrong at play start | Fixed in this pass |
| Redo advertised in Edit menu but not implemented (scene + prefab) | Undo history incomplete / misleading | Fixed in this pass |

### Major usability issues

| Issue | Why it matters | Status |
|-------|----------------|--------|
| Asset Browser Import button was a status-only stub | Designers cannot import files from UI | Fixed in this pass |
| Toolbar New Folder button was a stub while context menu worked | Inconsistent / dead control | Fixed in this pass |
| No Duplicate / Copy Path / Prefab filter in Asset Browser | Expected file-browser actions missing | Fixed in this pass |
| Editor New Project was a manual-JSON stub | Split launcher/editor workflow | Fixed in this pass |
| Launcher “Open last project on startup” saved but never opened | Preference lied | Fixed in this pass |
| Outliner Delete / F2 / Esc / Ctrl+A shortcuts missing | Keyboard workflow incomplete | Fixed in this pass |
| Transform spinboxes flooded undo stack | Undo unusable after scrubbing | Fixed in this pass |
| Gizmo drag / rename / light-env edits often not undoable | Edits irreversible | Fixed in this pass |
| Misleading viewport Lit/Unlit/Wireframe and Settings menu stubs | Fake controls erode trust | Fixed / clarified in this pass |
| Visibility button in Details was a no-op | Affordance with no effect | Fixed in this pass |
| Material node Rename was a stub | Context menu lied | Fixed in this pass |
| HUI hierarchy lacked context menu | Hierarchy editing harder than canvas | Fixed in this pass |

### Minor usability issues

| Issue | Why it matters | Status |
|-------|----------------|--------|
| Breadcrumbs display-only | Navigation requires tree/up button | Fixed in this pass |
| Folder tree selection not synced to current path | Easy to lose place | Fixed in this pass |
| Created prefab/UI files did not auto-open | Extra click vs materials | Fixed in this pass |
| Multi-select not reflected in Details header | Ambiguous what is being edited | Fixed in this pass |
| Outliner empty scene had no guidance | Blank hierarchy | Fixed in this pass |
| Dock/window layout not remembered | Workspace reset every launch | Fixed in this pass |
| Undo/Redo actions never disabled when empty | No feedback | Fixed in this pass |
| Default project creation lacked `assets/scripts` and non-fatal HDRI | Fragile create path | Fixed in this pass |

### Polish / Future enhancement (deferred)

| Issue | Severity | Why deferred |
|-------|----------|--------------|
| Asset move with reference rewriting | Future enhancement | Needs scene/prefab/material reference updater |
| Recursive project-wide asset search | Future enhancement | Needs indexer / AssetManager API |
| Filesystem undo / trash for assets | Future enhancement | Cross-session safety and open-editor paths |
| Perspective / ortho / lit renderer modes | Future enhancement | Requires `EngineFrameRenderer` API work |
| Dedicated texture viewer | Future enhancement | New editor surface |
| Full reimport pipeline UI | Future enhancement | DerivedDataCache / ReimportManager wiring beyond copy-import |
| Qt UI automated tests | Future enhancement | No Qt Test harness yet |
| Remove legacy C++ ScriptInspector | Future enhancement | Compatibility with older scenes |
| Portable build without engine source tree | Future enhancement | Packaging architecture |

---

## Features Added / Changed in This Pass

See implementation sections below and `CHANGELOG.md` `[Unreleased]` entries dated 2026-09-01 (UX polish).

### Implemented categories

1. **Correctness & safety** — close prompt, Lua template, camera units, no-op controls
2. **Scene workflow** — redo stacks, coalesced undo, shortcuts, empty/multi-select states, layout persistence, visibility toggle
3. **Asset Browser** — import, duplicate, copy path, filters, breadcrumbs, folder sync, create auto-open
4. **Specialized editors** — material rename + undo UI, HUI hierarchy context menu
5. **Project / launcher** — New Project in editor, open-last-project, create defaults

---

## Validation Notes

- Automated: `MaterialCompatibilityTests`, `PrefabSystemTests` — both passed (2026-09-01)
- Build: `HephaestusEngine`, `HephaestusLauncher`, `HephaestusGame` Release — succeeded
- Manual: `TestProject` opened via `--project` and entered render loop without crash
- Manual read-only: Boomer-Shooter `default.hpscene` loaded, meshes resolved, framed, and rendered (absolute Capsule.glb path worked on this machine; still a portability risk)
- Known limit: no Qt widget automated tests; renderer Lit/Unlit/Wireframe still deferred

---

## Intentionally Deferred Larger Improvements

The Phase 1 polish pass deferred the items below. **Phase 2 implementation status is recorded in the next section** — do not treat this list as current backlog without reading it.

1. Reference-aware asset move/rename across all serialized formats
2. Renderer view modes (Lit/Unlit/Wireframe/Ortho) as real pipelines
3. Toast notification center (status bar remains primary non-modal feedback)
4. Autosave / recovery for crashes
5. Template project gallery in launcher
6. Recursive project-wide asset search / indexer
7. Filesystem undo/trash for asset ops
8. Dedicated texture viewer

---

## Validated Next UX/QOL Phase (2026-09-01)

Code-verified planning and Phase 2 implementation notes. Authored asset identity remains **path-based** (`assets/…`, `.engine/…`, `scripts/…`, `builtin://…`). A global GUID migration is **not** planned; importer `assetId` stays provenance-only in `.hephaestus/assetdb.json`.

### Confirmed findings (pre-Phase-2)

- `AssetManager` already recursively indexes assets; Asset Browser search was folder-local only.
- `ReimportManager` / `AssetDatabase` / DDC compiled but were unwired; DDC is still not a runtime load path.
- No autosave/recovery existed; `engine.general.autosaveInterval` was schema-only.
- Viewport was perspective + Lit-only (misleading controls clarified in Phase 1).
- Texture open was a not-implemented stub; no Qt Test harness existed.
- Absolute in-project paths could persist (material browse, prefab overrides); Boomer-Shooter Capsule absolute override was a known portability risk.

### Priority ranking (Phase 2)

**Priority A (foundations — implemented in this phase):** portable `AssetPathPolicy`; project-wide Asset Browser search; `AssetReferenceIndex`; scene `RecoveryStore` + editor wiring; safe rename/move/trash via `AssetOperationService`; headless tests for path/index/ops/recovery.

**Priority B (implemented in this phase):** manual reimport (`EditorAssetPipeline`); dependency inspector dialog; `EditorNotificationService`; Lit/Unlit/Wireframe; orthographic standard views; `TextureViewerDialog`; unified `ProjectCreationService`; recent-project prune/remove/reveal; optional `HEPHAESTUS_BUILD_QT_TESTS`.

**Priority C (still deferred):** automatic FileWatcher reimport; DDC-first runtime loading; filesystem Ctrl+Z; large notification center; template gallery / sample gameplay packs; advanced renderer diagnostics (normals/depth/etc.); project-wide GUID migration.

### Phase sequence delivered

1. **2A** — `AssetPathPolicy`, recursive search, reference index, scene recovery snapshots  
2. **2B** — dependency UI, open-document registry, transactional rename/move, trash/restore, notifications  
3. **2C** — manual reimport wiring (no auto-watch)  
4. **2D** — viewport modes, ortho views, texture viewer  
5. **2E** — project creation service, recent-projects management, expanded CTest + optional Qt Test  

### Validation gates

- Headless: `AssetPathPolicyTests`, `AssetReferenceIndexTests`, `AssetOperationTransactionTests`, `RecoveryStoreTests`, `ProjectCreationTests`, `EditorLogicSmokeTests`, plus existing material/prefab tests  
- Manual: relocate/clone project resolves portable keys; reimport preserves references; recovery restore does not overwrite real `.hpscene` until Save; Wireframe disabled when unsupported  

### Still deferred after Phase 2

- Auto-reimport / FileWatcher default-on  
- Multi-document recovery for every prefab/material/HUI surface (scene recovery is live; registry exists for extension)  
- Template gallery and First/Third-person starters  
- Advanced viewport diagnostics  
- Global GUID authored references  

---

## Architecture Guardrails Observed

- Qt remains the only editor UI touched
- Reused `EditorContext`, `EditorUndoManager`, `EditorDocument`, `EditorAssetActions`, `PrefabEditorManager`, launcher create patterns via ToolsCommon helpers
- No renderer architecture rewrite, no asset format breakage, no prefab schema changes
- Phase 2 extends Assets / QtEditor / ToolsCommon incrementally; no competing parallel asset databases for authored content

---

## Implementation Outcomes (this pass)

| Area | Result |
|------|--------|
| Close prompt + layout persistence | Implemented |
| Lua template lifecycle names | Fixed |
| Play camera radian bug | Fixed |
| Scene/Prefab redo | Implemented |
| Coalesced transform + gizmo/light/env undo | Implemented |
| Outliner shortcuts / empty / multi-select hint | Implemented |
| Visibility toggle | Wired to MeshRenderer |
| Asset Import/Duplicate/Copy Path/filters/breadcrumbs | Implemented |
| Material rename + Undo/Redo buttons | Implemented |
| HUI hierarchy context menu | Implemented |
| Editor New Project + launcher open-last | Implemented |
| CreateProject HDRI non-fatal + extra folders | Implemented |

## Implementation Outcomes (Phase 2)

| Area | Result |
|------|--------|
| Portable `AssetPathPolicy` + save-time normalization | Implemented |
| Project-wide Asset Browser search + Show in Folder | Implemented |
| `AssetReferenceIndex` + Find References dialog | Implemented |
| Scene recovery autosave + crash prompt | Implemented |
| Safe rename/move + trash/restore | Implemented |
| Manual reimport pipeline UI | Implemented |
| Texture viewer | Implemented |
| Lit/Unlit/Wireframe + ortho views | Implemented |
| `ProjectCreationService` + recent-projects management | Implemented |
| Headless tests + optional Qt Test harness | Implemented |

## Implementation Outcomes (Phase 3)

| Area | Result |
|------|--------|
| AABB viewport scene query + surface placement | Implemented |
| Placement preview, click/Shift-repeat/Escape cancel | Implemented |
| Translate/rotate/scale snapping + drag bypass | Implemented |
| Hierarchy- and prefab-aware duplicate + Alt-drag | Implemented |
| World-preserving editor reparenting | Implemented |
| Center/free translate handle + gizmo cancel | Implemented |
| Marquee, prefab-root pick, viewport Delete/context menu | Implemented |
| User-local hide/isolate/lock + camera bookmarks | Implemented |
| Outliner expansion persistence and authoring filters | Implemented |
| Align/distribute, drop, surface snap, group, replace | Implemented |
| Frame All, audio placement, scene statistics, selection cycling | Implemented |
| Headless scene-authoring regression tests | Implemented |

Phase 3 editor-only state is stored below `.hephaestus/editor/scenes/` and is
excluded from source control. Runtime `MeshRendererComponent.visible` remains
gameplay data and is not changed by editor hide/isolate commands.
