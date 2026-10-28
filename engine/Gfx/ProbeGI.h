#pragma once

#include "ProbeBake.h"
#include "VulkanBuffer.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class Scene;
class VulkanDevice;

namespace Hephaestus {

// Only the first enabled, baked ProbeVolumeComponent in a scene is lit.
class ProbeGI {
public:
    static constexpr uint32_t kSunMapResolution = 2048;

    // Layouts and placeholder data every lit draw binds; must succeed before the surface pipelines are built.
    bool Init(VulkanDevice* device);
    // Optional: without it the probes see the sun unoccluded.
    bool InitSunMap(VkRenderPass depthOnlyRenderPass);
    void Shutdown();

    VkDescriptorSetLayout SetLayout() const { return m_setLayout; }
    VkDescriptorSetLayout EmptySetLayout() const { return m_emptySetLayout; }
    // Set 2 of the surface pipelines: the probe result, or placeholders with probeCounts.w = 0.
    VkDescriptorSet DrawSet(uint32_t imageIndex) const { return m_sets[imageIndex][1]; }

    bool CreateSwapchainResources(VkRenderPass sceneRenderPass, uint32_t sceneSubpass,
                                  VkDescriptorSetLayout cameraSetLayout, uint32_t imageCount);
    void DestroySwapchainResources();

    // Before command recording for imageIndex: (re)loads the bake, fills the parameters and descriptor sets.
    void Prepare(Scene* scene, const std::string& projectRoot, uint32_t imageIndex, VkBuffer lightingBuffer,
                 const Vec3* sunDirection);
    bool Active() const { return m_active; }

    bool HasSunMap() const { return m_active && m_sunValid; }
    VkFramebuffer SunFramebuffer() const { return m_sunFramebuffer; }
    const Mat4& SunViewProj() const { return m_sunViewProj; }

    void BeginTiming(VkCommandBuffer cmd, uint32_t imageIndex);
    void RecordUpdate(VkCommandBuffer cmd, uint32_t imageIndex);
    void RecordDebug(VkCommandBuffer cmd, uint32_t imageIndex, VkDescriptorSet cameraSet);

    // GPU time of the last completed sun map + probe update, in milliseconds (negative until known).
    float LastGpuMilliseconds() const { return m_lastGpuMs; }
    size_t ProbeCount() const { return m_probeCount; }

private:
    bool createLayouts();
    bool createComputePipeline();
    bool createPlaceholders();
    bool loadBake(const std::filesystem::path& file, std::string& outError);
    void destroyBake();
    void writeDescriptorSets(uint32_t imageIndex, VkBuffer lightingBuffer);

    VulkanDevice* m_device = nullptr;
    VkSampler m_atlasSampler = VK_NULL_HANDLE;
    VkSampler m_depthSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_emptySetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_computeLayout = VK_NULL_HANDLE;
    VkPipeline m_computePipeline = VK_NULL_HANDLE;

    VkImage m_sunImage = VK_NULL_HANDLE;
    VmaAllocation m_sunAllocation = VK_NULL_HANDLE;
    VkImageView m_sunView = VK_NULL_HANDLE;
    VkFramebuffer m_sunFramebuffer = VK_NULL_HANDLE;
    Mat4 m_sunViewProj;
    bool m_sunValid = false;

    // Swapchain-sized resources.
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<std::array<VkDescriptorSet, 2>> m_sets;  // [image][0 = compute, 1 = draw]
    std::vector<VulkanBuffer> m_paramBuffers;
    VkPipelineLayout m_debugLayout = VK_NULL_HANDLE;
    VkPipeline m_debugPipeline = VK_NULL_HANDLE;
    VkQueryPool m_queryPool = VK_NULL_HANDLE;
    std::vector<bool> m_queryWritten;
    float m_timestampPeriodNs = 0.0f;
    float m_lastGpuMs = -1.0f;

    // Loaded bake.
    std::string m_bakeKey;
    ProbeBake m_bakeGrid;  // header fields only
    VulkanBuffer m_rays;
    VulkanBuffer m_offsets;
    struct Atlas {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    Atlas m_irradiance[2];
    Atlas m_visibility[2];
    Atlas m_placeholderAtlas;  // 1x1 RGBA16F, GENERAL
    VulkanBuffer m_placeholderBuffer;
    size_t m_probeCount = 0;
    uint32_t m_parity = 0;
    uint32_t m_resetFrames = 0;
    float m_intensity = 1.0f;
    bool m_active = false;
};

} // namespace Hephaestus
