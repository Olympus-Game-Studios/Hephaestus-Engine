/**
 * @file EngineFrameRenderer.h
 * @brief Shared frame rendering system for both GLFW and Qt editor paths.
 * 
 * This class encapsulates the per-frame rendering logic extracted from main.cpp,
 * allowing both the legacy GLFW renderer and the Qt editor to use identical
 * rendering code. The renderer handles:
 * - Scene mesh rendering with materials
 * - Skybox/environment rendering
 * - Shadow map generation
 * - Selection outlines (editor)
 * - Gizmo rendering (editor)
 * - Billboards for lights (editor)
 */

#pragma once

#include "MaterialTextureSlots.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <memory>
#include <functional>
#include <string>
#include <unordered_map>
#include <chrono>
#include <filesystem>

#include "VulkanPipeline.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "../Scene/EditorIcons.h"
#include <array>
#include "VulkanGizmo.h"
#include "VulkanLightGizmo.h"
#include "SkyIrradiance.h"
#include "ProbeBake.h"
#include "ProbeGI.h"
#include "../Assets/CompiledMaterialShader.h"
#include "../Assets/MeshAsset.h"
#include "../Scene/Components.h"
#include "../Core/EditorDebugDraw.h"
#include "../UI/HephaestusUI/UIRenderer.h"

// Forward declarations
class VulkanDevice;
class VulkanSwapchain;
class VulkanPipeline;
class Entity;
class Scene;

namespace Hephaestus::HephaestusUI {
class VulkanHUIRenderer;
} // namespace Hephaestus::HephaestusUI

namespace Hephaestus {

/**
 * @brief Camera projection mode for rendering.
 */
enum class ProjectionMode {
    Perspective = 0,
    Orthographic = 1
};

/**
 * @brief Viewport shading mode (editor view modes).
 */
enum class ViewShadingMode {
    Lit = 0,
    Unlit = 1,
    Wireframe = 2
};

/**
 * @brief Camera data for rendering - platform agnostic.
 */
struct RenderCamera {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 5.0f;
    float yawDeg = 90.0f;
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
    float nearPlane = 0.1f;
    float farPlane = 10000.0f;
    float fovDeg = 60.0f;
    float orthoHalfExtent = 10.0f;
    ProjectionMode projectionMode = ProjectionMode::Perspective;
    
    // Helper to compute forward direction
    void GetForwardDirection(float& outX, float& outY, float& outZ) const;
    
    // Helper to get camera position as array
    void GetPosition(float* outPos) const {
        outPos[0] = positionX;
        outPos[1] = positionY;
        outPos[2] = positionZ;
    }
};

/**
 * @brief Configuration for frame rendering.
 */
struct FrameRenderConfig {
    bool renderSkybox = true;
    bool renderShadows = true;
    bool renderGizmo = true;
    bool renderBillboards = true;
    bool renderOutlines = true;
    bool renderGrid = true;         // World grid overlay (editor viewport only)
    bool isEditorMode = false;      // Editor sets true; game / standalone defaults off
    bool isPlaying = false;
    bool isPaused = false;
    ViewShadingMode shadingMode = ViewShadingMode::Lit;
    
    // Clear color (used if no skybox)
    float clearColorR = 0.1f;
    float clearColorG = 0.2f;
    float clearColorB = 0.3f;
    
    // Selection for outline rendering (entt::entity as uint32_t)
    uint32_t selectedEntity = static_cast<uint32_t>(-1); // entt::null (primary)
    std::vector<uint32_t> selectedEntities;               // All selected entities for multi-select outlines
    std::vector<uint32_t> editorHiddenEntities; ///< sorted ascending
    std::vector<uint32_t> editorIsolatedEntities;
    std::vector<uint32_t> editorLockedEntities;
    
    // Gizmo configuration (read from EditorContext)
    int gizmoMode = 1;      // 0=None, 1=Translate, 2=Rotate, 3=Scale
    int gizmoSpace = 0;     // 0=World, 1=Local
    bool enableSnap = false;
    float snapValue = 1.0f; // Current snap value based on mode

    std::vector<EditorDebugLine> debugLines;
    std::vector<EditorDebugPoint> debugPoints;
};

/**
 * @brief Per-frame rendering statistics collected during RenderFrame().
 */
struct FrameStats {
    int drawCalls   = 0;
    int meshesDrawn = 0;
    int entitiesSkipped = 0;
    float probeGIGpuMs = -1.0f;  // last measured probe GI update, negative when unknown or inactive
};

/**
 * @brief Shared frame rendering system.
 * 
 * Usage:
 * 1. Create instance
 * 2. Call Init() with device/swapchain
 * 3. Each frame: call RenderFrame()
 * 4. On resize: call OnResize()
 * 5. On shutdown: call Shutdown()
 */
class EngineFrameRenderer {
public:
    EngineFrameRenderer();
    ~EngineFrameRenderer();

    // Non-copyable
    EngineFrameRenderer(const EngineFrameRenderer&) = delete;
    EngineFrameRenderer& operator=(const EngineFrameRenderer&) = delete;

    /**
     * @brief Initialize the renderer with Vulkan resources.
     * @param device Initialized VulkanDevice (must outlive renderer)
     * @param swapchain Initialized VulkanSwapchain (must outlive renderer)
     * @param editorOverlays Create grid, gizmo, billboard, outline, ID-picker and debug-overlay resources
     * @return true if initialization succeeded
     */
    bool Init(VulkanDevice* device, VulkanSwapchain* swapchain, bool editorOverlays);

    /**
     * @brief Handle viewport resize.
     * @param width New width
     * @param height New height
     */
    void OnResize(uint32_t width, uint32_t height);

    /**
     * @brief Render a frame.
     * @param scene Scene to render (can be null for empty frame)
     * @param camera Camera data for view/projection
     * @param config Render configuration
     * @param deltaTime Time since last frame in seconds
     * @return true if frame was rendered successfully
     */
    bool RenderFrame(Scene* scene, const RenderCamera& camera, 
                     const FrameRenderConfig& config, float deltaTime);

    /**
     * @brief Seconds between the last two frames' pacing points (where waiting for the GPU and the swapchain
     * returns), or 0 when the last RenderFrame did not reach it. Gameplay stepped by it moves evenly on screen.
     */
    double PacedFrameSeconds() const { return m_pacedFrameSeconds; }

    /**
     * @brief Shutdown and cleanup all resources.
     */
    void Shutdown();

    /**
     * @brief Check if renderer is initialized and ready.
     */
    bool IsInitialized() const { return m_initialized; }
    bool HasDeviceLost() const { return m_deviceLost; }

    const FrameStats& GetLastFrameStats() const { return m_lastFrameStats; }

    /**
     * @brief Set the project root directory for resolving relative asset paths (HDRI, fonts, UI images).
     */
    void SetProjectRoot(const std::string& root);
    const std::string& GetProjectRoot() const { return m_projectRoot; }

    /**
     * @brief Caps material textures to this many pixels on their longer side (0 = their full size). Textures
     * already loaded are reloaded at the new size, so this waits for the GPU to go idle when anything changes.
     */
    void SetMaxMaterialTextureSize(uint32_t pixels);

    /// Draws a material that is not a file (the Material Editor preview's) with a compiled graph; null removes it.
    /// The component must stay at the same address while the override is set.
    void SetMaterialShaderOverride(const MaterialComponent* material, const CompiledMaterialShader* shader);
    uint32_t GetMaxMaterialTextureSize() const { return m_maxMaterialTextureSize; }

    /**
     * @brief A full-screen rifle scope drawn by the tone map pass: the scene through a round lens (barrel
     * distortion, colour fringing and darker edges), a dark tube around it, black outside and an optional
     * mil-dot reticle. A radius of 0 turns it off.
     */
    struct ScopeLens {
        float radius = 0.0f;         ///< lens radius as a fraction of the screen height
        float distortion = 0.12f;    ///< barrel distortion at the lens edge
        float aberration = 0.006f;   ///< colour fringing at the edge, as a fraction of the radius
        float edgeDarkening = 0.45f; ///< 0 to 1
        float ringWidth = 0.14f;     ///< the tube's width, as a fraction of the radius
        float opacity = 1.0f;        ///< fades the whole scope in and out
        bool reticle = true;
    };
    void SetScopeLens(const ScopeLens& lens) { m_scopeLens = lens; }
    const ScopeLens& GetScopeLens() const { return m_scopeLens; }

    // --- Runtime HUI Overlay ---

    /**
     * @brief Provide the current HUI draw commands to render as an overlay.
     * The vector must stay alive until RenderFrame() (which consumes it)
     * returns. Pass an empty list or skip calling to draw no overlay.
     */
    void SubmitHUICommands(const std::vector<Hephaestus::HephaestusUI::UIDrawCommand>& commands);

    /** @brief True when the Vulkan HUI overlay backend was initialized. */
    bool HasHUIOverlay() const;

    // --- Mesh Library Management ---

    /**
     * @brief Add a mesh to the library.
     * @param mesh MeshGPU to add (takes ownership)
     * @return Handle to the mesh
     */
    size_t AddMesh(MeshGPU mesh);

    /**
     * @brief Upload static mesh data to device-local buffers and add it to the library.
     * @return Mesh handle, or MeshRendererComponent::InvalidMesh when there is no geometry
     */
    size_t AddStaticMesh(const std::string& name, const MeshData& data,
                         const std::filesystem::path& sourcePath, bool imported);

    /**
     * @brief Upload a skinned mesh (rest-pose bone UBO, eval/palette joint indices).
     */
    size_t AddSkinnedMesh(const std::string& name,
                          const std::vector<SkinnedVertex>& vertices,
                          const std::vector<uint32_t>& indices,
                          const std::vector<MeshData::Submesh>& submeshes,
                          const AABB& bounds,
                          const std::vector<Mat4>& bonePalette,
                          const std::filesystem::path& sourcePath,
                          bool imported = false);

    /**
     * @brief Get mesh by handle.
     * @param handle Mesh handle from AddMesh
     * @return Pointer to mesh or nullptr if invalid
     */
    MeshGPU* GetMesh(size_t handle);

    /**
     * @brief Get mesh handle by source key.
     * @param sourceKey Mesh source key (path)
     * @return Handle or SIZE_MAX if not found
     */
    size_t GetMeshHandle(const std::string& sourceKey) const;

    // Static, visible mesh triangles in world space for probe baking. Editor only: packaged games keep no CPU copy.
    ProbeBakeGeometry CollectProbeBakeGeometry(Scene& scene);
    bool ProbeHardwareRayTracingAvailable() const { return m_probeGI.HardwareRayTracingAvailable(); }

    /**
     * @brief Register an additional lookup key (alias) for an existing mesh.
     * @param aliasKey Alternative key (e.g. relative asset path)
     * @param handle   Handle returned by AddMesh
     *
     * This allows the same mesh to be found by both its absolute source
     * path and a portable relative path such as "assets/meshes/Cube.glb".
     */
    void AddMeshAlias(const std::string& aliasKey, size_t handle);

    /** Destroy GPU mesh data keyed by source/alias so the next upload recreates it. */
    void InvalidateMesh(const std::string& sourceKey);

    /**
     * @brief Upload a mesh that is replaced often (construction shapes) into host-visible buffers.
     * Replacing never waits on the GPU: the previous buffers are released once no frame in flight reads them.
     * @param handle Slot to replace, or MeshRendererComponent::InvalidMesh for a new slot
     * @return The slot holding the mesh, or InvalidMesh when there is no geometry
     */
    size_t UploadDynamicMesh(size_t handle, const std::string& name, const MeshData& data);
    /** Free a slot from UploadDynamicMesh; its buffers are released after the frames in flight. */
    void ReleaseDynamicMesh(size_t handle);

    /** Wait until in-flight frames finish. Required before destroying GPU meshes. */
    void WaitGpuIdle();

    /**
     * @brief Get total mesh count.
     */
    size_t GetMeshCount() const { return m_meshLibrary.size(); }

    /**
     * @brief Access mesh library directly (for compatibility with existing code).
     */
    std::vector<MeshGPU>& GetMeshLibrary() { return m_meshLibrary; }
    const std::vector<MeshGPU>& GetMeshLibrary() const { return m_meshLibrary; }

    // --- Entity ID Picking ---

    /**
     * @brief Queue a GPU pick at swapchain pixel coordinates (see QtEditorEngine toPixelCoords).
     * @return Stale lastPickedEntity; use TakeViewportPickResult after the next successful frame
     *         to read the resolved id once GPU readback completes (typically one frame later).
     */
    uint32_t PickEntityAt(uint32_t screenX, uint32_t screenY);

    /**
     * @brief If a pick readback finished at the start of the last RenderFrame, return the entity id once.
     * @param outEntityId Raw uint32 entity handle; UINT32_MAX means empty / background.
     */
    bool TakeViewportPickResult(uint32_t& outEntityId);

    /**
     * @brief Check if ID picking is available.
     */
    bool IsPickingAvailable() const { return m_idPicker.initialized; }

    // One-shot readback of the completed editor frame, after 3D/editor/HUI drawing.
    // Request on the render thread; PollFrameCapture never waits for the GPU.
    struct FrameCapture {
        uint32_t width = 0, height = 0;
        uint64_t frame = 0;
        std::vector<uint8_t> rgba;
    };
    bool RequestFrameCapture(std::string& outError);
    bool PollFrameCapture(FrameCapture& out, std::string& outError);
    void CancelFrameCapture();

    // --- Transform Gizmo (Editor only) ---

    /**
     * @brief Get the transform gizmo for direct manipulation.
     */
    VulkanGizmo* GetGizmo() { return &m_gizmo; }
    
    /**
     * @brief Check if gizmo is available.
     */
    bool IsGizmoAvailable() const { return m_gizmoInitialized; }

    /**
     * @brief Update gizmo state for rendering.
     * @param selectedEntity Entity handle to manipulate (entt::null if none)
     * @param transform Transform of the selected entity
     * @param viewMatrix Current view matrix
     * @param projMatrix Current projection matrix
     * @param cameraPos Camera position
     * @param mode Gizmo mode (translate/rotate/scale)
     * @param space Coordinate space (world/local)
     */
    void UpdateGizmoState(uint32_t selectedEntity, const Mat4& transform,
                          const Mat4& viewMatrix, const Mat4& projMatrix,
                          const Vec3& cameraPos, int mode, int space);

    /**
     * @brief Check if gizmo is being hovered at screen coordinates.
     * @param screenX X coordinate in viewport pixels
     * @param screenY Y coordinate in viewport pixels  
     * @param outAxis Output: hovered axis (-1 if none)
     * @return true if hovering over gizmo
     */
    bool CheckGizmoHover(float screenX, float screenY, const Mat4& viewMatrix, 
                         const Mat4& projMatrix, int& outAxis);

    // --- Accessors ---

    VulkanDevice* GetDevice() const { return m_device; }
    VulkanSwapchain* GetSwapchain() const { return m_swapchain; }
    VulkanPipeline* GetPipeline() const { return m_pipeline.get(); }

    /** True when a wireframe graphics pipeline was created (requires fillModeNonSolid). */
    bool SupportsWireframe() const;

private:
    // Initialization helpers
    bool createPipeline();
    bool createSyncObjects();
    bool createCommandBuffers();
    bool createUniformBuffers();
    bool createDescriptorSets();
    bool createShadowResources();
    bool createBillboardResources();
    bool createSkinnedBoneLayout();
    bool createSkinnedMainPipelines();
    bool createSkinnedShadowPipeline();
    bool createSkinnedOutlinePipelines();
    bool createSkinnedIdPipelines();
    void cleanupSkinnedGraphicsPipelines();
    void cleanupSkinnedResources();
    void allocateSkinnedBoneDescriptor(MeshGPU& mesh);
    VkDescriptorSet resolveSkinnedBoneSet(entt::entity entity, const CharacterComponent* character,
                                          const MeshGPU& mesh);
    void destroyCharacterBonePalettes();
    VkPipeline getSkinnedPipeline(bool backfaceCulling, bool wireframe) const;

    // Per-frame helpers
    void updateCameraUBO(uint32_t imageIndex, const RenderCamera& camera,
                         const FrameRenderConfig& config);
    void updateLightingUBO(uint32_t imageIndex, Scene* scene, const RenderCamera& camera);
    void recordShadowPass(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene, const RenderCamera& camera,
                          const FrameRenderConfig& config);
    void recordMainPass(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene, 
                        const FrameRenderConfig& config);
    void recordGizmo(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene);
    void recordBillboards(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene);
    void recordLightGizmos(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene,
                           const FrameRenderConfig& config);
    void recordOutlines(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene, 
                        const FrameRenderConfig& config);
    void recordGrid(VkCommandBuffer cmd, uint32_t imageIndex, const FrameRenderConfig& config);
    void recordDebugOverlay(VkCommandBuffer cmd, uint32_t imageIndex, const FrameRenderConfig& config);
    void recordHUIOverlay(VkCommandBuffer cmd, uint32_t imageIndex);
    bool createUIResources();
    bool createGridResources();
    bool createDebugOverlayResources();
    bool createToneMapResources();
    void cleanupToneMapResources();
    void recordToneMap(VkCommandBuffer cmd, Scene* scene, uint32_t imageIndex);
    bool createBloomResources();
    void cleanupBloomResources();
    bool bloomEnabled(Scene* scene);
    void recordBloom(VkCommandBuffer cmd, const EnvironmentComponent::PostProcess& post);
    bool createOutlineResources();
    void cleanupOutlineResources();
    
    // ID picking helpers
    bool createIdPickerResources();
    void recordIdPass(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene,
                      const FrameRenderConfig& config);
    void cleanupIdPickerResources();
    uint32_t resolvePickRegion() const;
    
    // Gizmo helpers (Editor only)
    bool createGizmoResources();
    void cleanupGizmoResources();
    bool createLightGizmoResources();
    void cleanupLightGizmoResources();

    // Cleanup helpers
    void cleanupSwapchainResources();
    void cleanupBillboardResources();
    void destroyAllResources();

private:
    bool m_initialized = false;
    bool m_swapchainDirty = false;
    bool m_deviceLost = false;
    uint32_t m_pendingWidth = 0;
    uint32_t m_pendingHeight = 0;

    // External resources (not owned)
    VulkanDevice* m_device = nullptr;
    VulkanSwapchain* m_swapchain = nullptr;

    // Runtime HUI overlay (Vulkan backend for the packaged game UI)
    std::unique_ptr<Hephaestus::HephaestusUI::VulkanHUIRenderer> m_uiOverlay;
    const std::vector<Hephaestus::HephaestusUI::UIDrawCommand>* m_uiCommands = nullptr;
    bool m_uiCommandsSubmitted = false;

    // Owned resources
    std::unique_ptr<VulkanPipeline> m_pipeline;

    // Frame timing
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t m_currentFrame = 0;
    uint64_t m_renderedFrameCount = 0;
    struct CaptureReadback {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        uint32_t width = 0, height = 0;
        uint64_t frame = 0;
        VkFence fence = VK_NULL_HANDLE;
        bool requested = false, submitted = false, cancelled = false;
        std::vector<uint8_t> pixels;
        std::string error;
    } m_capture;
    void finishCapture();

    // Synchronization
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    std::vector<VkFence> m_imagesInFlight;

    // Command buffers
    std::vector<VkCommandBuffer> m_commandBuffers;

    // Uniform buffers
    std::vector<VulkanBuffer> m_uniformBuffers;      // Camera UBO
    std::vector<VulkanBuffer> m_lightingBuffers;     // Lighting UBO
    std::vector<VulkanBuffer> m_materialBuffers;     // Dynamic material UBO

    // Descriptor pool and sets
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    // Dummy texture for unbound slots
    VulkanTexture m_dummyTexture;

    // Mesh library
    std::vector<MeshGPU> m_meshLibrary;
    std::unordered_map<std::string, size_t> m_meshLibraryLookup;

    struct SkinnedPass {
        bool layoutReady = false;
        VkDescriptorSetLayout boneSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool bonePool = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipeline pipelineBackface = VK_NULL_HANDLE;
        VkPipeline pipelineWireframe = VK_NULL_HANDLE;
        VkPipeline pipelineWireframeBackface = VK_NULL_HANDLE;
        VkPipelineLayout shadowLayout = VK_NULL_HANDLE;
        VkPipeline shadowPipeline = VK_NULL_HANDLE;
        VkPipelineLayout outlineLayout = VK_NULL_HANDLE;
        VkPipeline outlinePipeline = VK_NULL_HANDLE;
        VkPipeline outlineMaskPipeline = VK_NULL_HANDLE;
        VkPipelineLayout idLayout = VK_NULL_HANDLE;
        VkPipeline idPipeline = VK_NULL_HANDLE;
        VkPipeline idPipelineBackface = VK_NULL_HANDLE;
    };
    SkinnedPass m_skinned;

    // One palette per frame in flight: a frame still on the GPU keeps reading its own pose while the next is written.
    struct CharacterBoneGpu {
        VulkanBuffer buffers[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSet sets[MAX_FRAMES_IN_FLIGHT] = {};
        uint64_t writtenFrame[MAX_FRAMES_IN_FLIGHT] = {};
        uint64_t lastUsedFrame = 0;
    };
    std::unordered_map<uint32_t, CharacterBoneGpu> m_characterBonePalettes;
    struct RetiredMesh {
        MeshGPU mesh;
        uint64_t lastUsedFrame = 0;
    };
    std::unordered_map<uint64_t, RetiredMesh> m_retiredMeshes;
    uint64_t m_nextRetiredMesh = 0;
    std::vector<size_t> m_freeDynamicMeshSlots;
    void retireMesh(MeshGPU&& mesh);
    uint64_t m_frameSerial = 0;
    std::chrono::steady_clock::time_point m_lastPacedTime{};
    bool m_hasPacedTime = false;
    double m_pacedFrameSeconds = 0.0;
    void releaseCharacterBonePalette(CharacterBoneGpu& gpu);

    // Material texture slots
    struct MaterialTextureSlot {
        bool valid = false;
        VulkanTexture texture;
        VkDescriptorImageInfo descriptor{};
        std::string key;
        std::string resolvedPath;
        bool linear = false;
        uint64_t lastUsedFrame = 0; // the frame that last looked it up
    };
    // A texture whose slot was given to another, destroyed once every swapchain image's descriptors were rewritten
    // and no frame in flight can read it.
    struct RetiredTexture {
        VulkanTexture texture;
        uint64_t lastUsedFrame = 0;
    };
    std::unordered_map<uint64_t, RetiredTexture> m_retiredTextures;
    uint64_t m_nextRetiredTexture = 0;
    uint32_t m_maxMaterialTextureSize = 0;
    ScopeLens m_scopeLens;
    std::vector<MaterialTextureSlot> m_materialTextureSlots;
    std::unordered_map<std::string, uint32_t> m_materialTextureLookup;
    std::vector<std::vector<uint32_t>> m_pendingTextureSlotUpdates;
    uint32_t m_defaultMaterialTextureSlot = 0;

    // Per-frame material UBO parameters
    VkDeviceSize m_materialStride = 0;           // Aligned stride for one material in the dynamic UBO
    static constexpr uint32_t kMaxMaterialsPerFrame = 1024;

    // Material asset cache (hot-reloads from .hematerial files)
    // A material's compiled graph (`.hemshader`); its pipelines are built on first draw and dropped with the swapchain.
    struct MaterialShaderProgram {
        CompiledMaterialShader compiled;
        SurfacePipelines pipelines;
        bool pipelinesFailed = false;
    };
    struct MaterialCacheEntry {
        MaterialComponent material;
        std::filesystem::file_time_type lastWriteTime{};
        std::filesystem::file_time_type shaderWriteTime{};
        std::unique_ptr<MaterialShaderProgram> shader;
    };
    std::unordered_map<const MaterialComponent*, MaterialShaderProgram*> m_materialShaderPrograms;
    std::unordered_map<const MaterialComponent*, std::unique_ptr<MaterialShaderProgram>> m_materialShaderOverrides;
    /// The compiled graph's pipelines for a material, or null to draw it with the standard surface shader.
    const SurfacePipelines* materialShaderPipelines(const MaterialComponent* material);
    void destroyMaterialShaderPipelines();
    std::unordered_map<std::string, MaterialCacheEntry> m_materialAssetCache;
    std::unordered_map<std::string, std::filesystem::file_time_type> m_materialAssetCacheFailed;
    // What each material path resolved to and when its file was last checked, so draws don't touch the disk.
    struct MaterialPathCheck {
        const MaterialComponent* material = nullptr;
        std::chrono::steady_clock::time_point checkedAt{};
    };
    std::unordered_map<std::string, MaterialPathCheck> m_materialPathChecks;
    static constexpr std::chrono::milliseconds kMaterialRecheckInterval{500};
    // Each model's default slot materials (MeshSettings, <model>.hpmesh), rechecked like material paths.
    struct MeshDefaultsCheck {
        std::vector<std::string> materials;
        std::chrono::steady_clock::time_point checkedAt{};
    };
    std::unordered_map<std::string, MeshDefaultsCheck> m_meshDefaultMaterials;
    const std::vector<std::string>& meshDefaultMaterials(const std::string& meshAssetPath);

    // Material system helpers
    const MaterialComponent* resolveMaterialFromPath(const std::string& materialPath);
    const MaterialComponent* loadMaterialFromPath(const std::string& materialPath);
    uint32_t ensureMaterialTextureSlot(const std::string& rawPath, bool linear = false);
    void updateEnvironmentReflectionDescriptor(uint32_t imageIndex);
    EnvironmentComponent* findEnvironment(Scene* scene);
    const MaterialComponent* resolveMaterialForSlot(Entity& entity, const MeshRendererComponent& renderer, int slot);
    void prepareEnvironment(Scene* scene, const FrameRenderConfig& config, uint32_t imageIndex);
    void updateTextureSlotInDescriptors(uint32_t slotIndex, uint32_t imageIndex);
    void flushPendingTextureDescriptorUpdates(uint32_t imageIndex);

    // Per-frame stats (written during RenderFrame, read via GetLastFrameStats)
    FrameStats m_lastFrameStats;

    // Debug: material system logging
    bool m_materialDebugLogged = false;
    int m_materialDebugDrawCount = 0;

    // Shadow resources
    struct ShadowMap {
        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        uint32_t resolution = 4096;
        bool IsValid() const { return image != VK_NULL_HANDLE; }
        void Destroy();
    };
    ShadowMap m_directionalShadow;

    // Point and spot light shadows share one depth atlas of 512x512 tiles (shaders/triangle.frag:localShadow).
    static constexpr uint32_t kLocalShadowTile = 512;
    static constexpr uint32_t kLocalShadowColumns = 8;
    static constexpr uint32_t kLocalShadowRows = 4;
    static constexpr int kMaxShadowedPointLights = 4;  // six tiles each
    static constexpr int kMaxShadowedSpotLights = 8;
    ShadowMap m_localShadow;  // shares m_directionalShadow's render pass, pipelines and sampler
    struct LocalShadowJob {
        Mat4 viewProj;
        Vec3 lightPos;
        float range = 0.0f;
        uint32_t tile = 0;
    };
    std::vector<LocalShadowJob> m_localShadowJobs;  // filled by updateLightingUBO, drawn by recordLocalShadowPass
    bool m_traceShadows = false;    // some light asked for ray-traced shadows this frame (set by updateLightingUBO)
    bool m_sunRayTraced = false;    // the sun's shadow is ray traced, so the cascade pass is skipped
    bool createLocalShadowResources();
    void transitionShadowMapForSampling(VkImage image);
    void recordLocalShadowPass(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene, const FrameRenderConfig& config);
    // The ray-query build of triangle.frag reads the scene acceleration structure in ProbeGI's set.
    const char* surfaceFragmentShader() const {
        return m_probeGI.HardwareRayTracingAvailable() ? "triangle_rt.frag.spv" : "triangle.frag.spv";
    }
    void drawShadowCasters(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene, const FrameRenderConfig& config,
                           const Mat4& viewProj, const Vec3* cullCenter, float cullRadius);
    // Masked (alpha cutout) static surfaces in the shadow passes; reads materialTextures from the main set 0.
    VkPipelineLayout m_maskedShadowLayout = VK_NULL_HANDLE;
    VkPipeline m_maskedShadowPipeline = VK_NULL_HANDLE;
    bool createMaskedShadowPipeline();
    void destroyMaskedShadowPipeline();

    std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();  // material graph Time
    ProbeGI m_probeGI;  // set 2 of the main and skinned surface pipelines
    void recordProbeGISunPass(VkCommandBuffer cmd, Scene* scene);

    // Billboard resources for light icons
    struct BillboardPipeline {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
        bool initialized = false;
        void Cleanup();
    };
    BillboardPipeline m_billboardPipeline;
    VkDescriptorPool m_billboardDescriptorPool = VK_NULL_HANDLE;
    // Per Hephaestus::EditorIcon; an icon whose image is missing has no sets and is not drawn.
    std::array<VulkanTexture, static_cast<size_t>(Hephaestus::EditorIcon::Count)> m_editorIconTextures;
    std::array<std::vector<VkDescriptorSet>, static_cast<size_t>(Hephaestus::EditorIcon::Count)> m_editorIconSets;

    // Billboard ID picking pipeline (renders billboards into the entity-ID buffer)
    struct BillboardIdPipeline {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        bool initialized = false;

        struct PushConstants {
            float posX, posY, posZ;
            float size;
            uint32_t entityId;
            uint32_t padding[3];
        };
    };
    BillboardIdPipeline m_billboardIdPipeline;

    // Skybox
    struct Skybox {
        bool initialized = false;
        bool hasHDRI = false;
        VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptorSets;

        // Environment texture (loaded HDRI or fallback)
        VulkanTexture environmentTexture;
        IrradianceSH irradianceSH{};

        // Cube vertex buffer for skybox
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexAllocation = VK_NULL_HANDLE;

        // Cached loaded path (empty if none loaded)
        std::string loadedHDRIPath;

        // Push constants for the skybox shader
        struct SkyboxPushConstants {
            float intensity = 1.0f;
            float solidColorR = 0.1f;
            float solidColorG = 0.2f;
            float solidColorB = 0.3f;
            int useSolidColor = 1; // 1 = solid, 0 = HDRI
        };

        bool Init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                  VmaAllocator allocator, VkPhysicalDevice physDevice,
                  VkCommandPool cmdPool, VkQueue queue, uint32_t frameCount);
        void Recreate(VkDevice device, VkRenderPass renderPass, VkExtent2D extent);
        bool LoadHDRI(const std::string& path, VmaAllocator allocator,
                      VkPhysicalDevice physDevice, VkCommandPool cmdPool, VkQueue queue,
                      VkDevice device);
        void UpdateDescriptorSet(uint32_t imageIndex, VkBuffer cameraUBO, VkDevice device);
        void Draw(VkCommandBuffer cmd, uint32_t imageIndex, const SkyboxPushConstants& pc);
        void Cleanup(VmaAllocator allocator, VkDevice device);

    private:
        VkDevice m_device = VK_NULL_HANDLE;
        void CreateCubeVertexBuffer(VmaAllocator allocator);
        void CreateDescriptorSetLayout(VkDevice device);
        void CreatePipeline(VkDevice device, VkRenderPass renderPass, VkExtent2D extent);
        static std::vector<char> ReadShaderFile(const std::string& name);
        VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code);
    };
    Skybox m_skybox;
    bool createSkyboxResources();
    std::string m_lastSkyboxPath;  // For live-editing change detection
    float m_lastSkyboxIntensity = -1.0f;
    std::string m_projectRoot;    // Project root for resolving relative asset paths

    // Editor grid
    struct EditorGrid {
        bool initialized = false;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptorSets;
        
        struct GridPushConstants {
            float gridSize = 100.0f;      // Size of grid in world units
            float cellSize = 1.0f;        // Size of each cell
            float fadeDistance = 50.0f;   // Distance to start fading
            float lineWidth = 1.0f;       // Line width in pixels
        };

        void Init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                  VkDescriptorSetLayout cameraDescLayout, VmaAllocator allocator);
        void Recreate(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                      VkDescriptorSetLayout cameraDescLayout);
        void Draw(VkCommandBuffer cmd, VkDescriptorSet cameraDescSet, const GridPushConstants& pc);
        void Cleanup(VkDevice device);
    };
    EditorGrid m_editorGrid;

    struct DebugOverlay {
        bool initialized = false;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        // One buffer per frame in flight: a frame still on the GPU keeps reading its own while the next is written
        // or grown.
        VulkanBuffer vertexBuffers[MAX_FRAMES_IN_FLIGHT];
        uint32_t vertexCapacity[MAX_FRAMES_IN_FLIGHT] = {};
        uint32_t vertexCount = 0;
    };
    DebugOverlay m_debugOverlay;

    struct ToneMap {
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        uint32_t frame = 0;
        bool bloomThisFrame = false;
    };
    ToneMap m_toneMap;

    struct Bloom {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        std::vector<VkImageView> mipViews;
        std::vector<VkExtent2D> mipSizes;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> sets;  // bright pass, downsamples, then upsamples
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        bool layoutReady = false;
    };
    Bloom m_bloom;

    // Entity ID picker for viewport selection
    struct IdPicker {
        bool initialized = false;
        
        // Offscreen ID buffer
        VkImage colorImage = VK_NULL_HANDLE;
        VkImageView colorImageView = VK_NULL_HANDLE;
        VmaAllocation colorAllocation = VK_NULL_HANDLE;
        VkImage depthImage = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;
        VmaAllocation depthAllocation = VK_NULL_HANDLE;
        
        // Render pass and framebuffer
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        
        // Pipelines for rendering entity IDs
        VkPipeline pipeline = VK_NULL_HANDLE; // No culling
        VkPipeline pipelineBackface = VK_NULL_HANDLE; // Backface culling enabled
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        
        // Readback staging buffer (sized for SAMPLE_SIDE x SAMPLE_SIDE region)
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = VK_NULL_HANDLE;
        void* mappedStagingBuffer = nullptr;
        
        // Current dimensions
        uint32_t width = 0;
        uint32_t height = 0;
        
        // Multi-pixel sample region (must be odd). 5x5 = 25 pixels.
        static constexpr uint32_t SAMPLE_RADIUS = 2;
        static constexpr uint32_t SAMPLE_SIDE = SAMPLE_RADIUS * 2 + 1;
        static constexpr uint32_t SAMPLE_PIXEL_COUNT = SAMPLE_SIDE * SAMPLE_SIDE;
        static constexpr uint32_t STAGING_BYTES = SAMPLE_PIXEL_COUNT * 4; // RGBA8
        
        // GPU pick state
        bool pickCopyRequested = false;
        bool pickReadPending = false;
        bool pickJustCompleted = false;
        uint32_t pickX = 0;
        uint32_t pickY = 0;
        uint32_t sampleOriginX = 0; // top-left of the copied region (clamped)
        uint32_t sampleOriginY = 0;
        uint32_t sampleW = 0;       // actual copied width/height (may be < SAMPLE_SIDE near edges)
        uint32_t sampleH = 0;
        uint32_t lastPickedEntity = UINT32_MAX;
        
        struct IdPushConstants {
            Mat4 model;
            uint32_t entityId;
            uint32_t padding[3];
        };
    };
    IdPicker m_idPicker;

    // Selection outline
    struct OutlineRenderer {
        bool initialized = false;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipeline maskPipeline = VK_NULL_HANDLE; // Depth-only pass so hidden meshes still clip the hull
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        
        // Uses the same descriptor set layout as the main pipeline (camera UBO)
        struct OutlinePushConstants {
            Mat4 model;
            float expand;
            float padding[3];
        };
        
        void Init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                  VkDescriptorSetLayout cameraDescLayout, VmaAllocator allocator);
        void Recreate(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                      VkDescriptorSetLayout cameraDescLayout);
        void Draw(VkCommandBuffer cmd, VkDescriptorSet cameraDescSet, 
                  VkBuffer vertexBuffer, VkBuffer indexBuffer, uint32_t indexCount,
                  const Mat4& modelMatrix, bool depthMaskFirst);
        void Cleanup(VkDevice device);
    };
    OutlineRenderer m_outlineRenderer;

    // Transform gizmo (editor-only)
    VulkanGizmo m_gizmo;
    bool m_gizmoInitialized = false;
    uint32_t m_gizmoTargetEntity = UINT32_MAX;
    Mat4 m_gizmoTargetTransform = Mat4::Identity();
    Mat4 m_gizmoViewMatrix = Mat4::Identity();
    Mat4 m_gizmoProjectionMatrix = Mat4::Identity();
    Vec3 m_gizmoCameraPos = Vec3(0, 0, 0);

    // Directional light gizmo (editor-only)
    VulkanLightGizmo m_lightGizmo;
    bool m_lightGizmoInitialized = false;
    Mat4 m_frameViewMatrix = Mat4::Identity();        // camera of the frame being recorded
    Mat4 m_frameProjectionMatrix = Mat4::Identity();
    Vec3 m_frameCameraPos = Vec3(0, 0, 0);
    Mat4 m_frameViewFrame = Mat4::Identity();         // forward/up/right world frame of the frame's view

    bool m_editorOverlays = false;
};

// Releases and erases the entries (with a lastUsedFrame) that no frame still in flight can be reading.
template <typename Map, typename Release>
void RetireIdleEntries(Map& entries, uint64_t frame, uint64_t framesInFlight, Release&& release)
{
    for (auto it = entries.begin(); it != entries.end();) {
        if (frame - it->second.lastUsedFrame >= framesInFlight) {
            release(it->second);
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace Hephaestus
