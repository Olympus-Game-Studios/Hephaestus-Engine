# Animation audit

**Overall: 0 / 5 (Missing)**  
Runtime 0 · Editor 0 · Lua 0 · Tests 0

Hephaestus does **not** have an animation system. Mesh import is **static geometry**. Do not infer animation from glTF/FBX file types or from the word “instance”.

---

## Evidence of absence

- No matches in `engine/` for animation clips, blend trees, skeletons, bones, `AnimationController`, notifies, IK, retargeting.
- `engine/Gfx/Vertex.h`: `pos`, `normal`, `color`, `texCoord` only — **no joints/weights**.
- `MeshAsset.h` / `MeshData`: vertices + indices + submeshes.
- `ModelImporter`: OBJ, ASCII FBX, glTF/GLB **first mesh primitives**. Binary FBX rejected. No animation/skin channels.
- Asset types treat `.gltf`/`.glb`/`.fbx` as static meshes.

Qt `setAnimated` hits on tree widgets are **UI polish**, not skeletal animation.

---

## Feature table

| Feature | Exists | Maturity |
|---------|--------|--------:|
| Skeletal meshes | No | 0 |
| Skeleton import | No | 0 |
| Clip import / playback | No | 0 |
| Blending / transitions | No | 0 |
| State machines / graphs / parameters | No | 0 |
| Root motion | No | 0 |
| Additive / layers | No | 0 |
| Events / notifies | No | 0 |
| IK / retargeting | No | 0 |
| Editor preview / timeline | No | 0 |

---

## What users can do instead

- Animate **transforms in Lua** (`onUpdate` rotate/translate) — fine for props, fans, simple bobbing.
- Swap **static meshes** for fake frames — not a pipeline.
- FPS **capsule** player without a skinned body (current samples).

---

## Strategic note

Animation is a **major new subsystem** (importer + vertex format + skinned draw + clip player). Editor graphs should wait for runtime clips ([roadmap](ENGINE_PRIORITY_ROADMAP.md)).

If the next games are walking-sim / puzzle / FPS with weapons-as-parented meshes, this can stay Level 0.

---

## Recommended next step

Only if character games are in scope: glTF skins + clip playback + a skinned pass in the **shared** renderer. Not an animation graph.
