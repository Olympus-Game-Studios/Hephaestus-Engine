# Engine capability matrix

Source of truth: repository audit 2026-09-02 (`72a4dd7`).  
Maturity: 0 Missing … 5 Advanced. Do not treat this table as a feature checklist for marketing.

| Area | Capability | Maturity | Status | Main limitation |
|------|------------|--------:|--------|-----------------|
| Rendering | Vulkan device/swapchain | 3 | Usable | Qt attach is Win32-only |
| Rendering | Forward PBR mesh pass | 3 | Usable | Fixed `triangle.frag`; no custom GPU shaders |
| Rendering | Deferred / clustered | 0 | Missing | Not implemented |
| Rendering | Directional + point + spot lights | 3 | Usable | One effective directional; 16+16 cap; hardcoded ambient |
| Rendering | Directional CSM shadows | 3 | Usable | No point/spot shadows; `shadowNormalBias` unwired |
| Rendering | HDRI skybox | 3 | Usable | Equirect sample; yaw UI unwired; no prefiltered IBL |
| Rendering | Environment specular | 2 | Functional but incomplete | Comment in shader: nearby objects not reflected |
| Rendering | SSR / probes | 0 | Missing | — |
| Rendering | Tone map / gamma | 2 | Functional but incomplete | Hardcoded Reinhard + 2.2; exposure setting unwired |
| Rendering | True transparency | 1 | Foundation | Opacity discard only; mesh blend disabled |
| Rendering | MSAA / TAA | 0 | Missing | `VK_SAMPLE_COUNT_1_BIT` |
| Rendering | Bloom / SSAO / fog / volumetrics / decals | 0 | Missing | — |
| Rendering | Instancing / GPU cull / LOD / streaming | 0 | Missing | Distance-cull fields unwired |
| Rendering | Lit/Unlit/Wireframe | 3 | Usable | Editor only |
| Rendering | Shared frame renderer | 2 | Functional but incomplete | Game uses parallel `GameMain` loop |
| Materials | PBR slot model | 3 | Usable | Albedo/normal/rough/metal/AO/emissive |
| Materials | `.hematerial` + graph editor | 3 | Usable | Compile **evaluates** graph into slots |
| Materials | Shader generation | 1 | Unwired | GLSL snippets for diagnostics; not SPIR-V |
| Materials | Runtime material instances | 1 | Foundation | Component copies; no instance API |
| Materials | Lua material API | 0 | Missing | — |
| Scene/ECS | EnTT entities + hierarchy | 3 | Usable | No streaming; IDs are `entt::entity` |
| Scene/ECS | `.hpscene` save/load | 4 | Strong | JSON; play-mode snapshot works |
| Scene/ECS | Additive / partition streaming | 0 | Missing | — |
| Scene/ECS | Stable entity GUIDs | 0 | Missing | — |
| Prefabs | Instantiate + overrides | 4 | Strong | Nested *instances* and variants missing |
| Prefabs | Apply / Revert / Unpack / Create From Selection | 4 | Strong | Tested |
| Prefabs | Prefab Editor | 3 | Usable | Same inspectors as scene |
| Physics | Jolt rigid bodies | 3 | Usable | Local transform sync; single collision layer |
| Physics | Box/Sphere/Capsule/Mesh | 3 | Usable | Cylinder enum unwired |
| Physics | Character motor (FPS) | 3 | Usable | Not CharacterVirtual; Lua helpers |
| Physics | Raycast | 3 | Usable | No overlap/shape-cast API |
| Physics | Triggers / sensors | 2 | Functional but incomplete | `isTrigger` → Jolt sensor; no Lua callbacks |
| Physics | Joints / ragdolls / vehicles | 0 | Missing | — |
| Physics | Layers / filters | 1 | Stub | Filters always return true |
| Physics | Debug draw | 1 | Unwired | Inspector checkbox; no renderer |
| Scripting | Lua runtime + lifecycle | 3 | Usable | Hot-reload on mtime; unsandboxed |
| Scripting | Scene/Input/Physics/UI/Audio Lua | 3 | Usable | No materials; no Lua tests |
| Scripting | Exposed variables Inspector | 1 | Foundation | Map exists; Inspector is path/enabled only |
| Scripting | C++ `ScriptEngine` | 1 | Legacy | Still constructed in play path |
| UI | `.hui` + runtime + editor | 3 | Usable | Panel/Button/Text/Image/boxes/grid |
| UI | Complex app UI | 1 | Foundation | No fields, sliders, scroll, keyboard focus |
| Audio | 2D play/loop/volume | 2 | Functional but incomplete | WinMM MCI; WAV/MP3 |
| Audio | 3D / mixer / buses | 0 | Missing | — |
| Animation | Skeletal / clips / graphs | 0 | Missing | Vertex has no joints/weights |
| Input | Keyboard/mouse poll | 2 | Functional but incomplete | GLFW names + Qt `RuntimeInputState` |
| Input | Actions / rebind / gamepad | 0 | Missing | GLFW gamepad unused by engine |
| Assets | Path policy + reference index | 4 | Strong | Path identity by design |
| Assets | Import mesh/texture/audio | 3 | Usable | ASCII FBX only; no skins |
| Assets | DDC + auto-reimport | 2 | Functional but incomplete | Manual reimport; watcher off |
| Assets | Safe move/rename/trash | 4 | Strong | Tested |
| Build | Windows package + CLI + Qt | 4 | Strong | Windows x64 only |
| Build | Linux/macOS/console packages | 0 | Missing | CMake Apple bundle flags ≠ support |
| Editor | Scene/Inspector/Outliner/Browser | 4 | Strong | Phases 1–4 |
| Editor | Specialized editors | 3 | Usable | Material/HUI/Prefab/Texture/Build |
| World | Game mode / save / nav / AI | 0 | Missing | Lua must implement |
| World | Camera component + play camera | 3 | Usable | Lua Camera API |
| World | Prefab spawn | 3 | Usable | `Scene.instantiate` |
| Perf | FPS / draw / RAM overlay | 1 | Foundation | No GPU timers or scopes |
| Testing | Assets/prefab/build/authoring | 4 | Strong | Headless CTest |
| Testing | Renderer/physics/Lua/play mode | 0 | Missing | — |
| Platform | Windows x64 | 4 | Strong | Primary |
| Platform | Linux editor viewport | 1 | Foundation | `AttachToSurface` not implemented |
| Platform | macOS | 1 | Foundation | Bundle metadata only |
| Platform | Consoles | 0 | Missing | — |
| Navigation | Navmesh | 0 | Missing | — |
| VFX | Particles | 0 | Missing | Empty Add Actors “FX” category |
| Terrain | Heightmap | 0 | Missing | — |
| Networking | Multiplayer | 0 | Missing | — |
| Save | Game save/load | 0 | Missing | Editor `RecoveryStore` is not this |
| Localization | i18n | 0 | Missing | — |
