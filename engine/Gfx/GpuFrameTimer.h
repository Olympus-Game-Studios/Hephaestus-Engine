#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

class VulkanDevice;

namespace Hephaestus {

// GPU time of each stage of a frame, from timestamps written between the stages.
struct GpuFrameTimes {
    bool valid = false;
    float shadowMs = 0.0f;       // sun cascades
    float rayTracingMs = 0.0f;   // skinned posing, scene acceleration structure, camera-following probe grids
    float localShadowMs = 0.0f;  // point and spot shadow atlas
    float probeVolumeMs = 0.0f;  // placed probe volume: sun map + update
    float sceneMs = 0.0f;        // scene pass (surfaces, reflections, ray-traced shadows)
    float temporalMs = 0.0f;     // temporal accumulation
    float overlayMs = 0.0f;      // tone map, editor overlays, HUI
    float totalMs = 0.0f;
};

class GpuFrameTimer {
public:
    enum Mark : uint32_t { Start, Shadows, RayTracing, LocalShadows, ProbeVolume, Scene, Temporal, End, kMarks };

    bool Init(VulkanDevice* device, uint32_t imageCount);
    void Shutdown();

    // Reads the times this image's previous frame recorded; call after its fence was waited on.
    void Collect(uint32_t imageIndex);
    // Outside a render pass, before any Mark for this image.
    void Begin(VkCommandBuffer cmd, uint32_t imageIndex);
    void Mark(VkCommandBuffer cmd, uint32_t imageIndex, Mark mark);

    const GpuFrameTimes& Last() const { return m_last; }
    // Exponential average over roughly the last 50 frames: single frames swing with GPU clock changes.
    const GpuFrameTimes& Smoothed() const { return m_smoothed; }

private:
    VulkanDevice* m_device = nullptr;
    VkQueryPool m_pool = VK_NULL_HANDLE;
    uint32_t m_imageCount = 0;
    float m_periodNs = 0.0f;
    std::vector<uint32_t> m_written;  // bitmask of marks written per image
    GpuFrameTimes m_last;
    GpuFrameTimes m_smoothed;
};

} // namespace Hephaestus
