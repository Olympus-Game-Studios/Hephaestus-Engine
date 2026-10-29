#include "TemporalResolve.h"

#include "ProbeGpuUtil.h"
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"

#include <cmath>
#include <iostream>
#include <utility>

namespace Hephaestus {

namespace {

constexpr float kCurrentWeight = 0.1f;  // about ten frames of history

struct Push {
    Mat4 currentToPrevious;
    Vec4 params;
    Vec4 cameraDepth;
};

// General 4x4 inverse (Gauss-Jordan, partial pivoting) of a column-major matrix; identity when singular.
Mat4 Inverse(const Mat4& m) {
    double a[4][8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            a[r][c] = m.m[c * 4 + r];
            a[r][c + 4] = r == c ? 1.0 : 0.0;
        }
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r) {
            if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;
        }
        if (std::fabs(a[pivot][col]) < 1e-12) return Mat4::Identity();
        for (int c = 0; c < 8; ++c) std::swap(a[col][c], a[pivot][c]);
        const double inv = 1.0 / a[col][col];
        for (int c = 0; c < 8; ++c) a[col][c] *= inv;
        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            const double f = a[r][col];
            for (int c = 0; c < 8; ++c) a[r][c] -= f * a[col][c];
        }
    }
    Mat4 out;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) out.m[c * 4 + r] = static_cast<float>(a[r][c + 4]);
    }
    return out;
}

} // namespace

bool TemporalResolve::Init(VulkanDevice* device) {
    m_device = device;
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device->device, &sampler, nullptr, &m_pointSampler) != VK_SUCCESS) return false;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    if (vkCreateSampler(device->device, &sampler, nullptr, &m_linearSampler) != VK_SUCCESS) return false;

    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(std::size(bindings));
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device->device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) return false;
    const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device->device, &pl, nullptr, &m_pipelineLayout) != VK_SUCCESS) return false;

    VkShaderModule module = ProbeGpu::LoadShader(device->device, "temporal_resolve.comp.spv");
    if (!module) return false;
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module,
                  "main", nullptr};
    info.layout = m_pipelineLayout;
    const VkResult result = vkCreateComputePipelines(device->device, VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline);
    vkDestroyShaderModule(device->device, module, nullptr);
    return result == VK_SUCCESS;
}

bool TemporalResolve::CreateSwapchainResources(const VulkanSwapchain& swapchain) {
    VkDevice device = m_device->device;
    m_extent = swapchain.extent;
    for (Image& history : m_history) {
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = VulkanSwapchain::kHdrFormat;
        image.extent = {m_extent.width, m_extent.height, 1};
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo alloc{};
        alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(m_device->allocator, &image, &alloc, &history.image, &history.allocation, nullptr) !=
            VK_SUCCESS) {
            return false;
        }
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = history.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VulkanSwapchain::kHdrFormat;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &view, nullptr, &history.view) != VK_SUCCESS) return false;
    }
    ProbeGpu::SubmitOnce(m_device, [&](VkCommandBuffer cmd) {
        for (Image& history : m_history) {
            VkImageMemoryBarrier toGeneral{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            toGeneral.srcQueueFamilyIndex = toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGeneral.image = history.image;
            toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &toGeneral);
        }
    });

    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
    };
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 2;
    pool.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
    pool.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &pool, nullptr, &m_pool) != VK_SUCCESS) return false;
    const VkDescriptorSetLayout layouts[2] = {m_setLayout, m_setLayout};
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = m_pool;
    alloc.descriptorSetCount = 2;
    alloc.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(device, &alloc, m_sets) != VK_SUCCESS) return false;
    for (uint32_t parity = 0; parity < 2; ++parity) {
        const VkDescriptorImageInfo current{m_pointSampler, swapchain.hdrImageView,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo depth{m_pointSampler, swapchain.depthImageView,
                                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo history{m_linearSampler, m_history[1 - parity].view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo resolved{VK_NULL_HANDLE, m_history[parity].view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo* infos[4] = {&current, &depth, &history, &resolved};
        VkWriteDescriptorSet writes[4] = {};
        for (uint32_t b = 0; b < 4; ++b) {
            writes[b] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[b].dstSet = m_sets[parity];
            writes[b].dstBinding = b;
            writes[b].descriptorCount = 1;
            writes[b].descriptorType = b == 3 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[b].pImageInfo = infos[b];
        }
        vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
    }
    m_historyValid = false;
    return true;
}

void TemporalResolve::DestroySwapchainResources() {
    if (!m_device) return;
    VkDevice device = m_device->device;
    for (Image& history : m_history) {
        if (history.view) vkDestroyImageView(device, history.view, nullptr);
        if (history.image) vmaDestroyImage(m_device->allocator, history.image, history.allocation);
        history = Image{};
    }
    if (m_pool) vkDestroyDescriptorPool(device, m_pool, nullptr);
    m_pool = VK_NULL_HANDLE;
    m_sets[0] = m_sets[1] = VK_NULL_HANDLE;
    m_historyValid = false;
}

void TemporalResolve::Shutdown() {
    if (!m_device) return;
    DestroySwapchainResources();
    VkDevice device = m_device->device;
    if (m_pipeline) vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    if (m_pointSampler) vkDestroySampler(device, m_pointSampler, nullptr);
    if (m_linearSampler) vkDestroySampler(device, m_linearSampler, nullptr);
    m_pipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_setLayout = VK_NULL_HANDLE;
    m_pointSampler = m_linearSampler = VK_NULL_HANDLE;
    m_device = nullptr;
}

void TemporalResolve::Record(VkCommandBuffer cmd, const Mat4& viewProj, const Vec4& cameraDepth, bool reset) {
    // Earlier frames' passes, tone maps and reflections are done with the history images.
    VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    before.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);

    Push push;
    push.currentToPrevious = Mat4::Multiply(m_previousViewProj, Inverse(viewProj));
    push.params = Vec4(kCurrentWeight, (reset || !m_historyValid) ? 1.0f : 0.0f, 0.0f, 0.0f);
    push.cameraDepth = cameraDepth;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_sets[m_parity], 0,
                            nullptr);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (m_extent.width + 7) / 8, (m_extent.height + 7) / 8, 1);

    // The tone map samples the result directly.
    VkMemoryBarrier written{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    written.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    written.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                         &written, 0, nullptr, 0, nullptr);

    m_previousViewProj = viewProj;
    m_historyValid = true;
    m_parity = 1 - m_parity;
}

} // namespace Hephaestus
