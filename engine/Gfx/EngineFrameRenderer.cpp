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
    
    if (!createDescriptorSets()) {
        std::cerr << "[EngineFrameRenderer] Failed to create descriptor sets\n";
        return false;
    }
    
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
        updateLightingUBO(imageIndex, scene);
    }
    
    // Record command buffer
    VkCommandBuffer cmd = m_commandBuffers[imageIndex];
    vkResetCommandBuffer(cmd, 0);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
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
    // For now, we'll skip texture binding and just use uniform colors
    
    // Update descriptor sets with buffer bindings only
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
        
        VkWriteDescriptorSet descriptorWrites[3] = {};
        
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
        
        vkUpdateDescriptorSets(m_device->device, 3, descriptorWrites, 0, nullptr);
    }
    
    return true;
}

bool EngineFrameRenderer::createShadowResources()
{
    // TODO: Implement shadow map creation
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

void EngineFrameRenderer::updateLightingUBO(uint32_t imageIndex, Scene* scene)
{
    LightingUBO lightUBO{};
    
    // Default ambient lighting
    lightUBO.ambientColorIntensity = Vec4(0.1f, 0.1f, 0.15f, 0.3f);
    
    // Default directional light
    lightUBO.dirLightDirIntensity = Vec4(0.0f, -1.0f, 0.0f, 0.5f);
    lightUBO.dirLightColor = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    
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
            }
            
            if (light.type == LightType::Directional) {
                lightUBO.dirLightDirIntensity = Vec4(light.direction.x, light.direction.y, 
                                                     light.direction.z, light.intensity);
                lightUBO.dirLightColor = Vec4(light.color.x, light.color.y, light.color.z, 1.0f);
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
    if (scene && !m_meshLibrary.empty()) {
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
        }
    }
    
    vkCmdEndRenderPass(cmd);
}

void EngineFrameRenderer::recordGizmo(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene)
{
    // TODO: Implement gizmo rendering
}

void EngineFrameRenderer::recordBillboards(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene)
{
    // TODO: Implement billboard rendering
}

void EngineFrameRenderer::recordOutlines(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene)
{
    // TODO: Implement outline rendering
}

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
    // TODO: Implement shadow map cleanup
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

} // namespace Hephaestus
