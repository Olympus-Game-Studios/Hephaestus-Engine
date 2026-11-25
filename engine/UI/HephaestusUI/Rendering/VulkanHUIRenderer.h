#pragma once

#include "HUIGeometryBuilder.h"
#include "HUIRenderBackend.h"
#include "UIFontAtlas.h"

#include "../../../Gfx/VulkanBuffer.h"
#include "../../../Gfx/VulkanTexture.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class VulkanDevice;

namespace Hephaestus::HephaestusUI {

/**
 * VulkanHUIRenderer.h
 *
 * Vulkan backend for the HephaestusUI runtime renderer (Stage 8A).
 *
 * Consumes the renderer-independent HUI draw command list and draws it as an
 * overlay inside the shared swapchain render pass (after the 3D scene):
 *
 *   - Batched dynamic vertex/index buffers (one set per swapchain image)
 *   - Single lightweight hui.vert/hui.frag pipeline
 *   - SDF rounded-rect corners, premultiplied alpha blending
 *   - Depth test/write disabled (UI is an overlay)
 *   - Dynamic scissor rectangles for widget clipping
 *   - Lazy glyph atlas (stb_truetype) cached per font/size/style
 *   - Texture cache reusing the engine VulkanTexture path
 *
 * No Qt dependency. Renderer-agnostic layout/behaviour stays in the HUI core.
 */
class VulkanHUIRenderer final : public IHUIRenderBackend, public IFontAtlasProvider {
public:
    VulkanHUIRenderer() = default;
    ~VulkanHUIRenderer() override;

    VulkanHUIRenderer(const VulkanHUIRenderer&) = delete;
    VulkanHUIRenderer& operator=(const VulkanHUIRenderer&) = delete;

    /** Create Vulkan resources. The device must outlive this renderer. */
    bool Init(VulkanDevice* device, VkRenderPass renderPass, uint32_t subpass, VkFormat swapchainFormat,
              const VkExtent2D& extent, uint32_t swapchainImageCount);

    /** Recreate swapchain-dependent resources (pipeline + per-image buffers). */
    void OnSwapchainRecreated(VkRenderPass renderPass, uint32_t subpass, VkFormat swapchainFormat,
                              const VkExtent2D& extent, uint32_t swapchainImageCount);

    /** Destroy swapchain-dependent resources (kept alive: textures/atlases). */
    void OnSwapchainDestroyed();

    /** Destroy all owned resources. */
    void Shutdown();

    // ── IHUIRenderBackend ────────────────────────────────────────────────────
    void SetViewportSize(const UIVec2& viewportSize) override;
    void SetProjectRoot(const std::string& projectRoot) override;
    void SetDrawCommands(const std::vector<UIDrawCommand>& commands) override;
    void ClearDrawCommands() override;
    void PrepareFrame() override;

    // ── IFontAtlasProvider ───────────────────────────────────────────────────
    UIFontAtlas* GetOrCreateAtlas(const UIFontKey& key) override;

    /**
     * Record the UI overlay into the swapchain render pass. Call after
     * PrepareFrame() for the same frame. `imageIndex` selects the per-image
     * vertex/index buffers (safe to write: their previous command buffer has
     * completed).
     */
    void RecordDraw(VkCommandBuffer cmd, uint32_t imageIndex);

private:
    struct UITextureEntry {
        VulkanTexture texture;
        VkDescriptorSet set = VK_NULL_HANDLE;
        uint32_t width = 1;
        uint32_t height = 1;
        bool valid = false;
    };

    struct AtlasEntry {
        std::unique_ptr<UIFontAtlas> atlas;
        uint32_t slot = 0;
    };

    /** A GPU texture (and its descriptor set) that was replaced and must be
     *  destroyed only after in-flight frames finish sampling it. */
    struct RetiredTexture {
        VulkanTexture texture;
        VkDescriptorSet set = VK_NULL_HANDLE;
        uint32_t retireFrame = 0;
    };

    static constexpr uint32_t kMaxFramesInFlight = 2;

    struct FrameBuffers {
        VulkanBuffer vertex;
        VulkanBuffer index;
        uint32_t vertexCapacity = 0;
        uint32_t indexCapacity = 0;
    };

    struct PushConstants {
        float viewportW = 1.0f;
        float viewportH = 1.0f;
        float srgbOutput = 0.0f; // 1 when the swapchain color attachment is sRGB
        float pad = 0.0f;
    };

    // Resource creation
    bool createPipeline(VkRenderPass renderPass);
    bool createFrameBuffers();
    bool createDescriptorResources();
    void destroyFrameBuffers();
    void destroyDescriptorResources();
    void destroyPipelineResources();
    void destroyWhiteTexture();

    // Texture cache
    UITextureInfo ResolveTexture(const std::string& imageId);
    uint32_t ReserveTextureSlot(uint32_t width, uint32_t height);
    void WriteDescriptorSet(uint32_t slot);
    VkDescriptorSet GetDescriptorSetForSlot(int32_t slot) const;
    std::string ResolveAssetPath(const std::string& imageId) const;

    // Atlas GPU upload
    void UploadDirtyAtlases();
    void ReapRetiredTextures();
    bool UploadPixelsToTexture(VulkanTexture& texture, const uint8_t* pixels,
                               uint32_t width, uint32_t height);
    /** Overwrites an existing texture's pixels (same size) with a staged copy. */
    bool CopyPixelsToTexture(VulkanTexture& texture, const uint8_t* pixels, uint32_t width, uint32_t height);
    /** A UIDynamicImages image: its slot, re-uploaded when the store's version moves on. */
    UITextureInfo ResolveDynamicTexture(const std::string& imageId);

    // Recording helpers
    static VkShaderModule LoadShaderModule(VkDevice device, const std::string& name);
    bool EnsureFrameBuffers(uint32_t imageIndex, uint32_t vertexCount, uint32_t indexCount);

    VulkanDevice* m_Device = nullptr;
    VmaAllocator m_Allocator = VK_NULL_HANDLE;
    uint32_t m_SwapchainImageCount = 0;

    UIVec2 m_Viewport{};
    std::string m_ProjectRoot;
    std::vector<std::string> m_FontSearchPaths;

    // Commands (owned by the caller during the frame)
    const std::vector<UIDrawCommand>* m_Commands = nullptr;

    // Per-frame geometry
    UIBatchData m_Data;
    HUIGeometryBuilder m_GeometryBuilder;
    bool m_GeometryReady = false;

    // Pipeline resources
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    uint32_t m_Subpass = 0;
    bool m_SrgbOutput = false;

    // Per-image vertex/index buffers
    std::vector<FrameBuffers> m_FrameBuffers;

    // Texture cache (slot 0 = solid white)
    std::vector<UITextureEntry> m_Textures;
    struct DynamicTextureEntry {
        uint32_t slot = 0;
        uint64_t version = 0;
    };
    std::unordered_map<std::string, DynamicTextureEntry> m_DynamicTextures;
    std::unordered_map<std::string, uint32_t> m_TextureLookup;
    std::unordered_map<std::string, bool> m_MissingAssetsLogged;

    // Font atlas cache
    std::unordered_map<UIFontKey, AtlasEntry, UIFontKeyHash> m_Atlases;
    std::unordered_map<UIFontKey, bool, UIFontKeyHash> m_MissingFontsLogged;
    std::vector<RetiredTexture> m_RetiredTextures;
    uint32_t m_FrameCounter = 0;

    bool m_Initialized = false;
};

} // namespace Hephaestus::HephaestusUI