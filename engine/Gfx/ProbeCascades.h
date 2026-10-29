#pragma once

#include "ProbeCascadeGrid.h"
#include "VulkanBuffer.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <vector>

class VulkanDevice;

namespace Hephaestus {

// Camera-following probe grids traced with hardware ray tracing every frame (shaders/cascade_trace.comp and
// cascade_update.comp). Owned by ProbeGI, which binds the result into its draw set.
class ProbeCascades {
public:
    static constexpr uint32_t kProbeBudget = 4096;  // probes traced per frame
    static constexpr float kBaseSpacing = 0.5f;

    // Pipelines need ray queries; without them only the parameter buffers exist and the cascades stay off.
    bool Init(VulkanDevice* device, bool rayQueries);
    void Shutdown();

    bool CreateSwapchainResources(uint32_t imageCount);
    void DestroySwapchainResources();

    // Before recording: counts = nullptr turns the cascades off. Otherwise (re)creates the atlases for that grid
    // size, follows the camera, schedules this frame's probes and writes the set.
    void Prepare(uint32_t imageIndex, const Cell* counts, const Vec3& camera, const Mat4& rotation,
                 VkBuffer lighting, VkAccelerationStructureKHR scene, VkBuffer instances);
    // After the scene acceleration structure is built, before the scene pass.
    void Record(VkCommandBuffer cmd, uint32_t imageIndex);

    bool Active() const { return m_active; }
    uint32_t JobCount() const { return m_jobCount; }
    VkBuffer ParamsBuffer(uint32_t imageIndex) const { return m_frames[imageIndex].params.buffer; }
    VkBuffer StateBuffer() const { return m_state.buffer; }
    VkBuffer OffsetBuffer() const { return m_offsets.buffer; }
    int ProbesPerCascade() const { return m_hasGrid ? m_grid.ProbesPerCascade() : 0; }
    VkImageView IrradianceView() const { return m_irradiance.view; }
    VkImageView VisibilityView() const { return m_visibility.view; }

private:
    struct Atlas {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct Frame {
        VulkanBuffer params;
        VulkanBuffer jobs;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    bool createGrid(const Cell& counts);
    void destroyGrid();
    bool createAtlas(Atlas& atlas, VkFormat format, uint32_t width, uint32_t height);
    void writeParams(uint32_t imageIndex, bool active, const Mat4& rotation);

    VulkanDevice* m_device = nullptr;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_tracePipeline = VK_NULL_HANDLE;
    VkPipeline m_updatePipeline = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::vector<Frame> m_frames;

    ProbeCascadeGrid m_grid;
    bool m_hasGrid = false;
    ProbeCascadeScheduler m_scheduler;
    std::array<Cell, ProbeCascadeGrid::kCascades> m_minCells{};
    Atlas m_irradiance;
    Atlas m_visibility;
    VulkanBuffer m_state;  // ivec4 per probe: world cell, updates
    VulkanBuffer m_offsets;  // vec4 per probe: relocation in spacings, switched off
    VulkanBuffer m_rays;   // vec4 per traced ray: radiance, distance
    uint32_t m_jobCount = 0;
    bool m_active = false;
};

} // namespace Hephaestus
