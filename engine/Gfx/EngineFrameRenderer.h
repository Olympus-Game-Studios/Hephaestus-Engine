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

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <memory>
#include <functional>
#include <string>
#include <unordered_map>

#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "../Assets/MeshAsset.h"

// Forward declarations
class VulkanDevice;
class VulkanSwapchain;
class VulkanPipeline;
class Scene;

namespace Hephaestus {

/**
 * @brief Camera data for rendering - platform agnostic.
 */
struct RenderCamera {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 5.0f;
    float yawDeg = 90.0f;
    float pitchDeg = 0.0f;
    float nearPlane = 0.1f;
    float farPlane = 10000.0f;
    
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
    bool isEditorMode = true;
    bool isPlaying = false;
    bool isPaused = false;
    
    // Clear color (used if no skybox)
    float clearColorR = 0.1f;
    float clearColorG = 0.2f;
    float clearColorB = 0.3f;
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
     * @return true if initialization succeeded
     */
    bool Init(VulkanDevice* device, VulkanSwapchain* swapchain);

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
     * @brief Shutdown and cleanup all resources.
     */
    void Shutdown();

    /**
     * @brief Check if renderer is initialized and ready.
     */
    bool IsInitialized() const { return m_initialized; }

    // --- Mesh Library Management ---

    /**
     * @brief Add a mesh to the library.
     * @param mesh MeshGPU to add (takes ownership)
     * @return Handle to the mesh
     */
    size_t AddMesh(MeshGPU mesh);

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

    /**
     * @brief Get total mesh count.
     */
    size_t GetMeshCount() const { return m_meshLibrary.size(); }

    /**
     * @brief Access mesh library directly (for compatibility with existing code).
     */
    std::vector<MeshGPU>& GetMeshLibrary() { return m_meshLibrary; }
    const std::vector<MeshGPU>& GetMeshLibrary() const { return m_meshLibrary; }

    // --- Accessors ---

    VulkanDevice* GetDevice() const { return m_device; }
    VulkanSwapchain* GetSwapchain() const { return m_swapchain; }
    VulkanPipeline* GetPipeline() const { return m_pipeline.get(); }

private:
    // Initialization helpers
    bool createPipeline();
    bool createSyncObjects();
    bool createCommandBuffers();
    bool createUniformBuffers();
    bool createDescriptorSets();
    bool createShadowResources();
    bool createBillboardResources();

    // Per-frame helpers
    void updateCameraUBO(uint32_t imageIndex, const RenderCamera& camera);
    void updateLightingUBO(uint32_t imageIndex, Scene* scene);
    void recordShadowPass(VkCommandBuffer cmd, Scene* scene);
    void recordMainPass(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene, 
                        const FrameRenderConfig& config);
    void recordGizmo(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene);
    void recordBillboards(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene);
    void recordOutlines(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene);

    // Cleanup helpers
    void cleanupSwapchainResources();
    void destroyAllResources();

private:
    bool m_initialized = false;
    bool m_swapchainDirty = false;
    uint32_t m_pendingWidth = 0;
    uint32_t m_pendingHeight = 0;

    // External resources (not owned)
    VulkanDevice* m_device = nullptr;
    VulkanSwapchain* m_swapchain = nullptr;

    // Owned resources
    std::unique_ptr<VulkanPipeline> m_pipeline;

    // Frame timing
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t m_currentFrame = 0;

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

    // Material texture slots
    struct MaterialTextureSlot {
        bool valid = false;
        VulkanTexture texture;
    };
    std::vector<MaterialTextureSlot> m_materialTextureSlots;
    std::unordered_map<std::string, uint32_t> m_materialTextureLookup;
    uint32_t m_defaultMaterialTextureSlot = 0;

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
        uint32_t resolution = 2048;
        bool IsValid() const { return image != VK_NULL_HANDLE; }
        void Destroy();
    };
    ShadowMap m_directionalShadow;

    // Billboard resources for light icons
    struct BillboardPipeline {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
        void Cleanup();
    };
    BillboardPipeline m_billboardPipeline;
    VkDescriptorPool m_billboardDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_billboardDescriptorSets_Point;
    std::vector<VkDescriptorSet> m_billboardDescriptorSets_Spot;
    std::vector<VkDescriptorSet> m_billboardDescriptorSets_Directional;
    VulkanTexture m_pointLightTexture;
    VulkanTexture m_spotLightTexture;
    VulkanTexture m_directionalLightTexture;

    // Skybox
    struct Skybox {
        bool hasHDRI = false;
        VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptorSets;
        VkSampler sampler = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        bool LoadHDRI(const std::string& path, VmaAllocator allocator,
                      VkPhysicalDevice physDevice, VkCommandPool cmdPool, VkQueue queue);
        void UpdateDescriptorSet(uint32_t imageIndex, VkBuffer cameraUBO);
        void Draw(VkCommandBuffer cmd, uint32_t imageIndex);
        void Cleanup(VmaAllocator allocator);
    };
    Skybox m_skybox;
};

} // namespace Hephaestus
