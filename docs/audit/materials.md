# Materials audit

**Overall: 3 / 5 (Usable PBR slot workflow)**  
Runtime 3 · Editor 3 · Lua 0 · Tests 3

The Material Editor is a **visual graph that compiles by evaluating nodes into a `MaterialComponent`**, then the GPU shades that struct with the **fixed** `triangle.frag` PBR shader. It is **not** a shader graph that emits unique SPIR-V per asset.

That distinction is the most important materials finding.

---

## PBR material model

- **Exists:** Yes
- **Maturity:** 3
- **Evidence:** `MaterialComponent` in `engine/Scene/Components.h` — albedo, metallic, roughness, AO, emissive + strength, reflection strength, specular, opacity, opacity mask, refraction (reserved), five texture slots, world-aligned scale.
- **Runtime:** `EngineFrameRenderer` uploads a material UBO and binds textures from paths.
- **Limitations:** Cutout opacity, not blended glass. Simplified normals. No clearcoat/sheen/anisotropy/subsurface.
- **Next:** Keep slot model stable; document reserved refraction.

---

## Material assets and serialization

- **Exists:** `.hematerial` JSON
- **Maturity:** 3
- **Evidence:** `engine/UI/MaterialEditor/MaterialAssetSerializer.*` — load compat for legacy (no graph) and graph documents; portable `resourcePath` rewrite via `AssetPathPolicy`.
- **Tests:** `engine/Tests/MaterialCompatibilityTests.cpp` (legacy save, graph round-trip, shader snippet generation).
- **Editor:** Graph dialog save; MeshRenderer material slots; Asset Browser.
- **Lua:** None.

---

## Material graph

- **Exists:** Yes
- **Maturity:** 3 authoring · 2 as a *shader* graph
- **Evidence:** `MaterialGraph.h` `NodeType` includes constants, parameters, texture samples, math, lerp, fresnel, time, world position, triplanar, etc.
- **Compile path:** `MaterialGraphQtEditorDialog` → `EvaluateGraph` → `MaterialComponent`. `GenerateMaterialShader` runs for **diagnostics** only.
- **Implication:** `Time`, `Noise`, `Fresnel`, `CameraVector` do **not** animate on GPU. They bake to a snapshot at Compile (and whatever the evaluator does for unconnected defaults). Connecting a texture to albedo **does** flow into `useAlbedoTexture` + path.

### Node support (practical)

| Category | Usable at runtime after bake | Notes |
|----------|------------------------------|-------|
| Texture samples / world-aligned / triplanar flags | Yes if they resolve to a path + flags | Evaluator copies texture path and world-aligned |
| Constants / color params | Yes | Become albedo/metal/rough numbers |
| Math on constants | Yes | Baked |
| Time / view / fresnel / noise | **No live GPU** | Bake-time only |
| Custom lighting models | No | Always `triangle.frag` |

---

## Texture sampling / maps

- **Exists:** Albedo, normal, roughness, metallic, AO
- **Maturity:** 3
- **Editor:** Texture discovery dropdown, `TextureViewerDialog` for previewing files
- **Import:** `AssetImporters` + stb (`png/jpg/tga/exr/hdr`)
- **Limitations:** No ORM packing convention beyond “use this file as roughness”; no texture streaming

---

## UV / world-aligned / triplanar

- **Exists:** `useWorldAlignedTexture` + `worldTextureScale` on component; graph nodes `WorldAlignedTexture`, `TriplanarTexture`, `TextureTilingOffset`
- **Maturity:** 2–3
- **Runtime:** World-aligned flag consumed in mesh shader UV path (engine renderer)
- **Limitation:** One world-aligned mode for the whole material (evaluator promotes from albedo/other slots)

---

## Parameters / instances / submeshes

- **Per-submesh materials:** `MeshRendererComponent.appliedMaterialPaths` / `materialIndices` — **yes**, used when drawing submeshes.
- **Runtime instances:** Copy of `MaterialComponent` on entities; no `MaterialInstance` asset with parent + overrides.
- **Inspector knobs for graph parameters:** Graph node parameters exist; live entity materials are the baked component, not a parameter block.
- **Maturity:** 2 instances · 3 submesh slots

---

## Shader generation vs evaluation

| Path | Used in product? |
|------|------------------|
| `EvaluateGraph` → `MaterialComponent` | **Yes** (compile/save/preview) |
| `GenerateMaterialShader` → GLSL string | Diagnostics + tests; **not** `glslc` input |

- **Maturity:** Shader gen 1
- **Next:** Either (a) document “bake-only graph” in the Material Editor UI, or (b) a real permutation/SPIR-V compiler — (a) is the honest cheap fix.

---

## Live preview / UX

- **Exists:** Preview viewport in `MaterialGraphQtEditorDialog` (`schedulePreviewUpdate`)
- **Maturity:** 3 for slot materials
- **Undo:** Graph undo/redo toolbar (changelog 2026-09-01)
- **Limitations:** Preview is the standard PBR shader, so it matches runtime **slot** results, not imaginary custom shaders

---

## Reuse / caching / performance

- Renderer caches loaded `.hematerial` and texture slots (`ensureMaterialTextureSlot`, slot cap logged when full).
- No shader permutation cache because there is one mesh shader.

---

## Missing common nodes (as GPU features)

Even if the enum contains them, they are not GPU features until a real compiler exists: animated time, procedural noise, parallax, detail blends as extra samples beyond five slots, refraction.

---

## Production workflow closeness

**Close enough for:** props, architecture, simple characters-as-static-meshes, HDRI lookdev.

**Not close enough for:** unique shader graphs (water, hologram, foliage wind), layered materials, runtime dirt overlays from Lua.

**Designer vs TA:** Designers can assign `.hematerial` files. TAs can build graphs that **sample textures and set PBR numbers**. They cannot ship a new lighting model without engine C++/GLSL.

---

## Recommended next step

1. Label Compile as “Bake to PBR” in UI/docs.
2. Hide or mark GPU-only-looking nodes that do not survive bake.
3. Do not start a shader compiler until the shared renderer exists ([rendering.md](rendering.md)).
