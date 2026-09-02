# Genre readiness

Could a **small team** reasonably build this with **systems Hephaestus currently provides** (Windows, Lua, Qt editor, Stage 6 package)? Not “in theory with enough C++.”

Scale: **Good fit** · **Possible with custom work** · **Major engine gaps** · **Poor fit currently**

---

## Summary

| Genre | Fit | Primary blockers |
|-------|-----|------------------|
| Walking simulator / gallery | **Good fit** | Save optional; no animation needed |
| First-person puzzle | **Good fit** / possible | Interaction + save in Lua |
| First-person horror | **Possible with custom work** | Save, interaction, 2D audio, no animation |
| Simple FPS / arena (capsule enemies) | **Possible with custom work** | AI/nav, save, no anim; dual renderer risk |
| First-person shooter (AAA-ish) | **Major engine gaps** | Anim, VFX, audio 3D, profiling, AI |
| Third-person action | **Major engine gaps** | **Animation** |
| Platformer | **Major engine gaps** | 2D tools or 3D anim + camera + coyote helpers |
| Top-down | **Possible with custom work** | Ortho exists in **editor**; game camera Lua; no anim |
| Simulation / tycoon | **Major engine gaps** | UI complexity, sim tick, save |
| Survival / crafting | **Major engine gaps** | Save, inventory, streaming, survival systems |
| RPG | **Major engine gaps** | Anim, UI, save, data, combat |
| Strategy / RTS | **Poor fit** | Units, picking scale, UI, no instancing |
| UI-heavy management | **Major engine gaps** | HUI widget set |
| Multiplayer | **Poor fit** | No net |
| Large open world | **Poor fit** | No streaming/terrain/nav |
| Space (flight) | **Possible with custom work** | 6DOF in Lua; no specialized camera/physics |

---

## Designer vs programmer by genre

Walking-sim: designers can block out; Lua for interactables.  
Horror FPS: programmers own most gameplay; designers place lights/prefabs/audio.  
Anything with characters: **blocked for both** until animation exists.

---

## “Can we make a game?” — three hypotheticals

### 1. Small first-person horror (Windows)

Needs vs engine:

| Need | Verdict |
|------|---------|
| Player movement | **Engine provides** — CharacterMotor + Lua |
| Input | **Engine provides** (keyboard/mouse only) |
| Physics | **Engine provides** — static world + raycast |
| Interaction / doors | **Moderate custom Lua** (raycast + transform); triggers unwired |
| Lighting / HDRI | **Engine provides** |
| UI (hint / pause) | **Engine provides** — HUI |
| Audio stings / loop | **Engine provides** (2D Windows) |
| Save/load | **Missing blocker** for a shippable loop — custom JSON possible |
| Packaging | **Engine provides** |

**Verdict: Possible with custom Lua** if the team writes save and interaction. Not a missing renderer.

### 2. Walking simulator / museum

| Need | Verdict |
|------|---------|
| Static meshes, lights, audio logs (2D) | Engine |
| Simple UI | Engine |
| Linear scene loads | **Major custom** (`Scene.load` missing — could swap scenes with C++ later or one big scene) |
| Animation of characters | Not required |

**Verdict: Good fit** as a single-scene or a few whole-scene reloads implemented ad hoc.

### 3. Third-person action

| Need | Verdict |
|------|---------|
| Skinned mesh + clips | **Missing blocker** |
| Camera follow | Moderate Lua |
| Combat VFX | Missing |
| Anim notify → hitboxes | Missing |

**Verdict: Major engine gaps.** Do not start this title on Hephaestus today.

---

## Sanity check against the roadmap

Genre results agree with [ENGINE_PRIORITY_ROADMAP.md](ENGINE_PRIORITY_ROADMAP.md): **unify renderer, physics correctness, input, save**, then animation **if** character games are the goal. Clustered lighting and networking would not have unblocked the horror or walking-sim titles.
