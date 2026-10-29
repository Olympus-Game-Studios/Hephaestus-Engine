#include "ProbeCascades.h"

#include "ProbeGpuUtil.h"
#include "VulkanDevice.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <iostream>

namespace Hephaestus {

namespace {

constexpr int kIrradianceTile = 10;  // shaders/probe_common.glsl IRRADIANCE_TILE
constexpr int kVisibilityTile = 18;  // shaders/probe_common.glsl VISIBILITY_TILE
constexpr uint32_t kRaysPerProbe = 128;
constexpr float kMaxHysteresis = 0.95f;
constexpr float kMaxRaySpacings = 2.6f;  // a ray's distance is clamped to this many probe spacings

struct GpuCascadeParams {
    int32_t counts[4];
    int32_t minCell[ProbeCascadeGrid::kCascades][4];
    float spacing[4];
    float settings[4];
    Mat4 rotation;
};

enum Binding : uint32_t {
    kLighting = 1,
    kIrradianceStore = 6,
    kVisibilityStore = 8,
    kScene = 11,
    kParams = 12,
    kState = 13,
    kIrradianceRead = 14,
    kVisibilityRead = 15,
    kInstances = 16,
    kJobs = 17,
    kRays = 18,
    kOffsets = 19,
};

} // namespace

bool ProbeCascades::Init(VulkanDevice* device, bool rayQueries) {
    m_device = device;
    if (!rayQueries) return true;
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device->device, &sampler, nullptr, &m_sampler) != VK_SUCCESS) return false;

    const VkShaderStageFlags compute = VK_SHADER_STAGE_COMPUTE_BIT;
    const VkDescriptorSetLayoutBinding bindings[] = {
        {kLighting, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, compute, nullptr},
        {kIrradianceStore, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr},
        {kVisibilityStore, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr},
        {kScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, compute, nullptr},
        {kParams, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, compute, nullptr},
        {kState, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, compute, nullptr},
        {kIrradianceRead, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, compute, nullptr},
        {kVisibilityRead, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, compute, nullptr},
        {kInstances, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, compute, nullptr},
        {kJobs, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, compute, nullptr},
        {kRays, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, compute, nullptr},
        {kOffsets, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, compute, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(std::size(bindings));
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device->device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) return false;
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    if (vkCreatePipelineLayout(device->device, &pl, nullptr, &m_pipelineLayout) != VK_SUCCESS) return false;

    auto build = [&](const char* shader, VkPipeline& out) {
        VkShaderModule module = ProbeGpu::LoadShader(device->device, shader);
        if (!module) return false;
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT,
                      module, "main", nullptr};
        info.layout = m_pipelineLayout;
        const VkResult result = vkCreateComputePipelines(device->device, VK_NULL_HANDLE, 1, &info, nullptr, &out);
        vkDestroyShaderModule(device->device, module, nullptr);
        return result == VK_SUCCESS;
    };
    if (!build("cascade_trace.comp.spv", m_tracePipeline) || !build("cascade_update.comp.spv", m_updatePipeline)) {
        std::cerr << "[ProbeCascades] Pipelines unavailable; dynamic GI is off\n";
        if (m_tracePipeline) vkDestroyPipeline(device->device, m_tracePipeline, nullptr);
        if (m_updatePipeline) vkDestroyPipeline(device->device, m_updatePipeline, nullptr);
        m_tracePipeline = m_updatePipeline = VK_NULL_HANDLE;
    }
    return true;
}

bool ProbeCascades::CreateSwapchainResources(uint32_t imageCount) {
    m_frames.assign(imageCount, Frame{});
    for (Frame& frame : m_frames) {
        try {
            frame.params.Create(m_device->allocator, sizeof(GpuCascadeParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU);
        } catch (const std::exception&) {
            return false;
        }
    }
    for (uint32_t i = 0; i < imageCount; ++i) writeParams(i, false, Mat4::Identity());
    if (!m_tracePipeline) return true;

    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * imageCount},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * imageCount},
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, imageCount},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 * imageCount},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * imageCount},
    };
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = imageCount;
    pool.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
    pool.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(m_device->device, &pool, nullptr, &m_pool) != VK_SUCCESS) return false;
    std::vector<VkDescriptorSetLayout> layouts(imageCount, m_setLayout);
    std::vector<VkDescriptorSet> sets(imageCount);
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = m_pool;
    alloc.descriptorSetCount = imageCount;
    alloc.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(m_device->device, &alloc, sets.data()) != VK_SUCCESS) return false;
    for (uint32_t i = 0; i < imageCount; ++i) {
        m_frames[i].set = sets[i];
        try {
            m_frames[i].jobs.Create(m_device->allocator, kProbeBudget * sizeof(uint32_t),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        } catch (const std::exception&) {
            return false;
        }
    }
    return true;
}

void ProbeCascades::DestroySwapchainResources() {
    if (!m_device) return;
    for (Frame& frame : m_frames) {
        frame.params.Destroy(m_device->allocator);
        frame.jobs.Destroy(m_device->allocator);
    }
    m_frames.clear();
    if (m_pool) vkDestroyDescriptorPool(m_device->device, m_pool, nullptr);
    m_pool = VK_NULL_HANDLE;
    m_active = false;
}

void ProbeCascades::Shutdown() {
    if (!m_device) return;
    DestroySwapchainResources();
    destroyGrid();
    VkDevice device = m_device->device;
    if (m_tracePipeline) vkDestroyPipeline(device, m_tracePipeline, nullptr);
    if (m_updatePipeline) vkDestroyPipeline(device, m_updatePipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    if (m_sampler) vkDestroySampler(device, m_sampler, nullptr);
    m_tracePipeline = m_updatePipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_setLayout = VK_NULL_HANDLE;
    m_sampler = VK_NULL_HANDLE;
    m_device = nullptr;
}

bool ProbeCascades::createAtlas(Atlas& atlas, VkFormat format, uint32_t width, uint32_t height) {
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {width, height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(m_device->allocator, &image, &alloc, &atlas.image, &atlas.allocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = atlas.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vkCreateImageView(m_device->device, &view, nullptr, &atlas.view) == VK_SUCCESS;
}

bool ProbeCascades::createGrid(const Cell& counts) {
    m_grid.counts = counts;
    m_grid.baseSpacing = kBaseSpacing;
    const uint32_t tilesX = static_cast<uint32_t>(counts[0] * counts[1]);
    const uint32_t tilesY = static_cast<uint32_t>(counts[2] * ProbeCascadeGrid::kCascades);
    const size_t probes = static_cast<size_t>(m_grid.ProbesPerCascade()) * ProbeCascadeGrid::kCascades;
    if (!createAtlas(m_irradiance, VK_FORMAT_R16G16B16A16_SFLOAT, tilesX * kIrradianceTile, tilesY * kIrradianceTile) ||
        !createAtlas(m_visibility, VK_FORMAT_R16G16_SFLOAT, tilesX * kVisibilityTile, tilesY * kVisibilityTile)) {
        return false;
    }
    try {
        m_state.Create(m_device->allocator, probes * 4 * sizeof(int32_t),
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VMA_MEMORY_USAGE_GPU_ONLY);
        m_offsets.Create(m_device->allocator, probes * 4 * sizeof(float),
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VMA_MEMORY_USAGE_GPU_ONLY);
        m_rays.Create(m_device->allocator, static_cast<VkDeviceSize>(kProbeBudget) * kRaysPerProbe * 4 * sizeof(float),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    } catch (const std::exception& e) {
        std::cerr << "[ProbeCascades] " << e.what() << "\n";
        return false;
    }
    // Atlases stay in GENERAL (written as storage images, sampled by surfaces); every probe starts untraced.
    ProbeGpu::SubmitOnce(m_device, [&](VkCommandBuffer cmd) {
        for (Atlas* atlas : {&m_irradiance, &m_visibility}) {
            VkImageMemoryBarrier toGeneral{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toGeneral.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            toGeneral.srcQueueFamilyIndex = toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGeneral.image = atlas->image;
            toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &toGeneral);
            const VkClearColorValue zero{};
            const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(cmd, atlas->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        }
        vkCmdFillBuffer(cmd, m_state.buffer, 0, VK_WHOLE_SIZE, static_cast<uint32_t>(INT_MIN));
        vkCmdFillBuffer(cmd, m_offsets.buffer, 0, VK_WHOLE_SIZE, 0);
        VkMemoryBarrier cleared{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        cleared.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cleared.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                             &cleared, 0, nullptr, 0, nullptr);
    });
    m_scheduler.Reset(m_grid);
    m_hasGrid = true;
    std::cout << "[ProbeCascades] " << ProbeCascadeGrid::kCascades << " grids of " << counts[0] << "x" << counts[1]
              << "x" << counts[2] << " probes, " << kBaseSpacing << " m finest spacing\n";
    return true;
}

void ProbeCascades::destroyGrid() {
    for (Atlas* atlas : {&m_irradiance, &m_visibility}) {
        if (atlas->view) vkDestroyImageView(m_device->device, atlas->view, nullptr);
        if (atlas->image) vmaDestroyImage(m_device->allocator, atlas->image, atlas->allocation);
        *atlas = Atlas{};
    }
    m_state.Destroy(m_device->allocator);
    m_offsets.Destroy(m_device->allocator);
    m_rays.Destroy(m_device->allocator);
    m_hasGrid = false;
}

void ProbeCascades::writeParams(uint32_t imageIndex, bool active, const Mat4& rotation) {
    GpuCascadeParams params{};
    params.counts[0] = m_grid.counts[0];
    params.counts[1] = m_grid.counts[1];
    params.counts[2] = m_grid.counts[2];
    params.counts[3] = active ? ProbeCascadeGrid::kCascades : 0;
    for (int c = 0; c < ProbeCascadeGrid::kCascades; ++c) {
        for (int a = 0; a < 3; ++a) params.minCell[c][a] = m_minCells[c][a];
        params.spacing[c] = m_grid.Spacing(c);
    }
    params.settings[0] = kMaxHysteresis;
    params.settings[1] = kMaxRaySpacings;
    params.settings[2] = static_cast<float>(m_jobCount);
    params.rotation = rotation;
    void* mapped = nullptr;
    vmaMapMemory(m_device->allocator, m_frames[imageIndex].params.allocation, &mapped);
    std::memcpy(mapped, &params, sizeof(params));
    vmaUnmapMemory(m_device->allocator, m_frames[imageIndex].params.allocation);
}

void ProbeCascades::Prepare(uint32_t imageIndex, const Cell* counts, const Vec3& camera, const Mat4& rotation,
                            VkBuffer lighting, VkAccelerationStructureKHR scene, VkBuffer instances) {
    m_active = false;
    m_jobCount = 0;
    if (!m_device || imageIndex >= m_frames.size()) return;
    const bool wanted = counts && m_tracePipeline && scene && instances;
    if (wanted && (!m_hasGrid || m_grid.counts != *counts)) {
        vkDeviceWaitIdle(m_device->device);
        destroyGrid();
        if (!createGrid(*counts)) {
            std::cerr << "[ProbeCascades] Could not create the probe grids; dynamic GI is off\n";
            destroyGrid();
        }
    }
    if (!counts && m_hasGrid) {
        vkDeviceWaitIdle(m_device->device);  // a lower quality tier gives the atlases' memory back
        destroyGrid();
    }
    if (!wanted || !m_hasGrid) {
        writeParams(imageIndex, false, rotation);
        return;
    }

    for (int c = 0; c < ProbeCascadeGrid::kCascades; ++c) m_minCells[c] = m_grid.MinCell(c, camera);
    m_scheduler.Move(m_minCells);
    const std::vector<uint32_t> jobs = m_scheduler.Schedule(kProbeBudget);
    m_jobCount = static_cast<uint32_t>(jobs.size());
    Frame& frame = m_frames[imageIndex];
    if (m_jobCount > 0) {
        void* mapped = nullptr;
        vmaMapMemory(m_device->allocator, frame.jobs.allocation, &mapped);
        std::memcpy(mapped, jobs.data(), jobs.size() * sizeof(uint32_t));
        vmaUnmapMemory(m_device->allocator, frame.jobs.allocation);
    }
    writeParams(imageIndex, true, rotation);

    VkWriteDescriptorSetAccelerationStructureKHR asWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    asWrite.accelerationStructureCount = 1;
    asWrite.pAccelerationStructures = &scene;
    const VkDescriptorBufferInfo lightingInfo{lighting, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo paramsInfo{frame.params.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo stateInfo{m_state.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo instancesInfo{instances, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo jobsInfo{frame.jobs.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo raysInfo{m_rays.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo offsetsInfo{m_offsets.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorImageInfo irrStore{VK_NULL_HANDLE, m_irradiance.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo visStore{VK_NULL_HANDLE, m_visibility.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo irrRead{m_sampler, m_irradiance.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo visRead{m_sampler, m_visibility.view, VK_IMAGE_LAYOUT_GENERAL};
    auto write = [&](uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* buffer,
                     const VkDescriptorImageInfo* image) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = frame.set;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = type;
        w.pBufferInfo = buffer;
        w.pImageInfo = image;
        return w;
    };
    VkWriteDescriptorSet sceneWrite = write(kScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, nullptr, nullptr);
    sceneWrite.pNext = &asWrite;
    const VkWriteDescriptorSet writes[] = {
        write(kLighting, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &lightingInfo, nullptr),
        write(kIrradianceStore, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &irrStore),
        write(kVisibilityStore, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &visStore),
        sceneWrite,
        write(kParams, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &paramsInfo, nullptr),
        write(kState, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &stateInfo, nullptr),
        write(kIrradianceRead, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &irrRead),
        write(kVisibilityRead, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &visRead),
        write(kInstances, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &instancesInfo, nullptr),
        write(kJobs, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &jobsInfo, nullptr),
        write(kRays, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &raysInfo, nullptr),
        write(kOffsets, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &offsetsInfo, nullptr),
    };
    vkUpdateDescriptorSets(m_device->device, static_cast<uint32_t>(std::size(writes)), writes, 0, nullptr);
    m_active = true;
}

void ProbeCascades::Invalidate() {
    if (!m_hasGrid) return;
    m_scheduler.Reset(m_grid);
    m_clearPending = true;
}

void ProbeCascades::Record(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (!m_active || m_jobCount == 0) return;
    if (m_clearPending) {
        // Earlier frames may still read the state; then every probe reads as never traced.
        VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        before.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
        vkCmdFillBuffer(cmd, m_state.buffer, 0, VK_WHOLE_SIZE, static_cast<uint32_t>(INT_MIN));
        vkCmdFillBuffer(cmd, m_offsets.buffer, 0, VK_WHOLE_SIZE, 0);
        VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &after,
                             0, nullptr, 0, nullptr);
        m_clearPending = false;
    }
    auto barrier = [&](VkPipelineStageFlags src, VkAccessFlags srcAccess, VkPipelineStageFlags dst,
                       VkAccessFlags dstAccess) {
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        b.srcAccessMask = srcAccess;
        b.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(cmd, src, dst, 0, 1, &b, 0, nullptr, 0, nullptr);
    };
    // Earlier frames' surfaces and probe passes are done with the atlases, state and rays.
    barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_frames[imageIndex].set, 0,
                            nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_tracePipeline);
    vkCmdDispatch(cmd, m_jobCount, 1, 1);
    barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_updatePipeline);
    vkCmdDispatch(cmd, m_jobCount, 1, 1);
    barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

} // namespace Hephaestus
