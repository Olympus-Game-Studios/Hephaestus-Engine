# Save system audit

**Maturity: Level 0 — Missing (game saves)**

Do not confuse:

| System | What it is |
|--------|------------|
| `RecoveryStore` | **Editor** crash recovery under `.hephaestus/recovery/` |
| `PlayModeManager` snapshot | **Editor** play/stop restore |
| `SceneSerializer` | **Authoring** `.hpscene` |
| Game save/load | **Does not exist** |

There is no slot API, checkpoint component, or Lua `Save.*`.

`ROADMAP.md` marks “save/load runtime persistence” complete (Nov 2025). That claim tracks **scene serialize + packaging**, not player saves. See [DOCUMENTATION_GAPS.md](DOCUMENTATION_GAPS.md).

**Why it matters:** Almost every shippable game. Teams will dump JSON of `entt` ids if the engine offers nothing — that will break.

**Recommended:** Small Lua-facing API: write/read a JSON blob keyed by **names / prefab instance ids**, plus `Scene.load` for whole-level changes. Not a binary versioned RPG save graph.

**Urgency:** Priority A for shipping games; not needed to continue editor work.
