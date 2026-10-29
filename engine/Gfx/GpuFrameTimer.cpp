#include "GpuFrameTimer.h"

#include "VulkanDevice.h"

namespace Hephaestus {

bool GpuFrameTimer::Init(VulkanDevice* device, uint32_t imageCount) {
    m_device = device;
    m_imageCount = imageCount;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device->physicalDevice, &props);
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device->physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device->physicalDevice, &familyCount, families.data());
    if (device->graphicsFamily >= familyCount || families[device->graphicsFamily].timestampValidBits == 0) {
        return true;  // no timestamps on this queue: times stay invalid
    }
    m_periodNs = props.limits.timestampPeriod;
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = kMarks * imageCount;
    m_written.assign(imageCount, 0);
    return vkCreateQueryPool(device->device, &info, nullptr, &m_pool) == VK_SUCCESS;
}

void GpuFrameTimer::Shutdown() {
    if (m_device && m_pool) vkDestroyQueryPool(m_device->device, m_pool, nullptr);
    m_pool = VK_NULL_HANDLE;
    m_written.clear();
    m_last = GpuFrameTimes{};
    m_smoothed = GpuFrameTimes{};
}

void GpuFrameTimer::Collect(uint32_t imageIndex) {
    if (!m_pool || imageIndex >= m_imageCount) return;
    const uint32_t written = m_written[imageIndex];
    m_written[imageIndex] = 0;
    if ((written & (1u << Start)) == 0 || (written & (1u << End)) == 0) return;
    uint64_t stamps[kMarks] = {};
    if (vkGetQueryPoolResults(m_device->device, m_pool, imageIndex * kMarks, kMarks, sizeof(stamps), stamps,
                              sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
        return;
    }
    // A stage that was not recorded this frame takes no time: it inherits the previous mark.
    uint64_t at[kMarks];
    for (uint32_t m = 0; m < kMarks; ++m) {
        at[m] = (written & (1u << m)) ? stamps[m] : at[m == 0 ? 0 : m - 1];
    }
    auto ms = [&](uint32_t from, uint32_t to) {
        return at[to] > at[from] ? static_cast<float>(at[to] - at[from]) * m_periodNs / 1.0e6f : 0.0f;
    };
    m_last.valid = true;
    m_last.shadowMs = ms(Start, Shadows);
    m_last.rayTracingMs = ms(Shadows, RayTracing);
    m_last.localShadowMs = ms(RayTracing, LocalShadows);
    m_last.probeVolumeMs = ms(LocalShadows, ProbeVolume);
    m_last.sceneMs = ms(ProbeVolume, Scene);
    m_last.temporalMs = ms(Scene, Temporal);
    m_last.overlayMs = ms(Temporal, End);
    m_last.totalMs = ms(Start, End);

    const float k = m_smoothed.valid ? 0.02f : 1.0f;
    auto blend = [k](float& average, float sample) { average += (sample - average) * k; };
    blend(m_smoothed.shadowMs, m_last.shadowMs);
    blend(m_smoothed.rayTracingMs, m_last.rayTracingMs);
    blend(m_smoothed.localShadowMs, m_last.localShadowMs);
    blend(m_smoothed.probeVolumeMs, m_last.probeVolumeMs);
    blend(m_smoothed.sceneMs, m_last.sceneMs);
    blend(m_smoothed.temporalMs, m_last.temporalMs);
    blend(m_smoothed.overlayMs, m_last.overlayMs);
    blend(m_smoothed.totalMs, m_last.totalMs);
    m_smoothed.valid = true;
}

void GpuFrameTimer::Begin(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (!m_pool) return;
    vkCmdResetQueryPool(cmd, m_pool, imageIndex * kMarks, kMarks);
    Mark(cmd, imageIndex, Start);
}

void GpuFrameTimer::Mark(VkCommandBuffer cmd, uint32_t imageIndex, enum Mark mark) {
    if (!m_pool) return;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_pool, imageIndex * kMarks + mark);
    m_written[imageIndex] |= 1u << mark;
}

} // namespace Hephaestus
