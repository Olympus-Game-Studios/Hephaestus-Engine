# Hephaestus Engine — Agent Instructions

This repository is a C++20 game engine with a Qt-based editor (`HephaestusEngine`) and a packaged game runtime (`HephaestusGame`). Qt editor UI and runtime HephaestusUI have different owners and dependencies.

`.agents/` contains the authoritative detailed engineering rules. Before implementation, explicitly read `.agents/general.md` and `.agents/git.md`, then read every relevant topic file below. Do not assume that a link, listing, or optional agent skill has loaded the rule contents. When a task crosses subsystem/host boundaries, read all applicable files.

| File | Consult for |
| --- | --- |
| `.agents/general.md` | Every implementation: reuse, simplicity, scope, and correctness |
| `.agents/architecture.md` | Subsystem placement, dependencies, shared services, editor/runtime boundaries |
| `.agents/cpp.md` | C/C++ source, headers, CMake-adjacent implementation, lifetime and performance |
| `.agents/rendering.md` | Vulkan frame rendering, swapchain, GPU resources, and rendering lifecycle |
| `.agents/editor.md` | Qt editor and launcher UI |
| `.agents/runtime.md` | HephaestusUI, game runtime, lifecycle, and packaging |
| `.agents/plugins.md` | Native plugin API, lifecycle, and plugin content |
| `.agents/testing.md` | Build/test selection and reporting evidence accurately |
| `.agents/changelog.md` | Editing or deciding whether to update `CHANGELOG.md` |
| `.agents/documentation.md` | User/developer docs and public APIs |
| `.agents/README.md` | Agent support, loading behavior, adapters, and maintaining this system |

## Always applicable

- Inspect current implementation and nearby tests before relying on plans, audits, or older documentation as evidence of behavior.
- Make the smallest complete, maintainable change; reuse existing systems and avoid speculative abstractions or unrelated refactors. Do not trade correctness, safety, API compatibility, or measured performance for brevity.
- Build and run relevant existing checks when feasible. Clearly distinguish compilation, unit/integration tests, Editor Play, and packaged Development/Release verification; never claim a check that was not performed.
- Preserve uncommitted work. Keep changes focused. Do not perform destructive Git operations, commit, or push without explicit authorization.

## Rule precedence

Follow higher-priority system/developer requirements and explicit user requirements first, then correctness and safety, then verified source/build constraints, then the applicable `.agents/` rules, and finally nearby local conventions. Rules refine one another; if applicable project guidance genuinely conflicts, do not silently discard correctness or user intent—resolve the conflict or explain the ambiguity.
