#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include "../Core/Math.h"
#include "vk_mem_alloc.h"

// Forward declarations
struct VulkanBuffer;

// Vertex structure for gizmo geometry
struct GizmoVertex {
    Vec3 position;
    Vec3 normal;
    Vec3 color;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(GizmoVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attribs(3);
        attribs[0].binding = 0;
        attribs[0].location = 0;
        attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribs[0].offset = offsetof(GizmoVertex, position);

        attribs[1].binding = 0;
        attribs[1].location = 1;
        attribs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribs[1].offset = offsetof(GizmoVertex, normal);

        attribs[2].binding = 0;
        attribs[2].location = 2;
        attribs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribs[2].offset = offsetof(GizmoVertex, color);
        return attribs;
    }
};

/**
 * VulkanGizmo - Custom gizmo renderer using Vulkan
 * Provides translate/rotate/scale manipulation handles
 */
class VulkanGizmo {
public:
    enum class Mode {
        Translate,
        Rotate,
        Scale
    };

    enum class Space {
        World,
        Local
    };

    struct GizmoPushConstants {
        Mat4 model;
        Vec4 state;  // x = hovered axis, y = active axis, z = gizmo scale, w unused
    };

    VulkanGizmo();
    ~VulkanGizmo();

    // Initialize the gizmo system
    void Init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent, 
              VkDescriptorSetLayout cameraDescriptorLayout, VmaAllocator allocator);

    // Recreate pipeline resources when swapchain/renderpass changes
    void Recreate(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                  VkDescriptorSetLayout cameraDescriptorLayout);
    
    // Cleanup resources
    void Cleanup(VkDevice device, VmaAllocator allocator);

    // Prepare gizmo state for the current frame (must be called before interaction/render)
    void UpdateState(const Mat4& objectTransform, const Mat4& viewMatrix,
                     const Mat4& projMatrix, const Vec3& cameraPos,
                     Mode mode, Space space);

    // Render the gizmo using the cached state
    void Render(VkCommandBuffer cmd, VkDescriptorSet cameraDescriptor);

    // Hover helpers
    int CheckHover(const Ray& ray) const;
    void SetHoveredAxis(int axis);
    int GetHoveredAxis() const { return m_HoveredAxis; }
    int GetActiveAxis() const { return m_ActiveAxis; }

    // Drag helpers
    void BeginDrag(const Ray& ray, int axis);
    Mat4 HandleDrag(const Ray& ray, bool bypassSnap = false);
    void EndDrag();
    Mat4 CancelDrag();
    void SetSizeMultiplier(float multiplier) { m_SizeMultiplier = multiplier > 0.0f ? multiplier : 1.0f; }

    void SetSnapSettings(bool enabled, float increment) {
        m_SnapEnabled = enabled;
        m_SnapIncrement = increment;
    }
    bool IsDragging() const { return m_IsDragging; }

private:
    struct GeometryBuffers;

    // Generate geometry for different gizmo types
    void GenerateTranslateGeometry(VkDevice device, VmaAllocator allocator);
    void GenerateRotateGeometry(VkDevice device, VmaAllocator allocator);
    void GenerateScaleGeometry(VkDevice device, VmaAllocator allocator);

    // Create pipeline for gizmo rendering
    void CreatePipeline(VkDevice device, VkRenderPass renderPass, 
                       VkExtent2D extent, VkDescriptorSetLayout cameraDescriptorLayout);

    // Helper to create arrow geometry (for translate)
    void AddArrow(std::vector<GizmoVertex>& vertices, std::vector<uint16_t>& indices,
                  const Vec3& direction, float length, float radius, const Vec3& color);

    // Helper to create torus segment (for rotate)
    void AddTorusSegment(std::vector<GizmoVertex>& vertices, std::vector<uint16_t>& indices,
                        const Vec3& axis, float radius, int segments, const Vec3& color);

    // Helper to create cube handle (for scale)
    void AddCube(std::vector<GizmoVertex>& vertices, std::vector<uint16_t>& indices,
                const Vec3& position, float size, const Vec3& color);

    // Calculate gizmo size based on distance to camera (constant screen size)
    float CalculateGizmoScale(const Vec3& gizmoPos, const Vec3& cameraPos, 
                             const Mat4& projMatrix) const;

    GeometryBuffers* GetGeometryForMode();
    const GeometryBuffers* GetGeometryForMode() const;
    float GetAxisLengthForCurrentMode() const;
    bool ClosestPointOnAxis(const Ray& ray, const Vec3& origin, const Vec3& axisDir,
                            float& outAxisParam, float& outDistance, bool clampToAxis) const;
    bool IntersectRotationRing(const Ray& ray, const Vec3& axisDir,
                               float& outDistanceFromRing) const;
    bool IntersectPlane(const Ray& ray, const Vec3& planeOrigin, const Vec3& planeNormal, Vec3& outPoint) const;

    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;

    struct GeometryBuffers {
        VulkanBuffer* vertexBuffer = nullptr;
        VulkanBuffer* indexBuffer = nullptr;
        uint32_t indexCount = 0;
    };

    GeometryBuffers m_TranslateGeo;
    GeometryBuffers m_RotateGeo;
    GeometryBuffers m_ScaleGeo;

    // Cached state
    Mode m_Mode = Mode::Translate;
    Space m_Space = Space::World;
    bool m_StateValid = false;
    Mat4 m_ObjectTransform = Mat4::Identity();
    Mat4 m_ViewMatrix = Mat4::Identity();
    Mat4 m_ProjMatrix = Mat4::Identity();
    Vec3 m_CameraPos = Vec3(0, 0, 0);
    Vec3 m_GizmoOrigin = Vec3(0, 0, 0);
    Vec3 m_AxisDirections[3] = { Vec3(1,0,0), Vec3(0,1,0), Vec3(0,0,1) };
    float m_CurrentScale = 1.0f;

    // Interaction state
    int m_HoveredAxis = -1;     // 0=X, 1=Y, 2=Z, 3=uniform (center cube)
    int m_ActiveAxis = -1;
    bool m_IsDragging = false;
    Mat4 m_DragStartTransform = Mat4::Identity();
    Vec3 m_DragStartPosition = Vec3(0, 0, 0);
    Vec3 m_DragAxisDir = Vec3(1, 0, 0);
    float m_DragStartAxisParam = 0.0f;
    Vec3 m_DragStartScale = Vec3(1, 1, 1);
    Vec3 m_RotationStartVector = Vec3(1, 0, 0);
    Vec3 m_FreeMoveStartPoint = Vec3(0, 0, 0);
    Vec3 m_DragPlaneNormal = Vec3(0, 1, 0);
    bool m_RotationStartValid = false;
    float m_UniformDragStartDistance = 1.0f; // For uniform scale: initial screen-plane distance
    bool m_SnapEnabled = false;
    float m_SnapIncrement = 1.0f;

    // Tuning constants
    const float m_TranslateLength = 1.6f;
    const float m_ScaleLength = 1.5f;
    const float m_RotateRadius = 1.2f;
    const float m_AxisThickness = 0.08f;
    const float m_RotationPickWidth = 0.18f;
    const float m_ScreenScaleFactor = 0.2f;
    float m_SizeMultiplier = 1.0f;
    const float m_UniformScaleCubeSize = 0.25f;  // Size of center cube for uniform scale
};
