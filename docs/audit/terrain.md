# Terrain audit

**Maturity: Level 0 — Missing**

No heightmap, clipmaps, splatmap, or terrain collider types in `engine/`. Worlds are **static meshes** + Jolt mesh colliders.

**Workaround:** DCC terrain exported as glTF/OBJ. Mesh collision exists for statics.

**Necessary?** Open-world / survival — yes later. Small interiors — no.

**Recommended:** Do not prioritize. Mesh colliders + modular kits cover current scope. A terrain system would depend on streaming, LOD, and physics mesh tools that are still thin.
