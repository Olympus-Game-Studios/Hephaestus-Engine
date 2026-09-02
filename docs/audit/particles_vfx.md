# Particles / VFX audit

**Maturity: Level 0 — Missing**

No `ParticleSystem`, GPU particles, ribbon, decal projector, or Niagara-style graph in `engine/`. Add Actors includes an empty **"FX"** category (`AddActorsPanel.cpp`).

Transparency is cutout-only ([rendering.md](rendering.md)), so even a CPU particle sprite pass would need an alpha path.

**Workarounds:** Animated materials (bake-only — cannot be live Time nodes), Lua-spawned meshes, billboards (light icons are editor-only).

**Necessary for Hephaestus goals?** Nice for shooters/magic; not required for walking-sim. **Do not prioritize** before shared renderer + alpha pass.

**Recommended:** Defer. When started: CPU emitters + textured quads in the transparent pass, then editor. Not a node graph.
