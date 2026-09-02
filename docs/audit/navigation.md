# Navigation audit

**Maturity: Level 0 — Missing**

No navmesh, Recast/Detour, grid pathfinding, or AI steering types exist under `engine/`.

This **prevents engine-provided AI navigation**. It does **not** block:

- FPS players (`CharacterMotor` + Lua)
- Simple chasing via raycasts / distance in Lua
- Scripted moving platforms (once physics world-transforms work)

**Is an engine navmesh necessary?** Only if AI walking around authored geometry is a common title need. Implement **after** physics queries and (for humanoids) animation clips. Do not build a navmesh editor first.

**Recommended:** Stay Level 0 until a project needs NPC pathing. Then runtime bake + debug draw, then editor volumes.
