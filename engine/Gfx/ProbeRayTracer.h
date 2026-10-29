#pragma once

#include "../Core/Math.h"
#include "VulkanBuffer.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

class VulkanDevice;
struct MeshGPU;

namespace Hephaestus {

struct ProbeTraceInstance {
    const MeshGPU* mesh = nullptr;
    Mat4 world;
    Vec3 albedo{1.0f, 1.0f, 1.0f};
    Vec3 emissive{0.0f, 0.0f, 0.0f};
};

// Builds the scene acceleration structure each frame and fills probe ray hits from it with VK_KHR_ray_query
// (shaders/probe_trace.comp). The same structure serves ray-traced shadows in shaders/triangle.frag. Owned by ProbeGI.
class ProbeRayTracer {
public:
    bool Init(VulkanDevice* device);
    void Shutdown();
    bool Ready() const { return m_pipeline != VK_NULL_HANDLE; }

    bool CreateSwapchainResources(uint32_t imageCount);
    void DestroySwapchainResources();

    // Before recording: builds missing per-mesh acceleration structures and uploads this image's instances.
    // May replace SceneStructure(imageIndex), so descriptors naming it are written after this.
    bool PrepareScene(uint32_t imageIndex, const std::vector<ProbeTraceInstance>& instances);
    // Valid from CreateSwapchainResources on; holds geometry only on frames that ran PrepareScene and RecordScene.
    VkAccelerationStructureKHR SceneStructure(uint32_t imageIndex) const { return m_frames[imageIndex].tlas.handle; }
    // Per-instance material and buffer addresses (TraceInstance in the trace shaders), filled by PrepareScene.
    VkBuffer InstanceData(uint32_t imageIndex) const { return m_frames[imageIndex].instanceData.buffer; }
    // Before recording a probe trace: writes this image's trace set.
    void WriteTraceSet(uint32_t imageIndex, VkBuffer params, VkBuffer lighting, VkBuffer offsets, VkBuffer hits);
    // Builds this frame's scene acceleration structure for compute and fragment ray queries.
    void RecordScene(VkCommandBuffer cmd, uint32_t imageIndex);
    // Traces probeCount * 128 rays into the hits buffer; RecordScene must come first in the same frame.
    void Record(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t probeCount);

private:
    struct AccelerationStructure {
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        VulkanBuffer buffer;
        VkDeviceAddress address = 0;
    };
    struct FrameResources {
        AccelerationStructure tlas;
        VulkanBuffer instanceBuffer;  // VkAccelerationStructureInstanceKHR[]
        VulkanBuffer instanceData;    // shader TraceInstance[]
        VulkanBuffer scratch;
        uint32_t capacity = 0;
        uint32_t count = 0;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    bool buildMeshStructure(const MeshGPU& mesh);
    bool ensureCapacity(FrameResources& frame, uint32_t instanceCount);
    void destroyStructure(AccelerationStructure& as);
    VkDeviceAddress bufferAddress(VkBuffer buffer) const;

    VulkanDevice* m_device = nullptr;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDeviceSize m_scratchAlignment = 1;
    std::vector<FrameResources> m_frames;
    std::unordered_map<VkBuffer, AccelerationStructure> m_meshStructures;  // keyed by the mesh vertex buffer
};

} // namespace Hephaestus
