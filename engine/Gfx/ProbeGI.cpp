#include "ProbeGI.h"

#include "VulkanDevice.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>

namespace Hephaestus {

namespace {

constexpr int kIrradianceTile = 10;  // shaders/probe_common.glsl IRRADIANCE_TILE
constexpr int kVisibilityTile = 18;  // shaders/probe_common.glsl VISIBILITY_TILE
constexpr float kHysteresis = 0.97f;
constexpr uint32_t kDebugSphereVertices = 12 * 8 * 6;  // shaders/probe_debug.vert SEGMENTS * RINGS * 6

struct ProbeGIParams {
    Vec4 firstProbe;
    Vec4 probeSpacing;
    int32_t probeCounts[4];
    Mat4 sunViewProj;
    Vec4 giParams;
    Mat4 rayRotation;
};

struct GpuRayHit {
    Vec4 normalDistance;
    Vec4 albedo;
    Vec4 emissive;
};

enum Binding : uint32_t {
    kParams = 0,
    kLighting = 1,
    kRays = 2,
    kOffsets = 3,
    kSunDepth = 4,
    kIrradianceOut = 6,
    kVisibilityOut = 8,
    kIrradianceRead = 9,
    kVisibilityRead = 10,
    kScene = 11,  // scene acceleration structure, only with hardware ray tracing
};

VkShaderModule LoadShader(VkDevice device, const std::string& name) {
    const std::string candidates[] = {"shaders/" + name, "../shaders/" + name, "build/Release/shaders/" + name,
                                      "../build/Release/shaders/" + name};
    for (const auto& path : candidates) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) continue;
        const size_t size = static_cast<size_t>(file.tellg());
        std::vector<char> code(size);
        file.seekg(0);
        file.read(code.data(), static_cast<std::streamsize>(size));
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &info, nullptr, &module) == VK_SUCCESS) {
            return module;
        }
        return VK_NULL_HANDLE;
    }
    std::cerr << "[ProbeGI] Missing shader " << name << "\n";
    return VK_NULL_HANDLE;
}

void SubmitOnce(VulkanDevice* device, const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = device->commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device->device, &alloc, &cmd);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    record(cmd);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(device->graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(device->graphicsQueue);
    vkFreeCommandBuffers(device->device, device->commandPool, 1, &cmd);
}

// Uniform random rotation (Shoemake): traced probes see new directions every frame, and hysteresis
// averages them, which removes the blotches a fixed set of 128 directions leaves.
Mat4 RandomRotation(std::mt19937& rng) {
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const float u1 = u(rng), u2 = u(rng) * 6.28318531f, u3 = u(rng) * 6.28318531f;
    const float a = std::sqrt(1.0f - u1), b = std::sqrt(u1);
    const float x = a * std::sin(u2), y = a * std::cos(u2), z = b * std::sin(u3), w = b * std::cos(u3);
    Mat4 m = Mat4::Identity();
    m.m[0] = 1 - 2 * (y * y + z * z);
    m.m[1] = 2 * (x * y + z * w);
    m.m[2] = 2 * (x * z - y * w);
    m.m[4] = 2 * (x * y - z * w);
    m.m[5] = 1 - 2 * (x * x + z * z);
    m.m[6] = 2 * (y * z + x * w);
    m.m[8] = 2 * (x * z + y * w);
    m.m[9] = 2 * (y * z - x * w);
    m.m[10] = 1 - 2 * (x * x + y * y);
    return m;
}

std::filesystem::file_time_type WriteTime(const std::filesystem::path& file) {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(file, ec);
    return ec ? std::filesystem::file_time_type{} : time;
}

} // namespace

bool ProbeGI::Init(VulkanDevice* device) {
    m_device = device;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device->physicalDevice, &props);
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device->physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device->physicalDevice, &familyCount, families.data());
    const bool timestamps = device->graphicsFamily < familyCount &&
                            families[device->graphicsFamily].timestampValidBits > 0;
    m_timestampPeriodNs = timestamps ? props.limits.timestampPeriod : 0.0f;

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device->device, &sampler, nullptr, &m_atlasSampler) != VK_SUCCESS) return false;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vkCreateSampler(device->device, &sampler, nullptr, &m_depthSampler) != VK_SUCCESS) return false;

    m_tracerReady = m_tracer.Init(device);
    if (device->SupportsRayQuery() && !m_tracerReady) {
        std::cerr << "[ProbeGI] Hardware ray tracing unavailable; probe volumes use their bakes\n";
    }
    if (!createLayouts() || !createPlaceholders()) return false;
    if (!createComputePipeline()) {
        std::cerr << "[ProbeGI] Probe update pipeline unavailable; probe volumes will not be lit\n";
    }
    return true;
}

bool ProbeGI::createLayouts() {
    const VkShaderStageFlags compute = VK_SHADER_STAGE_COMPUTE_BIT;
    const VkShaderStageFlags all = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkDescriptorSetLayoutBinding bindings[] = {
        {kParams, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, all, nullptr},
        {kLighting, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, compute, nullptr},
        {kRays, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, compute, nullptr},
        {kOffsets, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, all, nullptr},
        {kSunDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, compute, nullptr},
        {kIrradianceOut, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr},
        {kVisibilityOut, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr},
        {kIrradianceRead, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, all, nullptr},
        {kVisibilityRead, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, all, nullptr},
        {kScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(std::size(bindings)) - (m_tracerReady ? 0 : 1);
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(m_device->device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) return false;
    VkDescriptorSetLayoutCreateInfo emptyInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    if (vkCreateDescriptorSetLayout(m_device->device, &emptyInfo, nullptr, &m_emptySetLayout) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    return vkCreatePipelineLayout(m_device->device, &pl, nullptr, &m_computeLayout) == VK_SUCCESS;
}

bool ProbeGI::createPlaceholders() {
    try {
        const float zeros[4] = {};
        m_placeholderBuffer.Create(m_device->allocator, sizeof(zeros), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   VMA_MEMORY_USAGE_CPU_TO_GPU, const_cast<float*>(zeros));
    } catch (const std::exception&) {
        return false;
    }
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    image.extent = {1, 1, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(m_device->allocator, &image, &alloc, &m_placeholderAtlas.image, &m_placeholderAtlas.allocation,
                       nullptr) != VK_SUCCESS) {
        return false;
    }
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = m_placeholderAtlas.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(m_device->device, &view, nullptr, &m_placeholderAtlas.view) != VK_SUCCESS) return false;
    SubmitOnce(m_device, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier toGeneral{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toGeneral.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        toGeneral.srcQueueFamilyIndex = toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image = m_placeholderAtlas.image;
        toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &toGeneral);
        const VkClearColorValue zero{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, m_placeholderAtlas.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        VkMemoryBarrier cleared{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        cleared.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cleared.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                             &cleared, 0, nullptr, 0, nullptr);
    });
    return true;
}

bool ProbeGI::createComputePipeline() {
    VkShaderModule module = LoadShader(m_device->device, "probe_update.comp.spv");
    if (!module) return false;
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT,
                  module, "main", nullptr};
    info.layout = m_computeLayout;
    const VkResult result =
        vkCreateComputePipelines(m_device->device, VK_NULL_HANDLE, 1, &info, nullptr, &m_computePipeline);
    vkDestroyShaderModule(m_device->device, module, nullptr);
    return result == VK_SUCCESS;
}

bool ProbeGI::InitSunMap(VkRenderPass depthOnlyRenderPass) {
    if (depthOnlyRenderPass == VK_NULL_HANDLE) {
        return true;  // no shadow pass: probes treat the sun as unoccluded
    }
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_D32_SFLOAT;
    image.extent = {kSunMapResolution, kSunMapResolution, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(m_device->allocator, &image, &alloc, &m_sunImage, &m_sunAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = m_sunImage;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_D32_SFLOAT;
    view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(m_device->device, &view, nullptr, &m_sunView) != VK_SUCCESS) return false;
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = depthOnlyRenderPass;
    fb.attachmentCount = 1;
    fb.pAttachments = &m_sunView;
    fb.width = kSunMapResolution;
    fb.height = kSunMapResolution;
    fb.layers = 1;
    if (vkCreateFramebuffer(m_device->device, &fb, nullptr, &m_sunFramebuffer) != VK_SUCCESS) return false;
    // The depth-only pass ends in SHADER_READ_ONLY_OPTIMAL; start there so frames without a sun still bind it.
    SubmitOnce(m_device, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_sunImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
    });
    return true;
}

bool ProbeGI::CreateSwapchainResources(VkRenderPass sceneRenderPass, uint32_t sceneSubpass,
                                       VkDescriptorSetLayout cameraSetLayout, uint32_t imageCount) {
    VkDevice device = m_device->device;
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 * imageCount},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 * imageCount},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6 * imageCount},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 * imageCount},
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 2 * imageCount},
    };
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 2 * imageCount;
    pool.poolSizeCount = static_cast<uint32_t>(std::size(sizes)) - (m_tracerReady ? 0 : 1);
    pool.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &pool, nullptr, &m_descriptorPool) != VK_SUCCESS) return false;
    m_sets.assign(imageCount, {});
    std::vector<VkDescriptorSetLayout> layouts(2 * imageCount, m_setLayout);
    std::vector<VkDescriptorSet> sets(2 * imageCount);
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 2 * imageCount;
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &allocInfo, sets.data()) != VK_SUCCESS) return false;
    for (uint32_t i = 0; i < imageCount; ++i) {
        m_sets[i] = {sets[2 * i], sets[2 * i + 1]};
    }

    m_paramBuffers.resize(imageCount);
    for (auto& buffer : m_paramBuffers) {
        try {
            buffer.Create(m_device->allocator, sizeof(ProbeGIParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VMA_MEMORY_USAGE_CPU_TO_GPU);
        } catch (const std::exception&) {
            return false;
        }
    }

    VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    query.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query.queryCount = 2 * imageCount;
    if (vkCreateQueryPool(device, &query, nullptr, &m_queryPool) != VK_SUCCESS) return false;
    m_queryWritten.assign(imageCount, false);
    // The surface pipelines were built to read the scene structure, so it must exist from here on.
    if (m_tracerReady && !m_tracer.CreateSwapchainResources(imageCount)) {
        std::cerr << "[ProbeGI] Hardware ray tracing resources failed\n";
        return false;
    }

    const VkDescriptorSetLayout setLayouts[] = {cameraSetLayout, m_setLayout};
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 2;
    pl.pSetLayouts = setLayouts;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device, &pl, nullptr, &m_debugLayout) != VK_SUCCESS) return false;

    VkShaderModule vert = LoadShader(device, "probe_debug.vert.spv");
    VkShaderModule frag = LoadShader(device, "probe_debug.frag.spv");
    if (!vert || !frag) {
        if (vert) vkDestroyShaderModule(device, vert, nullptr);
        if (frag) vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &cba;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dyn;
    info.layout = m_debugLayout;
    info.renderPass = sceneRenderPass;
    info.subpass = sceneSubpass;
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_debugPipeline);
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    return result == VK_SUCCESS;
}

void ProbeGI::DestroySwapchainResources() {
    if (!m_device) return;
    VkDevice device = m_device->device;
    m_tracer.DestroySwapchainResources();
    if (m_debugPipeline) vkDestroyPipeline(device, m_debugPipeline, nullptr);
    if (m_debugLayout) vkDestroyPipelineLayout(device, m_debugLayout, nullptr);
    if (m_queryPool) vkDestroyQueryPool(device, m_queryPool, nullptr);
    if (m_descriptorPool) vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    for (auto& buffer : m_paramBuffers) buffer.Destroy(m_device->allocator);
    m_debugPipeline = VK_NULL_HANDLE;
    m_debugLayout = VK_NULL_HANDLE;
    m_queryPool = VK_NULL_HANDLE;
    m_descriptorPool = VK_NULL_HANDLE;
    m_paramBuffers.clear();
    m_sets.clear();
    m_queryWritten.clear();
}

void ProbeGI::Shutdown() {
    if (!m_device) return;
    VkDevice device = m_device->device;
    DestroySwapchainResources();
    destroyBake();
    m_tracer.Shutdown();
    if (m_sunFramebuffer) vkDestroyFramebuffer(device, m_sunFramebuffer, nullptr);
    if (m_sunView) vkDestroyImageView(device, m_sunView, nullptr);
    if (m_sunImage) vmaDestroyImage(m_device->allocator, m_sunImage, m_sunAllocation);
    if (m_computePipeline) vkDestroyPipeline(device, m_computePipeline, nullptr);
    if (m_computeLayout) vkDestroyPipelineLayout(device, m_computeLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    if (m_emptySetLayout) vkDestroyDescriptorSetLayout(device, m_emptySetLayout, nullptr);
    m_placeholderBuffer.Destroy(m_device->allocator);
    if (m_placeholderAtlas.view) vkDestroyImageView(device, m_placeholderAtlas.view, nullptr);
    if (m_placeholderAtlas.image) vmaDestroyImage(m_device->allocator, m_placeholderAtlas.image, m_placeholderAtlas.allocation);
    m_placeholderAtlas = Atlas{};
    m_emptySetLayout = VK_NULL_HANDLE;
    if (m_atlasSampler) vkDestroySampler(device, m_atlasSampler, nullptr);
    if (m_depthSampler) vkDestroySampler(device, m_depthSampler, nullptr);
    m_sunFramebuffer = VK_NULL_HANDLE;
    m_sunView = VK_NULL_HANDLE;
    m_sunImage = VK_NULL_HANDLE;
    m_computePipeline = VK_NULL_HANDLE;
    m_computeLayout = VK_NULL_HANDLE;
    m_setLayout = VK_NULL_HANDLE;
    m_atlasSampler = VK_NULL_HANDLE;
    m_depthSampler = VK_NULL_HANDLE;
    m_device = nullptr;
}

void ProbeGI::destroyBake() {
    if (!m_device) return;
    m_rays.Destroy(m_device->allocator);
    m_offsets.Destroy(m_device->allocator);
    for (Atlas* atlas : {&m_irradiance[0], &m_irradiance[1], &m_visibility[0], &m_visibility[1]}) {
        if (atlas->view) vkDestroyImageView(m_device->device, atlas->view, nullptr);
        if (atlas->image) vmaDestroyImage(m_device->allocator, atlas->image, atlas->allocation);
        *atlas = Atlas{};
    }
    m_bakeKey.clear();
    m_probeCount = 0;
    m_active = false;
    m_hardware = false;
}

bool ProbeGI::loadBake(const std::filesystem::path& file, std::string& outError) {
    ProbeBake bake;
    if (!ReadProbeBake(file, bake, outError)) return false;
    if (bake.raysPerProbe != kProbeBakeRaysPerProbe) {
        outError = file.generic_string() + " has " + std::to_string(bake.raysPerProbe) + " rays per probe; " +
                   std::to_string(kProbeBakeRaysPerProbe) + " are required";
        return false;
    }
    std::vector<GpuRayHit> hits(bake.hits.size());
    for (size_t i = 0; i < hits.size(); ++i) {
        const ProbeRayHit& h = bake.hits[i];
        hits[i].normalDistance = Vec4(h.normal.x, h.normal.y, h.normal.z, h.distance);
        hits[i].albedo = Vec4(h.albedo.x, h.albedo.y, h.albedo.z, 1.0f);
        hits[i].emissive = Vec4(h.emissive.x, h.emissive.y, h.emissive.z, h.backface ? 1.0f : 0.0f);
    }
    std::vector<Vec4> offsets(bake.probeOffsets.size());
    for (size_t i = 0; i < offsets.size(); ++i) {
        offsets[i] = Vec4(bake.probeOffsets[i].x, bake.probeOffsets[i].y, bake.probeOffsets[i].z, 0.0f);
    }
    try {
        m_rays.Create(m_device->allocator, hits.size() * sizeof(GpuRayHit), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU, hits.data());
        m_offsets.Create(m_device->allocator, offsets.size() * sizeof(Vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VMA_MEMORY_USAGE_CPU_TO_GPU, offsets.data());
    } catch (const std::exception& e) {
        outError = e.what();
        return false;
    }

    if (!createAtlases(bake.probeCounts)) {
        outError = "Could not create probe atlases";
        return false;
    }

    bake.hits.clear();
    bake.probeOffsets.clear();
    m_bakeGrid = std::move(bake);
    m_probeCount = static_cast<size_t>(m_bakeGrid.probeCounts[0]) * m_bakeGrid.probeCounts[1] * m_bakeGrid.probeCounts[2];
    m_parity = 0;
    m_resetFrames = 1;
    return true;
}

bool ProbeGI::createHardwareVolume(const int counts[3], std::string& outError) {
    const size_t probes = static_cast<size_t>(counts[0]) * counts[1] * counts[2];
    std::vector<Vec4> offsets(probes, Vec4(0.0f, 0.0f, 0.0f, 0.0f));
    try {
        m_rays.Create(m_device->allocator, probes * kProbeBakeRaysPerProbe * sizeof(GpuRayHit),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        m_offsets.Create(m_device->allocator, offsets.size() * sizeof(Vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VMA_MEMORY_USAGE_CPU_TO_GPU, offsets.data());
    } catch (const std::exception& e) {
        outError = e.what();
        return false;
    }
    if (!createAtlases(counts)) {
        outError = "Could not create probe atlases";
        return false;
    }
    m_bakeGrid = ProbeBake{};
    for (int a = 0; a < 3; ++a) m_bakeGrid.probeCounts[a] = counts[a];
    m_bakeGrid.raysPerProbe = kProbeBakeRaysPerProbe;
    m_probeCount = probes;
    m_parity = 0;
    m_resetFrames = 1;
    m_hardware = true;
    return true;
}

bool ProbeGI::createAtlases(const int counts[3]) {
    const uint32_t tilesX = static_cast<uint32_t>(counts[0] * counts[1]);
    const uint32_t tilesY = static_cast<uint32_t>(counts[2]);
    auto createAtlas = [&](Atlas& atlas, VkFormat format, uint32_t tile) {
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = format;
        image.extent = {tilesX * tile, tilesY * tile, 1};
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
    };
    for (int i = 0; i < 2; ++i) {
        if (!createAtlas(m_irradiance[i], VK_FORMAT_R16G16B16A16_SFLOAT, kIrradianceTile) ||
            !createAtlas(m_visibility[i], VK_FORMAT_R16G16_SFLOAT, kVisibilityTile)) {
            return false;
        }
    }
    // Atlases stay in GENERAL: written as storage images and sampled, alternating every frame.
    SubmitOnce(m_device, [&](VkCommandBuffer cmd) {
        for (Atlas* atlas : {&m_irradiance[0], &m_irradiance[1], &m_visibility[0], &m_visibility[1]}) {
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
        VkMemoryBarrier cleared{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        cleared.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cleared.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                             &cleared, 0, nullptr, 0, nullptr);
    });
    return true;
}

void ProbeGI::Prepare(Scene* scene, const std::string& projectRoot, uint32_t imageIndex, VkBuffer lightingBuffer,
                      const Vec3* sunDirection, bool allowHardware, bool traceShadows,
                      const std::function<std::vector<ProbeTraceInstance>()>& collectInstances) {
    prepareVolume(scene, projectRoot, imageIndex, lightingBuffer, sunDirection, allowHardware);
    m_sceneTraced = false;
    if (!m_tracerReady || imageIndex >= m_sets.size()) return;
    if (scene && (traceShadows || (m_active && m_hardware))) {
        m_sceneTraced = m_tracer.PrepareScene(imageIndex, collectInstances());
    }
    if (m_active && m_hardware) {
        if (m_sceneTraced) {
            m_tracer.WriteTraceSet(imageIndex, m_paramBuffers[imageIndex].buffer, lightingBuffer, m_offsets.buffer,
                                   m_rays.buffer);
        } else {
            m_active = false;
        }
    }
    VkWriteDescriptorSetAccelerationStructureKHR asWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    const VkAccelerationStructureKHR structure = m_tracer.SceneStructure(imageIndex);
    asWrite.accelerationStructureCount = 1;
    asWrite.pAccelerationStructures = &structure;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.pNext = &asWrite;
    write.dstSet = m_sets[imageIndex][1];
    write.dstBinding = kScene;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    vkUpdateDescriptorSets(m_device->device, 1, &write, 0, nullptr);
}

void ProbeGI::RecordScene(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (m_sceneTraced) {
        m_tracer.RecordScene(cmd, imageIndex);
    }
}

void ProbeGI::prepareVolume(Scene* scene, const std::string& projectRoot, uint32_t imageIndex,
                            VkBuffer lightingBuffer, const Vec3* sunDirection, bool allowHardware) {
    const bool traceVolumes = m_tracerReady && allowHardware;
    m_active = false;
    if (!m_device || imageIndex >= m_sets.size()) return;
    auto writeInactive = [&]() {
        const ProbeGIParams params{};
        void* mapped = nullptr;
        vmaMapMemory(m_device->allocator, m_paramBuffers[imageIndex].allocation, &mapped);
        std::memcpy(mapped, &params, sizeof(params));
        vmaUnmapMemory(m_device->allocator, m_paramBuffers[imageIndex].allocation);
        writeDescriptorSets(imageIndex, lightingBuffer);
    };
    if (!m_computePipeline || !scene) {
        writeInactive();
        return;
    }

    if (m_queryWritten[imageIndex] && m_timestampPeriodNs > 0.0f) {
        uint64_t stamps[2] = {};
        if (vkGetQueryPoolResults(m_device->device, m_queryPool, 2 * imageIndex, 2, sizeof(stamps), stamps,
                                  sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            m_lastGpuMs = static_cast<float>(stamps[1] - stamps[0]) * m_timestampPeriodNs / 1.0e6f;
        }
        m_queryWritten[imageIndex] = false;
    }

    const ProbeVolumeComponent* volume = nullptr;
    entt::entity volumeEntity = entt::null;
    for (entt::entity handle : scene->GetRegistry().view<ProbeVolumeComponent>()) {
        const auto& candidate = scene->GetRegistry().get<ProbeVolumeComponent>(handle);
        if (candidate.enabled && ((candidate.hardwareRayTracing && traceVolumes) || !candidate.bakePath.empty())) {
            volume = &candidate;
            volumeEntity = handle;
            break;
        }
    }
    if (!volume) {
        if (!m_bakeKey.empty()) {
            vkDeviceWaitIdle(m_device->device);
            destroyBake();
        }
        writeInactive();
        return;
    }

    const bool hardware = volume->hardwareRayTracing && traceVolumes;
    const int counts[3] = {std::max(1, volume->probeCountX), std::max(1, volume->probeCountY),
                           std::max(1, volume->probeCountZ)};
    std::filesystem::path file = std::filesystem::path(projectRoot) / volume->bakePath;
    if (projectRoot.empty() || !std::filesystem::exists(file)) {
        file = volume->bakePath;
    }
    const std::string key = hardware
        ? "rt|" + std::to_string(counts[0]) + "x" + std::to_string(counts[1]) + "x" + std::to_string(counts[2]) + "|" +
              std::to_string(static_cast<uint32_t>(volumeEntity))
        : file.generic_string() + "|" + std::to_string(WriteTime(file).time_since_epoch().count()) + "|" +
              std::to_string(static_cast<uint32_t>(volumeEntity));
    if (hardware && key != m_bakeKey) {
        vkDeviceWaitIdle(m_device->device);
        destroyBake();
        std::string error;
        if (!createHardwareVolume(counts, error)) {
            std::cerr << "[ProbeGI] " << error << "\n";
            destroyBake();
        }
        m_bakeKey = key;
        if (m_probeCount > 0) {
            std::cout << "[ProbeGI] Tracing " << m_probeCount << " probes with hardware ray tracing\n";
        }
    } else if (!hardware && key != m_bakeKey) {
        vkDeviceWaitIdle(m_device->device);
        destroyBake();
        std::string error;
        if (!loadBake(file, error)) {
            std::cerr << "[ProbeGI] " << error << "\n";
            destroyBake();
        }
        m_bakeKey = key;  // a failed load is not retried until the file or volume changes
        if (m_probeCount > 0) {
            std::cout << "[ProbeGI] Loaded " << m_probeCount << " probes from " << file.generic_string() << "\n";
        }
    }
    if (m_probeCount == 0) {
        writeInactive();
        return;
    }
    if (m_hardware) {
        // Traced volumes follow the component live; only a probe-count change rebuilds them.
        const Mat4 world = scene->GetWorldTransform(Entity(volumeEntity, scene));
        m_bakeGrid.center = Vec3(world.m[12], world.m[13], world.m[14]);
        m_bakeGrid.size = volume->size;
    }

    ProbeVolumeComponent grid;
    grid.size = m_bakeGrid.size;
    grid.probeCountX = m_bakeGrid.probeCounts[0];
    grid.probeCountY = m_bakeGrid.probeCounts[1];
    grid.probeCountZ = m_bakeGrid.probeCounts[2];
    const Vec3 first = vadd(m_bakeGrid.center, grid.ProbeOffset(0, 0, 0));
    const float sizes[3] = {grid.size.x, grid.size.y, grid.size.z};
    float spacing[3];
    for (int a = 0; a < 3; ++a) {
        spacing[a] = m_bakeGrid.probeCounts[a] > 1 ? sizes[a] / static_cast<float>(m_bakeGrid.probeCounts[a] - 1) : 0.0f;
    }
    const float step = vlen(Vec3(spacing[0], spacing[1], spacing[2]));
    const float maxDistance = step > 0.0f ? 1.5f * step : std::max(1.0f, vlen(grid.size));

    m_sunValid = sunDirection != nullptr && m_sunFramebuffer != VK_NULL_HANDLE;
    if (m_sunValid) {
        const Vec3 dir = vnorm(*sunDirection);
        const float radius = 0.5f * vlen(grid.size) + maxDistance;
        Vec3 up(0.0f, 1.0f, 0.0f);
        if (std::fabs(vdot(dir, up)) > 0.99f) up = Vec3(1.0f, 0.0f, 0.0f);
        const Vec3 eye = vsub(m_bakeGrid.center, vscale(dir, 2.0f * radius));
        const Mat4 view = Mat4::LookAt(eye, m_bakeGrid.center, up);
        const Mat4 proj = Mat4::OrthographicShadow(-radius, radius, -radius, radius, 0.1f, 4.0f * radius);
        m_sunViewProj = Mat4::Multiply(proj, view);
    }

    ProbeGIParams params{};
    params.firstProbe = Vec4(first.x, first.y, first.z, volume->intensity);
    params.probeSpacing = Vec4(spacing[0], spacing[1], spacing[2], kHysteresis);
    params.probeCounts[0] = m_bakeGrid.probeCounts[0];
    params.probeCounts[1] = m_bakeGrid.probeCounts[1];
    params.probeCounts[2] = m_bakeGrid.probeCounts[2];
    params.probeCounts[3] = 1;
    params.sunViewProj = m_sunViewProj;
    params.rayRotation = m_hardware ? RandomRotation(m_rng) : Mat4::Identity();
    params.giParams = Vec4(maxDistance, m_sunValid && !m_hardware ? 1.0f : 0.0f, m_resetFrames > 0 ? 1.0f : 0.0f,
                           m_hardware ? 1.0f : 0.0f);
    void* mapped = nullptr;
    vmaMapMemory(m_device->allocator, m_paramBuffers[imageIndex].allocation, &mapped);
    std::memcpy(mapped, &params, sizeof(params));
    vmaUnmapMemory(m_device->allocator, m_paramBuffers[imageIndex].allocation);

    writeDescriptorSets(imageIndex, lightingBuffer);
    m_intensity = volume->intensity;
    m_active = true;
}

void ProbeGI::writeDescriptorSets(uint32_t imageIndex, VkBuffer lightingBuffer) {
    const uint32_t read = m_parity;
    const uint32_t written = 1 - m_parity;
    const bool loaded = m_probeCount > 0;
    // Placeholders are only read by draws; no dispatch runs while the volume is not loaded.
    const VkImageView placeholder = m_placeholderAtlas.view;
    for (int which = 0; which < 2; ++which) {
        const uint32_t sampled = which == 0 ? read : written;  // compute reads history, drawing reads the result
        VkDescriptorSet set = m_sets[imageIndex][which];
        const VkDescriptorBufferInfo params{m_paramBuffers[imageIndex].buffer, 0, sizeof(ProbeGIParams)};
        const VkDescriptorBufferInfo lighting{lightingBuffer, 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo rays{loaded ? m_rays.buffer : m_placeholderBuffer.buffer, 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo offsets{loaded ? m_offsets.buffer : m_placeholderBuffer.buffer, 0, VK_WHOLE_SIZE};
        const VkDescriptorImageInfo sun{m_depthSampler, m_sunView ? m_sunView : placeholder,
                                        m_sunView ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo irrOut{VK_NULL_HANDLE, loaded ? m_irradiance[written].view : placeholder,
                                           VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo visOut{VK_NULL_HANDLE, loaded ? m_visibility[written].view : placeholder,
                                           VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo irrRead{m_atlasSampler, loaded ? m_irradiance[sampled].view : placeholder,
                                            VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo visRead{m_atlasSampler, loaded ? m_visibility[sampled].view : placeholder,
                                            VK_IMAGE_LAYOUT_GENERAL};
        auto write = [&](uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* buffer,
                         const VkDescriptorImageInfo* image) {
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = set;
            w.dstBinding = binding;
            w.descriptorCount = 1;
            w.descriptorType = type;
            w.pBufferInfo = buffer;
            w.pImageInfo = image;
            return w;
        };
        const VkWriteDescriptorSet writes[] = {
            write(kParams, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &params, nullptr),
            write(kLighting, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &lighting, nullptr),
            write(kRays, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &rays, nullptr),
            write(kOffsets, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &offsets, nullptr),
            write(kSunDepth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &sun),
            write(kIrradianceOut, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &irrOut),
            write(kVisibilityOut, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &visOut),
            write(kIrradianceRead, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &irrRead),
            write(kVisibilityRead, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &visRead),
        };
        vkUpdateDescriptorSets(m_device->device, static_cast<uint32_t>(std::size(writes)), writes, 0, nullptr);
    }
}

void ProbeGI::BeginTiming(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (!m_active || m_timestampPeriodNs <= 0.0f) return;
    vkCmdResetQueryPool(cmd, m_queryPool, 2 * imageIndex, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queryPool, 2 * imageIndex);
}

void ProbeGI::RecordUpdate(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (!m_active) return;
    // The atlases alternate between frames in flight: earlier compute and fragment work must be done with them.
    VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    before.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);

    if (m_hardware) {
        m_tracer.Record(cmd, imageIndex, static_cast<uint32_t>(m_probeCount));
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computeLayout, 0, 1, &m_sets[imageIndex][0], 0,
                            nullptr);
    vkCmdDispatch(cmd, static_cast<uint32_t>(m_probeCount), 1, 1);

    VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    after.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &after, 0,
                         nullptr, 0, nullptr);
    if (m_timestampPeriodNs > 0.0f) {
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_queryPool, 2 * imageIndex + 1);
        m_queryWritten[imageIndex] = true;
    }
    m_parity = 1 - m_parity;
    if (m_resetFrames > 0) --m_resetFrames;
}

void ProbeGI::RecordDebug(VkCommandBuffer cmd, uint32_t imageIndex, VkDescriptorSet cameraSet) {
    if (!m_active || !m_debugPipeline) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugPipeline);
    const uint32_t dynamicOffset = 0;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugLayout, 0, 1, &cameraSet, 1, &dynamicOffset);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugLayout, 1, 1, &m_sets[imageIndex][1], 0,
                            nullptr);
    float spacing = 0.0f;
    for (int a = 0; a < 3; ++a) {
        const float sizes[3] = {m_bakeGrid.size.x, m_bakeGrid.size.y, m_bakeGrid.size.z};
        if (m_bakeGrid.probeCounts[a] > 1) {
            const float s = sizes[a] / static_cast<float>(m_bakeGrid.probeCounts[a] - 1);
            spacing = spacing == 0.0f ? s : std::min(spacing, s);
        }
    }
    const float radius = spacing > 0.0f ? 0.12f * spacing : 0.25f;
    vkCmdPushConstants(cmd, m_debugLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(radius), &radius);
    vkCmdDraw(cmd, kDebugSphereVertices, static_cast<uint32_t>(m_probeCount), 0, 0);
}

} // namespace Hephaestus
