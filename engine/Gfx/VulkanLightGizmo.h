#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "../Core/Math.h"
#include "vk_mem_alloc.h"

// Forward declarations
struct VulkanBuffer;

/**
 * @brief Editor-only visual gizmo for directional lights.
 *
 * Draws a camera-facing sun glyph at the light, plus a direction arrow and
 * dashed parallel rays along the light's forward axis (-Z, the direction the
 * light travels). It ignores depth, keeps a constant screen size, brightens
 * when the light is selected, and shares gizmo.vert/gizmo.frag with VulkanGizmo.
 *
 * Only rendered in editor mode; never included in play/runtime builds.
 */
class VulkanLightGizmo {
public:
    VulkanLightGizmo();
    ~VulkanLightGizmo();

    /** Initialise pipeline + geometry.  Call once after the device is ready. */
    void Init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
              VkDescriptorSetLayout cameraDescriptorLayout, VmaAllocator allocator);

    /** Recreate the pipeline after a swapchain resize. */
    void Recreate(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                  VkDescriptorSetLayout cameraDescriptorLayout);

    /** Destroy all GPU resources. */
    void Cleanup(VkDevice device, VmaAllocator allocator);

    /**
     * @brief Draw one directional-light gizmo inside the main render pass.
     *
     * @param worldMatrix   Full world transform of the light entity.
     * @param viewMatrix    Current view matrix (orients the sun glyph).
     * @param projMatrix    Current projection matrix (constant screen size).
     * @param cameraPos     Current camera position.
     * @param isSelected    True when this entity is the editor selection.
     * @param isHovered     True when the cursor is hovering the entity.
     */
    void Draw(VkCommandBuffer cmd, VkDescriptorSet cameraDescSet,
              const Mat4& worldMatrix, const Mat4& viewMatrix,
              const Mat4& projMatrix, const Vec3& cameraPos,
              bool isSelected, bool isHovered);

private:
    struct GeometryBuffers {
        VulkanBuffer* vertexBuffer = nullptr;
        VulkanBuffer* indexBuffer  = nullptr;
        uint32_t      indexCount   = 0;
    };

    void GenerateGeometry(VmaAllocator allocator);
    void CreatePipeline(VkDevice device, VkRenderPass renderPass,
                        VkExtent2D extent,
                        VkDescriptorSetLayout cameraDescLayout);
    void DrawRange(VkCommandBuffer cmd, uint32_t firstIndex, uint32_t indexCount,
                   const Mat4& model, const Vec4& state) const;

    // Vulkan resources
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    GeometryBuffers  m_geo;
    uint32_t         m_bodyIndexCount = 0;   // arrow and rays; the sun glyph follows

    // Tuning, in gizmo units
    static constexpr float kArrowLength  = 2.0f;
    static constexpr float kArrowRadius  = 0.028f;
    static constexpr float kRayOffset    = 0.34f;
    static constexpr float kRayStart     = 0.4f;
    static constexpr float kRayLength    = 1.5f;
    static constexpr float kRayRadius    = 0.014f;
    static constexpr float kSunRadius    = 0.2f;
    static constexpr float kScreenScale  = 0.18f;
};
