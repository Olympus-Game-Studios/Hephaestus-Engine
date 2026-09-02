# Performance / profiling audit

**Overall: 1 / 5 (Foundation overlay only)**  
Developers **cannot** currently diagnose GPU, script, or physics cost in a structured way.

---

## What exists

| Signal | Evidence | Maturity |
|--------|----------|--------:|
| FPS in editor status/overlay | `EditorMainWindow` | 2 |
| Draw calls / meshes drawn / skipped | `FrameStats` in `EngineFrameRenderer` | 2 |
| Process RAM (Windows Working Set) | Editor overlay | 1 |
| EngineLog | `engine/Core/EngineLog.*` | 3 logging, not profiling |
| Crash minidump | `RuntimeBootstrap` Windows | 2 |

---

## What does not exist

- GPU timestamps / query pools
- Hierarchical CPU profiler
- Physics / Lua / import timers
- Capture/export (RenderDoc integration not in engine)
- Allocator tracking / leak tooling
- Automated frame benchmarks
- Script profiler (README unchecked)

`ROADMAP.md` “real-time profiling HUD” is aspirational. The overlay is a thin stats strip.

---

## Large-scene behaviour

No streaming, instancing, or occlusion. Cost grows with entity mesh draws. Snapshot undo duplicates JSON. Material texture slots have a logged cap.

Unknown at scale — **insufficient evidence** beyond architecture.

---

## Recommended next step

Scoped CPU timers (render, physics, Lua) in the existing overlay. GPU timestamps after renderer unification. Do not build a Tracy-level product first.
