#pragma once

#include "../Core/Math.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>

class VulkanDevice;
struct VulkanSwapchain;

namespace Hephaestus {

// Temporal accumulation of the HDR scene between the scene pass and tone mapping (shaders/temporal_resolve.comp).
// Owns two history images; the result is copied back into the swapchain's HDR image for the tone map.
class TemporalResolve {
public:
    bool Init(VulkanDevice* device);
    void Shutdown();

    bool CreateSwapchainResources(const VulkanSwapchain& swapchain);
    void DestroySwapchainResources();

    // After the scene pass (HDR in SHADER_READ_ONLY, depth in DEPTH_STENCIL_READ_ONLY); leaves HDR in
    // TRANSFER_DST for the overlay pass to load. reset discards history (first frame, scene change, camera cut).
    void Record(VkCommandBuffer cmd, const Mat4& viewProj, bool reset);

    // Last frame's result for screen-space reflections: rgb = HDR colour, a = depth (GENERAL layout), the camera
    // it was drawn with, and whether one exists yet.
    VkImageView LastResultView() const { return m_history[1 - m_parity].view; }
    VkSampler LinearSampler() const { return m_linearSampler; }
    const Mat4& LastViewProj() const { return m_previousViewProj; }
    bool HasLastResult() const { return m_historyValid; }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    VulkanDevice* m_device = nullptr;
    VkSampler m_pointSampler = VK_NULL_HANDLE;
    VkSampler m_linearSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[2] = {};  // [parity]: writes history[parity], reads history[1 - parity]

    VkImage m_hdrImage = VK_NULL_HANDLE;
    VkExtent2D m_extent{};
    Image m_history[2];
    uint32_t m_parity = 0;
    bool m_historyValid = false;
    Mat4 m_previousViewProj = Mat4::Identity();
};

} // namespace Hephaestus
