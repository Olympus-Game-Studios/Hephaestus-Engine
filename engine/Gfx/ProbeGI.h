#pragma once

#include "ProbeBake.h"
#include "ProbeCascades.h"
#include "ProbeRayTracer.h"
#include "VulkanBuffer.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <vector>

class Scene;
class VulkanDevice;

namespace Hephaestus {

// Only the first enabled ProbeVolumeComponent that is baked, or that asks for hardware ray tracing on a GPU
// that supports it, is lit.
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
    // collectInstances is called only when the volume traces with hardware ray tracing or traceShadows is set.
    // allowHardware = false makes volumes that ask for hardware ray tracing use their bakes (lower quality tiers).
    // cascadeCounts turns on the camera-following probe grids at that size (nullptr = off).
    void Prepare(Scene* scene, const std::string& projectRoot, uint32_t imageIndex, VkBuffer lightingBuffer,
                 const Vec3* sunDirection, bool allowHardware, bool traceShadows, const Cell* cascadeCounts,
                 const Vec3& camera, const std::function<std::vector<ProbeTraceInstance>()>& collectInstances);
    bool Active() const { return m_active; }
    // When true, DrawSet binds the scene acceleration structure at binding 11 and surfaces use triangle_rt.frag.
    bool HardwareRayTracingAvailable() const { return m_tracerReady; }
    // Whether this frame's scene acceleration structure holds the scene (Prepare collected instances).
    bool SceneTraced() const { return m_sceneTraced; }
    // Builds the scene acceleration structure Prepare filled and updates the camera-following probe grids;
    // before RecordUpdate and the scene pass.
    void RecordScene(VkCommandBuffer cmd, uint32_t imageIndex);
    bool CascadesActive() const { return m_cascades.Active(); }
    uint32_t CascadeProbesTraced() const { return m_cascades.JobCount(); }
    bool UsingHardwareRayTracing() const { return m_active && m_hardware; }

    bool HasSunMap() const { return m_active && m_sunValid && !m_hardware; }
    VkFramebuffer SunFramebuffer() const { return m_sunFramebuffer; }
    const Mat4& SunViewProj() const { return m_sunViewProj; }

    void BeginTiming(VkCommandBuffer cmd, uint32_t imageIndex);
    void RecordUpdate(VkCommandBuffer cmd, uint32_t imageIndex);
    void RecordDebug(VkCommandBuffer cmd, uint32_t imageIndex, VkDescriptorSet cameraSet);
    // The finest camera-following grid's probes; switched-off probes draw red.
    void RecordCascadeDebug(VkCommandBuffer cmd, uint32_t imageIndex, VkDescriptorSet cameraSet);

    // GPU time of the last completed sun map + probe update, in milliseconds (negative until known).
    float LastGpuMilliseconds() const { return m_lastGpuMs; }
    size_t ProbeCount() const { return m_probeCount; }

private:
    bool createLayouts();
    bool createComputePipeline();
    bool createPlaceholders();
    bool loadBake(const std::filesystem::path& file, std::string& outError);
    bool createHardwareVolume(const int counts[3], std::string& outError);
    void prepareVolume(Scene* scene, const std::string& projectRoot, uint32_t imageIndex, VkBuffer lightingBuffer,
                       const Vec3* sunDirection, bool allowHardware);
    bool createAtlases(const int counts[3]);
    void destroyBake();
    void writeDescriptorSets(uint32_t imageIndex, VkBuffer lightingBuffer);
    void writeCascadeDescriptors(uint32_t imageIndex);
    void prepareScene(Scene* scene, uint32_t imageIndex, VkBuffer lightingBuffer, bool traceScene,
                      const std::function<std::vector<ProbeTraceInstance>()>& collectInstances);

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
    VkPipeline m_cascadeDebugPipeline = VK_NULL_HANDLE;
    VkQueryPool m_queryPool = VK_NULL_HANDLE;
    std::vector<bool> m_queryWritten;
    float m_timestampPeriodNs = 0.0f;
    float m_lastGpuMs = -1.0f;

    // Loaded bake.
    std::string m_bakeKey;
    ProbeBake m_bakeGrid;  // header fields only
    VulkanBuffer m_rays;  // baked hits, or the per-frame traced hits in hardware mode
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
    bool m_hardware = false;
    std::mt19937 m_rng{0x5eed};
    ProbeRayTracer m_tracer;
    bool m_tracerReady = false;
    ProbeCascades m_cascades;
    bool m_sceneTraced = false;
};

} // namespace Hephaestus
