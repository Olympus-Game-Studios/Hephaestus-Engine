# Hephaestus Engine

## Overview

**Hephaestus Engine** is a custom, in-development game engine built for developers who want direct ownership of their stack. It combines a modern **C++20** core, a **Vulkan** renderer, and a **data-oriented scene model** so teams can iterate quickly without fighting a black-box toolchain.

The project’s purpose is straightforward: provide a **lean, understandable foundation** for 3D games and tools—one where performance, clarity, and flexibility matter more than shipping every feature a AAA studio expects on day one.

What sets Hephaestus apart is not a single checkbox feature, but how it is put together:

- **Custom-built** — You are working with engine code you can read, extend, and profile end to end.
- **Performance-minded** — Vulkan and a focused runtime path keep the graphics stack explicit and controllable.
- **Flexible workflows** — Scenes, materials, meshes, and UI lean on readable asset formats and scripting where it helps.
- **Modern design** — ECS-backed scenes, structured editor tooling, and a roadmap aimed at professional-quality tools without the weight of a megascale engine.

Hephaestus is **actively developed**. It is a strong base for experimentation and small-to-medium projects, not a drop-in replacement for mature commercial engines.

---

## Core Technologies

| Technology | Role |
|------------|------|
| **C++ (C++20)** | Core language for the runtime, renderer integration, scene systems, and editor glue. Keeps hot paths fast and dependencies explicit. |
| **Vulkan** | Primary graphics API: swapchains, pipelines, materials, lighting, shadow passes, and sky rendering are driven through Vulkan with engine-level abstractions. |
| **Qt** | **Editor UI**: docking panels, viewport hosting, asset browsing, inspectors, and native Qt widgets—purpose-built for desktop tooling, not a web-based shell. |
| **Lua** | **Primary scripting language** for gameplay and UI control: attach scripts to entities, respond to lifecycle events, and drive **HephaestusUI** at runtime without recompiling the engine. |
| **Jolt Physics** | Rigid-body simulation and physics stepping integrated with the scene and components, aimed at stable, modern physics behavior. |

Supporting pieces (such as **EnTT** for ECS storage, **GLFW** for windowing on some paths, and asset helpers) exist to support these pillars—they are implementation details behind the same architectural goals.

---

## Architecture

Hephaestus is organized as **modular subsystems** with clear responsibilities. At a high level:

### Rendering system

The renderer owns Vulkan device lifetime, frame resources, material binding, lighting data, optional **directional shadow mapping** (including cascade-related data paths), and **skybox / environment** rendering (including HDRI-driven backgrounds where configured). It is shared between editor viewports and packaged runtimes so what you see while authoring is close to what players get.

### Entity Component System (ECS)

Scenes are built from **entities** composed of **components** (tags, transforms, mesh renderers, materials, cameras, scripts, physics-related data, and more). The ECS layer uses **EnTT** under the hood for efficient queries and iteration while presenting a small, engine-specific API to gameplay and editor code.

### Scripting system (Lua)

**Lua** is the supported way to script behavior: lifecycle hooks (such as awake, update, destroy) and APIs for entities, input, UI, and engine services. C++ remains the place for engine internals and performance-critical systems; Lua is for iteration speed and designer-friendly logic.

### Asset system

Projects use **file-based assets**: meshes, textures, **PBR material definitions**, scenes, blueprints, **`.hui`** UI documents, and scripts. The editor surfaces these through browsers and inspectors; export tooling can gather referenced assets for distributable builds.

### Editor vs runtime separation

The **editor** is a Qt application that hosts the viewport, manipulates the ECS scene, and edits assets. The **runtime** loads the same kinds of data but strips away authoring chrome—packaged builds can boot straight into play mode. That split keeps iteration in the editor while preserving a smaller player-facing footprint.

---

## Editor

The Hephaestus editor is built with **Qt**: dockable panels, native controls, and a layout familiar to anyone who has used modern DCC or game tools. The experience is intentionally **Unreal-adjacent** in spirit—**outliner**, **details**, **viewport toggles**, and **project-centric workflows**—without claiming parity with a finished commercial editor.

Highlights:

- **Viewport** — Real-time 3D view with controls for navigation and display options (including toggles such as shadows and skybox where implemented).
- **Asset browser** — Project file discovery, filtering, and creation shortcuts for supported asset types (including UI documents).
- **Scene hierarchy / Outliner** — Create, rename, reparent, and select entities; mirrors the ECS scene structure.
- **Details / inspectors** — Edit transforms, components, and referenced assets per selection.
- **Material authoring** — PBR-oriented editing with support for both property-driven workflows and **node-based material graphs** (where enabled in the build), plus live preview against the scene.

The editor is **evolving**: new panels, shortcuts, and polish land regularly; expect rough edges compared to decade-old commercial tools.

---

## UI System (HephaestusUI)

**HephaestusUI** is the engine’s **custom in-game UI stack**. Layout and styling are described in **`.hui`** files—structured documents (JSON-based) that define widget trees, properties, anchors, and optional **controller** Lua scripts.

Goals:

- **Separation of layout and logic** — Designers (or programmers) define structure in `.hui`; behavior lives in Lua or shared code.
- **Runtime control** — Scripts load and unload screens through a small, managed API. The recommended high-level surface is **`display.ui(path)`** and **`destroy.ui(target)`**, which handle lifecycle, duplicate paths, and cleanup when leaving play mode. Lower-level APIs remain available for advanced use.
- **Tooling direction** — The project includes **visual editing directions** (including hierarchy- and canvas-oriented tooling) aimed eventually at something closer to **Figma** or **Unreal UMG** in workflow—**iterative**, not finished.

HephaestusUI is **game-facing** (menus, HUDs, overlays). It complements—not replaces—the **Qt editor chrome**.

---

## Rendering Features

The following reflects the **current** implementation direction; it is **not** an exhaustive AAA feature list.

- **PBR materials** — Metallic–roughness style workflow with albedo, emissive, roughness, metallic, and ambient occlusion channels; texture bindings for albedo, normals, roughness, AO, and related options as exposed in the material system.
- **Lighting** — Directional lighting with engine support for **shadow mapping** (including cascade-related data for directional shadows), plus **point** and **spot** lights with bounded counts suitable for real-time use.
- **Environment** — **Skybox** rendering with support for **HDRI-based** environments (including intensity control) and **solid-color** environment fallbacks when no HDRI is used.

Exact limits (light counts, shadow resolution, platform quirks) depend on project settings and hardware; treat them as **project-tunable implementation details**, not marketing guarantees.

---

## Current Strengths

- **Rapid prototyping** — Lua scripts, `.hui` UI, and fast editor iteration shorten the loop from idea to playable.
- **Custom workflows** — Full source access makes it practical to tailor import steps, tools, and renderer behavior to a team’s pipeline.
- **Full control** — No opaque binary core: you can trace behavior from script bindings through to Vulkan.
- **Lightweight footprint** — Compared to large commercial engines, the codebase and runtime are modest—easier to reason about for small teams and solo developers.
- **Coherent stack** — Vulkan + ECS + Jolt + Qt editor + Lua is a deliberate combination for a modern, extensible foundation.

---

## Limitations

Hephaestus is **not** a finished AAA platform. A candid summary:

- **Active development** — APIs, file formats, and editor UX change; upgrades may require project maintenance.
- **Feature gap vs major engines** — No claim to matching Unreal/Unity ecosystems (marketplace scale, animation suites, full audio middleware, multiplayer services, etc.).
- **Tooling maturity** — Visual UI authoring, content pipelines, and debugging aids are **improving** but still behind established products.
- **Documentation and onboarding** — Growing, but contributors and users should expect to read code and engine docs alongside guides.
- **Platform and QA surface** — Best-effort cross-platform direction; not every combination is continuously validated at shipping quality.

These limitations are normal for a **young engine**; they are listed so teams can set expectations honestly.

---

## Use Cases

Hephaestus fits well when:

- You are building **indie or experimental 3D projects** where owning the engine stack is a plus.
- You want **teaching or research** into Vulkan, ECS, and engine architecture with real integration code.
- You need **custom pipelines** or internal tools that sit next to a renderer you can modify.
- You accept **some DIY** in exchange for transparency and control.

It is a weaker default when you need **turnkey multiplayer**, **film-grade animation**, or **immediate access to massive asset ecosystems** without integration work.

---

## Future Roadmap

Directional themes (not commitments on schedule):

- **Richer UI tools** — Stronger visual editing for `.hui`, better preview, and faster iteration for UI designers.
- **Visual scripting / node-based gameplay** — Exploration of designer-friendly logic that complements Lua.
- **Engine optimizations** — Profiling-driven improvements in rendering, loading, and editor responsiveness.
- **Expanded rendering** — Additional lighting models, post-processing, and material features as the Vulkan path matures.
- **Packaging and collaboration** — Smoother export, project templates, and contributor onboarding.

Roadmap items shift with contributor bandwidth and project needs.

---

## Philosophy

Hephaestus is built around a few non-negotiable ideas:

1. **Developer freedom** — The engine should get out of the way when you need to change core behavior; sensible defaults, but no mandatory “only one way” for every problem.
2. **Performance** — Choices like Vulkan and data-oriented scenes reflect a bias toward measurable speed and explicit resource use—not abstraction for its own sake.
3. **Clean design** — Subsystems stay separated (graphics, scene, scripting, UI, editor) so the graph of dependencies stays understandable as the codebase grows.
4. **Extensibility and community** — Scripting, readable assets, and open development invite **mods, plugins, and fork-friendly workflows** over time. Longer term, the architecture aims to support **creator- and mod-friendly** distribution (tooling that could resemble a dedicated mod “forge” in spirit) without over-promising specific products before they ship.

If that philosophy matches how you like to build games, Hephaestus is worth watching—and worth building with, with eyes open about its current stage.

---

*This document describes the Hephaestus Engine as of ongoing development. For the latest technical detail, see the repository’s `Documentation/` tree and `CHANGELOG.md`.*
