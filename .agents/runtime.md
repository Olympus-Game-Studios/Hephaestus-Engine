# HephaestusUI and packaged runtime

Runtime UI belongs in HephaestusUI and its draw-command/rendering path, not Qt. `engine/UI/HephaestusUI` has a Vulkan backend and is distinct from the Qt editor/launcher UI.

- Keep packaged-runtime dependencies free of Qt unless the target architecture is deliberately changed.
- Reuse existing UI context, element/layout, input-router, asset, and renderer paths. Preserve resize/resolution behavior, input ownership, batching, clipping, font/asset lifetime, and renderer synchronization where relevant.
- Check which behavior is shared with Editor Play and which is hosted differently in `HephaestusGame`. Inspect target sources, compile definitions, bootstrap, and package staging.
- For shared runtime UI contract changes, verify Editor Play and packaged behavior when feasible. An editor-only overlay or successful editor build does not establish packaged UI behavior.
- For lifecycle, scene, resource, scripting, or input changes, consider initialization, repeated startup/teardown, partial failure, stale handles, and cleanup in both relevant hosts.

Read `.agents/architecture.md` for subsystem placement and `.agents/testing.md` for the distinct verification levels and available workflows.
