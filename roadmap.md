Developer To-Do (Current Focus)
--------------------------------
- Finalize render graph lighting parity and verify HDR output across reference scenes.
- Complete FBX/GLTF import bridge with validation suite and error surfacing in editor.
- Implement transform gizmo undo/redo history stack with visual diff preview.
- Wire up convex collider authoring UI and fixed timestep integrator instrumentation.
- Stand up nightly smoke test workflow in CI with crash dump collection.

Hephaestus Engine Roadmap
=========================

Vision
------
- Deliver a modern, data-driven rendering and gameplay engine that powers Olympus Game Studios titles and scales to external teams.
- Maintain a tight iteration loop through hot-reloadable assets, an integrated editor, and robust tooling.
- Prioritize stability and developer ergonomics without sacrificing high-end graphics features.
- *Feature Highlights:* clustered lighting renderer, ECS-driven scene management, modular plugin surfaces, real-time profiling HUD.

Guiding Principles
------------------
- Incrementally ship vertical slices to validate workflow end-to-end.
- Keep ECS-centric architecture consistent across runtime, editor, and tooling.
- Automate validation (CI, tests, linting) before expanding feature breadth.
- Document features alongside implementation to ensure onboarding remains lightweight.

Milestones
----------

### 2025 Q4 – Alpha Foundations
- Lock down rendering core: finalize render graph implementation, lighting pass parity with reference scenes, HDR pipeline, and ImGui overlay updates.
- Ship stable asset ingestion path: mesh/material importers, texture compression presets, hot reload for shaders and materials.
- Complete editor selection & gizmo tooling round-trip (translation, rotation, scaling) with undo/redo history.
- Integrate core physics hooks for rigid bodies and colliders, ensuring deterministic stepping.
- Establish automated nightly builds and smoke tests on Windows; capture crash dumps.
- Publish developer quickstart and ECS overview updates in `Documentation/`.
- *Feature Highlights:*
  - Rendering: tile-based light culling, tone mapping pipeline, debug lighting visualizers.
  - Editor: transform gizmos with axis constraints, selection outline pass, history stack inspector.
  - Assets: FBX/GLTF import bridge, GPU texture atlas generator, shader reload without editor restart.
  - Physics: convex collider authoring, fixed timestep integrator, collision event bus.

### 2026 Q1 – Beta Content Workflow
- Expand scene serialization (prefabs, hierarchies, scene variants) with versioning support.
- Introduce scripted gameplay layer (C# or Lua bindings) with component access sandboxing.
- Implement asset dependency graph and background streaming for large scenes.
- Add lighting authoring tools in editor: light probes, shadow config, post-processing stack controls.
- Harden physics: broadphase optimizations, trigger volumes, physics debugger overlay.
- Stand up CI coverage for unit/integration tests; add linting and static analysis gates.
- *Feature Highlights:*
  - Scene: prefab override diffing, scene diff viewer, drag-and-drop hierarchy parenting.
  - Scripting: hot-reload scripts, component bindings generator, sandboxed console REPL.
  - Assets: streaming texture cache, dependency heatmap view, automated LOD baking.
  - Lighting: interactive probe placement, area light authoring, filmic color grading presets.
  - Physics: collision layer matrix editor, trigger callbacks, gizmo visualization for colliders.

### 2026 Q2 – Feature Complete (RC)
- Optimize GPU workload: batching, draw call consolidation, async compute passes, and shader permutation pruning.
- Deliver VFX pipeline: particle system authoring, curve editors, GPU particles prototype.
- Finalize audio integration path (spatial audio, mixer, basic DSP effects) and editor tooling.
- Implement save/load runtime persistence and project packaging workflow.
- Complete localization pipeline: resource tables, font atlas generation, text rendering validation.
- Conduct performance profiling sessions; document optimization playbook.
- *Feature Highlights:*
  - Rendering: automatic instancing, GPU-driven occlusion culling, RenderDoc capture integration.
  - VFX: node-based particle graph, curve-driven emitters, flipbook sprite support.
  - Audio: middleware bridge (Wwise/FMOD), spatial attenuation debugger, real-time mixer panel.
  - Persistence: binary scene snapshotter, patch-based save upgrades, deployment packager CLI.
  - Localization: string table editor, right-to-left text preview, locale fallback rules.
  - Tooling: frame capture analyzer, performance budget dashboards, automated regression detection.

### 2026 H2 – 1.0 Launch
- Ship cross-platform rendering path (Windows + Linux) with platform abstraction validated.
- Provide extensibility SDK: plugin templates, scripting API docs, and sample integrations.
- Reach feature parity in editor vs runtime (prefab overrides, animation timeline, navmesh baking).
- Finalize user-facing documentation site, tutorial videos, and example projects.
- Execute release hardening: regression sweeps, compatibility testing, security review, license audit.
- *Feature Highlights:*
  - Platform: Vulkan backend parity, Linux packaging scripts, cross-platform input stack.
  - Extensibility: plugin project generator, API reference site, sample gameplay plugin.
  - Editor: animation timeline scrubbing, navmesh visualization, prefab diff/merge tool.
  - Docs: interactive tutorial scenes, searchable documentation portal, automated changelog publishing.
  - QA: compatibility matrix dashboard, crash telemetry pipeline, build notarization workflow.

### Post-1.0 Horizons
- Console platform bring-up (Xbox, PlayStation) with certification tooling.
- Ray-tracing pipeline exploration (DXR/Vulkan RT) and advanced GI solutions.
- Networked multiplayer framework (rollback-friendly replication, dedicated server tooling).
- Procedural world tools (open-world streaming, terrain authoring, foliage systems).
- Marketplace ecosystem for community assets and extensions.
- *Feature Highlights:*
  - Consoles: platform abstraction layers, TRC compliance harness, controller remapping UI.
  - Ray Tracing: hybrid ray/path tracing prototype, denoising suite, ray query debug views.
  - Multiplayer: deterministic ECS replication, rollback netcode simulator, dedicated server orchestration CLI.
  - Procedural: heightmap sculpting, spline-based road systems, foliage scatter rules editor.
  - Marketplace: asset packaging standard, in-editor marketplace browser, licensing metadata support.

Cross-Track Initiatives
-----------------------
- **Rendering:** Deferred + forward+ hybrid pipeline, PBR material library, volumetrics, temporal AA upgrades.
- **Editor UX:** Dockable panels, customizable workspaces, UI theme polish, accessibility compliance.
- **Content Pipeline:** Continuous import daemons, version control integration (Perforce/Git LFS), dependency audits.
- **Tooling & Automation:** Deterministic builds via CMake presets, asset validation bots, crash telemetry dashboards.
- **QA & Testing:** Headless test harness, ECS regression scenarios, graphics screenshot comparisons, performance budget tracking.
- **Community & Support:** Contributor guidelines, issue triage rotations, roadmap transparency updates each quarter.

Dependencies & Risks
--------------------
- Rendering roadmap contingent on Vulkan backend stability; allocate buffer for API churn.
- Third-party library updates (ImGui, EnTT, GLFW) tracked quarterly; pin versions after RC.
- Tooling depends on Windows build farm availability; secure hardware before scaling CI.
- Scripting language decision blocks gameplay tooling; resolve by end of 2025 Q4.

Review Cadence
--------------
- Revisit roadmap at the end of each milestone; capture adjustments and retrospective learnings.
- Maintain a living KPI dashboard (build health, perf regressions, bug backlog) to inform scope decisions.
- Communicate updates via monthly internal demos and shared changelog in `Documentation/`.
