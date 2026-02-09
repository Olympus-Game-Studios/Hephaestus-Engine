/**
 * @file EngineFrameRenderer.cpp
 * @brief Implementation of the shared frame rendering system.
 */

#include "EngineFrameRenderer.h"
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "VulkanPipeline.h"
#include "../Assets/MeshAsset.h"
#include "../Scene/Scene.h"
#include "../Scene/Components.h"

#include <iostream>
#include <cstring>
#include <cmath>
#include <fstream>

namespace Hephaestus {

// ============================================================================
// UBO Structures (must match main.cpp and shader layouts)
// ============================================================================

struct alignas(16) UniformBufferObject {
    Mat4 view;
    Mat4 proj;
    Vec3 viewPos;
    float padding1;
};

struct alignas(16) MaterialUBO {
    Vec3 albedo;
    float albedoPadding;
    float metallic;
    float metallicPadding0;
    float metallicPadding1;
    float metallicPadding2;
    Vec3 emissive;
    float emissivePadding;
    float roughness;
    float ao;
    float emissiveStrength;
    uint32_t useAlbedoTexture;
    uint32_t albedoTextureIndex;
    uint32_t useNormalTexture;
    uint32_t normalTextureIndex;
    uint32_t useRoughnessTexture;
    uint32_t roughnessTextureIndex;
    uint32_t useAoTexture;
    uint32_t aoTextureIndex;
    uint32_t useWorldAlignedTexture;
    float worldTextureScale;
    uint32_t paddingTail[7];
};

struct LightingUBO {
    Mat4 dirLightViewProj;
    Vec4 dirShadowParams;
    Vec4 dirLightDirIntensity;
    Vec4 dirLightColor;
    Vec4 ambientColorIntensity;
    
    struct { float x, y, z, range; } pointLightPositions[16];
    struct { float r, g, b, intensity; } pointLightColors[16];
    struct { float constant, linear, quadratic, unused; } pointLightAttenuation[16];
    struct { float x, y, z, range; } spotLightPositions[16];
    struct { float x, y, z, innerAngle; } spotLightDirections[16];
    struct { float r, g, b, intensity; } spotLightColors[16];
    struct { float constant, linear, quadratic, outerAngle; } spotLightAttenuation[16];
    
    int pointLightCount;
    int spotLightCount;
    float padding5;
    float padding6;
};

// ============================================================================
// RenderCamera Implementation
// ============================================================================

void RenderCamera::GetForwardDirection(float& outX, float& outY, float& outZ) const 
{
    constexpr float DEG_TO_RAD = 3.1415926535f / 180.0f;
    float yawR = yawDeg * DEG_TO_RAD;
    float pitchR = pitchDeg * DEG_TO_RAD;
    outX = std::cos(yawR) * std::cos(pitchR);
    outY = std::sin(pitchR);
    outZ = std::sin(yawR) * std::cos(pitchR);
}

// ============================================================================
// EngineFrameRenderer Implementation
// ============================================================================

EngineFrameRenderer::EngineFrameRenderer() = default;

EngineFrameRenderer::~EngineFrameRenderer()
{
    Shutdown();
}

bool EngineFrameRenderer::Init(VulkanDevice* device, VulkanSwapchain* swapchain)
{
    if (m_initialized) {
        std::cerr << "[EngineFrameRenderer] Already initialized\n";
        return false;
    }
    
    if (!device || !swapchain) {
        std::cerr << "[EngineFrameRenderer] Invalid device or swapchain\n";
        return false;
    }
    
    m_device = device;
    m_swapchain = swapchain;
    
    // Create render resources
    if (!createPipeline()) {
        std::cerr << "[EngineFrameRenderer] Failed to create pipeline\n";
        return false;
    }
    
    if (!createSyncObjects()) {
        std::cerr << "[EngineFrameRenderer] Failed to create sync objects\n";
        return false;
    }
    
    if (!createCommandBuffers()) {
        std::cerr << "[EngineFrameRenderer] Failed to create command buffers\n";
        return false;
    }
    
    if (!createUniformBuffers()) {
        std::cerr << "[EngineFrameRenderer] Failed to create uniform buffers\n";
        return false;
    }
    
    // Create dummy texture for unbound texture slots (1x1 white pixel)
    if (!m_dummyTexture.CreateSolidColor(m_device->allocator, m_device->device,
                                          m_device->physicalDevice, m_device->commandPool,
                                          m_device->graphicsQueue, 0xFFFFFFFF)) {
        std::cerr << "[EngineFrameRenderer] Failed to create dummy texture\n";
        return false;
    }
    
    // Create shadow map resources (before descriptor sets so binding 4 gets the real shadow map)
    if (!createShadowResources()) {
        std::cerr << "[EngineFrameRenderer] Warning: Failed to create shadow resources (shadows will be disabled)\n";
        // Clean up any partially-created shadow resources so IsValid() returns false
        // and the shadow pass is correctly skipped at runtime.
        if (m_directionalShadow.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device->device, m_directionalShadow.pipeline, nullptr);
            m_directionalShadow.pipeline = VK_NULL_HANDLE;
        }
        if (m_directionalShadow.pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device->device, m_directionalShadow.pipelineLayout, nullptr);
            m_directionalShadow.pipelineLayout = VK_NULL_HANDLE;
        }
        if (m_directionalShadow.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device->device, m_directionalShadow.framebuffer, nullptr);
            m_directionalShadow.framebuffer = VK_NULL_HANDLE;
        }
        if (m_directionalShadow.renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device->device, m_directionalShadow.renderPass, nullptr);
            m_directionalShadow.renderPass = VK_NULL_HANDLE;
        }
        if (m_directionalShadow.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_device->device, m_directionalShadow.sampler, nullptr);
            m_directionalShadow.sampler = VK_NULL_HANDLE;
        }
        if (m_directionalShadow.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device->device, m_directionalShadow.imageView, nullptr);
            m_directionalShadow.imageView = VK_NULL_HANDLE;
        }
        if (m_directionalShadow.image != VK_NULL_HANDLE) {
            vmaDestroyImage(m_device->allocator, m_directionalShadow.image, m_directionalShadow.allocation);
            m_directionalShadow.image = VK_NULL_HANDLE;
            m_directionalShadow.allocation = VK_NULL_HANDLE;
        }
    }
    
    if (!createDescriptorSets()) {
        std::cerr << "[EngineFrameRenderer] Failed to create descriptor sets\n";
        return false;
    }
    
    // Create editor grid resources (optional, failure doesn't prevent init)
    if (!createGridResources()) {
        std::cerr << "[EngineFrameRenderer] Warning: Failed to create grid resources (grid will be disabled)\n";
    }
    
    // Create outline resources (optional, failure doesn't prevent init)
    if (!createOutlineResources()) {
        std::cerr << "[EngineFrameRenderer] Warning: Failed to create outline resources (outlines will be disabled)\n";
    }
    
    // Create ID picker resources (optional, failure doesn't prevent init)
    if (!createIdPickerResources()) {
        std::cerr << "[EngineFrameRenderer] Warning: Failed to create ID picker resources (picking will be disabled)\n";
    }
    
#ifndef HEPHAESTUS_GAME_RUNTIME
    // Create gizmo resources (optional, failure doesn't prevent init)
    if (!createGizmoResources()) {
        std::cerr << "[EngineFrameRenderer] Warning: Failed to create gizmo resources (gizmo will be disabled)\n";
    }
#endif
    
    m_initialized = true;
    std::cout << "[EngineFrameRenderer] Initialized successfully\n";
    return true;
}

void EngineFrameRenderer::OnResize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return;
    
    m_swapchainDirty = true;
    m_pendingWidth = width;
    m_pendingHeight = height;
}

bool EngineFrameRenderer::RenderFrame(Scene* scene, const RenderCamera& camera, 
                                       const FrameRenderConfig& config, float deltaTime)
{
    if (!m_initialized) return false;
    
    // Handle pending resize
    if (m_swapchainDirty) {
        vkDeviceWaitIdle(m_device->device);
        // Swapchain recreation is handled externally by QtEditorEngine
        m_swapchainDirty = false;
    }
    
    // Wait for previous frame
    vkWaitForFences(m_device->device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    
    // Process pending pick request (read from previous frame's copy)
    if (m_idPicker.pickPending && m_idPicker.mappedStagingBuffer) {
        const uint8_t* pixelData = static_cast<const uint8_t*>(m_idPicker.mappedStagingBuffer);
        uint32_t entityId = pixelData[0] | (pixelData[1] << 8) | 
                            (pixelData[2] << 16) | (pixelData[3] << 24);
        m_idPicker.lastPickedEntity = entityId;
        m_idPicker.pickPending = false;
    }
    
    // Acquire next image
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        m_device->device,
        m_swapchain->swapchain,
        UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE,
        &imageIndex
    );
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_swapchainDirty = true;
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "[EngineFrameRenderer] Failed to acquire swapchain image\n";
        return false;
    }
    
    vkResetFences(m_device->device, 1, &m_inFlightFences[m_currentFrame]);
    
    // Update uniform buffers
    updateCameraUBO(imageIndex, camera);
    if (scene) {
        updateLightingUBO(imageIndex, scene, camera);
    }
    
    // Record command buffer
    VkCommandBuffer cmd = m_commandBuffers[imageIndex];
    vkResetCommandBuffer(cmd, 0);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    // Record ID pass for entity picking (editor only)
    if (config.isEditorMode && m_idPicker.initialized) {
        recordIdPass(cmd, imageIndex, scene);
        
        // If there's a pending pick request, copy the pixel to staging buffer
        if (m_idPicker.pickPending && m_idPicker.pickX < m_idPicker.width && 
            m_idPicker.pickY < m_idPicker.height) {
            
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {static_cast<int32_t>(m_idPicker.pickX), 
                                  static_cast<int32_t>(m_idPicker.pickY), 0};
            region.imageExtent = {1, 1, 1};
            
            vkCmdCopyImageToBuffer(cmd, m_idPicker.colorImage, 
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   m_idPicker.stagingBuffer, 1, &region);
        }
    }
    
    // Record shadow pass (before main pass)
    if (config.renderShadows && m_directionalShadow.IsValid()) {
        recordShadowPass(cmd, scene, camera);
    }
    
    // Record main render pass
    recordMainPass(cmd, imageIndex, scene, config);
    
    vkEndCommandBuffer(cmd);
    
    // Submit
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_imageAvailableSemaphores[m_currentFrame];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderFinishedSemaphores[m_currentFrame];
    
    if (vkQueueSubmit(m_device->graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
        std::cerr << "[EngineFrameRenderer] Failed to submit draw command buffer\n";
        return false;
    }
    
    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrame];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain->swapchain;
    presentInfo.pImageIndices = &imageIndex;
    
    result = vkQueuePresentKHR(m_device->presentQueue, &presentInfo);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_swapchainDirty = true;
    }
    
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    return true;
}

void EngineFrameRenderer::Shutdown()
{
    if (!m_initialized) return;
    
    if (m_device && m_device->device) {
        vkDeviceWaitIdle(m_device->device);
    }
    
    destroyAllResources();
    m_initialized = false;
    std::cout << "[EngineFrameRenderer] Shutdown complete\n";
}

// ============================================================================
// Mesh Library Management
// ============================================================================

size_t EngineFrameRenderer::AddMesh(MeshGPU mesh)
{
    m_meshLibrary.push_back(std::move(mesh));
    size_t handle = m_meshLibrary.size() - 1;
    
    if (!m_meshLibrary[handle].sourceKey.empty()) {
        m_meshLibraryLookup[m_meshLibrary[handle].sourceKey] = handle;
    }
    
    return handle;
}

MeshGPU* EngineFrameRenderer::GetMesh(size_t handle)
{
    if (handle >= m_meshLibrary.size()) return nullptr;
    return &m_meshLibrary[handle];
}

size_t EngineFrameRenderer::GetMeshHandle(const std::string& sourceKey) const
{
    auto it = m_meshLibraryLookup.find(sourceKey);
    if (it != m_meshLibraryLookup.end()) {
        return it->second;
    }
    return SIZE_MAX;
}

// ============================================================================
// Private: Resource Creation
// ============================================================================

bool EngineFrameRenderer::createPipeline()
{
    try {
        m_pipeline = std::make_unique<VulkanPipeline>();
        m_pipeline->Init(m_device->device, m_swapchain->renderPass, m_swapchain->extent);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[EngineFrameRenderer] Pipeline creation failed: " << e.what() << "\n";
        return false;
    }
}

bool EngineFrameRenderer::createSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imagesInFlight.resize(m_swapchain->images.size(), VK_NULL_HANDLE);
    
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(m_device->device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device->device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device->device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

bool EngineFrameRenderer::createCommandBuffers()
{
    m_commandBuffers.resize(m_swapchain->images.size());
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_device->commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());
    
    return vkAllocateCommandBuffers(m_device->device, &allocInfo, m_commandBuffers.data()) == VK_SUCCESS;
}

bool EngineFrameRenderer::createUniformBuffers()
{
    size_t imageCount = m_swapchain->images.size();
    
    // Camera UBO
    m_uniformBuffers.resize(imageCount);
    for (auto& buffer : m_uniformBuffers) {
        buffer.Create(m_device->allocator, sizeof(UniformBufferObject),
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    
    // Lighting UBO
    m_lightingBuffers.resize(imageCount);
    for (auto& buffer : m_lightingBuffers) {
        buffer.Create(m_device->allocator, sizeof(LightingUBO),
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    
    // Material dynamic UBO
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_device->physicalDevice, &props);
    VkDeviceSize minUboAlignment = props.limits.minUniformBufferOffsetAlignment;
    VkDeviceSize materialUboSize = sizeof(MaterialUBO);
    VkDeviceSize materialStride = (minUboAlignment > 0) 
        ? ((materialUboSize + minUboAlignment - 1) & ~(minUboAlignment - 1))
        : materialUboSize;
    
    constexpr uint32_t maxMaterialsPerFrame = 1024;
    VkDeviceSize totalMaterialSize = materialStride * maxMaterialsPerFrame;
    
    m_materialBuffers.resize(imageCount);
    for (auto& buffer : m_materialBuffers) {
        buffer.Create(m_device->allocator, totalMaterialSize,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU);
        
        // Initialize with a sensible default material (white, rough, non-metallic)
        MaterialUBO defaultMaterial{};
        defaultMaterial.albedo = Vec3(1.0f, 1.0f, 1.0f);  // White
        defaultMaterial.albedoPadding = 0.0f;
        defaultMaterial.metallic = 0.0f;
        defaultMaterial.metallicPadding0 = 0.0f;
        defaultMaterial.metallicPadding1 = 0.0f;
        defaultMaterial.metallicPadding2 = 0.0f;
        defaultMaterial.emissive = Vec3(0.0f, 0.0f, 0.0f);
        defaultMaterial.emissivePadding = 0.0f;
        defaultMaterial.roughness = 0.5f;
        defaultMaterial.ao = 1.0f;
        defaultMaterial.emissiveStrength = 0.0f;
        defaultMaterial.useAlbedoTexture = 0;
        defaultMaterial.albedoTextureIndex = 0;
        defaultMaterial.useNormalTexture = 0;
        defaultMaterial.normalTextureIndex = 0;
        defaultMaterial.useRoughnessTexture = 0;
        defaultMaterial.roughnessTextureIndex = 0;
        defaultMaterial.useAoTexture = 0;
        defaultMaterial.aoTextureIndex = 0;
        defaultMaterial.useWorldAlignedTexture = 0;
        defaultMaterial.worldTextureScale = 1.0f;
        
        void* mapped = nullptr;
        if (vmaMapMemory(m_device->allocator, buffer.allocation, &mapped) == VK_SUCCESS) {
            // Fill first material slot with defaults
            std::memcpy(mapped, &defaultMaterial, sizeof(defaultMaterial));
            vmaFlushAllocation(m_device->allocator, buffer.allocation, 0, sizeof(defaultMaterial));
            vmaUnmapMemory(m_device->allocator, buffer.allocation);
        }
    }
    
    return true;
}

bool EngineFrameRenderer::createDescriptorSets()
{
    size_t imageCount = m_swapchain->images.size();
    
    // Create descriptor pool
    constexpr uint32_t kTextureArraySize = 128;
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(imageCount) },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, static_cast<uint32_t>(imageCount) },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(imageCount) },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(imageCount) * kTextureArraySize },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(imageCount) }
    };
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 5;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = static_cast<uint32_t>(imageCount);
    
    if (vkCreateDescriptorPool(m_device->device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        return false;
    }
    
    // Allocate descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(imageCount, m_pipeline->descriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
    allocInfo.pSetLayouts = layouts.data();
    
    m_descriptorSets.resize(imageCount);
    if (vkAllocateDescriptorSets(m_device->device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        return false;
    }
    
    // Create a dummy texture for the texture array (1x1 white pixel)
    // This is needed because descriptor sets must be valid even if not used
    
    // Update descriptor sets with buffer bindings AND texture bindings
    for (size_t i = 0; i < imageCount; ++i) {
        VkDescriptorBufferInfo cameraBufferInfo{};
        cameraBufferInfo.buffer = m_uniformBuffers[i].buffer;
        cameraBufferInfo.offset = 0;
        cameraBufferInfo.range = sizeof(UniformBufferObject);
        
        VkDescriptorBufferInfo materialBufferInfo{};
        materialBufferInfo.buffer = m_materialBuffers[i].buffer;
        materialBufferInfo.offset = 0;
        materialBufferInfo.range = sizeof(MaterialUBO);
        
        VkDescriptorBufferInfo lightingBufferInfo{};
        lightingBufferInfo.buffer = m_lightingBuffers[i].buffer;
        lightingBufferInfo.offset = 0;
        lightingBufferInfo.range = sizeof(LightingUBO);
        
        // Create image info array for texture array binding (128 slots all pointing to dummy)
        std::vector<VkDescriptorImageInfo> textureArrayInfos(kTextureArraySize);
        for (uint32_t t = 0; t < kTextureArraySize; ++t) {
            textureArrayInfos[t].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            textureArrayInfos[t].imageView = m_dummyTexture.imageView;
            textureArrayInfos[t].sampler = m_dummyTexture.sampler;
        }
        
        // Shadow map - use actual shadow map if valid, otherwise dummy texture
        VkDescriptorImageInfo shadowMapInfo{};
        shadowMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (m_directionalShadow.IsValid()) {
            shadowMapInfo.imageView = m_directionalShadow.imageView;
            shadowMapInfo.sampler = m_directionalShadow.sampler;
        } else {
            shadowMapInfo.imageView = m_dummyTexture.imageView;
            shadowMapInfo.sampler = m_dummyTexture.sampler;
        }
        
        VkWriteDescriptorSet descriptorWrites[5] = {};
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &cameraBufferInfo;
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = m_descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &materialBufferInfo;
        
        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = m_descriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pBufferInfo = &lightingBufferInfo;
        
        // Binding 3: Texture array
        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = m_descriptorSets[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[3].descriptorCount = kTextureArraySize;
        descriptorWrites[3].pImageInfo = textureArrayInfos.data();
        
        // Binding 4: Shadow map
        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = m_descriptorSets[i];
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].dstArrayElement = 0;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pImageInfo = &shadowMapInfo;
        
        vkUpdateDescriptorSets(m_device->device, 5, descriptorWrites, 0, nullptr);
    }
    
    return true;
}

bool EngineFrameRenderer::createShadowResources()
{
    if (!m_device || !m_swapchain || !m_pipeline) return false;
    
    VkDevice device = m_device->device;
    VmaAllocator allocator = m_device->allocator;
    uint32_t resolution = m_directionalShadow.resolution;
    
    // Create depth image for shadow map
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.extent = {resolution, resolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_directionalShadow.image,
                       &m_directionalShadow.allocation, nullptr) != VK_SUCCESS) {
        std::cerr << "[ShadowMap] Failed to create depth image\n";
        return false;
    }
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_directionalShadow.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &m_directionalShadow.imageView) != VK_SUCCESS) {
        std::cerr << "[ShadowMap] Failed to create image view\n";
        return false;
    }
    
    // Create sampler with comparison function for PCF
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // Far plane = 1.0
    samplerInfo.compareEnable = VK_FALSE; // We'll sample and compare in shader
    samplerInfo.maxAnisotropy = 1.0f;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_directionalShadow.sampler) != VK_SUCCESS) {
        std::cerr << "[ShadowMap] Failed to create sampler\n";
        return false;
    }
    
    // Create render pass for shadow map
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    
    // Two subpass dependencies:
    // [0] Ensure previous frame's shader reads complete before depth writes begin
    // [1] Ensure depth writes complete before next render pass samples the shadow map
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;
    
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_directionalShadow.renderPass) != VK_SUCCESS) {
        std::cerr << "[ShadowMap] Failed to create render pass\n";
        return false;
    }
    
    // Create framebuffer
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_directionalShadow.renderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &m_directionalShadow.imageView;
    fbInfo.width = resolution;
    fbInfo.height = resolution;
    fbInfo.layers = 1;
    
    if (vkCreateFramebuffer(device, &fbInfo, nullptr, &m_directionalShadow.framebuffer) != VK_SUCCESS) {
        std::cerr << "[ShadowMap] Failed to create framebuffer\n";
        return false;
    }
    
    // Create pipeline layout with push constants
    struct ShadowPushConstants {
        Mat4 lightViewProj;
        Mat4 model;
    };
    
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ShadowPushConstants);
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_directionalShadow.pipelineLayout) != VK_SUCCESS) {
        std::cerr << "[ShadowMap] Failed to create pipeline layout\n";
        return false;
    }
    
    // Load shadow shaders - try multiple paths for compatibility
    auto loadShader = [device](const std::string& name) -> VkShaderModule {
        const std::string candidates[] = {
            "shaders/" + name,
            "../shaders/" + name,
            "build/Release/shaders/" + name,
            "../build/Release/shaders/" + name
        };
        for (const auto& path : candidates) {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open()) continue;
            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> code(fileSize);
            file.seekg(0);
            file.read(code.data(), fileSize);
            file.close();
            
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
            
            VkShaderModule shaderModule;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
            return shaderModule;
        }
        return VK_NULL_HANDLE;
    };
    
    VkShaderModule vertShader = loadShader("shadowmap.vert.spv");
    VkShaderModule fragShader = loadShader("shadowmap.frag.spv");
    
    if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
        std::cerr << "[ShadowMap] Failed to load shadow shaders\n";
        if (vertShader) vkDestroyShaderModule(device, vertShader, nullptr);
        if (fragShader) vkDestroyShaderModule(device, fragShader, nullptr);
        return false;
    }
    
    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShader;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShader;
    shaderStages[1].pName = "main";
    
    // Vertex input - only position needed
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 11; // Same vertex format as main mesh
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription.offset = 0;
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(resolution);
    viewport.height = static_cast<float>(resolution);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {resolution, resolution};
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.5f;
    rasterizer.depthBiasSlopeFactor = 1.5f;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    // No color blending - depth only
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;
    
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = m_directionalShadow.pipelineLayout;
    pipelineInfo.renderPass = m_directionalShadow.renderPass;
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_directionalShadow.pipeline) != VK_SUCCESS) {
        std::cerr << "[ShadowMap] Failed to create graphics pipeline\n";
        vkDestroyShaderModule(device, vertShader, nullptr);
        vkDestroyShaderModule(device, fragShader, nullptr);
        return false;
    }
    
    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
    
    std::cout << "[ShadowMap] Created directional shadow map (" << resolution << "x" << resolution << ")\n";
    return true;
}

bool EngineFrameRenderer::createBillboardResources()
{
    // TODO: Implement billboard resources creation
    return true;
}

// ============================================================================
// Private: Per-Frame Updates
// ============================================================================

void EngineFrameRenderer::updateCameraUBO(uint32_t imageIndex, const RenderCamera& camera)
{
    constexpr float DEG_TO_RAD = 3.1415926535f / 180.0f;
    
    float yawR = camera.yawDeg * DEG_TO_RAD;
    float pitchR = camera.pitchDeg * DEG_TO_RAD;
    
    Vec3 position{camera.positionX, camera.positionY, camera.positionZ};
    Vec3 front{
        std::cos(yawR) * std::cos(pitchR),
        std::sin(pitchR),
        std::sin(yawR) * std::cos(pitchR)
    };
    front = vnorm(front);
    Vec3 up{0.0f, 1.0f, 0.0f};
    
    UniformBufferObject ubo{};
    ubo.view = Mat4::LookAt(position, vadd(position, front), up);
    
    uint32_t safeWidth = std::max(m_swapchain->extent.width, 1u);
    uint32_t safeHeight = std::max(m_swapchain->extent.height, 1u);
    float aspect = static_cast<float>(safeWidth) / static_cast<float>(safeHeight);
    
    ubo.proj = Mat4::Perspective(60.0f * DEG_TO_RAD, aspect, camera.nearPlane, camera.farPlane);
    ubo.viewPos = position;
    ubo.padding1 = 0.0f;
    
    void* mapped = nullptr;
    vmaMapMemory(m_device->allocator, m_uniformBuffers[imageIndex].allocation, &mapped);
    std::memcpy(mapped, &ubo, sizeof(ubo));
    vmaUnmapMemory(m_device->allocator, m_uniformBuffers[imageIndex].allocation);
}

void EngineFrameRenderer::updateLightingUBO(uint32_t imageIndex, Scene* scene, const RenderCamera& camera)
{
    LightingUBO lightUBO{};
    
    // Default ambient lighting
    lightUBO.ambientColorIntensity = Vec4(0.1f, 0.1f, 0.15f, 0.3f);
    
    // Default directional light
    lightUBO.dirLightDirIntensity = Vec4(0.0f, -1.0f, 0.0f, 0.5f);
    lightUBO.dirLightColor = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    
    // Default shadow parameters - disabled
    lightUBO.dirShadowParams = Vec4(0.005f, 0.5f, 1.0f / m_directionalShadow.resolution, 0.0f);
    lightUBO.dirLightViewProj = Mat4::Identity();
    
    lightUBO.pointLightCount = 0;
    lightUBO.spotLightCount = 0;
    
    // If scene is provided, gather lights from it
    if (scene) {
        auto lightView = scene->GetEntitiesWith<LightComponent>();
        int pointLightIndex = 0;
        int spotLightIndex = 0;
        
        for (auto entityHandle : lightView) {
            Entity entity(entityHandle, scene);
            auto& light = entity.GetComponent<LightComponent>();
            
            Vec3 lightPos{0.0f, 0.0f, 0.0f};
            Vec3 lightDir{0.0f, -1.0f, 0.0f};
            
            if (entity.HasComponent<TransformComponent>()) {
                auto& transform = entity.GetComponent<TransformComponent>();
                lightPos = transform.position;
                
                // Compute light direction from rotation for directional lights
                if (light.type == LightType::Directional) {
                    Mat4 rotation = Mat4::RotationZ(transform.rotation.z);
                    rotation = Mat4::Multiply(Mat4::RotationY(transform.rotation.y), rotation);
                    rotation = Mat4::Multiply(Mat4::RotationX(transform.rotation.x), rotation);
                    lightDir = Mat4::TransformDirection(rotation, Vec3(0, 0, -1));
                    lightDir = vnorm(lightDir);
                }
            }
            
            if (light.type == LightType::Directional) {
                lightUBO.dirLightDirIntensity = Vec4(lightDir.x, lightDir.y, 
                                                     lightDir.z, light.intensity);
                lightUBO.dirLightColor = Vec4(light.color.x, light.color.y, light.color.z, 1.0f);
                
                // Debug: check why shadows might not be enabled
                static bool shadowConditionDebugPrinted = false;
                if (!shadowConditionDebugPrinted) {
                    std::cout << "[ShadowMap] Directional light found: IsValid=" << m_directionalShadow.IsValid()
                              << " castShadows=" << light.castShadows << "\n";
                    shadowConditionDebugPrinted = true;
                }
                
                // Set up shadow matrix - center shadow frustum on camera position
                if (m_directionalShadow.IsValid() && light.castShadows) {
                    float shadowExtent = light.shadowExtent;    // Half-size of shadow ortho box
                    float shadowDistance = light.shadowDistance; // Distance from camera center along light direction
                    
                    // Debug: log that shadows are being enabled
                    static bool shadowDebugPrinted = false;
                    if (!shadowDebugPrinted) {
                        std::cout << "[ShadowMap] Enabling shadows: extent=" << shadowExtent 
                                  << " distance=" << shadowDistance << " bias=" << light.shadowBias 
                                  << " strength=" << light.shadowStrength << "\n";
                        shadowDebugPrinted = true;
                    }
                    
                    // Center shadow frustum on camera position for stable shadows
                    Vec3 cameraPos = Vec3(camera.positionX, camera.positionY, camera.positionZ);
                    Vec3 towardLight = vscale(lightDir, -1.0f);
                    Vec3 lightPosForShadow = vadd(cameraPos, vscale(towardLight, shadowDistance));
                    
                    // Handle degenerate up vector when light points straight up/down
                    Vec3 up = Vec3(0.0f, 1.0f, 0.0f);
                    if (std::abs(vdot(lightDir, up)) > 0.99f) {
                        up = Vec3(1.0f, 0.0f, 0.0f);
                    }
                    
                    float shadowFar = shadowDistance * 3.0f;
                    Mat4 lightView = Mat4::LookAt(lightPosForShadow, cameraPos, up);
                    Mat4 lightProj = Mat4::OrthographicShadow(-shadowExtent, shadowExtent, -shadowExtent, shadowExtent, 0.1f, shadowFar);
                    lightUBO.dirLightViewProj = Mat4::Multiply(lightProj, lightView);
                    
                    // Enable shadows - bias, strength, texel size, enabled
                    lightUBO.dirShadowParams = Vec4(light.shadowBias, light.shadowStrength, 
                                                    1.0f / m_directionalShadow.resolution, 1.0f);
                }
            } else if (light.type == LightType::Point && pointLightIndex < 16) {
                lightUBO.pointLightPositions[pointLightIndex] = {lightPos.x, lightPos.y, lightPos.z, light.range};
                lightUBO.pointLightColors[pointLightIndex] = {light.color.x, light.color.y, light.color.z, light.intensity};
                lightUBO.pointLightAttenuation[pointLightIndex] = {1.0f, 0.09f, 0.032f, 0.0f};
                ++pointLightIndex;
            } else if (light.type == LightType::Spot && spotLightIndex < 16) {
                lightUBO.spotLightPositions[spotLightIndex] = {lightPos.x, lightPos.y, lightPos.z, light.range};
                lightUBO.spotLightDirections[spotLightIndex] = {light.direction.x, light.direction.y, 
                                                                 light.direction.z, light.innerConeAngle};
                lightUBO.spotLightColors[spotLightIndex] = {light.color.x, light.color.y, light.color.z, light.intensity};
                lightUBO.spotLightAttenuation[spotLightIndex] = {1.0f, 0.09f, 0.032f, light.outerConeAngle};
                ++spotLightIndex;
            }
        }
        
        lightUBO.pointLightCount = pointLightIndex;
        lightUBO.spotLightCount = spotLightIndex;
    }
    
    void* mapped = nullptr;
    vmaMapMemory(m_device->allocator, m_lightingBuffers[imageIndex].allocation, &mapped);
    std::memcpy(mapped, &lightUBO, sizeof(lightUBO));
    vmaFlushAllocation(m_device->allocator, m_lightingBuffers[imageIndex].allocation, 0, VK_WHOLE_SIZE);
    vmaUnmapMemory(m_device->allocator, m_lightingBuffers[imageIndex].allocation);
}

void EngineFrameRenderer::recordShadowPass(VkCommandBuffer cmd, Scene* scene, const RenderCamera& camera)
{
    if (!m_directionalShadow.IsValid() || !scene) {
        static bool skipDebugPrinted = false;
        if (!skipDebugPrinted) {
            std::cout << "[ShadowPass] Skipping: IsValid=" << m_directionalShadow.IsValid()
                      << " scene=" << (scene != nullptr) << "\n";
            skipDebugPrinted = true;
        }
        return;
    }
    
    // Find the first directional light with castShadows enabled
    // TODO: Support multiple shadow-casting lights via shadow atlas
    auto& registry = scene->GetRegistry();
    Mat4 lightViewProj = Mat4::Identity();
    bool hasDirectionalLight = false;
    
    auto lightView = registry.view<TransformComponent, LightComponent>();
    for (auto entity : lightView) {
        auto& light = lightView.get<LightComponent>(entity);
        if (light.type == LightType::Directional && light.castShadows) {
            auto& transform = lightView.get<TransformComponent>(entity);
            
            // Compute light direction from rotation
            Mat4 rotation = Mat4::RotationZ(transform.rotation.z);
            rotation = Mat4::Multiply(Mat4::RotationY(transform.rotation.y), rotation);
            rotation = Mat4::Multiply(Mat4::RotationX(transform.rotation.x), rotation);
            Vec3 lightDir = Mat4::TransformDirection(rotation, Vec3(0, 0, -1));
            lightDir = vnorm(lightDir);
            
            // Center shadow frustum on camera position for stable shadows
            float shadowExtent = light.shadowExtent;
            float shadowDistance = light.shadowDistance;
            
            Vec3 cameraPos = Vec3(camera.positionX, camera.positionY, camera.positionZ);
            Vec3 towardLight = vscale(lightDir, -1.0f);
            Vec3 lightPos = vadd(cameraPos, vscale(towardLight, shadowDistance));
            
            // Handle degenerate up vector when light points straight up/down
            Vec3 up = Vec3(0.0f, 1.0f, 0.0f);
            if (std::abs(vdot(lightDir, up)) > 0.99f) {
                up = Vec3(1.0f, 0.0f, 0.0f);
            }
            
            float shadowFar = shadowDistance * 3.0f;
            Mat4 lightViewMat = Mat4::LookAt(lightPos, cameraPos, up);
            Mat4 lightProj = Mat4::OrthographicShadow(-shadowExtent, shadowExtent, -shadowExtent, shadowExtent, 0.1f, shadowFar);
            lightViewProj = Mat4::Multiply(lightProj, lightViewMat);
            hasDirectionalLight = true;
            break; // MVP: only first shadow-casting directional light
        }
    }
    
    if (!hasDirectionalLight) return;
    
    // Begin shadow render pass
    VkClearValue clearValue{};
    clearValue.depthStencil = {1.0f, 0};
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_directionalShadow.renderPass;
    renderPassInfo.framebuffer = m_directionalShadow.framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {m_directionalShadow.resolution, m_directionalShadow.resolution};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_directionalShadow.pipeline);
    
    // Render all mesh entities
    auto meshView = registry.view<TransformComponent, MeshRendererComponent>();
    VkBuffer currentVertexBuffer = VK_NULL_HANDLE;
    VkBuffer currentIndexBuffer = VK_NULL_HANDLE;
    int shadowMeshCount = 0;
    
    struct ShadowPushConstants {
        Mat4 lightViewProj;
        Mat4 model;
    };
    
    for (auto entity : meshView) {
        auto& meshComp = meshView.get<MeshRendererComponent>(entity);
        if (!meshComp.HasValidMesh() || !meshComp.visible) continue;
        
        // Use meshHandle directly like the main pass (string lookup fails due to path mismatch)
        if (meshComp.meshHandle >= m_meshLibrary.size()) continue;
        
        const MeshGPU& mesh = m_meshLibrary[meshComp.meshHandle];
        
        // Bind buffers if changed
        if (mesh.vertexBuffer.buffer != currentVertexBuffer) {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer.buffer, &offset);
            currentVertexBuffer = mesh.vertexBuffer.buffer;
        }
        if (mesh.indexBuffer.buffer != currentIndexBuffer) {
            vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            currentIndexBuffer = mesh.indexBuffer.buffer;
        }
        
        // Push constants
        auto& transform = meshView.get<TransformComponent>(entity);
        ShadowPushConstants pc;
        pc.lightViewProj = lightViewProj;
        pc.model = transform.GetLocalMatrix();
        vkCmdPushConstants(cmd, m_directionalShadow.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                          0, sizeof(pc), &pc);
        
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        shadowMeshCount++;
    }
    
    static bool shadowPassDebugPrinted = false;
    if (!shadowPassDebugPrinted && shadowMeshCount > 0) {
        std::cout << "[ShadowPass] Rendered " << shadowMeshCount << " meshes to shadow map\n";
        shadowPassDebugPrinted = true;
    }
    
    vkCmdEndRenderPass(cmd);
}

void EngineFrameRenderer::recordMainPass(VkCommandBuffer cmd, uint32_t imageIndex, 
                                          Scene* scene, const FrameRenderConfig& config)
{
    VkClearValue clearValues[2];
    clearValues[0].color = {{config.clearColorR, config.clearColorG, config.clearColorB, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_swapchain->renderPass;
    renderPassInfo.framebuffer = m_swapchain->framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain->extent;
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;
    
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // Set dynamic viewport/scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_swapchain->extent.width);
    viewport.height = static_cast<float>(m_swapchain->extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, m_swapchain->extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
    // Bind pipeline
    VkPipeline pipeline = m_pipeline->GetPipeline(false);
    if (pipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    }
    
    // Render scene meshes if we have a scene
    static std::string lastSceneName;
    bool debugPrint = false;
    if (scene) {
        std::string sceneName = scene->GetName();
        if (sceneName != lastSceneName) {
            debugPrint = true;
            lastSceneName = sceneName;
        }
    }
    
    if (scene && !m_meshLibrary.empty()) {
        auto renderView = scene->GetEntitiesWith<TransformComponent, MeshRendererComponent>();
        
        int drawnCount = 0;
        int skippedCount = 0;
        
        VkBuffer currentVertexBuffer = VK_NULL_HANDLE;
        VkBuffer currentIndexBuffer = VK_NULL_HANDLE;
        
        for (auto entityHandle : renderView) {
            Entity entity(entityHandle, scene);
            auto& transform = entity.GetComponent<TransformComponent>();
            auto& renderer = entity.GetComponent<MeshRendererComponent>();
            
            if (!renderer.visible) {
                skippedCount++;
                continue;
            }
            if (!renderer.HasValidMesh() || renderer.meshHandle >= m_meshLibrary.size()) {
                skippedCount++;
                continue;
            }
            
            MeshGPU& mesh = m_meshLibrary[renderer.meshHandle];
            if (mesh.indexCount == 0 || mesh.vertexBuffer.buffer == VK_NULL_HANDLE ||
                mesh.indexBuffer.buffer == VK_NULL_HANDLE) {
                if (debugPrint) {
                    std::cout << "[EngineFrameRenderer] Skipping mesh: indexCount=" << mesh.indexCount 
                              << " vertexBuffer=" << (void*)mesh.vertexBuffer.buffer 
                              << " indexBuffer=" << (void*)mesh.indexBuffer.buffer << "\n";
                }
                skippedCount++;
                continue;
            }
            
            // Bind vertex buffer if different
            if (mesh.vertexBuffer.buffer != currentVertexBuffer) {
                VkBuffer vb = mesh.vertexBuffer.buffer;
                VkDeviceSize vbOffset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
                currentVertexBuffer = vb;
            }
            
            // Bind index buffer if different
            if (mesh.indexBuffer.buffer != currentIndexBuffer) {
                vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                currentIndexBuffer = mesh.indexBuffer.buffer;
            }
            
            // Push model matrix
            Mat4 modelMatrix = scene->GetWorldTransform(entity);
            vkCmdPushConstants(cmd, m_pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 
                              0, sizeof(Mat4), modelMatrix.m);
            
            // Bind descriptor set with default material offset
            uint32_t dynOffset = 0;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout,
                                    0, 1, &m_descriptorSets[imageIndex], 1, &dynOffset);
            
            // Draw
            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
            drawnCount++;
        }
        
        if (debugPrint) {
            std::cout << "[EngineFrameRenderer] Drew " << drawnCount << " meshes, skipped " << skippedCount << "\n";
        }
    } else if (debugPrint) {
        std::cout << "[EngineFrameRenderer] No scene or empty mesh library: scene=" << (void*)scene 
                  << " meshLibrary.size=" << m_meshLibrary.size() << "\n";
    }
    
    // Render editor grid (after opaque meshes, before transparent)
    if (config.isEditorMode && config.renderGrid) {
        recordGrid(cmd, imageIndex, config);
    }
    
    // Render selection outlines (in editor mode)
    if (config.isEditorMode && config.renderOutlines && config.selectedEntity != UINT32_MAX) {
        recordOutlines(cmd, imageIndex, scene, config);
    }
    
#ifndef HEPHAESTUS_GAME_RUNTIME
    // Render gizmo on selected entity
    if (config.isEditorMode && config.renderGizmo && m_gizmoInitialized && 
        m_gizmoTargetEntity != UINT32_MAX) {
        m_gizmo.Render(cmd, m_descriptorSets[imageIndex]);
    }
#endif

    vkCmdEndRenderPass(cmd);
}

void EngineFrameRenderer::recordGizmo(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene)
{
    // Gizmo is rendered directly in recordMainPass when target entity is set
    // This method is kept for future use if separate pass is needed
}

void EngineFrameRenderer::recordBillboards(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene)
{
    // TODO: Implement billboard rendering
}

void EngineFrameRenderer::recordOutlines(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene,
                                          const FrameRenderConfig& config)
{
    if (!m_outlineRenderer.initialized || !scene) return;
    if (config.selectedEntity == UINT32_MAX) return;
    
    // Find the selected entity's mesh/transform
    auto& registry = scene->GetRegistry();
    entt::entity selectedEnt = static_cast<entt::entity>(config.selectedEntity);
    
    if (!registry.valid(selectedEnt)) return;
    
    // Get transform component
    auto* transformComp = registry.try_get<TransformComponent>(selectedEnt);
    if (!transformComp) return;
    
    // Get mesh renderer component
    auto* meshComp = registry.try_get<MeshRendererComponent>(selectedEnt);
    if (!meshComp || !meshComp->HasValidMesh()) return;
    
    // Find the mesh in our library
    auto it = m_meshLibraryLookup.find(meshComp->meshAssetPath);
    if (it == m_meshLibraryLookup.end()) return;
    
    size_t meshIdx = it->second;
    const MeshGPU& mesh = m_meshLibrary[meshIdx];
    
    Mat4 modelMatrix = transformComp->GetLocalMatrix();
    
    // Draw the outline
    m_outlineRenderer.Draw(cmd, m_descriptorSets[imageIndex], 
                           mesh.vertexBuffer.buffer, mesh.indexBuffer.buffer,
                           mesh.indexCount, modelMatrix);
}

// ============================================================================
// Gizmo Integration (Editor-only)
// ============================================================================

#ifndef HEPHAESTUS_GAME_RUNTIME

bool EngineFrameRenderer::createGizmoResources()
{
    if (!m_device || !m_swapchain || !m_pipeline) return false;
    
    try {
        m_gizmo.Init(m_device->device, m_swapchain->renderPass, m_swapchain->extent,
                     m_pipeline->descriptorSetLayout, m_device->allocator);
        m_gizmoInitialized = true;
        std::cout << "[EngineFrameRenderer] Gizmo initialized\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[EngineFrameRenderer] Failed to initialize gizmo: " << e.what() << "\n";
        return false;
    }
}

void EngineFrameRenderer::cleanupGizmoResources()
{
    if (m_gizmoInitialized && m_device) {
        m_gizmo.Cleanup(m_device->device, m_device->allocator);
        m_gizmoInitialized = false;
    }
}

void EngineFrameRenderer::UpdateGizmoState(uint32_t selectedEntity, const Mat4& transform,
                                            const Mat4& viewMatrix, const Mat4& projMatrix,
                                            const Vec3& cameraPos, int mode, int space)
{
    if (!m_gizmoInitialized) return;
    
    m_gizmoTargetEntity = selectedEntity;
    m_gizmoTargetTransform = transform;
    m_gizmoViewMatrix = viewMatrix;
    m_gizmoProjectionMatrix = projMatrix;
    m_gizmoCameraPos = cameraPos;
    
    if (selectedEntity != UINT32_MAX) {
        VulkanGizmo::Mode gizmoMode = VulkanGizmo::Mode::Translate;
        switch (mode) {
            case 1: gizmoMode = VulkanGizmo::Mode::Translate; break;
            case 2: gizmoMode = VulkanGizmo::Mode::Rotate; break;
            case 3: gizmoMode = VulkanGizmo::Mode::Scale; break;
        }
        
        VulkanGizmo::Space gizmoSpace = (space == 0) ? VulkanGizmo::Space::World : VulkanGizmo::Space::Local;
        
        m_gizmo.UpdateState(transform, viewMatrix, projMatrix, cameraPos, gizmoMode, gizmoSpace);
    }
}

bool EngineFrameRenderer::CheckGizmoHover(float screenX, float screenY, const Mat4& viewMatrix, 
                                          const Mat4& projMatrix, int& outAxis)
{
    if (!m_gizmoInitialized || m_gizmoTargetEntity == UINT32_MAX) {
        outAxis = -1;
        return false;
    }
    
    // Create ray from screen position
    float x = (2.0f * screenX) / m_swapchain->extent.width - 1.0f;
    float y = (2.0f * screenY) / m_swapchain->extent.height - 1.0f;
    
    Mat4 invProj = Mat4::Invert(projMatrix);
    Mat4 invView = Mat4::Invert(viewMatrix);
    
    Vec3 rayNearNDC(x, y, 0.0f);
    Vec3 rayFarNDC(x, y, 1.0f);
    
    Vec3 rayNearView = Mat4::TransformPoint(invProj, rayNearNDC);
    Vec3 rayFarView = Mat4::TransformPoint(invProj, rayFarNDC);
    
    Vec3 rayNearWorld = Mat4::TransformPoint(invView, rayNearView);
    Vec3 rayFarWorld = Mat4::TransformPoint(invView, rayFarView);
    
    Vec3 rayDir = vnorm(vsub(rayFarWorld, rayNearWorld));
    
    Ray ray(rayNearWorld, rayDir);
    outAxis = m_gizmo.CheckHover(ray);
    m_gizmo.SetHoveredAxis(outAxis);
    
    return outAxis >= 0;
}

#endif // HEPHAESTUS_GAME_RUNTIME

// ============================================================================
// Private: Cleanup
// ============================================================================

void EngineFrameRenderer::cleanupSwapchainResources()
{
    // Cleanup resources that depend on swapchain size
}

void EngineFrameRenderer::destroyAllResources()
{
    if (!m_device || !m_device->device) return;
    
    // Cleanup command buffers
    if (!m_commandBuffers.empty()) {
        vkFreeCommandBuffers(m_device->device, m_device->commandPool,
            static_cast<uint32_t>(m_commandBuffers.size()), m_commandBuffers.data());
        m_commandBuffers.clear();
    }
    
    // Cleanup sync objects
    for (auto semaphore : m_imageAvailableSemaphores) {
        vkDestroySemaphore(m_device->device, semaphore, nullptr);
    }
    for (auto semaphore : m_renderFinishedSemaphores) {
        vkDestroySemaphore(m_device->device, semaphore, nullptr);
    }
    for (auto fence : m_inFlightFences) {
        vkDestroyFence(m_device->device, fence, nullptr);
    }
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_imagesInFlight.clear();
    
    // Cleanup uniform buffers
    for (auto& buffer : m_uniformBuffers) {
        buffer.Destroy(m_device->allocator);
    }
    for (auto& buffer : m_lightingBuffers) {
        buffer.Destroy(m_device->allocator);
    }
    for (auto& buffer : m_materialBuffers) {
        buffer.Destroy(m_device->allocator);
    }
    m_uniformBuffers.clear();
    m_lightingBuffers.clear();
    m_materialBuffers.clear();
    
    // Cleanup dummy texture
    m_dummyTexture.Destroy(m_device->allocator, m_device->device);
    
    // Cleanup descriptor pool
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    m_descriptorSets.clear();
    
    // Cleanup mesh library
    for (auto& mesh : m_meshLibrary) {
        mesh.Destroy(m_device->allocator);
    }
    m_meshLibrary.clear();
    m_meshLibraryLookup.clear();
    
    // Cleanup editor grid
    m_editorGrid.Cleanup(m_device->device);
    
    // Cleanup outline renderer
    cleanupOutlineResources();
    
    // Cleanup ID picker
    cleanupIdPickerResources();
    
#ifndef HEPHAESTUS_GAME_RUNTIME
    // Cleanup gizmo
    cleanupGizmoResources();
#endif

    // Cleanup shadow map resources
    if (m_directionalShadow.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device->device, m_directionalShadow.pipeline, nullptr);
        m_directionalShadow.pipeline = VK_NULL_HANDLE;
    }
    if (m_directionalShadow.pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device->device, m_directionalShadow.pipelineLayout, nullptr);
        m_directionalShadow.pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_directionalShadow.framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device->device, m_directionalShadow.framebuffer, nullptr);
        m_directionalShadow.framebuffer = VK_NULL_HANDLE;
    }
    if (m_directionalShadow.renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device->device, m_directionalShadow.renderPass, nullptr);
        m_directionalShadow.renderPass = VK_NULL_HANDLE;
    }
    if (m_directionalShadow.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device->device, m_directionalShadow.sampler, nullptr);
        m_directionalShadow.sampler = VK_NULL_HANDLE;
    }
    if (m_directionalShadow.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device->device, m_directionalShadow.imageView, nullptr);
        m_directionalShadow.imageView = VK_NULL_HANDLE;
    }
    if (m_directionalShadow.image != VK_NULL_HANDLE) {
        vmaDestroyImage(m_device->allocator, m_directionalShadow.image, m_directionalShadow.allocation);
        m_directionalShadow.image = VK_NULL_HANDLE;
        m_directionalShadow.allocation = VK_NULL_HANDLE;
    }

    // Cleanup pipeline
    if (m_pipeline) {
        m_pipeline->Cleanup();
        m_pipeline.reset();
    }
    
    m_device = nullptr;
    m_swapchain = nullptr;
}

// ============================================================================
// Nested struct cleanup
// ============================================================================

void EngineFrameRenderer::ShadowMap::Destroy()
{
    // Note: This needs device/allocator passed - called from destroyAllResources
    // which handles the cleanup directly
}

void EngineFrameRenderer::BillboardPipeline::Cleanup()
{
    // TODO: Implement billboard pipeline cleanup
}

bool EngineFrameRenderer::Skybox::LoadHDRI(const std::string& path, VmaAllocator allocator,
                                            VkPhysicalDevice physDevice, VkCommandPool cmdPool, VkQueue queue)
{
    // TODO: Implement HDRI loading
    return false;
}

void EngineFrameRenderer::Skybox::UpdateDescriptorSet(uint32_t imageIndex, VkBuffer cameraUBO)
{
    // TODO: Implement skybox descriptor update
}

void EngineFrameRenderer::Skybox::Draw(VkCommandBuffer cmd, uint32_t imageIndex)
{
    // TODO: Implement skybox drawing
}

void EngineFrameRenderer::Skybox::Cleanup(VmaAllocator allocator)
{
    // TODO: Implement skybox cleanup
}

// ============================================================================
// Editor Grid Implementation
// ============================================================================

void EngineFrameRenderer::EditorGrid::Init(VkDevice device, VkRenderPass renderPass, 
                                            VkExtent2D extent, VkDescriptorSetLayout cameraDescLayout,
                                            VmaAllocator allocator)
{
    if (initialized) return;
    
    // Create pipeline layout with push constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(GridPushConstants);
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &cameraDescLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        std::cerr << "[EditorGrid] Failed to create pipeline layout\n";
        return;
    }
    
    // Load shaders
    auto loadShader = [device](const std::string& name) -> VkShaderModule {
        const std::string candidates[] = {
            "shaders/" + name,
            "../shaders/" + name,
            "build/Release/shaders/" + name,
            "../build/Release/shaders/" + name
        };
        for (const auto& path : candidates) {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open()) continue;
            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> code(fileSize);
            file.seekg(0);
            file.read(code.data(), fileSize);
            file.close();
            
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
            
            VkShaderModule shaderModule;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
            return shaderModule;
        }
        return VK_NULL_HANDLE;
    };
    
    VkShaderModule vertShader = loadShader("grid.vert.spv");
    VkShaderModule fragShader = loadShader("grid.frag.spv");
    
    if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
        std::cerr << "[EditorGrid] Failed to load grid shaders\n";
        if (vertShader) vkDestroyShaderModule(device, vertShader, nullptr);
        if (fragShader) vkDestroyShaderModule(device, fragShader, nullptr);
        return;
    }
    
    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShader;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShader;
    shaderStages[1].pName = "main";
    
    // No vertex input - we generate fullscreen quad in shader
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    // Alpha blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        std::cerr << "[EditorGrid] Failed to create graphics pipeline\n";
    }
    
    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
    
    initialized = true;
    std::cout << "[EditorGrid] Initialized successfully\n";
}

void EngineFrameRenderer::EditorGrid::Recreate(VkDevice device, VkRenderPass renderPass, 
                                                VkExtent2D extent, VkDescriptorSetLayout cameraDescLayout)
{
    Cleanup(device);
    // Re-init with new render pass/extent
    // Note: Init needs allocator which we don't have here, so this is limited
    // For now, just recreate the pipeline
}

void EngineFrameRenderer::EditorGrid::Draw(VkCommandBuffer cmd, VkDescriptorSet cameraDescSet, 
                                            const GridPushConstants& pc)
{
    if (!initialized || pipeline == VK_NULL_HANDLE) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 
                            0, 1, &cameraDescSet, 0, nullptr);
    vkCmdPushConstants(cmd, pipelineLayout, 
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                       0, sizeof(GridPushConstants), &pc);
    
    // Draw fullscreen quad (6 vertices)
    vkCmdDraw(cmd, 6, 1, 0, 0);
}

void EngineFrameRenderer::EditorGrid::Cleanup(VkDevice device)
{
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
        descriptorLayout = VK_NULL_HANDLE;
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    descriptorSets.clear();
    initialized = false;
}

void EngineFrameRenderer::recordGrid(VkCommandBuffer cmd, uint32_t imageIndex, 
                                      const FrameRenderConfig& config)
{
    if (!config.renderGrid || !config.isEditorMode) return;
    
    EditorGrid::GridPushConstants pc;
    pc.gridSize = 10.0f;     // Major grid lines every 10 units
    pc.cellSize = 1.0f;      // Minor grid lines every 1 unit
    pc.fadeDistance = 100.0f; // Fade out at 100 units
    pc.lineWidth = 1.0f;
    
    // Grid uses same camera descriptor set as main pipeline
    m_editorGrid.Draw(cmd, m_descriptorSets[imageIndex], pc);
}

bool EngineFrameRenderer::createGridResources()
{
    if (!m_device || !m_swapchain) return false;
    
    // Get camera descriptor set layout from main pipeline
    VkDescriptorSetLayout cameraDescLayout = m_pipeline ? m_pipeline->descriptorSetLayout : VK_NULL_HANDLE;
    if (cameraDescLayout == VK_NULL_HANDLE) {
        std::cerr << "[EngineFrameRenderer] Cannot create grid - no camera descriptor layout\n";
        return false;
    }
    
    m_editorGrid.Init(m_device->device, m_swapchain->renderPass, m_swapchain->extent,
                      cameraDescLayout, m_device->allocator);
    
    return m_editorGrid.initialized;
}

// ============================================================================
// Selection Outline Renderer Implementation
// ============================================================================

void EngineFrameRenderer::OutlineRenderer::Init(VkDevice device, VkRenderPass renderPass,
                                                 VkExtent2D extent, VkDescriptorSetLayout cameraDescLayout,
                                                 VmaAllocator allocator)
{
    if (initialized) return;
    
    // Create pipeline layout with push constants (same layout as main shader)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(OutlinePushConstants);
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &cameraDescLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        std::cerr << "[OutlineRenderer] Failed to create pipeline layout\n";
        return;
    }
    
    // Load shaders
    auto loadShader = [device](const std::string& name) -> VkShaderModule {
        const std::string candidates[] = {
            "shaders/" + name,
            "../shaders/" + name,
            "build/Release/shaders/" + name,
            "../build/Release/shaders/" + name
        };
        for (const auto& path : candidates) {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open()) continue;
            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> code(fileSize);
            file.seekg(0);
            file.read(code.data(), fileSize);
            file.close();
            
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
            
            VkShaderModule shaderModule;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
            return shaderModule;
        }
        return VK_NULL_HANDLE;
    };
    
    VkShaderModule vertShader = loadShader("outline.vert.spv");
    VkShaderModule fragShader = loadShader("outline.frag.spv");
    
    if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
        std::cerr << "[OutlineRenderer] Failed to load outline shaders\n";
        if (vertShader) vkDestroyShaderModule(device, vertShader, nullptr);
        if (fragShader) vkDestroyShaderModule(device, fragShader, nullptr);
        return;
    }
    
    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShader;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShader;
    shaderStages[1].pName = "main";
    
    // Vertex input - same as main pipeline (position, normal, color, texcoord)
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 11; // pos(3) + normal(3) + color(3) + texcoord(2)
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = 0;
    
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 3;
    
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[2].offset = sizeof(float) * 6;
    
    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[3].offset = sizeof(float) * 9;
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    // Cull front faces - we draw expanding back faces behind the object
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  // Don't write to depth buffer
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    // Opaque blending for outline
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        std::cerr << "[OutlineRenderer] Failed to create graphics pipeline\n";
        vkDestroyShaderModule(device, vertShader, nullptr);
        vkDestroyShaderModule(device, fragShader, nullptr);
        return;
    }
    
    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
    
    initialized = true;
    std::cout << "[OutlineRenderer] Initialized\n";
}

void EngineFrameRenderer::OutlineRenderer::Recreate(VkDevice device, VkRenderPass renderPass,
                                                     VkExtent2D extent, VkDescriptorSetLayout cameraDescLayout)
{
    // Cleanup old pipeline
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    
    // Recreate with new extent - reinit
    VmaAllocator dummyAllocator = VK_NULL_HANDLE; // Not used in Init for OutlineRenderer
    initialized = false;
    Init(device, renderPass, extent, cameraDescLayout, dummyAllocator);
}

void EngineFrameRenderer::OutlineRenderer::Draw(VkCommandBuffer cmd, VkDescriptorSet cameraDescSet,
                                                 VkBuffer vertexBuffer, VkBuffer indexBuffer,
                                                 uint32_t indexCount, const Mat4& modelMatrix)
{
    if (!initialized || pipeline == VK_NULL_HANDLE) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &cameraDescSet, 0, nullptr);
    
    OutlinePushConstants pc;
    pc.model = modelMatrix;
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}

void EngineFrameRenderer::OutlineRenderer::Cleanup(VkDevice device)
{
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    initialized = false;
}

bool EngineFrameRenderer::createOutlineResources()
{
    if (!m_device || !m_swapchain) return false;
    
    VkDescriptorSetLayout cameraDescLayout = m_pipeline ? m_pipeline->descriptorSetLayout : VK_NULL_HANDLE;
    if (cameraDescLayout == VK_NULL_HANDLE) {
        std::cerr << "[EngineFrameRenderer] Cannot create outline - no camera descriptor layout\n";
        return false;
    }
    
    m_outlineRenderer.Init(m_device->device, m_swapchain->renderPass, m_swapchain->extent,
                           cameraDescLayout, m_device->allocator);
    
    return m_outlineRenderer.initialized;
}

void EngineFrameRenderer::cleanupOutlineResources()
{
    if (m_device) {
        m_outlineRenderer.Cleanup(m_device->device);
    }
}

// ============================================================================
// Entity ID Picker Implementation
// ============================================================================

bool EngineFrameRenderer::createIdPickerResources()
{
    if (!m_device || !m_swapchain) return false;
    
    VkDevice device = m_device->device;
    VmaAllocator allocator = m_device->allocator;
    uint32_t width = m_swapchain->extent.width;
    uint32_t height = m_swapchain->extent.height;
    
    if (width == 0 || height == 0) return false;
    
    m_idPicker.width = width;
    m_idPicker.height = height;
    
    // Create color image (RGBA8 for entity ID encoding)
    VkImageCreateInfo colorImageInfo{};
    colorImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    colorImageInfo.imageType = VK_IMAGE_TYPE_2D;
    colorImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorImageInfo.extent = {width, height, 1};
    colorImageInfo.mipLevels = 1;
    colorImageInfo.arrayLayers = 1;
    colorImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    colorImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    colorImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    colorImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    colorImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    if (vmaCreateImage(allocator, &colorImageInfo, &allocInfo, 
                       &m_idPicker.colorImage, &m_idPicker.colorAllocation, nullptr) != VK_SUCCESS) {
        std::cerr << "[IdPicker] Failed to create color image\n";
        return false;
    }
    
    // Create color image view
    VkImageViewCreateInfo colorViewInfo{};
    colorViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    colorViewInfo.image = m_idPicker.colorImage;
    colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorViewInfo.subresourceRange.baseMipLevel = 0;
    colorViewInfo.subresourceRange.levelCount = 1;
    colorViewInfo.subresourceRange.baseArrayLayer = 0;
    colorViewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &colorViewInfo, nullptr, &m_idPicker.colorImageView) != VK_SUCCESS) {
        std::cerr << "[IdPicker] Failed to create color image view\n";
        return false;
    }
    
    // Create depth image
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.format = VK_FORMAT_D32_SFLOAT;
    depthImageInfo.extent = {width, height, 1};
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vmaCreateImage(allocator, &depthImageInfo, &allocInfo, 
                       &m_idPicker.depthImage, &m_idPicker.depthAllocation, nullptr) != VK_SUCCESS) {
        std::cerr << "[IdPicker] Failed to create depth image\n";
        return false;
    }
    
    // Create depth image view
    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = m_idPicker.depthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = VK_FORMAT_D32_SFLOAT;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.baseArrayLayer = 0;
    depthViewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &depthViewInfo, nullptr, &m_idPicker.depthImageView) != VK_SUCCESS) {
        std::cerr << "[IdPicker] Failed to create depth image view\n";
        return false;
    }
    
    // Create render pass
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    
    VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_idPicker.renderPass) != VK_SUCCESS) {
        std::cerr << "[IdPicker] Failed to create render pass\n";
        return false;
    }
    
    // Create framebuffer
    VkImageView fbAttachments[] = {m_idPicker.colorImageView, m_idPicker.depthImageView};
    
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_idPicker.renderPass;
    fbInfo.attachmentCount = 2;
    fbInfo.pAttachments = fbAttachments;
    fbInfo.width = width;
    fbInfo.height = height;
    fbInfo.layers = 1;
    
    if (vkCreateFramebuffer(device, &fbInfo, nullptr, &m_idPicker.framebuffer) != VK_SUCCESS) {
        std::cerr << "[IdPicker] Failed to create framebuffer\n";
        return false;
    }
    
    // Create pipeline layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(IdPicker::IdPushConstants);
    
    VkDescriptorSetLayout cameraDescLayout = m_pipeline ? m_pipeline->descriptorSetLayout : VK_NULL_HANDLE;
    if (cameraDescLayout == VK_NULL_HANDLE) {
        std::cerr << "[IdPicker] No camera descriptor layout available\n";
        return false;
    }
    
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &cameraDescLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_idPicker.pipelineLayout) != VK_SUCCESS) {
        std::cerr << "[IdPicker] Failed to create pipeline layout\n";
        return false;
    }
    
    // Load shaders
    auto loadShader = [device](const std::string& name) -> VkShaderModule {
        const std::string candidates[] = {
            "shaders/" + name,
            "../shaders/" + name,
            "build/Release/shaders/" + name,
            "../build/Release/shaders/" + name
        };
        for (const auto& path : candidates) {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open()) continue;
            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> code(fileSize);
            file.seekg(0);
            file.read(code.data(), fileSize);
            file.close();
            
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
            
            VkShaderModule shaderModule;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
            return shaderModule;
        }
        return VK_NULL_HANDLE;
    };
    
    VkShaderModule vertShader = loadShader("entityid.vert.spv");
    VkShaderModule fragShader = loadShader("entityid.frag.spv");
    
    if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
        std::cerr << "[IdPicker] Failed to load entity ID shaders\n";
        if (vertShader) vkDestroyShaderModule(device, vertShader, nullptr);
        if (fragShader) vkDestroyShaderModule(device, fragShader, nullptr);
        return false;
    }
    
    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShader;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShader;
    shaderStages[1].pName = "main";
    
    // Vertex input - same as main pipeline
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(float) * 8; // pos(3) + normal(3) + uv(2)
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attribDescs[3]{};
    attribDescs[0].binding = 0;
    attribDescs[0].location = 0;
    attribDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribDescs[0].offset = 0;
    attribDescs[1].binding = 0;
    attribDescs[1].location = 1;
    attribDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribDescs[1].offset = sizeof(float) * 3;
    attribDescs[2].binding = 0;
    attribDescs[2].location = 2;
    attribDescs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attribDescs[2].offset = sizeof(float) * 6;
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 3;
    vertexInputInfo.pVertexAttributeDescriptions = attribDescs;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_idPicker.pipelineLayout;
    pipelineInfo.renderPass = m_idPicker.renderPass;
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, 
                                  &m_idPicker.pipeline) != VK_SUCCESS) {
        std::cerr << "[IdPicker] Failed to create graphics pipeline\n";
        vkDestroyShaderModule(device, vertShader, nullptr);
        vkDestroyShaderModule(device, fragShader, nullptr);
        return false;
    }
    
    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
    
    // Create staging buffer for readback (small, just needs a few pixels)
    VkBufferCreateInfo stagingBufInfo{};
    stagingBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufInfo.size = 4; // 4 bytes for one RGBA8 pixel
    stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    stagingBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    VmaAllocationInfo allocInfoResult;
    if (vmaCreateBuffer(allocator, &stagingBufInfo, &stagingAllocInfo,
                        &m_idPicker.stagingBuffer, &m_idPicker.stagingAllocation, 
                        &allocInfoResult) != VK_SUCCESS) {
        std::cerr << "[IdPicker] Failed to create staging buffer\n";
        return false;
    }
    m_idPicker.mappedStagingBuffer = allocInfoResult.pMappedData;
    
    m_idPicker.initialized = true;
    std::cout << "[IdPicker] Initialized successfully (" << width << "x" << height << ")\n";
    return true;
}

void EngineFrameRenderer::recordIdPass(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene)
{
    if (!m_idPicker.initialized || !scene) return;
    
    VkClearValue clearValues[2];
    clearValues[0].color = {{1.0f, 1.0f, 1.0f, 1.0f}}; // Clear to max entity ID (no entity)
    clearValues[1].depthStencil = {1.0f, 0};
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_idPicker.renderPass;
    renderPassInfo.framebuffer = m_idPicker.framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {m_idPicker.width, m_idPicker.height};
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;
    
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_idPicker.width);
    viewport.height = static_cast<float>(m_idPicker.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, {m_idPicker.width, m_idPicker.height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_idPicker.pipeline);
    
    // Bind camera descriptor set
    uint32_t dynOffset = 0;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_idPicker.pipelineLayout,
                            0, 1, &m_descriptorSets[imageIndex], 1, &dynOffset);
    
    // Render all entities with their IDs
    auto renderView = scene->GetEntitiesWith<TransformComponent, MeshRendererComponent>();
    
    VkBuffer currentVertexBuffer = VK_NULL_HANDLE;
    VkBuffer currentIndexBuffer = VK_NULL_HANDLE;
    
    for (auto entityHandle : renderView) {
        Entity entity(entityHandle, scene);
        auto& transform = entity.GetComponent<TransformComponent>();
        auto& renderer = entity.GetComponent<MeshRendererComponent>();
        
        if (!renderer.visible) continue;
        if (!renderer.HasValidMesh() || renderer.meshHandle >= m_meshLibrary.size()) continue;
        
        MeshGPU& mesh = m_meshLibrary[renderer.meshHandle];
        if (mesh.indexCount == 0 || mesh.vertexBuffer.buffer == VK_NULL_HANDLE ||
            mesh.indexBuffer.buffer == VK_NULL_HANDLE) continue;
        
        // Bind vertex buffer if different
        if (mesh.vertexBuffer.buffer != currentVertexBuffer) {
            VkBuffer vb = mesh.vertexBuffer.buffer;
            VkDeviceSize vbOffset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
            currentVertexBuffer = vb;
        }
        
        // Bind index buffer if different
        if (mesh.indexBuffer.buffer != currentIndexBuffer) {
            vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            currentIndexBuffer = mesh.indexBuffer.buffer;
        }
        
        // Push model matrix and entity ID
        IdPicker::IdPushConstants pc;
        pc.model = scene->GetWorldTransform(entity);
        pc.entityId = static_cast<uint32_t>(entityHandle);
        
        vkCmdPushConstants(cmd, m_idPicker.pipelineLayout, 
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(IdPicker::IdPushConstants), &pc);
        
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
    }
    
    vkCmdEndRenderPass(cmd);
}

uint32_t EngineFrameRenderer::PickEntityAt(uint32_t screenX, uint32_t screenY)
{
    if (!m_idPicker.initialized || !m_idPicker.mappedStagingBuffer) {
        return UINT32_MAX;
    }
    
    // Bounds check
    if (screenX >= m_idPicker.width || screenY >= m_idPicker.height) {
        return UINT32_MAX;
    }
    
    // Set up pick request - will be processed next frame
    m_idPicker.pickPending = true;
    m_idPicker.pickX = screenX;
    m_idPicker.pickY = screenY;
    
    // For now, return last picked entity (1-frame latency)
    // A more sophisticated implementation would use fences and async readback
    return m_idPicker.lastPickedEntity;
}

void EngineFrameRenderer::cleanupIdPickerResources()
{
    if (!m_device || !m_device->device) return;
    
    VkDevice device = m_device->device;
    VmaAllocator allocator = m_device->allocator;
    
    if (m_idPicker.stagingBuffer) {
        vmaDestroyBuffer(allocator, m_idPicker.stagingBuffer, m_idPicker.stagingAllocation);
        m_idPicker.stagingBuffer = VK_NULL_HANDLE;
    }
    
    if (m_idPicker.pipeline) {
        vkDestroyPipeline(device, m_idPicker.pipeline, nullptr);
        m_idPicker.pipeline = VK_NULL_HANDLE;
    }
    
    if (m_idPicker.pipelineLayout) {
        vkDestroyPipelineLayout(device, m_idPicker.pipelineLayout, nullptr);
        m_idPicker.pipelineLayout = VK_NULL_HANDLE;
    }
    
    if (m_idPicker.framebuffer) {
        vkDestroyFramebuffer(device, m_idPicker.framebuffer, nullptr);
        m_idPicker.framebuffer = VK_NULL_HANDLE;
    }
    
    if (m_idPicker.renderPass) {
        vkDestroyRenderPass(device, m_idPicker.renderPass, nullptr);
        m_idPicker.renderPass = VK_NULL_HANDLE;
    }
    
    if (m_idPicker.colorImageView) {
        vkDestroyImageView(device, m_idPicker.colorImageView, nullptr);
        m_idPicker.colorImageView = VK_NULL_HANDLE;
    }
    
    if (m_idPicker.colorImage) {
        vmaDestroyImage(allocator, m_idPicker.colorImage, m_idPicker.colorAllocation);
        m_idPicker.colorImage = VK_NULL_HANDLE;
    }
    
    if (m_idPicker.depthImageView) {
        vkDestroyImageView(device, m_idPicker.depthImageView, nullptr);
        m_idPicker.depthImageView = VK_NULL_HANDLE;
    }
    
    if (m_idPicker.depthImage) {
        vmaDestroyImage(allocator, m_idPicker.depthImage, m_idPicker.depthAllocation);
        m_idPicker.depthImage = VK_NULL_HANDLE;
    }
    
    m_idPicker.initialized = false;
    m_idPicker.mappedStagingBuffer = nullptr;
}

} // namespace Hephaestus
