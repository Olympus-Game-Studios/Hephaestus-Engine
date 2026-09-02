# Testing / quality audit

**Overall: 3 / 5 for covered slices; ~0 for runtime simulation**  
Test **count is not quality**. The suite is excellent for **paths, prefabs, and packaging** and absent for **the frame, physics step, and Lua**.

---

## CTest / headless

Option `HEPHAESTUS_BUILD_COMPAT_TESTS` (default ON). Executables registered in root `CMakeLists.txt`.

| Test | What it actually covers |
|------|-------------------------|
| `MaterialCompatibilityTests` | `.hematerial` load/save, graph, GLSL **snippet** gen |
| `PrefabSystemTests` | Prefab IO, scene instance, apply/revert, unpack, missing, blueprint, spawn vars |
| `SceneAuthoringTests` | Snap, AABB pick, parent world, duplicate |
| `InspectorEditTests` | Selection model / batch helpers |
| `AssetPathPolicyTests` | Portable keys |
| `AssetReferenceIndexTests` | Referrers / rewrite |
| `AssetOperationTransactionTests` | Rename/trash/rollback |
| `RecoveryStoreTests` | Editor recovery files |
| `ProjectCreationTests` | Folder layout |
| `EditorLogicSmokeTests` | Editor logic **without Vulkan** |
| `ProjectValidationTests` | Broken refs / malformed content |
| `RuntimeLocatorTests` | Engine root discovery |
| `BuildServiceTests` | Build orchestration |
| `PackageLayoutTests` | Layout, `game.ini`, notices, exclusions |
| `EditorQtSmokeTests` | Opt-in `HEPHAESTUS_BUILD_QT_TESTS`; trivial widget |

Fixture: `engine/Tests/Fixtures/PackagePortabilityProject/`.

---

## Not covered (critical)

| Area | Coverage |
|------|----------|
| Vulkan / `EngineFrameRenderer` / `GameMain` draw | None |
| Jolt step / CharacterMotor / raycast | None |
| Lua bindings / lifecycle / hot reload | None |
| HUI hit-test / display.ui | None |
| Audio decode/play | None |
| ModelImporter glTF/FBX | None |
| Full Qt play mode | None |
| Linux/macOS | None |
| Crash / fuzz | Minidump exists; not tested |

---

## CI

No first-party workflows. Extern repos under `extern/` have their own CI — irrelevant to Hephaestus.

---

## Quality observations

- Prefab and package tests are **behavior** tests, not smoke-only — high value.
- Material tests do **not** prove GPU PBR.
- `EditorLogicSmokeTests` explicitly avoid Vulkan — correct, but leaves the hole.

---

## Recommended next step

1. Windows CI: existing CTest.
2. Headless Lua + physics (no GPU).
3. Optional Vulkan device-create smoke where drivers exist.
4. Do not wait for UI automation to gate `main`.
