# Build / runtime audit (Stage 6)

**Overall: 4 / 5 on Windows x64**  
Runtime 4 · Editor 4 · Tests 4 · Other platforms 0–1

Stage 6 is the **most complete shipping slice** in the engine. Do not mark Linux/macOS/consoles as supported because CMake has `if(APPLE)` bundle metadata.

---

## Architecture

| Target | Role |
|--------|------|
| `HephaestusBuildCore` | `BuildService`, `PackageStager`, `ProjectValidator`, `RuntimeLocator`, `BuildManifest` + path/index |
| `HephaestusBuild` (`HephaestusBuildCli`) | Headless CLI |
| `HephaestusEngine` | Qt editor |
| `HephaestusGame` | Generic player (`GameMain` + `RuntimeBootstrap`) |
| `HephaestusLauncher` | Project launcher |
| Qt Build window | `BuildSettingsDialog` → same service |

Sources: `engine/Build/*.cpp`, `engine/BuildCli.cpp`, `engine/Core/RuntimeBootstrap.*`, `engine/Game/GameMain.cpp`.

`export_game.ps1` is a **wrapper**. `SceneExporter` is **retired from CMake** but source files remain.

---

## Packaging behaviour

- Copies `assets/`, `scripts/`, `.engine` runtime content, compiled shaders, builtin meshes/HDRI as configured
- Writes `game.ini` (runtime keys only), manifests, notices, size report, optional ZIP
- Transactional staging (`_building` / previous package preserved)
- Named file-count progress; `--clean-output`; locked-output retry (changelog 6C/D)
- **Does not** compile a per-project native game — ships the generic player + content

**Maturity:** 4

---

## Relocation / dependencies / Lua / shaders

- `RuntimeLocator` distinguishes source checkout vs distributed editor
- Portability tests: relocated package, Lua, HUI, audio, physics, shaders, HDRI, no source-tree fallback
- Windows: Lua DLL, dbghelp minidump, winmm
- VSync from `game.ini`

**Maturity:** 4 Windows portability

---

## Qt frontend

Development/Release presets, startup scene, Validate / Package / Package & Run / Run Last / Clean, cancellation, Problems, logs, `Ctrl+Shift+B`.

**Maturity:** 4

---

## Platform

| Platform | Claim vs evidence |
|----------|-------------------|
| Windows x64 | **Supported** — packaging enum, DLLs, tests |
| Linux | GLFW game *might* compile; Qt viewport surface **not** implemented; audio no-op; no package target |
| macOS | Bundle strings in CMake; MoltenVK comments; **not** a shipping editor |
| Consoles | **Missing** |

See [platform_support.md](platform_support.md).

---

## CI / install / patching

- **No** first-party GitHub Actions (`.github/ISSUE_DRAFTS/03-ci-smoke-tests.md` only)
- No installer/update/patch differential — full package ZIP
- **Maturity:** 0 CI · 0 patching

---

## Tests

`ProjectValidationTests`, `PackageLayoutTests`, `RuntimeLocatorTests`, `BuildServiceTests`, fixture `engine/Tests/Fixtures/PackagePortabilityProject/`.

---

## Recommended next step

CI on existing CTest. Unify `GameMain` renderer. Do not start console SDKs. Linux **game** (GLFW) before Linux **editor**.
