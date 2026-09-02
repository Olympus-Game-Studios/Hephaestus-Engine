# Packaged runtime: Present HephaestusUI draw commands

Description
-----------
Add a packaged-runtime presentation backend for the draw-command list produced by `HephaestusUI::UIRenderer`. Stage 7A1 intentionally converges scene rendering only; `GameMain` continues to update HUI without compositing its commands into the game window.

Acceptance criteria
-------------------
- Present supported HUI rectangles, images, text, opacity, z-order, and clip commands in packaged games.
- Keep HUI presentation separate from `EngineFrameRenderer` scene-rendering responsibilities.
- Preserve the Qt editor's existing `RuntimeUIOverlayWidget` presentation path.
- Verify a packaged `.hui` HUD and interactive menu render at multiple window sizes.
- Document unsupported HUI commands or font/image limitations.

Notes / Implementation pointers
------------------------------
- Treat this as post-Stage-7A1 work; do not reintroduce a second scene renderer.
- Reuse the existing retained UI system and emitted `UIDrawCommand` list.
- Keep GLFW input routing and runtime window presentation in the game host layer.

Suggested labels: `area:ui`, `area:runtime`, `priority:high`
