#pragma once
#include <vulkan/vulkan.h>
#include <vector>
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
        Vec4 color;  // RGBA color for this gizmo component
    };

    VulkanGizmo();
    ~VulkanGizmo();

    // Initialize the gizmo system
    void Init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent, 
              VkDescriptorSetLayout cameraDescriptorLayout, VmaAllocator allocator);
    
    // Cleanup resources
    void Cleanup(VkDevice device, VmaAllocator allocator);

    // Render the gizmo for a given transform
    void Render(VkCommandBuffer cmd, VkDescriptorSet cameraDescriptor, 
                const Mat4& objectTransform, const Mat4& viewMatrix, 
                const Vec3& cameraPos, Mode mode, Space space);

    // Check if mouse is hovering over gizmo component
    // Returns -1 if no hit, or axis index (0=X, 1=Y, 2=Z, 3=View plane)
    int CheckHover(const Ray& ray, const Mat4& objectTransform, 
                   const Vec3& cameraPos, Mode mode, Space space);

    // Handle mouse drag interaction
    // Returns the delta transformation to apply
    Mat4 HandleDrag(const Ray& currentRay, const Ray& previousRay, 
                    int activeAxis, const Mat4& objectTransform, 
                    const Vec3& cameraPos, Mode mode, Space space);

    void SetMode(Mode mode) { m_Mode = mode; }
    void SetSpace(Space space) { m_Space = space; }
    Mode GetMode() const { return m_Mode; }
    Space GetSpace() const { return m_Space; }

private:
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

    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;

    // Geometry buffers for each mode
    struct GeometryBuffers {
        VulkanBuffer* vertexBuffer = nullptr;
        VulkanBuffer* indexBuffer = nullptr;
        uint32_t indexCount = 0;
    };

    GeometryBuffers m_TranslateGeo;
    GeometryBuffers m_RotateGeo;
    GeometryBuffers m_ScaleGeo;

    Mode m_Mode = Mode::Translate;
    Space m_Space = Space::World;

    int m_HoveredAxis = -1;
    bool m_IsDragging = false;
    Vec3 m_DragStartPoint;
    Mat4 m_DragStartTransform;
};
