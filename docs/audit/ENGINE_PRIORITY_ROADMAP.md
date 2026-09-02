# Engine priority roadmap

Synthesized from the 2026-09-02 repository audit (`72a4dd7`).  
This is **not** a list of every missing engine feature. Priorities weigh impact on shipping small games, existing architecture, and finish-before-add leverage.

Related: [ENGINE_AUDIT_INDEX.md](ENGINE_AUDIT_INDEX.md), [GENRE_READINESS.md](GENRE_READINESS.md), [ENGINE_ARCHITECTURE_RISKS.md](ENGINE_ARCHITECTURE_RISKS.md).

---

## Immediate next focus (3–5 items)

### 1. Unify packaged runtime onto `EngineFrameRenderer`

| | |
|--|--|
| **Why it matters** | Editor play and shipped games already diverge (shadows, skybox, materials). Every new render feature will be implemented twice or ship broken in packages. |
| **Current maturity** | 2 (two working loops) |
| **What exists** | `engine/Gfx/EngineFrameRenderer.*` (editor); parallel CSM/UBO/draw in `engine/Game/GameMain.cpp` |
| **What to add** | Drive `HephaestusGame` through the shared renderer; keep GameMain as host (window, ini, Lua, physics tick) |
| **Unlocks** | Honest lighting/material parity; cheaper post-process later |
| **Dependencies** | None |
| **Risk** | Medium (game path is large) |
| **Effort** | Medium–high |
| **Timing** | Now — finish-before-adding |

### 2. Physics correctness (world transforms, layers, triggers, debug draw)

| | |
|--|--|
| **Why it matters** | Hierarchy, gameplay volumes, and collider authoring are already in the editor but wrong or silent. |
| **Current maturity** | 3 runtime / 1 layers / 1 debug draw |
| **What exists** | Jolt bodies, sensors (`isTrigger`), raycast, CharacterMotor, inspector `debugDraw` |
| **What to add** | Sync via `Scene::GetWorldTransform`; implement layer filters; Lua overlap/trigger; actually draw colliders |
| **Unlocks** | Doors, volumes, parented props, designer-trustworthy colliders |
| **Dependencies** | Scene hierarchy (already there) |
| **Risk** | Medium (behavior change for existing scenes) |
| **Effort** | Medium |
| **Timing** | Now |

### 3. Input abstraction (actions + gamepad)

| | |
|--|--|
| **Why it matters** | All sample gameplay binds GLFW key names. No rebind, no controller, no UI vs game contexts except cursor mode. |
| **Current maturity** | 2 |
| **What exists** | Lua `Input.*` poll; `RuntimeInputState` for Qt play mode |
| **What to add** | Action/axis map asset, contexts, dead zones, gamepad via GLFW (already in GLFW, unused) |
| **Unlocks** | Settings screens, controller games, less brittle FPS scripts |
| **Dependencies** | Optional HUI for rebind UI later |
| **Risk** | Low–medium |
| **Effort** | Medium |
| **Timing** | Now |

### 4. Save/load + scene change helpers

| | |
|--|--|
| **Why it matters** | Almost every shippable game needs persistence and level changes. Teams should not each invent JSON dumps of EnTT IDs. |
| **Current maturity** | 0 (game); editor recovery is unrelated |
| **What exists** | `SceneSerializer` string snapshots; `PlayModeManager` editor snapshots |
| **What to add** | Explicit save-game format (not editor recovery), slot API, Lua `Scene.load` / additive optional later |
| **Unlocks** | Horror/puzzle/FPS progression; checkpoints |
| **Dependencies** | Stable IDs or name-based save keys (prefer names/prefab instance ids over raw `entt` ids) |
| **Risk** | Medium if tied to unstable entity IDs |
| **Effort** | Medium |
| **Timing** | Now for a minimal API; not a full RPG save graph |

### 5. Animation import + clip playback *(if character games are in scope)*

| | |
|--|--|
| **Why it matters** | Largest genre lock. Skinning is not implied by glTF mesh import. |
| **Current maturity** | 0 |
| **What exists** | Static `Vertex` (pos/normal/color/uv); custom glTF/OBJ importer |
| **What to add** | Joints/weights, clip playback on a SkinnedMesh, editor preview. **Not** graphs/IK in this step. |
| **Unlocks** | Third-person, melee, humanoid enemies |
| **Dependencies** | Importer + vertex format + renderer skinning pass |
| **Risk** | High (touches renderer + import) |
| **Effort** | High |
| **Timing** | Immediate only if 3rd-person/action is a goal; otherwise Near-term |

If the next shipped titles are walking-sim / puzzle / FPS with capsules, **defer animation** and do items 1–4 first.

---

## Priority A — Major blockers

Systems whose absence or weakness significantly limits making real games.

| Item | Why |
|------|-----|
| Dual renderer | Packaged games are a second renderer |
| Physics hierarchy + layers + trigger events | Gameplay correctness |
| Input maps + gamepad | Every interactive genre |
| Save/load + level change | Shipping a loop |
| Animation clip playback | Character genres |
| Lua Inspector variables | Designers cannot tune scripts without editing Lua |
| Wire unused render/physics inspector fields | Trust: UI that does nothing |

---

## Priority B — Important growth areas

Materially better engine; not blocking all projects.

| Item | Why |
|------|-----|
| Cross-platform audio (replace WinMM) | Linux/macOS packages currently silent |
| 3D audio / listener | FPS/horror presence |
| True transparency / alpha pass | VFX, glass, foliage |
| MSAA or cheap TAA | Visual quality |
| Auto-reimport + DDC as load path | Artist iteration |
| Overlap / shapecast queries | AI and interaction beyond raycast |
| HUI text fields / sliders / scroll | Settings and inventory |
| Minimal event/message bus | Decouple Lua systems |
| CPU/GPU profiler scopes | Diagnose large scenes |
| First-party CI smoke | Protect Stage 6 + tests |
| Renderer/physics/Lua tests | Current coverage hole |

---

## Priority C — Advanced / specialized

Useful later; not essential for small 3D games.

- SSR, probes, SSAO, bloom, volumetrics, decals
- GPU instancing, occlusion culling, LOD, texture streaming
- Animation graphs, additive layers, IK, retargeting
- Navmesh + AI movement helpers
- Particles
- Terrain
- Nested prefab instances / variants
- Joints, ragdolls, vehicles
- Localization pipeline
- Linux/macOS editor surfaces
- Plugin SDK / C#

---

## Do not prioritize yet

Impressive, but premature given foundations:

| Feature | Why wait |
|---------|----------|
| Deferred / clustered lighting | Forward path still dual-stacked and untested |
| GPU particles / VFX graph | No clip animation, no profiler, no transparency pass |
| Networking | No gameplay framework, no tick model for netcode |
| Consoles | Windows packaging is the only real target |
| C# scripting | Lua is the product language |
| Global authored GUIDs | Path identity was an explicit design choice |
| Full IBL cubemap pipeline | Finish yaw + bake path + shared renderer first |
| World partition | No streaming primitives yet |

---

## Finish before adding

Systems already ~50–80% present. Better ROI than greenfield features.

| System | Existing | Missing | Recommendation |
|--------|----------|---------|----------------|
| Shared renderer | ~60% (`EngineFrameRenderer` editor-complete) | Game host still forks | **Finish now** |
| Inspector lighting/env fields | 80% serialized + UI | Yaw, normal bias, render distance, exposure unused | Wire or hide |
| Physics layers | Declared in `JoltPhysicsEngine` | Always-collide filters | Implement or delete declarations |
| Physics debug draw | Component + inspector | No draw | Implement or hide checkbox |
| Cylinder shape | Enum + serialize | No Jolt `case` | Implement or remove from IO |
| Lua script variables | `LuaScriptComponent.variables` + spawn `vars` | Inspector UI | Finish Inspector reflection |
| FileWatcher | Implemented | Not started by pipeline | Enable when artist workflow needs it |
| DDC | Store/Has/GetPath | Not the runtime load path | Finish only if import cost hurts |
| Material shader generator | GLSL emit + diagnostics | Not SPIR-V | Document bake-only **or** compile for real |
| `SceneExporter.cpp` | Retired from CMake | Sources remain | Delete or quarantine (do not revive) |
| C++ `ScriptEngine` | Still ticked | No content path | Stop constructing; keep files archived |
| `MeshCollisionSystem` | Built | Never constructed | Remove or isolate |
| `RelationshipComponent` | Declared | Unused | Remove or use |

---

## Foundational blockers (do not invent rewrites)

Observed in code, not theoretical:

1. **No property/reflection metadata for Lua vars** — Inspector cannot show script knobs. `PropertyValue` exists; UI does not consume it for Lua.
2. **No gameplay event bus** — Trigger enter/exit, UI events, and animation notifies would all need one later. A small dispatcher is worth doing once, not a new ECS.
3. **Unstable entity identity** — Saves and networking cannot use raw `entt::entity`. Prefab `instanceId` is a start.
4. **Dual frame loop** — Blocks every graphics feature.
5. **Profiling absence** — Cannot validate large-scene work.

Do **not** rewrite ECS, Vulkan, or the asset path model to fix these.

---

## Feature dependency graph

```text
Unify EngineFrameRenderer ──► post-process / MSAA / debug views in packages
        │
        └──► material GPU shaders (if ever more than bake)

Physics world-transform ──► parented colliders, moving platforms
        │
        └──► trigger events ──► doors/volumes/interaction
                │
                └──► gameplay event bus (optional shared)

Input poll (today) ──► action maps ──► rebind UI (HUI)
        │
        └──► gamepad

glTF static mesh (today) ──► joints/weights ──► clip playback
        │
        └──► anim preview ──► state machine ──► editor graph
                              └──► root motion / notifies

Save format (name/prefab based) ──► checkpoints ──► later streaming

Navmesh (later) ──► AI movement helpers ──► AI tooling
```

Rule: do not build animation *editors* or AI *tools* before the runtime they author.

---

## “Can we make a game?” sanity check

Full write-up: [GENRE_READINESS.md](GENRE_READINESS.md).

| Hypothetical | Verdict |
|--------------|---------|
| Small first-person horror (Windows) | **Possible with custom Lua** for save, interaction, doors; engine provides move, lights, audio 2D, UI, package |
| Walking simulator / gallery | **Good fit** |
| Third-person action | **Major gaps** (animation) |
| Multiplayer / open world | **Poor fit** |

This confirms Priority A is **runtime unification + gameplay plumbing**, not clustered lighting.

---

## Near-term (after immediate)

- Audio backend replacement
- HUI form controls
- Overlap queries
- Auto-reimport
- CI
- If animation started: preview + simple blend

## Mid-term

- Transparency pass, MSAA, basic post
- Navmesh
- Particles
- Nested prefabs
- Linux runtime (GLFW game) before Linux editor

## Long-term

- Networking, terrain, deferred, consoles, localization, C#
