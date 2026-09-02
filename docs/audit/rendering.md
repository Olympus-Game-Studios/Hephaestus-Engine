# Rendering audit

**Overall: 3 / 5 (Usable forward renderer)**  
Runtime 3 · Editor 3 · Lua 1 · Tests 0

Hephaestus renders a **forward Vulkan** scene: opaque PBR meshes, one directional cascaded shadow map, point/spot lights in a UBO, equirect HDRI skybox, Reinhard tonemap. There is **no** deferred path, post stack, MSAA, GPU culling, or instancing.

The important architectural fact: **the editor and the packaged game do not share a frame loop.**

---

## Architecture

| Piece | Role |
|-------|------|
| `engine/Gfx/VulkanDevice.*` | Instance, device, VMA, GLFW surface + Win32 `AttachToSurface` |
| `engine/Gfx/VulkanSwapchain.*` | Color + depth, **1 sample**, vsync |
| `engine/Gfx/VulkanPipeline.*` | Main mesh pipelines (cull / wireframe variants) |
| `engine/Gfx/EngineFrameRenderer.*` | Editor frame: shadow → main (sky, meshes, grid, gizmos, outlines) |
| `engine/Gfx/VulkanSkybox.*` | Game-path skybox |
| `engine/Game/GameMain.cpp` | Standalone game: own UBO, CSM, draw |
| `shaders/*.vert/.frag` | GLSL compiled by `glslc` to SPIR-V at build |

**Exists:** Yes. **Maturity:** 3 editor / 2 cohesion.

---

## Feature records

### Vulkan architecture

- **Exists:** Yes
- **Maturity:** 3
- **Editor:** `InitWithoutSurface` + `AttachToSurface` (HWND)
- **Runtime:** GLFW `glfwCreateWindowSurface`
- **Limitations:** `VulkanDevice::AttachToSurface` non-Win32 returns false. Apple `VK_KHR_portability_subset` hinted in device code; Qt viewport still Win32.
- **Testing:** None (Qt smoke avoids Vulkan)
- **Next:** Share device/swapchain setup; implement non-Win32 only when Linux editor is a goal

### Render loop

- **Exists:** Yes (two)
- **Maturity:** 3 editor, 2 game cohesion
- **Evidence:** `EngineFrameRenderer::RenderFrame`; `GameMain.cpp` ~thousands of lines of parallel recording
- **Next:** Host game on `EngineFrameRenderer` ([roadmap](ENGINE_PRIORITY_ROADMAP.md))

### Forward vs deferred

- **Exists:** Forward only
- **Maturity:** Forward 3 · Deferred 0
- **Evidence:** Single color+depth pass; lighting in `triangle.frag`

### PBR

- **Exists:** Yes
- **Maturity:** 3
- **Evidence:** `MaterialComponent` + `triangle.frag` GGX / Smith / Fresnel. Textures: albedo, normal, roughness, metallic, AO. World-aligned UV option.
- **Limitations:** Normal mapping is a simplified (non-TBN) approximation (shader comments). No full diffuse IBL. `refraction` reserved unused.
- **Testing:** Material asset tests, not GPU gold images
- **Next:** Document approximation; TBN if animation/normal quality becomes a goal

### Lighting (directional / point / spot)

- **Exists:** Yes
- **Maturity:** 3
- **Evidence:** `LightComponent` factories; `updateLightingUBO`; shader loops; `LightInspector`
- **Limitations:** Hard cap 16 point + 16 spot; typically last directional wins; ambient hardcoded in UBO update (~dim blue-grey)
- **Lua:** None
- **Next:** Expose ambient on environment; document caps

### Shadows

- **Exists:** Directional CSM only
- **Maturity:** 3
- **Evidence:** 4096 atlas, 4 cascades, `shadowmap.vert/frag`, Poisson filter in `triangle.frag`; editor + game both implement CSM
- **Limitations:** No point/spot shadows. `shadowNormalBias` serialized/UI only. No cascade debug view.
- **Next:** Wire normal bias or hide; unify implementation

### HDRI / skybox

- **Exists:** Yes
- **Maturity:** 3 background · 2 IBL quality
- **Evidence:** `VulkanTexture::LoadHDRFromFile`; `skybox.frag` equirect; binding 5 env reflection; `EnvironmentInspector`
- **Limitations:** `rotationYaw` not applied in draw. No cubemap convolution. Dual skybox classes.
- **Next:** Apply yaw; then consider prefilter

### Tone mapping / gamma / exposure

- **Exists:** Partial
- **Maturity:** 2
- **Evidence:** Reinhard + `pow(…, 1/2.2)` in `triangle.frag` and `skybox.frag`. `project.rendering.defaultExposure` in `SettingsSchema.cpp` **not** in shaders.
- **Next:** Uniform exposure or remove setting

### Transparency / depth

- **Transparency exists:** Cutout (`discard` on opacity/mask)
- **Maturity:** 1 true alpha · 3 depth
- **Evidence:** `VulkanPipeline` mesh `blendEnable = VK_FALSE`. Billboard/grid blend.
- **Next:** Sorted alpha pass when VFX/glass needed

### MSAA / AA

- **Exists:** No
- **Maturity:** 0
- **Evidence:** `VK_SAMPLE_COUNT_1_BIT`

### Reflections / SSR / probes

- **Exists:** Basic env specular sample
- **Maturity:** 2 env · 0 SSR/probes
- **Evidence:** Shader comment that nearby objects are not reflected

### Ambient / SSAO

- **Exists:** Flat ambient + material AO maps
- **Maturity:** 2 ambient · 0 SSAO

### Bloom / post / fog / volumetrics / god rays / decals

- **Exists:** No
- **Maturity:** 0
- **Evidence:** No passes in `engine/Gfx` or project `shaders/`

### Instancing / GPU cull / occlusion / LOD / streaming

- **Exists:** No GPU instancing (prefab “instance” is authorship)
- **Distance cull:** Data + inspector only (`enableRenderDistance`) — **unwired**
- **Mips:** Optional CPU mip gen in `VulkanTexture`
- **Maturity:** 0–1

### Render debugging

- **Exists:** Lit / Unlit / Wireframe (editor)
- **Maturity:** 3
- **Evidence:** `ViewShadingMode`, toolbar, `triangle.frag` UBO, `fillModeNonSolid` wireframe
- **Runtime:** Packaged game stays Lit
- **Missing:** Albedo/normal/shadow-map debug views

### Shader pipeline

- **Exists:** Build-time SPIR-V via CMake `glslc`
- **Maturity:** 3 load path · 1 material custom shaders
- **Evidence:** `CMakeLists.txt` shader list; loaders probe multiple directories. `GenerateMaterialShader` used by Material Editor diagnostics + `MaterialCompatibilityTests`, **not** injected into GPU pipelines.
- **Next:** Single shader root; do not advertise runtime shader compile

### Portability

- **Exists:** Partial
- **Maturity:** 2
- **Evidence:** MoltenVK comments; Qt surface Win32; packaging enum Windows x64

---

## Scene components used by the renderer

| Component | File | Notes |
|-----------|------|-------|
| `MeshRendererComponent` | `Components.h` | `visible`, `hideInGame`, submesh material slots |
| `MaterialComponent` | same | PBR slots + texture paths |
| `LightComponent` | same | Dir/point/spot + shadow fields |
| `CameraComponent` | same | Play-mode camera |
| `EnvironmentComponent` | same | HDRI / solid; some fields unwired |
| `EditorIconComponent` | same | Light billboards, editor only |

---

## Testing

No Vulkan/frame/shadow tests. Packaging tests copy shaders/HDRI but do not render.

---

## Recommended next step

1. Run `HephaestusGame` through `EngineFrameRenderer`.
2. Hide or wire yaw, render distance, exposure, normal bias.
3. Add a headless “create device / compile pipelines” smoke if CI can load Vulkan.

Do **not** start deferred, SSR, or bloom while two forward paths exist.
