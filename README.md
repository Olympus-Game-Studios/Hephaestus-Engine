# Hephaestus Engine

Hephaestus is a game engine built from the ground up in C++20, with a Vulkan renderer and a Qt editor. The idea behind it is simple: put creativity before complexity. It should be approachable on your first day and still get out of the way once you know what you're doing.

It's made by Olympus Game Studios and is **source available**: you can read it, build it and make games with it. See [License](#license).

> **Just want to make games?** You don't need this repository. Get Hephaestus on Steam and read the [user guide](docs/user-guide/README.md) (also on the website at <https://hephaestus.olympusgames.dev/doc/hephaestus>). This README is for people building the engine from source or working on it.

## What's in it

- **Editor** with Default, Artist and Programmer workspace modes, a launcher, command search, undo for nearly everything and autosave recovery
- **Rendering**: Vulkan, PBR materials, directional, point and spot lights with soft shadows, HDRI skies with ambient lighting, probe volume global illumination (baked, or ray traced on GPUs with ray queries) and ray traced shadows
- **Scenes** built from EnTT entities and components, prefabs with overrides, Apply, Revert and Unpack
- **Materials** authored in a node based Material Editor
- **Physics** with Jolt: rigid bodies, mesh colliders, triggers, collision layers and a character controller
- **Characters**: skinned GLB and FBX import, a Character Editor, clip playback and crossfades
- **Lua 5.4 scripting** with Inspector variables, hot reload, a built in code editor and a debugger, plus scripting languages added by plugins
- **Game UI** (HephaestusUI) with a visual UI Editor and `.hui` layouts
- **Input** actions and axes for keyboard, mouse and gamepad
- **Plugins**: native editor plugins and scripting language plugins
- **Packaging** to a standalone Windows game (and a macOS `.app`) from the editor or the `HephaestusBuild` command line tool

What's missing or unfinished is listed honestly in [What's not there yet](docs/user-guide/whats-not-there-yet.md).

## Repository layout

| Path | What's there |
| --- | --- |
| `engine/` | All engine and editor source. `Gfx` (Vulkan renderer), `Scene` (ECS, prefabs, serialization), `Physics`, `Scripting`, `Animation`, `Audio`, `Input`, `Assets`, `Build` (packaging), `Game` (packaged game player), `UI/QtEditor` (editor), `UI/Launcher`, `UI/HephaestusUI` (game UI), `PluginAPI`, `Automation`, `Tests` |
| `shaders/` | GLSL sources, compiled to SPIR-V by the build |
| `Plugins/` | First party plugins: Primitive Tools (example), Hephaestus MCP (AI automation) and the FPS plugin (development only, not shipped) |
| `assets/`, `.engine/` | Engine content: built in meshes, skies, fonts, test content |
| `editor/` | Editor fonts, icons and styles |
| `extern/` | Third party code (Jolt, GLFW and ufbx as submodules; EnTT, VMA, Lua, nlohmann/json, cpp-httplib and stb vendored) |
| `tools/` | Release packaging scripts and the packaged game portability test |
| `docs/` | The user guide, plugin docs, code notes, plans and audits |
| `Documentation/` | Technical reference for engine developers (see [Documentation](#documentation)) |
| `Branding/` | Logos and editor design sources |

## Building from source

### Windows

You need:

- **Visual Studio 2022** with the Desktop development with C++ workload
- **CMake 3.24** or newer
- **Vulkan SDK** (the build uses `glslc` from it to compile shaders)
- **Qt 6** for MSVC 64 bit (releases use 6.10.2). CMake looks under `C:\Qt\6.*\msvc*`; pass `-DQt6_DIR=<Qt>/lib/cmake/Qt6` if yours is elsewhere
- **Git**, cloning with submodules

Lua is bundled in `extern/lua`, so there's nothing extra to install for it.

```powershell
git clone --recurse-submodules https://github.com/Olympus-Game-Studios/Hephaestus-Engine.git
cd Hephaestus-Engine
cmake --preset windows-release
cmake --build --preset windows-release
```

Run those from a Developer PowerShell for Visual Studio. The binaries land in `build\windows-release\Release\`: `HephaestusLauncher.exe`, `HephaestusEngine.exe` (the editor), `HephaestusGame.exe` (the game player) and `tools\HephaestusBuild.exe`.

`build.bat` is a shortcut for a plain `build\` folder: `build.bat`, `build.bat debug` or `build.bat --clean --reconfigure`.

If you already cloned without submodules, run `git submodule update --init --recursive`.

### macOS

You need the Xcode command line tools, CMake, Ninja, the Vulkan SDK (with MoltenVK) and Qt 6 for macOS.

```bash
cmake --preset macos-release
cmake --build --preset macos-release
```

This builds `HephaestusEngine.app`, `HephaestusLauncher.app` and `HephaestusGame.app` in `build/macos-release/Release/`. MoltenVK has no ray queries, so ray traced shadows and probe lighting use their fallbacks. Sound doesn't play on macOS yet. Pass `-DQt6_DIR=...` if CMake can't find Qt.

### Linux

There's a `linux-release` preset, but Linux isn't a supported platform yet and the editor viewport doesn't work there.

### CMake options

| Option | Default | Does |
| --- | --- | --- |
| `HEPHAESTUS_BUILD_EDITOR` | ON | Build the editor and launcher |
| `HEPHAESTUS_BUILD_GAME` | ON | Build the game player |
| `HEPHAESTUS_USE_QT` | ON | Build the Qt editor UI |
| `HEPHAESTUS_GAME_SCRIPTING` | ON | Start Lua and plugin language scripts in the game player |
| `HEPHAESTUS_RUNTIME_AUTOMATION` | OFF | Compile MCP automation into the game player |
| `HEPHAESTUS_BUILD_QT_TESTS` | OFF | Build the optional Qt Test editor harness |

### Tests

The engine has headless CTest suites for assets, prefabs, scenes, packaging, physics layers, characters, input, UI, scripting languages and more:

```powershell
ctest --test-dir build\windows-release -C Release --output-on-failure
```

## Running

Start `HephaestusLauncher` to create or open a project, which then opens the editor. The editor can also be started directly and asked to open a project from **File > Open Project...**.

The editor writes its log to `%LOCALAPPDATA%\Hephaestus\Logs\hephaestus.log` on Windows and to `~/Library/Logs/` on macOS. The Console panel shows the same log live.

## Making a release (Steam)

Each platform has one script that builds Release from a clean checkout in its own build folder and writes the folder that's uploaded to Steam. Both pin Qt 6.10.2, leave out the FPS plugin, `assets/tests`, tests and debug symbols, and include `LICENSE.md` and `THIRD_PARTY_NOTICES.md`.

Windows, from a Developer PowerShell for Visual Studio:

```powershell
tools\create_release_package.ps1 [-OutputPath dist\windows] [-Qt6Dir C:/Qt/6.10.2/msvc2022_64/lib/cmake/Qt6]
```

It builds in `build\steam-windows`, deploys Qt with `windeployqt` and writes `dist\windows`. The Visual C++ runtime isn't included because Steam installs it.

macOS:

```bash
tools/create_release_package_macos.sh [--qt6-dir <dir>] [--no-dmg] [--sign "Developer ID Application: Name (TEAMID)" --notary-profile hephaestus-notary]
```

It builds in `build/steam-macos` and writes `dist/macos/Hephaestus Engine.app` (which opens the launcher) and a `.dmg`. Use `--no-dmg` when the output is the Steam content. Without `--sign` it's ad hoc signed for local use. `./notarise-build.sh` signs and notarizes with the first Developer ID certificate in your keychain and the `hephaestus-notary` profile (override with `HEPHAESTUS_SIGN_IDENTITY` and `HEPHAESTUS_NOTARY_PROFILE`).

`tools/test_package_portability.ps1` packages a test project, moves it away from the source tree and checks that it still runs.

## Documentation

| Where | For |
| --- | --- |
| [`docs/user-guide/`](docs/user-guide/README.md) | Using Hephaestus: the guide for people making games |
| [`Documentation/`](Documentation/README.md) | Technical reference: file formats, runtime contracts, the HUI format and Lua binding details |
| [`docs/plugins/`](docs/plugins/plugin-system-1.0.md) | Writing native and scripting language plugins |
| [`docs/code-notes/hephaestus.md`](docs/code-notes/hephaestus.md) | Implementation notes, keyed by source file and symbol |
| [`docs/plans/`](docs/plans) and [`docs/audit/`](docs/audit) | Design plans and dated capability audits (snapshots, not current documentation) |
| [`CHANGELOG.md`](CHANGELOG.md) | What changed, newest first |
| [`ROADMAP.md`](ROADMAP.md) | Where things are heading |

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for coding guidelines and the pull request checklist, and [`AGENTS.md`](AGENTS.md) for the engineering rules (they apply to people as much as AI tools).

## License

Hephaestus Engine is source available, not open source. See [`LICENSE.md`](LICENSE.md) for the terms, and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for the licences of the third party components it includes.
