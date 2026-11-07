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
    float handle = -1.0f; // GizmoHandle id; -1 draws with plain vertex colour

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(GizmoVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attribs(4);
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

        attribs[3].binding = 0;
        attribs[3].location = 3;
        attribs[3].format = VK_FORMAT_R32_SFLOAT;
        attribs[3].offset = offsetof(GizmoVertex, handle);
        return attribs;
    }
};

// Keep in sync with the handle ids in shaders/gizmo.vert and shaders/gizmo.frag.
namespace GizmoHandle {
    constexpr float AxisX = 0.0f, AxisY = 1.0f, AxisZ = 2.0f;
    constexpr float Center = 3.0f;
    constexpr float PlaneXY = 4.0f, PlaneXZ = 5.0f, PlaneYZ = 6.0f;
    constexpr float RingX = 10.0f, RingY = 11.0f, RingZ = 12.0f;
    constexpr float ViewRing = 13.0f;
    constexpr float RotateArc = 20.0f;
    constexpr float LightBody = 30.0f;
}

// Editor theme axis-x/axis-y/axis-z tokens (EditorTheme.cpp).
inline Vec3 GizmoAxisColor(int axis) {
    switch (axis) {
        case 0:  return Vec3(0xE5 / 255.0f, 0x53 / 255.0f, 0x4B / 255.0f);
        case 1:  return Vec3(0x5D / 255.0f, 0xBB / 255.0f, 0x63 / 255.0f);
        default: return Vec3(0x5A / 255.0f, 0x8D / 255.0f, 0xEE / 255.0f);
    }
}

// World units that keep a gizmo at a constant on-screen size.
float GizmoScreenScale(const Vec3& gizmoPos, const Vec3& cameraPos, const Mat4& projMatrix,
                       float screenFactor);
// Unit vector from the gizmo towards the viewer (the view axis for orthographic cameras).
Vec3 GizmoToCamera(const Vec3& gizmoPos, const Vec3& cameraPos, const Mat4& viewMatrix,
                   const Mat4& projMatrix);
// Rotation whose Z axis is toCamera, so XY-plane geometry faces the viewer.
Mat4 GizmoBillboardRotation(const Vec3& toCamera, const Mat4& viewMatrix);
// Rotation matrix with the given orthonormal basis as its X, Y and Z columns.
Mat4 GizmoBasis(const Vec3& x, const Vec3& y, const Vec3& z);

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
        Vec4 state;  // x = hovered axis, y = active axis, z = gizmo scale, w = rotate arc sweep (radians)
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
    struct GeometryBuffers {
        VulkanBuffer* vertexBuffer = nullptr;
        VulkanBuffer* indexBuffer = nullptr;
        uint32_t indexCount = 0;
    };

    struct IndexRange {
        uint32_t first = 0;
        uint32_t count = 0;
    };

    void GenerateTranslateGeometry(VmaAllocator allocator);
    void GenerateRotateGeometry(VmaAllocator allocator);
    void GenerateScaleGeometry(VmaAllocator allocator);
    void GenerateBillboardGeometry(VmaAllocator allocator);

    void CreatePipeline(VkDevice device, VkRenderPass renderPass,
                       VkExtent2D extent, VkDescriptorSetLayout cameraDescriptorLayout);

    float CalculateGizmoScale(const Vec3& gizmoPos, const Vec3& cameraPos,
                             const Mat4& projMatrix) const;

    GeometryBuffers* GetGeometryForMode();
    const GeometryBuffers* GetGeometryForMode() const;
    float GetAxisLengthForCurrentMode() const;
    bool IsAxisPickable(int axis) const;
    bool IsPlanePickable(int plane) const;
    float AxisSignTowardCamera(int axis) const;
    bool ClosestPointOnAxis(const Ray& ray, const Vec3& origin, const Vec3& axisDir,
                            float& outAxisParam, float& outDistance, bool clampToAxis) const;
    float RayDistanceToRing(const Ray& ray, const Vec3& axisDir, float radius,
                            bool frontHalfOnly) const;
    bool IntersectPlane(const Ray& ray, const Vec3& planeOrigin, const Vec3& planeNormal, Vec3& outPoint) const;
    void DrawRange(VkCommandBuffer cmd, const GeometryBuffers& geo, IndexRange range,
                   const Mat4& model) const;

    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;

    GeometryBuffers m_TranslateGeo;
    GeometryBuffers m_RotateGeo;
    GeometryBuffers m_ScaleGeo;
    GeometryBuffers m_BillboardGeo;   // camera-facing pieces and the rotate arc disc
    IndexRange m_CenterDotRange;
    IndexRange m_ViewRingRange;
    IndexRange m_ArcDiscRange;

    // Cached state
    Mode m_Mode = Mode::Translate;
    Space m_Space = Space::World;
    bool m_StateValid = false;
    Mat4 m_ObjectTransform = Mat4::Identity();
    Mat4 m_ViewMatrix = Mat4::Identity();
    Mat4 m_ProjMatrix = Mat4::Identity();
    Vec3 m_CameraPos = Vec3(0, 0, 0);
    Vec3 m_GizmoOrigin = Vec3(0, 0, 0);
    Vec3 m_ToCamera = Vec3(0, 0, 1);
    Vec3 m_AxisDirections[3] = { Vec3(1,0,0), Vec3(0,1,0), Vec3(0,0,1) };
    float m_CurrentScale = 1.0f;

    // Interaction state
    int m_HoveredAxis = -1;     // 0-2 = X/Y/Z, 3 = centre (free move, uniform scale, view rotate), 4-6 = XY/XZ/YZ planes
    int m_ActiveAxis = -1;
    bool m_IsDragging = false;
    Mat4 m_DragStartTransform = Mat4::Identity();
    Vec3 m_DragStartPosition = Vec3(0, 0, 0);
    Vec3 m_DragAxisDir = Vec3(1, 0, 0);
    float m_DragStartAxisParam = 0.0f;
    Vec3 m_DragStartScale = Vec3(1, 1, 1);
    Vec3 m_RotationStartVector = Vec3(1, 0, 0);
    float m_RotationAngle = 0.0f;
    Vec3 m_FreeMoveStartPoint = Vec3(0, 0, 0);
    Vec3 m_DragPlaneNormal = Vec3(0, 1, 0);
    bool m_RotationStartValid = false;
    float m_UniformDragStartDistance = 1.0f; // For uniform scale: initial screen-plane distance
    bool m_SnapEnabled = false;
    float m_SnapIncrement = 1.0f;

    // Tuning constants, in gizmo units (multiplied by m_CurrentScale)
    static constexpr float kTranslateLength = 1.6f;
    static constexpr float kScaleLength = 1.5f;
    static constexpr float kRotateRadius = 1.2f;
    static constexpr float kViewRingRadius = 1.4f;
    static constexpr float kShaftRadius = 0.022f;
    static constexpr float kRingTubeRadius = 0.022f;
    static constexpr float kAxisPickRadius = 0.14f;
    static constexpr float kRingPickRadius = 0.12f;
    static constexpr float kPlaneMin = 0.30f;
    static constexpr float kPlaneMax = 0.56f;
    static constexpr float kCenterDotRadius = 0.08f;
    static constexpr float kCenterPickRadius = 0.16f;
    static constexpr float kScaleCubeSize = 0.15f;
    static constexpr float kUniformCubeSize = 0.2f;
    static constexpr float kScreenScaleFactor = 0.2f;
    float m_SizeMultiplier = 1.0f;
};
