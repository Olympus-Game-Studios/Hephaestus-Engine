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
 * Renders a wireframe arrow + parallel ray indicators to show the light's
 * forward direction (the direction light rays travel).  The gizmo:
 *   - Always aligns with the light entity's rotation (-Z forward).
 *   - Ignores depth testing so it's always visible.
 *   - Scales to a constant screen size regardless of camera distance.
 *   - Highlights on hover and selection.
 *   - Reuses the existing gizmo.vert/gizmo.frag shaders and GizmoVertex format.
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
     * @brief Draw one directional-light gizmo.
     *
     * Call this once per directional light entity while recording the main
     * render pass (after billboards, before / with the transform gizmo).
     *
     * @param cmd           Active command buffer inside the main render pass.
     * @param cameraDescSet Camera UBO descriptor set for the current frame.
     * @param worldMatrix   Full world transform of the light entity.
     * @param cameraPos     Current camera position (for constant-size scaling).
     * @param projMatrix    Current projection matrix (for constant-size scaling).
     * @param isSelected    True when this entity is the editor selection.
     * @param isHovered     True when the cursor is hovering the entity.
     */
    void Draw(VkCommandBuffer cmd, VkDescriptorSet cameraDescSet,
              const Mat4& worldMatrix, const Vec3& cameraPos,
              const Mat4& projMatrix, bool isSelected, bool isHovered);

private:
    // Push constants – identical layout to GizmoPushConstants so the same
    // gizmo.vert / gizmo.frag shaders can be used.
    struct PushConstants {
        Mat4 model;
        Vec4 state; // x = hoveredAxis(-1/0), y = activeAxis(-1/0), z = scale, w = 0
    };

    struct GeometryBuffers {
        VulkanBuffer* vertexBuffer = nullptr;
        VulkanBuffer* indexBuffer  = nullptr;
        uint32_t      indexCount   = 0;
    };

    void GenerateGeometry(VmaAllocator allocator);
    void CreatePipeline(VkDevice device, VkRenderPass renderPass,
                        VkExtent2D extent,
                        VkDescriptorSetLayout cameraDescLayout);

    // Helpers to add geometry pieces ---
    void AddArrow(std::vector<struct GizmoVertex>& verts,
                  std::vector<uint16_t>& indices,
                  const Vec3& start, const Vec3& direction,
                  float length, float radius, const Vec3& color);

    void AddLine(std::vector<struct GizmoVertex>& verts,
                 std::vector<uint16_t>& indices,
                 const Vec3& start, const Vec3& end,
                 float radius, const Vec3& color);

    void AddCircle(std::vector<struct GizmoVertex>& verts,
                   std::vector<uint16_t>& indices,
                   const Vec3& center, const Vec3& axis,
                   float radius, float tubeRadius,
                   int segments, const Vec3& color);

    float CalculateScale(const Vec3& gizmoPos, const Vec3& cameraPos,
                         const Mat4& projMatrix) const;

    // Vulkan resources
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    GeometryBuffers  m_geo;

    // Tuning
    static constexpr float kArrowLength    = 2.2f;  // Length of central direction arrow
    static constexpr float kArrowRadius    = 0.04f;  // Cylinder radius of shaft
    static constexpr float kRayLength      = 1.6f;  // Length of peripheral rays
    static constexpr float kRayRadius      = 0.025f; // Thinner peripheral rays
    static constexpr float kRayOffset      = 0.35f;  // Distance of rays from centre
    static constexpr float kCircleRadius   = 0.4f;   // Origin disk radius
    static constexpr float kCircleTube     = 0.025f;  // Tube radius of origin ring
    static constexpr float kScreenScale    = 0.18f;  // Constant-screen-size factor
};
