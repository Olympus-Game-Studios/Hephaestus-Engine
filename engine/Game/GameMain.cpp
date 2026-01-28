/**
 * GameMain.cpp
 * 
 * Standalone game runtime entrypoint for Hephaestus Engine.
 * This is a minimal, editor-free runtime that only initializes what's needed
 * to run a packaged game:
 * - Window and platform
 * - Vulkan renderer
 * - Input handling
 * - Asset loading
 * - Lua scripting (if enabled)
 * - Physics
 * - Scene loading and game loop
 * 
 * This does NOT include:
 * - ImGui/Editor UI
 * - Ultralight HTML UI (editor-only by default)
 * - Asset browser, inspector, outliner
 * - Material editor
 * - Project manager
 * 
 * Build target: HephaestusGame.exe
 */

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <entt/entt.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "Core/RuntimeBootstrap.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Engine core
#include "../Gfx/VulkanBuffer.h"
#include "../Gfx/VulkanDevice.h"
#include "../Gfx/VulkanPipeline.h"
#include "../Gfx/VulkanSkybox.h"
#include "../Gfx/VulkanSwapchain.h"
#include "../Gfx/VulkanTexture.h"

// Physics
#include "../Physics/JoltPhysicsEngine.h"
#include "../Physics/JoltPhysicsSystem.h"
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

// Windows headers can define macros that collide with Jolt symbols.
#ifdef RayCastResult
#undef RayCastResult
#endif

// Scene
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneSerializer.h"
#include "../Scene/Transform.h"

// Scripting
#if HEPHAESTUS_GAME_SCRIPTING
#include "../Scripting/LuaScriptEngine.h"
#include "../Scripting/ScriptEngine.h"
#endif

// Assets
#include "../Assets/MeshAsset.h"
#include "../Assets/ModelImporter.h"

// stb_image is already included via ModelImporter or elsewhere
// Do not re-define STB_IMAGE_IMPLEMENTATION here

namespace fs = std::filesystem;
using namespace Hephaestus::Runtime;

// ──────────────────────────────────────────────────────────────────────────────
// Uniform Buffer Object (must match shader layout)
// ──────────────────────────────────────────────────────────────────────────────

struct UniformBufferObject {
    Mat4 view;
    Mat4 proj;
    Vec3 viewPos;
    float padding1;
};

struct alignas(16) MaterialUBO {
    Vec3 albedo = Vec3(0.8f, 0.8f, 0.8f);
    float albedoPadding = 0.0f;
    float metallic = 0.0f;
    float metallicPadding0 = 0.0f;
    float metallicPadding1 = 0.0f;
    float metallicPadding2 = 0.0f;
    Vec3 emissive = Vec3(0.0f, 0.0f, 0.0f);
    float emissivePadding = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    float emissiveStrength = 1.0f;
    uint32_t useAlbedoTexture = 0;
    uint32_t albedoTextureIndex = 0;
    uint32_t useNormalTexture = 0;
    uint32_t normalTextureIndex = 0;
    uint32_t useRoughnessTexture = 0;
    uint32_t roughnessTextureIndex = 0;
    uint32_t useAoTexture = 0;
    uint32_t aoTextureIndex = 0;
    uint32_t useWorldAlignedTexture = 0;
    float worldTextureScale = 1.0f;
    uint32_t paddingTail[7] = {0};
};

struct LightingUBO {
    Mat4 dirLightViewProj;
    Vec4 dirShadowParams;           // x = bias, y = strength, z = texel size, w = enabled
    Vec4 dirLightDirIntensity;      // xyz = direction, w = intensity
    Vec4 dirLightColor;             // rgb = color, w = unused
    Vec4 ambientColorIntensity;     // rgb = ambient color, w = ambient intensity
    Vec4 pointLightPositions[16];
    Vec4 pointLightColors[16];
    Vec4 pointLightAttenuation[16];
    Vec4 spotLightPositions[16];
    Vec4 spotLightDirections[16];
    Vec4 spotLightColors[16];
    Vec4 spotLightAttenuation[16];
    int pointLightCount = 0;
    int spotLightCount = 0;
    float padding5 = 0.0f;
    float padding6 = 0.0f;
};

// ──────────────────────────────────────────────────────────────────────────────
// Shadow Mapping
// ──────────────────────────────────────────────────────────────────────────────

struct ShadowPushConstants {
    Mat4 lightViewProj;
    Mat4 model;
};

static VkFormat FindSupportedDepthFormat(VkPhysicalDevice phys,
                                         const std::vector<VkFormat>& candidates,
                                         VkImageTiling tiling,
                                         VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(phys, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (props.linearTilingFeatures & features) == features) {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("Failed to find supported depth format");
}

struct DirectionalShadowResources {
    uint32_t resolution = 2048;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorImageInfo descriptor{};
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    bool Create(VulkanDevice& vkdev) {
        Destroy();
        device = vkdev.device;
        physicalDevice = vkdev.physicalDevice;
        allocator = vkdev.allocator;
        commandPool = vkdev.commandPool;
        graphicsQueue = vkdev.graphicsQueue;

        auto fail = [&]() {
            Destroy();
            return false;
        };

        try {
            depthFormat = FindSupportedDepthFormat(
                physicalDevice,
                {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT},
                VK_IMAGE_TILING_OPTIMAL,
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        } catch (const std::exception& e) {
            HP_LOG(Error, std::string("[Shadow] ") + e.what());
            return false;
        }

        // Create depth image
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {resolution, resolution, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = depthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
            HP_LOG(Error, "[Shadow] Failed to create depth image");
            return fail();
        }

        // Create image view
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            HP_LOG(Error, "[Shadow] Failed to create image view");
            return fail();
        }

        // Create sampler
        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.maxAnisotropy = 1.0f;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
            HP_LOG(Error, "[Shadow] Failed to create sampler");
            return fail();
        }

        // Create render pass
        VkAttachmentDescription depthAttach{};
        depthAttach.format = depthFormat;
        depthAttach.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttach.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttach.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depthRef;

        VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &depthAttach;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        if (vkCreateRenderPass(device, &rpInfo, nullptr, &renderPass) != VK_SUCCESS) {
            HP_LOG(Error, "[Shadow] Failed to create render pass");
            return fail();
        }

        // Create framebuffer
        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &imageView;
        fbInfo.width = resolution;
        fbInfo.height = resolution;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffer) != VK_SUCCESS) {
            HP_LOG(Error, "[Shadow] Failed to create framebuffer");
            return fail();
        }

        // Load shadow shaders
        auto readBinaryFile = [](const fs::path& path) -> std::vector<char> {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open()) {
                fs::path alt = fs::path("../") / path;
                file.open(alt, std::ios::ate | std::ios::binary);
            }
            if (!file.is_open()) {
                throw std::runtime_error("Failed to open file: " + path.string());
            }
            size_t size = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(size);
            file.seekg(0);
            file.read(buffer.data(), size);
            return buffer;
        };

        std::vector<char> vertCode, fragCode;
        try {
            vertCode = readBinaryFile("shaders/shadowmap.vert.spv");
            fragCode = readBinaryFile("shaders/shadowmap.frag.spv");
        } catch (const std::exception& e) {
            HP_LOG(Warning, std::string("[Shadow] Shader load failed: ") + e.what());
            return fail();
        }

        VkShaderModuleCreateInfo smInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        smInfo.codeSize = vertCode.size();
        smInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
        VkShaderModule vertModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &smInfo, nullptr, &vertModule) != VK_SUCCESS)
            return fail();
        smInfo.codeSize = fragCode.size();
        smInfo.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
        VkShaderModule fragModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &smInfo, nullptr, &fragModule) != VK_SUCCESS) {
            vkDestroyShaderModule(device, vertModule, nullptr);
            return fail();
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        auto bindingDesc = Vertex::getBindingDescription();
        auto attribs = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDesc;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
        vertexInput.pVertexAttributeDescriptions = attribs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAsm{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;
        raster.depthBiasEnable = VK_TRUE;
        raster.depthBiasConstantFactor = 1.25f;
        raster.depthBiasSlopeFactor = 1.75f;

        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 0;

        VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynamics;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.size = sizeof(ShadowPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            HP_LOG(Error, "[Shadow] Failed to create pipeline layout");
            vkDestroyShaderModule(device, vertModule, nullptr);
            vkDestroyShaderModule(device, fragModule, nullptr);
            return fail();
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAsm;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &ms;
        pipelineInfo.pDepthStencilState = &depth;
        pipelineInfo.pColorBlendState = &blend;
        pipelineInfo.pDynamicState = &dyn;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
            HP_LOG(Error, "[Shadow] Failed to create pipeline");
            vkDestroyShaderModule(device, vertModule, nullptr);
            vkDestroyShaderModule(device, fragModule, nullptr);
            return fail();
        }

        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);

        // Initialize depth image to ATTACHMENT_OPTIMAL
        VkCommandBufferAllocateInfo cmdAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmdAlloc.commandPool = commandPool;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmd);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);
        vkFreeCommandBuffers(device, commandPool, 1, &cmd);

        descriptor.sampler = sampler;
        descriptor.imageView = imageView;
        descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        
        HP_LOG(Info, "[Shadow] Created shadow map resources (" + std::to_string(resolution) + "x" + std::to_string(resolution) + ")");
        return true;
    }

    void Destroy() {
        if (device == VK_NULL_HANDLE) return;
        if (pipeline) { vkDestroyPipeline(device, pipeline, nullptr); pipeline = VK_NULL_HANDLE; }
        if (pipelineLayout) { vkDestroyPipelineLayout(device, pipelineLayout, nullptr); pipelineLayout = VK_NULL_HANDLE; }
        if (framebuffer) { vkDestroyFramebuffer(device, framebuffer, nullptr); framebuffer = VK_NULL_HANDLE; }
        if (renderPass) { vkDestroyRenderPass(device, renderPass, nullptr); renderPass = VK_NULL_HANDLE; }
        if (sampler) { vkDestroySampler(device, sampler, nullptr); sampler = VK_NULL_HANDLE; }
        if (imageView) { vkDestroyImageView(device, imageView, nullptr); imageView = VK_NULL_HANDLE; }
        if (image) { vmaDestroyImage(allocator, image, allocation); image = VK_NULL_HANDLE; allocation = VK_NULL_HANDLE; }
        descriptor = {};
        device = VK_NULL_HANDLE;
    }

    bool IsValid() const {
        return pipeline != VK_NULL_HANDLE && framebuffer != VK_NULL_HANDLE && descriptor.imageView != VK_NULL_HANDLE;
    }
};

static DirectionalShadowResources g_DirectionalShadow;

// ──────────────────────────────────────────────────────────────────────────────
// Camera for Game Runtime
// ──────────────────────────────────────────────────────────────────────────────

struct GameCamera {
    Vec3 position = Vec3(0.0f, 2.0f, 5.0f);
    float yawDeg = -90.0f;
    float pitchDeg = 0.0f;
    float fovDeg = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    UniformBufferObject MakeUBO(VkExtent2D extent) const {
        float yawR = yawDeg * 3.14159265f / 180.0f;
        float pitchR = pitchDeg * 3.14159265f / 180.0f;
        Vec3 front = vnorm(v3(
            std::cos(yawR) * std::cos(pitchR),
            std::sin(pitchR),
            std::sin(yawR) * std::cos(pitchR)
        ));
        Vec3 up = v3(0.0f, 1.0f, 0.0f);
        
        UniformBufferObject ubo{};
        ubo.view = Mat4::LookAt(position, vadd(position, front), up);
        
        uint32_t w = std::max(extent.width, 1u);
        uint32_t h = std::max(extent.height, 1u);
        float aspect = static_cast<float>(w) / static_cast<float>(h);
        ubo.proj = Mat4::Perspective(fovDeg * 3.14159265f / 180.0f, aspect, nearPlane, farPlane);
        ubo.viewPos = position;
        ubo.padding1 = 0.0f;
        return ubo;
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Configuration
// ──────────────────────────────────────────────────────────────────────────────

struct GameConfig {
    std::string windowTitle = "Hephaestus Game";
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;
    bool vsync = true;
    std::string scenePath;
    std::string projectName;
};

// ──────────────────────────────────────────────────────────────────────────────
// Globals
// ──────────────────────────────────────────────────────────────────────────────

static bool g_FramebufferResized = false;
static fs::path g_RuntimeRoot;
static GameConfig g_Config;
static GameCamera* g_GameCameraPtr = nullptr; // For Lua camera binding

// ──────────────────────────────────────────────────────────────────────────────
// Mesh Library for Runtime
// ──────────────────────────────────────────────────────────────────────────────

static std::vector<MeshGPU> g_MeshLibrary;
static std::unordered_map<std::string, size_t> g_MeshLookup;
static std::vector<VulkanTexture> g_TextureLibrary;
static std::unordered_map<std::string, uint32_t> g_TextureLookup;
static std::unordered_map<std::string, MaterialComponent> g_MaterialLibrary;

static size_t LoadMeshToGPU(const MeshData& meshData, const std::string& name, 
                            VmaAllocator allocator, VkDevice device,
                            VkCommandPool cmdPool, VkQueue queue) {
    if (meshData.vertices.empty() || meshData.indices.empty()) {
        return SIZE_MAX;
    }
    
    MeshGPU mesh;
    mesh.name = name;
    mesh.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(meshData.indices.size());
    mesh.bounds = meshData.bounds;
    mesh.submeshes = meshData.submeshes;
    
    // Create vertex buffer
    VkDeviceSize vbSize = meshData.vertices.size() * sizeof(Vertex);
    mesh.vertexBuffer.Create(allocator, vbSize, 
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    
    // Create index buffer
    VkDeviceSize ibSize = meshData.indices.size() * sizeof(uint32_t);
    mesh.indexBuffer.Create(allocator, ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    
    // Stage and copy vertex data
    VulkanBuffer stagingVB;
    stagingVB.Create(allocator, vbSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void* vbData;
    vmaMapMemory(allocator, stagingVB.allocation, &vbData);
    memcpy(vbData, meshData.vertices.data(), vbSize);
    vmaUnmapMemory(allocator, stagingVB.allocation);
    
    // Stage and copy index data
    VulkanBuffer stagingIB;
    stagingIB.Create(allocator, ibSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void* ibData;
    vmaMapMemory(allocator, stagingIB.allocation, &ibData);
    memcpy(ibData, meshData.indices.data(), ibSize);
    vmaUnmapMemory(allocator, stagingIB.allocation);
    
    // Copy buffers
    VkCommandBufferAllocateInfo cmdAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAlloc.commandPool = cmdPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);
    
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    VkBufferCopy copyRegion{};
    copyRegion.size = vbSize;
    vkCmdCopyBuffer(cmd, stagingVB.buffer, mesh.vertexBuffer.buffer, 1, &copyRegion);
    
    copyRegion.size = ibSize;
    vkCmdCopyBuffer(cmd, stagingIB.buffer, mesh.indexBuffer.buffer, 1, &copyRegion);
    
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
    stagingVB.Destroy(allocator);
    stagingIB.Destroy(allocator);
    
    size_t idx = g_MeshLibrary.size();
    g_MeshLibrary.push_back(std::move(mesh));
    g_MeshLookup[name] = idx;
    
    return idx;
}

// ──────────────────────────────────────────────────────────────────────────────
// Callbacks
// ──────────────────────────────────────────────────────────────────────────────

static void FramebufferResizeCallback(GLFWwindow* /*window*/, int /*width*/, int /*height*/) {
    g_FramebufferResized = true;
}

static void ScrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    // Handle scroll for camera zoom, etc.
    (void)yoffset;
}

// ──────────────────────────────────────────────────────────────────────────────
// Path Resolution
// ──────────────────────────────────────────────────────────────────────────────

static fs::path ResolveRuntimePath(const fs::path& relative) {
    return (g_RuntimeRoot / relative).lexically_normal();
}

static std::string NormalizeAssetPath(const std::string& path) {
    if (path.empty()) {
        return "";
    }
    fs::path resolved = fs::path(path);
    if (resolved.is_relative()) {
        fs::path runtimeCandidate = (g_RuntimeRoot / resolved).lexically_normal();
        if (fs::exists(runtimeCandidate)) {
            return runtimeCandidate.string();
        }

        fs::path assetsCandidate = (g_RuntimeRoot / "assets" / resolved).lexically_normal();
        if (fs::exists(assetsCandidate)) {
            return assetsCandidate.string();
        }

        return runtimeCandidate.string();
    }
    return resolved.lexically_normal().string();
}

static bool LoadMaterialAsset(const std::string& rawPath, MaterialComponent& outMaterial) {
    std::string path = NormalizeAssetPath(rawPath);
    if (path.empty() || !fs::exists(path)) {
        return false;
    }

    std::ifstream in(path);
    if (!in) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    struct JsonValue {
        enum class Type { Null, Bool, Number, String, Array, Object };
        Type type = Type::Null;
        bool boolValue = false;
        double numberValue = 0.0;
        std::string stringValue;
        std::vector<JsonValue> arrayValue;
        std::unordered_map<std::string, JsonValue> objectValue;

        bool IsNull() const { return type == Type::Null; }
        bool IsBool() const { return type == Type::Bool; }
        bool IsNumber() const { return type == Type::Number; }
        bool IsString() const { return type == Type::String; }
        bool IsArray() const { return type == Type::Array; }
        bool IsObject() const { return type == Type::Object; }

        bool AsBool(bool fallback = false) const { return IsBool() ? boolValue : fallback; }
        double AsNumber(double fallback = 0.0) const { return IsNumber() ? numberValue : fallback; }
        const std::string& AsString() const { return stringValue; }
        const std::vector<JsonValue>& AsArray() const { return arrayValue; }

        const JsonValue* Find(const std::string& key) const {
            if (!IsObject()) return nullptr;
            auto it = objectValue.find(key);
            return it != objectValue.end() ? &it->second : nullptr;
        }
    };

    auto skipWhitespace = [](const char*& p, const char* end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    };

    std::function<bool(const char*&, const char*, JsonValue&)> parseValue;

    auto parseString = [&](const char*& p, const char* end, std::string& out) -> bool {
        if (*p != '"') return false;
        ++p;
        out.clear();
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                switch (*p) {
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case '\\': out += '\\'; break;
                    case '"': out += '"'; break;
                    default: out += *p; break;
                }
            } else {
                out += *p;
            }
            ++p;
        }
        if (p >= end || *p != '"') return false;
        ++p;
        return true;
    };

    std::function<bool(const char*&, const char*, JsonValue&)> parseObject =
        [&](const char*& p, const char* end, JsonValue& out) -> bool {
            if (*p != '{') return false;
            ++p;
            skipWhitespace(p, end);
            out.type = JsonValue::Type::Object;
            out.objectValue.clear();
            if (p < end && *p == '}') { ++p; return true; }
            while (p < end) {
                skipWhitespace(p, end);
                std::string key;
                if (!parseString(p, end, key)) return false;
                skipWhitespace(p, end);
                if (p >= end || *p != ':') return false;
                ++p;
                skipWhitespace(p, end);
                JsonValue value;
                if (!parseValue(p, end, value)) return false;
                out.objectValue.emplace(std::move(key), std::move(value));
                skipWhitespace(p, end);
                if (p < end && *p == '}') { ++p; return true; }
                if (p >= end || *p != ',') return false;
                ++p;
            }
            return false;
        };

    std::function<bool(const char*&, const char*, JsonValue&)> parseArray =
        [&](const char*& p, const char* end, JsonValue& out) -> bool {
            if (*p != '[') return false;
            ++p;
            skipWhitespace(p, end);
            out.type = JsonValue::Type::Array;
            out.arrayValue.clear();
            if (p < end && *p == ']') { ++p; return true; }
            while (p < end) {
                JsonValue value;
                if (!parseValue(p, end, value)) return false;
                out.arrayValue.push_back(std::move(value));
                skipWhitespace(p, end);
                if (p < end && *p == ']') { ++p; return true; }
                if (p >= end || *p != ',') return false;
                ++p;
                skipWhitespace(p, end);
            }
            return false;
        };

    parseValue = [&](const char*& p, const char* end, JsonValue& out) -> bool {
        skipWhitespace(p, end);
        if (p >= end) return false;
        if (*p == '{') return parseObject(p, end, out);
        if (*p == '[') return parseArray(p, end, out);
        if (*p == '"') { out.type = JsonValue::Type::String; return parseString(p, end, out.stringValue); }
        if (std::strncmp(p, "true", 4) == 0) { out.type = JsonValue::Type::Bool; out.boolValue = true; p += 4; return true; }
        if (std::strncmp(p, "false", 5) == 0) { out.type = JsonValue::Type::Bool; out.boolValue = false; p += 5; return true; }
        if (std::strncmp(p, "null", 4) == 0) { out.type = JsonValue::Type::Null; p += 4; return true; }

        char* endPtr = nullptr;
        double value = std::strtod(p, &endPtr);
        if (endPtr == p) return false;
        out.type = JsonValue::Type::Number;
        out.numberValue = value;
        p = endPtr;
        return true;
    };

    JsonValue root;
    const char* ptr = content.c_str();
    const char* end = ptr + content.size();
    if (!parseValue(ptr, end, root) || !root.IsObject()) {
        return false;
    }

    auto parseVec3 = [](const JsonValue& val, Vec3& out) {
        if (val.IsArray() && val.arrayValue.size() >= 3) {
            if (val.arrayValue[0].IsNumber()) out.x = static_cast<float>(val.arrayValue[0].AsNumber());
            if (val.arrayValue[1].IsNumber()) out.y = static_cast<float>(val.arrayValue[1].AsNumber());
            if (val.arrayValue[2].IsNumber()) out.z = static_cast<float>(val.arrayValue[2].AsNumber());
        }
    };

    MaterialComponent material;
    if (const JsonValue* albedo = root.Find("albedo")) parseVec3(*albedo, material.albedo);
    if (const JsonValue* metallic = root.Find("metallic")) if (metallic->IsNumber()) material.metallic = static_cast<float>(metallic->AsNumber());
    if (const JsonValue* roughness = root.Find("roughness")) if (roughness->IsNumber()) material.roughness = static_cast<float>(roughness->AsNumber());
    if (const JsonValue* ao = root.Find("ao")) if (ao->IsNumber()) material.ao = static_cast<float>(ao->AsNumber());
    if (const JsonValue* emissive = root.Find("emissive")) parseVec3(*emissive, material.emissive);
    if (const JsonValue* emissiveStrength = root.Find("emissiveStrength")) if (emissiveStrength->IsNumber()) material.emissiveStrength = static_cast<float>(emissiveStrength->AsNumber());
    if (const JsonValue* useAlbedoTexture = root.Find("useAlbedoTexture")) if (useAlbedoTexture->IsBool()) material.useAlbedoTexture = useAlbedoTexture->AsBool();
    if (const JsonValue* albedoTexturePath = root.Find("albedoTexturePath")) if (albedoTexturePath->IsString()) material.albedoTexturePath = albedoTexturePath->AsString();
    if (const JsonValue* useNormalTexture = root.Find("useNormalTexture")) if (useNormalTexture->IsBool()) material.useNormalTexture = useNormalTexture->AsBool();
    if (const JsonValue* normalTexturePath = root.Find("normalTexturePath")) if (normalTexturePath->IsString()) material.normalTexturePath = normalTexturePath->AsString();
    if (const JsonValue* useRoughnessTexture = root.Find("useRoughnessTexture")) if (useRoughnessTexture->IsBool()) material.useRoughnessTexture = useRoughnessTexture->AsBool();
    if (const JsonValue* roughnessTexturePath = root.Find("roughnessTexturePath")) if (roughnessTexturePath->IsString()) material.roughnessTexturePath = roughnessTexturePath->AsString();
    if (const JsonValue* useAoTexture = root.Find("useAoTexture")) if (useAoTexture->IsBool()) material.useAoTexture = useAoTexture->AsBool();
    if (const JsonValue* aoTexturePath = root.Find("aoTexturePath")) if (aoTexturePath->IsString()) material.aoTexturePath = aoTexturePath->AsString();
    if (const JsonValue* useWorldAlignedTexture = root.Find("useWorldAlignedTexture")) if (useWorldAlignedTexture->IsBool()) material.useWorldAlignedTexture = useWorldAlignedTexture->AsBool();
    if (const JsonValue* worldTextureScale = root.Find("worldTextureScale")) if (worldTextureScale->IsNumber()) material.worldTextureScale = static_cast<float>(worldTextureScale->AsNumber());

    // Minimal runtime graph evaluation for WorldAlignedTexture nodes.
    // Use graph data to fill missing texture or WA settings without overwriting valid root values.
    const bool needsTextureFromGraph = (!material.useAlbedoTexture || material.albedoTexturePath.empty());
    const bool needsWorldAlignedFlag = !material.useWorldAlignedTexture;
    if (needsTextureFromGraph || needsWorldAlignedFlag) {
        if (const JsonValue* graph = root.Find("graph")) {
            if (graph->IsObject()) {
                if (const JsonValue* nodes = graph->Find("nodes")) {
                    if (nodes->IsArray()) {
                        for (const auto& nodeValue : nodes->AsArray()) {
                            if (!nodeValue.IsObject()) {
                                continue;
                            }
                            const JsonValue* typeValue = nodeValue.Find("type");
                            if (!typeValue || !typeValue->IsString()) {
                                continue;
                            }
                            if (typeValue->AsString() != "WorldAlignedTexture") {
                                continue;
                            }

                            const JsonValue* resourceValue = nodeValue.Find("resourcePath");
                            if (resourceValue && resourceValue->IsString() && !resourceValue->AsString().empty()) {
                                if (needsTextureFromGraph) {
                                    material.useAlbedoTexture = true;
                                    material.albedoTexturePath = resourceValue->AsString();
                                }
                                if (needsWorldAlignedFlag) {
                                    material.useWorldAlignedTexture = true;
                                }
                                if (needsWorldAlignedFlag || needsTextureFromGraph) {
                                    const JsonValue* scaleValue = nodeValue.Find("worldAlignedScale");
                                    if (scaleValue && scaleValue->IsNumber()) {
                                        material.worldTextureScale = static_cast<float>(scaleValue->AsNumber());
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    outMaterial = std::move(material);
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Scene Auto-Discovery
// ──────────────────────────────────────────────────────────────────────────────

static std::string DiscoverDefaultScene() {
    fs::path scenesDir = g_RuntimeRoot / "assets" / "scenes";
    
    if (!fs::exists(scenesDir)) {
        HP_LOG(Warning, "No assets/scenes directory found");
        return "";
    }

    // Look for .hpscene files
    for (const auto& entry : fs::directory_iterator(scenesDir)) {
        if (entry.path().extension() == ".hpscene") {
            HP_LOG(Info, "Auto-discovered scene: " + entry.path().string());
            return entry.path().string();
        }
    }

    HP_LOG(Warning, "No .hpscene files found in assets/scenes/");
    return "";
}

// ──────────────────────────────────────────────────────────────────────────────
// Vulkan Debug Callback
// ──────────────────────────────────────────────────────────────────────────────

#ifdef _DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void* /*userData*/) {
    
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        HP_LOG(Warning, std::string("[Vulkan] ") + (pData ? pData->pMessage : ""));
    }
    return VK_FALSE;
}
#endif

// ──────────────────────────────────────────────────────────────────────────────
// Command Line Parsing
// ──────────────────────────────────────────────────────────────────────────────

static bool ParseCommandLine(int argc, char* argv[], GameConfig& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--scene" || arg == "-s") {
            if (i + 1 < argc) {
                config.scenePath = argv[++i];
            }
        } else if (arg == "--width" || arg == "-w") {
            if (i + 1 < argc) {
                config.windowWidth = std::stoi(argv[++i]);
            }
        } else if (arg == "--height" || arg == "-h") {
            if (i + 1 < argc) {
                config.windowHeight = std::stoi(argv[++i]);
            }
        } else if (arg == "--fullscreen" || arg == "-f") {
            config.fullscreen = true;
        } else if (arg == "--windowed") {
            config.fullscreen = false;
        } else if (arg == "--title" || arg == "-t") {
            if (i + 1 < argc) {
                config.windowTitle = argv[++i];
            }
        } else if (arg == "--help") {
            std::cout << "Hephaestus Game Runtime\n";
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --scene, -s <path>     Scene file to load\n";
            std::cout << "  --width, -w <pixels>   Window width (default: 1280)\n";
            std::cout << "  --height, -h <pixels>  Window height (default: 720)\n";
            std::cout << "  --fullscreen, -f       Start in fullscreen mode\n";
            std::cout << "  --windowed             Start in windowed mode\n";
            std::cout << "  --title, -t <title>    Window title\n";
            std::cout << "  --help                 Show this help message\n";
            return false;
        }
    }
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Configuration File Loading
// ──────────────────────────────────────────────────────────────────────────────

static void LoadConfigFile(GameConfig& config) {
    fs::path configPath = g_RuntimeRoot / "game.ini";
    
    if (!fs::exists(configPath)) {
        HP_LOG(Debug, "No game.ini found, using defaults");
        return;
    }

    std::ifstream file(configPath);
    if (!file.is_open()) {
        HP_LOG(Warning, "Could not open game.ini");
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Trim whitespace
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(key);
        trim(value);

        if (key == "window_title") {
            config.windowTitle = value;
        } else if (key == "window_width") {
            config.windowWidth = std::stoi(value);
        } else if (key == "window_height") {
            config.windowHeight = std::stoi(value);
        } else if (key == "fullscreen") {
            config.fullscreen = (value == "true" || value == "1");
        } else if (key == "vsync") {
            config.vsync = (value == "true" || value == "1");
        } else if (key == "scene") {
            config.scenePath = value;
        } else if (key == "project_name") {
            config.projectName = value;
        }
    }

    HP_LOG(Info, "Loaded configuration from game.ini");
}

// ──────────────────────────────────────────────────────────────────────────────
// Main Entry Point
// ──────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // ── Bootstrap ─────────────────────────────────
    BootstrapConfig bootstrapConfig;
    bootstrapConfig.applicationName = "HephaestusGame";
    bootstrapConfig.enableCrashHandler = true;
    bootstrapConfig.enableFileLogging = true;
    bootstrapConfig.validateDependencies = true;

    if (!Bootstrap::Initialize(bootstrapConfig)) {
        Bootstrap::ShowFatalError("Startup Error", 
            "Failed to initialize runtime bootstrap.\n"
            "The game cannot start.");
        return -1;
    }

    g_RuntimeRoot = Bootstrap::GetRuntimeRoot();
    HP_LOG(Info, "Runtime root: " + g_RuntimeRoot.string());

    // Change working directory to runtime root so shader paths work correctly
    std::error_code ec;
    fs::current_path(g_RuntimeRoot, ec);
    if (ec) {
        HP_LOG(Warning, "Failed to change working directory to runtime root: " + ec.message());
    } else {
        HP_LOG(Info, "Working directory set to: " + fs::current_path().string());
    }

    // ── Validate Dependencies ─────────────────────
    Bootstrap::BeginStage(InitStage::Bootstrap);
    
#if HEPHAESTUS_GAME_SCRIPTING
    std::vector<std::string> requiredDlls = {"lua.dll"};
#else
    std::vector<std::string> requiredDlls = {};
#endif
    // Note: Vulkan loader is typically system-installed
    std::vector<std::string> optionalDlls = {"vulkan-1.dll"};

    auto depCheck = Bootstrap::ValidateDependencies(requiredDlls, optionalDlls);
    if (!depCheck.success) {
        Bootstrap::ShowFatalError("Missing Dependencies", depCheck.errorMessage);
        Bootstrap::Shutdown();
        return -1;
    }
    
    Bootstrap::EndStage(InitStage::Bootstrap, true);

    // ── Load Configuration ────────────────────────
    LoadConfigFile(g_Config);
    
    // Command line overrides config file
    if (!ParseCommandLine(argc, argv, g_Config)) {
        Bootstrap::Shutdown();
        return 0; // --help was shown
    }

    // Auto-discover scene if not specified
    if (g_Config.scenePath.empty()) {
        g_Config.scenePath = DiscoverDefaultScene();
    }

    if (g_Config.scenePath.empty()) {
        Bootstrap::ShowFatalError("No Scene", 
            "No scene file was specified and no scenes were found.\n"
            "Please ensure your game package contains a scene file in assets/scenes/");
        Bootstrap::Shutdown();
        return -1;
    }

    // ── Platform/Window ───────────────────────────
    Bootstrap::BeginStage(InitStage::Platform);
    
    if (!glfwInit()) {
        Bootstrap::ShowFatalError("Platform Error", "Failed to initialize GLFW");
        Bootstrap::Shutdown();
        return -1;
    }
    
    Bootstrap::EndStage(InitStage::Platform, true);

    Bootstrap::BeginStage(InitStage::Window);
    
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    GLFWmonitor* monitor = nullptr;
    if (g_Config.fullscreen) {
        monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        g_Config.windowWidth = mode->width;
        g_Config.windowHeight = mode->height;
    }

    GLFWwindow* window = glfwCreateWindow(
        g_Config.windowWidth, g_Config.windowHeight,
        g_Config.windowTitle.c_str(), monitor, nullptr);
    
    if (!window) {
        Bootstrap::ShowFatalError("Window Error", "Failed to create game window");
        glfwTerminate();
        Bootstrap::Shutdown();
        return -1;
    }

    glfwSetFramebufferSizeCallback(window, FramebufferResizeCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    
    Bootstrap::EndStage(InitStage::Window, true);

    // ── Vulkan Instance ───────────────────────────
    Bootstrap::BeginStage(InitStage::Renderer);
    
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = g_Config.windowTitle.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Hephaestus Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t extCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + extCount);

#ifdef _DEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

#ifdef __APPLE__
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo instanceCI{};
    instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCI.pApplicationInfo = &appInfo;
    instanceCI.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceCI.ppEnabledExtensionNames = extensions.data();

#ifdef __APPLE__
    instanceCI.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

#ifdef _DEBUG
    const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};
    instanceCI.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    instanceCI.ppEnabledLayerNames = validationLayers.data();
#endif

    VkInstance instance{};
    VkResult result = vkCreateInstance(&instanceCI, nullptr, &instance);
    if (result != VK_SUCCESS) {
        Bootstrap::ShowFatalError("Renderer Error", 
            "Failed to create Vulkan instance.\n"
            "Please ensure your graphics drivers support Vulkan.");
        glfwDestroyWindow(window);
        glfwTerminate();
        Bootstrap::Shutdown();
        return -1;
    }

    // Vulkan device
    VulkanDevice vkdev;
    try {
        vkdev.Init(window, instance);
    } catch (const std::exception& e) {
        Bootstrap::ShowFatalError("Renderer Error", 
            std::string("Failed to initialize Vulkan device:\n") + e.what());
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        Bootstrap::Shutdown();
        return -1;
    }

    // Swapchain
    VulkanSwapchain swap;
    try {
        swap.Init(vkdev.physicalDevice, vkdev.device, vkdev.surface, window, vkdev.allocator);
    } catch (const std::exception& e) {
        Bootstrap::ShowFatalError("Renderer Error",
            std::string("Failed to create swapchain:\n") + e.what());
        vkdev.Cleanup();
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        Bootstrap::Shutdown();
        return -1;
    }

    // Pipeline
    VulkanPipeline pipeline;
    try {
        pipeline.Init(vkdev.device, swap.renderPass, swap.extent);
    } catch (const std::exception& e) {
        Bootstrap::ShowFatalError("Renderer Error",
            std::string("Failed to create graphics pipeline:\n") + e.what());
        swap.Cleanup();
        vkdev.Cleanup();
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        Bootstrap::Shutdown();
        return -1;
    }

    // Skybox
    VulkanSkybox skybox;
    try {
        skybox.Init(vkdev.device, swap.renderPass, swap.extent, vkdev.allocator,
                    vkdev.physicalDevice, vkdev.commandPool, vkdev.graphicsQueue,
                    static_cast<uint32_t>(swap.images.size()),
                    g_RuntimeRoot.string());
    } catch (const std::exception& e) {
        HP_LOG(Warning, std::string("Skybox initialization warning: ") + e.what());
        // Non-fatal, game can continue without skybox
    }
    
    // Shadow map
    if (!g_DirectionalShadow.Create(vkdev)) {
        HP_LOG(Warning, "Shadow map creation failed - shadows will be disabled");
    }
    
    Bootstrap::EndStage(InitStage::Renderer, true);

    // ── Scene Loading (early - needed by physics and scripting) ──────
    Bootstrap::BeginStage(InitStage::SceneLoad);
    
    Scene scene;
    
    // Resolve scene path (relative to runtime root if not absolute)
    fs::path scenePath = g_Config.scenePath;
    if (scenePath.is_relative()) {
        scenePath = g_RuntimeRoot / scenePath;
    }

    HP_LOG(Info, "Loading scene: " + scenePath.string());

    std::string loadError;
    if (!SceneSerializer::Deserialize(scene, scenePath.string(), loadError)) {
        Bootstrap::ShowFatalError("Scene Load Error",
            "Failed to load scene:\n" + scenePath.string() + "\n\nError: " + loadError);
        // Cleanup and exit
        pipeline.Cleanup();
        swap.Cleanup();
        vkdev.Cleanup();
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        Bootstrap::Shutdown();
        return -1;
    }

    HP_LOG(Info, "Scene loaded successfully: " + scene.GetName());
    
    // Load HDRI from scene's EnvironmentComponent
    auto envView = scene.GetEntitiesWith<EnvironmentComponent>();
    for (auto entityHandle : envView) {
        Entity envEntity(entityHandle, &scene);
        auto& env = envEntity.GetComponent<EnvironmentComponent>();
        
        if (env.type == EnvironmentComponent::BackgroundType::HDRI && !env.hdriPath.empty()) {
            fs::path hdriPath = env.hdriPath;
            if (hdriPath.is_relative()) {
                hdriPath = g_RuntimeRoot / hdriPath;
            }
            
            if (fs::exists(hdriPath)) {
                HP_LOG(Info, "Loading HDRI: " + hdriPath.string());
                if (skybox.LoadHDRI(hdriPath.string(), vkdev.allocator, vkdev.physicalDevice,
                                    vkdev.commandPool, vkdev.graphicsQueue)) {
                    HP_LOG(Info, "HDRI loaded successfully");
                } else {
                    HP_LOG(Warning, "Failed to load HDRI: " + hdriPath.string());
                }
            } else {
                HP_LOG(Warning, "HDRI file not found: " + hdriPath.string());
            }
        }
        break; // Only use first environment component
    }
    
    Bootstrap::EndStage(InitStage::SceneLoad, true);

    // ── Physics ───────────────────────────────────
    Bootstrap::BeginStage(InitStage::Physics);
    
    Physics::JoltPhysicsEngine joltPhysicsEngine;
    joltPhysicsEngine.Initialize();
    
    Physics::JoltPhysicsSystem physicsSystem(joltPhysicsEngine);
    physicsSystem.SetProjectRoot(g_RuntimeRoot);
    
    Bootstrap::EndStage(InitStage::Physics, true);

    // ── Scripting ─────────────────────────────────
#if HEPHAESTUS_GAME_SCRIPTING
    Bootstrap::BeginStage(InitStage::Scripting);
    
    LuaScriptEngine luaEngine;
    try {
        luaEngine.Initialize(&scene, window);
        HP_LOG(Info, "Lua scripting engine initialized");
    } catch (const std::exception& e) {
        HP_LOG(Error, std::string("Failed to initialize Lua: ") + e.what());
        // Continue without scripting
    }

    // Provide Jolt physics callbacks to Lua scripts (required for fp-controller)
    PhysicsBinding physicsBinding{};
    physicsBinding.hasBody = [&scene, &joltPhysicsEngine, &physicsSystem](uint32_t entityId) -> bool {
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!scene.GetRegistry().valid(handle)) {
            return false;
        }
        return !physicsSystem.GetBodyID(handle).IsInvalid();
    };

    physicsBinding.getLinearVelocity = [&scene, &joltPhysicsEngine, &physicsSystem](
                                           uint32_t entityId, Vec3& outVelocity) -> bool {
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!scene.GetRegistry().valid(handle)) {
            return false;
        }
        JPH::BodyID bodyId = physicsSystem.GetBodyID(handle);
        if (bodyId.IsInvalid()) {
            return false;
        }
        JPH::Vec3 vel = joltPhysicsEngine.GetBodyInterface().GetLinearVelocity(bodyId);
        outVelocity = Vec3(vel.GetX(), vel.GetY(), vel.GetZ());
        return true;
    };

    physicsBinding.setLinearVelocity = [&scene, &joltPhysicsEngine, &physicsSystem](
                                           uint32_t entityId, const Vec3& velocity, bool activate) -> bool {
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!scene.GetRegistry().valid(handle)) {
            return false;
        }
        JPH::BodyID bodyId = physicsSystem.GetBodyID(handle);
        if (bodyId.IsInvalid()) {
            return false;
        }
        joltPhysicsEngine.GetBodyInterface().SetLinearVelocity(
            bodyId, JPH::Vec3(velocity.x, velocity.y, velocity.z));
        if (activate) {
            joltPhysicsEngine.GetBodyInterface().ActivateBody(bodyId);
        }
        return true;
    };

    physicsBinding.setAngularVelocity = [&scene, &joltPhysicsEngine, &physicsSystem](
                                            uint32_t entityId, const Vec3& velocity, bool activate) -> bool {
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!scene.GetRegistry().valid(handle)) {
            return false;
        }
        JPH::BodyID bodyId = physicsSystem.GetBodyID(handle);
        if (bodyId.IsInvalid()) {
            return false;
        }
        joltPhysicsEngine.GetBodyInterface().SetAngularVelocity(
            bodyId, JPH::Vec3(velocity.x, velocity.y, velocity.z));
        if (activate) {
            joltPhysicsEngine.GetBodyInterface().ActivateBody(bodyId);
        }
        return true;
    };

    physicsBinding.setRotation = [&scene, &joltPhysicsEngine, &physicsSystem](
                                    uint32_t entityId, const Vec3& rotationEuler, bool activate) -> bool {
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!scene.GetRegistry().valid(handle)) {
            return false;
        }
        JPH::BodyID bodyId = physicsSystem.GetBodyID(handle);
        if (bodyId.IsInvalid()) {
            return false;
        }

        JPH::Quat qx = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), rotationEuler.x);
        JPH::Quat qy = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), rotationEuler.y);
        JPH::Quat qz = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), rotationEuler.z);
        JPH::Quat rot = qx * qy * qz;

        JPH::BodyInterface& bi = joltPhysicsEngine.GetBodyInterface();
        JPH::RVec3 pos = bi.GetCenterOfMassPosition(bodyId);
        bi.SetPositionAndRotation(bodyId, pos, rot,
                                  activate ? JPH::EActivation::Activate
                                           : JPH::EActivation::DontActivate);
        return true;
    };

    physicsBinding.raycast = [&scene, &joltPhysicsEngine, &physicsSystem](
                                uint32_t entityId, const Vec3& origin, const Vec3& direction, float maxDistance,
                                PhysicsRaycastHit& outHit) -> bool {
        entt::entity handle = static_cast<entt::entity>(entityId);
        JPH::BodyID ignoreBody = physicsSystem.GetBodyID(handle);

        Vec3 dir = direction;
        float lenSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
        if (lenSq < 1e-8f || maxDistance <= 0.0f) {
            return false;
        }
        float invLen = 1.0f / std::sqrt(lenSq);
        dir.x *= invLen;
        dir.y *= invLen;
        dir.z *= invLen;

        JPH::Vec3 start(origin.x, origin.y, origin.z);
        JPH::Vec3 end(origin.x + dir.x * maxDistance,
                      origin.y + dir.y * maxDistance,
                      origin.z + dir.z * maxDistance);
        JPH::RRayCast ray(JPH::RVec3(start), JPH::Vec3(end - start));

#ifdef RayCastResult
#undef RayCastResult
#endif

        JPH::RayCastResult rayCastResult;
        auto& query = joltPhysicsEngine.GetPhysicsSystem()->GetNarrowPhaseQuery();

        bool hit = false;
        if (!ignoreBody.IsInvalid()) {
            JPH::IgnoreSingleBodyFilter filter(ignoreBody);
            hit = query.CastRay(ray, rayCastResult, {}, {}, filter);
        } else {
            hit = query.CastRay(ray, rayCastResult);
        }

        if (!hit) {
            outHit.hit = false;
            return true;
        }

        outHit.hit = true;
        outHit.distance = rayCastResult.mFraction * maxDistance;
        outHit.point = Vec3(origin.x + dir.x * outHit.distance,
                            origin.y + dir.y * outHit.distance,
                            origin.z + dir.z * outHit.distance);
        outHit.normal = Vec3(0.0f, 1.0f, 0.0f);
        return true;
    };

    luaEngine.SetPhysicsBinding(std::move(physicsBinding));
    
    Bootstrap::EndStage(InitStage::Scripting, true);
#endif

    // ── Initialize Scene Systems ──────────────────
    // GameCamera will be set up later, but we need the pointer for binding
    GameCamera gameCamera;
    g_GameCameraPtr = &gameCamera;

#if HEPHAESTUS_GAME_SCRIPTING
    // Set up camera binding so Lua scripts can control the camera
    CameraBinding cameraBinding{};
    cameraBinding.setActive = [](bool /*active*/) {
        // In game runtime, camera is always active
    };
    cameraBinding.isActive = []() {
        return true;
    };
    cameraBinding.setPosition = [](const Vec3& pos) {
        if (g_GameCameraPtr) {
            g_GameCameraPtr->position = pos;
        }
    };
    cameraBinding.getPosition = []() {
        if (g_GameCameraPtr) {
            return g_GameCameraPtr->position;
        }
        return Vec3(0.0f, 0.0f, 0.0f);
    };
    cameraBinding.setRotation = [](const Vec3& rot) {
        if (g_GameCameraPtr) {
            // rot.x = pitch (radians), rot.y = yaw (radians)
            g_GameCameraPtr->pitchDeg = rot.x * 180.0f / 3.14159265f;
            g_GameCameraPtr->yawDeg = rot.y * 180.0f / 3.14159265f;
        }
    };
    cameraBinding.getRotation = []() {
        if (g_GameCameraPtr) {
            return Vec3(
                g_GameCameraPtr->pitchDeg * 3.14159265f / 180.0f,
                g_GameCameraPtr->yawDeg * 3.14159265f / 180.0f,
                0.0f
            );
        }
        return Vec3(0.0f, 0.0f, 0.0f);
    };
    cameraBinding.translate = [](const Vec3& delta) {
        if (g_GameCameraPtr) {
            g_GameCameraPtr->position = vadd(g_GameCameraPtr->position, delta);
        }
    };
    luaEngine.SetCameraBinding(std::move(cameraBinding));
    
    // Hydrate script components
    luaEngine.OnSimulationStarted();
#endif

    // ── Setup Rendering Infrastructure ────────────
    HP_LOG(Info, "Setting up rendering infrastructure...");
    
    size_t imageCount = swap.images.size();
    const size_t uboSize = sizeof(UniformBufferObject);
    const size_t materialUboSize = sizeof(MaterialUBO);
    const size_t lightingUboSize = sizeof(LightingUBO);
    const uint32_t materialStride = 256; // Aligned material size
    const uint32_t maxMaterialsPerFrame = 256;
    
    // Create uniform buffers
    std::vector<VulkanBuffer> uniformBuffers(imageCount);
    std::vector<VulkanBuffer> materialBuffers(imageCount);
    std::vector<VulkanBuffer> lightingBuffers(imageCount);
    
    for (size_t i = 0; i < imageCount; ++i) {
        uniformBuffers[i].Create(vkdev.allocator, uboSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        materialBuffers[i].Create(vkdev.allocator, materialStride * maxMaterialsPerFrame,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        lightingBuffers[i].Create(vkdev.allocator, lightingUboSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    
    // Create descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(imageCount * 2)},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, static_cast<uint32_t>(imageCount)},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(imageCount * 129)} // 128 textures + 1 shadow map
    };
    
    VkDescriptorPool descriptorPool{};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(imageCount);
    vkCreateDescriptorPool(vkdev.device, &poolInfo, nullptr, &descriptorPool);
    
    // Allocate descriptor sets
    std::vector<VkDescriptorSet> descriptorSets(imageCount);
    std::vector<VkDescriptorSetLayout> layouts(imageCount, pipeline.descriptorSetLayout);
    VkDescriptorSetAllocateInfo descAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    descAllocInfo.descriptorPool = descriptorPool;
    descAllocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    descAllocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(vkdev.device, &descAllocInfo, descriptorSets.data());
    
    // Create a default white texture for materials
    VulkanTexture defaultTexture;
    {
        uint32_t white = 0xFFFFFFFF;
        VkDeviceSize texSize = 4;
        
        VulkanBuffer stagingBuf;
        stagingBuf.Create(vkdev.allocator, texSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        void* mapped;
        vmaMapMemory(vkdev.allocator, stagingBuf.allocation, &mapped);
        memcpy(mapped, &white, texSize);
        vmaUnmapMemory(vkdev.allocator, stagingBuf.allocation);
        
        VkImageCreateInfo imgInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.extent = {1, 1, 1};
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        VmaAllocationCreateInfo allocCI{};
        allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateImage(vkdev.allocator, &imgInfo, &allocCI, &defaultTexture.image, &defaultTexture.allocation, nullptr);
        
        // Transition and copy
        VkCommandBuffer cmd;
        VkCommandBufferAllocateInfo cmdAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmdAlloc.commandPool = vkdev.commandPool;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = 1;
        vkAllocateCommandBuffers(vkdev.device, &cmdAlloc, &cmd);
        
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);
        
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.image = defaultTexture.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {1, 1, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuf.buffer, defaultTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        
        vkEndCommandBuffer(cmd);
        
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(vkdev.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(vkdev.graphicsQueue);
        
        vkFreeCommandBuffers(vkdev.device, vkdev.commandPool, 1, &cmd);
        stagingBuf.Destroy(vkdev.allocator);
        
        // Create image view
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = defaultTexture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(vkdev.device, &viewInfo, nullptr, &defaultTexture.imageView);
        
        // Create sampler
        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.maxLod = 1.0f;
        vkCreateSampler(vkdev.device, &samplerInfo, nullptr, &defaultTexture.sampler);
    }

    // Build texture library (slot 0 is default white)
    g_TextureLibrary.clear();
    g_TextureLookup.clear();
    g_TextureLibrary.reserve(128); // Reserve to prevent reallocation
    g_TextureLibrary.push_back(std::move(defaultTexture));
    g_TextureLookup["__default__"] = 0;

    // Load material assets referenced by mesh slots
    g_MaterialLibrary.clear();
    {
        auto meshView = scene.GetEntitiesWith<MeshRendererComponent>();
        for (auto entityHandle : meshView) {
            Entity entity(entityHandle, &scene);
            auto& renderer = entity.GetComponent<MeshRendererComponent>();
            
            for (const auto& slotPath : renderer.appliedMaterialPaths) {
                if (slotPath.empty()) {
                    continue;
                }
                std::string key = NormalizeAssetPath(slotPath);
                if (key.empty() || g_MaterialLibrary.count(key) > 0) {
                    continue;
                }
                MaterialComponent material;
                if (LoadMaterialAsset(slotPath, material)) {
                    g_MaterialLibrary.emplace(key, std::move(material));
                    HP_LOG(Info, "Loaded material asset: " + key);
                } else {
                    HP_LOG(Warning, "Failed to load material asset: " + key);
                }
            }
        }
    }

    // Load textures referenced by materials
    HP_LOG(Info, "Loading material textures...");
    auto loadTexture = [&](const std::string& path, const char* label) -> uint32_t {
        if (path.empty()) {
            return 0;
        }
        std::string key = NormalizeAssetPath(path);
        if (key.empty()) {
            return 0;
        }
        auto it = g_TextureLookup.find(key);
        if (it != g_TextureLookup.end()) {
            return it->second;
        }
        if (g_TextureLibrary.size() >= 128) {
            HP_LOG(Warning, std::string("Texture limit reached, skipping: ") + key);
            return 0;
        }
        if (!fs::exists(key)) {
            HP_LOG(Warning, std::string("Texture not found: ") + key);
            return 0;
        }

        VulkanTexture tex;
        if (!tex.LoadFromFile(vkdev.allocator, vkdev.device, vkdev.physicalDevice,
                              vkdev.commandPool, vkdev.graphicsQueue, key, true)) {
            HP_LOG(Warning, std::string("Failed to load ") + label + " texture: " + key);
            return 0;
        }

        uint32_t idx = static_cast<uint32_t>(g_TextureLibrary.size());
        g_TextureLibrary.push_back(std::move(tex));
        g_TextureLookup[key] = idx;
        HP_LOG(Info, std::string("Loaded ") + label + " texture: " + key);
        return idx;
    };

    auto materialView = scene.GetEntitiesWith<MaterialComponent>();
    for (auto entityHandle : materialView) {
        Entity entity(entityHandle, &scene);
        auto& mat = entity.GetComponent<MaterialComponent>();
        if (mat.useAlbedoTexture && !mat.albedoTexturePath.empty()) {
            loadTexture(mat.albedoTexturePath, "Albedo");
        }
        if (mat.useNormalTexture && !mat.normalTexturePath.empty()) {
            loadTexture(mat.normalTexturePath, "Normal");
        }
        if (mat.useRoughnessTexture && !mat.roughnessTexturePath.empty()) {
            loadTexture(mat.roughnessTexturePath, "Roughness");
        }
        if (mat.useAoTexture && !mat.aoTexturePath.empty()) {
            loadTexture(mat.aoTexturePath, "AO");
        }
    }

    for (const auto& entry : g_MaterialLibrary) {
        const MaterialComponent& mat = entry.second;
        if (mat.useAlbedoTexture && !mat.albedoTexturePath.empty()) {
            loadTexture(mat.albedoTexturePath, "Albedo");
        }
        if (mat.useNormalTexture && !mat.normalTexturePath.empty()) {
            loadTexture(mat.normalTexturePath, "Normal");
        }
        if (mat.useRoughnessTexture && !mat.roughnessTexturePath.empty()) {
            loadTexture(mat.roughnessTexturePath, "Roughness");
        }
        if (mat.useAoTexture && !mat.aoTexturePath.empty()) {
            loadTexture(mat.aoTexturePath, "AO");
        }
    }
    
    // Update descriptor sets
    for (size_t i = 0; i < imageCount; ++i) {
        VkDescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = uniformBuffers[i].buffer;
        cameraInfo.offset = 0;
        cameraInfo.range = uboSize;
        
        VkDescriptorBufferInfo materialInfo{};
        materialInfo.buffer = materialBuffers[i].buffer;
        materialInfo.offset = 0;
        materialInfo.range = materialUboSize;
        
        VkDescriptorBufferInfo lightingInfo{};
        lightingInfo.buffer = lightingBuffers[i].buffer;
        lightingInfo.offset = 0;
        lightingInfo.range = lightingUboSize;
        
        // Fill texture array with loaded textures, fallback to default (slot 0)
        std::vector<VkDescriptorImageInfo> texInfos(128);
        for (size_t t = 0; t < texInfos.size(); ++t) {
            const VulkanTexture& tex = (t < g_TextureLibrary.size())
                ? g_TextureLibrary[t]
                : g_TextureLibrary[0]; // Use index 0 (default white) as fallback
            texInfos[t].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            texInfos[t].imageView = tex.imageView;
            texInfos[t].sampler = tex.sampler;
        }
        
        // Shadow map - use actual shadow map if available, else fallback to default texture
        VkDescriptorImageInfo shadowMapInfo{};
        if (g_DirectionalShadow.IsValid()) {
            shadowMapInfo = g_DirectionalShadow.descriptor;
        } else {
            shadowMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            shadowMapInfo.imageView = g_TextureLibrary[0].imageView;
            shadowMapInfo.sampler = g_TextureLibrary[0].sampler;
        }
        
        std::array<VkWriteDescriptorSet, 5> writes{};
        
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cameraInfo;
        
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &materialInfo;
        
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &lightingInfo;
        
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = descriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = static_cast<uint32_t>(texInfos.size());
        writes[3].pImageInfo = texInfos.data();
        
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = descriptorSets[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &shadowMapInfo;
        
        vkUpdateDescriptorSets(vkdev.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    
    // ── Generate Builtin Primitive Meshes ─────────────────────────
    HP_LOG(Info, "Generating builtin primitive meshes...");
    
    // Cube mesh data (24 vertices with proper normals per face)
    const std::vector<Vertex> cubeVertices = {
        // Front face (Z+)
        {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // Back face (Z-)
        {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // Right face (X+)
        {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // Left face (X-)
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // Top face (Y+)
        {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // Bottom face (Y-)
        {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
    };
    const std::vector<uint32_t> cubeIndices = {
        0, 1, 2, 2, 3, 0,       // Front
        4, 5, 6, 6, 7, 4,       // Back
        8, 9, 10, 10, 11, 8,    // Right
        12, 13, 14, 14, 15, 12, // Left
        16, 17, 18, 18, 19, 16, // Top
        20, 21, 22, 22, 23, 20  // Bottom
    };
    
    MeshData cubeMeshData;
    cubeMeshData.vertices = cubeVertices;
    cubeMeshData.indices = cubeIndices;
    cubeMeshData.bounds = AABB(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));
    
    size_t builtinCubeHandle = LoadMeshToGPU(cubeMeshData, "builtin://cube", vkdev.allocator,
        vkdev.device, vkdev.commandPool, vkdev.graphicsQueue);
    if (builtinCubeHandle != SIZE_MAX) {
        HP_LOG(Info, "  Loaded builtin cube mesh at handle " + std::to_string(builtinCubeHandle));
    }
    
    // Sphere mesh generation
    auto generateSphereMesh = [](float radius, uint32_t rings, uint32_t segments) {
        MeshData data;
        const float pi = 3.14159265359f;
        
        for (uint32_t ring = 0; ring <= rings; ++ring) {
            float phi = pi * float(ring) / float(rings);
            float y = radius * std::cos(phi);
            float ringRadius = radius * std::sin(phi);
            
            for (uint32_t seg = 0; seg <= segments; ++seg) {
                float theta = 2.0f * pi * float(seg) / float(segments);
                float x = ringRadius * std::cos(theta);
                float z = ringRadius * std::sin(theta);
                
                Vertex v{};
                v.pos[0] = x; v.pos[1] = y; v.pos[2] = z;
                float len = std::sqrt(x*x + y*y + z*z);
                v.normal[0] = x/len; v.normal[1] = y/len; v.normal[2] = z/len;
                v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
                v.texCoord[0] = float(seg) / float(segments);
                v.texCoord[1] = float(ring) / float(rings);
                data.vertices.push_back(v);
            }
        }
        
        uint32_t stride = segments + 1;
        for (uint32_t ring = 0; ring < rings; ++ring) {
            for (uint32_t seg = 0; seg < segments; ++seg) {
                uint32_t i0 = ring * stride + seg;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + stride;
                uint32_t i3 = i2 + 1;
                data.indices.push_back(i0);
                data.indices.push_back(i2);
                data.indices.push_back(i1);
                data.indices.push_back(i1);
                data.indices.push_back(i2);
                data.indices.push_back(i3);
            }
        }
        data.bounds = AABB(Vec3(-radius, -radius, -radius), Vec3(radius, radius, radius));
        return data;
    };
    
    MeshData sphereMeshData = generateSphereMesh(0.5f, 24, 16);
    size_t builtinSphereHandle = LoadMeshToGPU(sphereMeshData, "builtin://sphere", vkdev.allocator,
        vkdev.device, vkdev.commandPool, vkdev.graphicsQueue);
    if (builtinSphereHandle != SIZE_MAX) {
        HP_LOG(Info, "  Loaded builtin sphere mesh at handle " + std::to_string(builtinSphereHandle));
    }
    
    // ── Load Scene Meshes ─────────────────────────
    HP_LOG(Info, "Loading scene meshes...");
    
    try {
        auto meshView = scene.GetEntitiesWith<MeshRendererComponent>();
        int meshEntityCount = 0;
        int loadedCount = 0;
        
        for (auto entityHandle : meshView) {
            meshEntityCount++;
            
            try {
                Entity entity(entityHandle, &scene);
                
                // Get entity name for better logging
                std::string entityName = "Entity_" + std::to_string(meshEntityCount);
                if (entity.HasComponent<TagComponent>()) {
                    entityName = entity.GetComponent<TagComponent>().tag;
                }
                
                auto& renderer = entity.GetComponent<MeshRendererComponent>();
                
                // Skip entities without mesh asset paths
                if (renderer.meshAssetPath.empty()) {
                    HP_LOG(Info, "  Skipping " + entityName + " (no mesh path)");
                    continue;
                }
                
                // Handle builtin:// meshes by assigning pre-generated handles
                if (renderer.meshAssetPath.rfind("builtin://", 0) == 0) {
                    std::string builtinKey = renderer.meshAssetPath;
                    // Normalize to lowercase for comparison
                    std::transform(builtinKey.begin(), builtinKey.end(), builtinKey.begin(), ::tolower);
                    
                    if (g_MeshLookup.count(builtinKey) > 0) {
                        renderer.meshHandle = g_MeshLookup[builtinKey];
                        HP_LOG(Info, "  " + entityName + " using builtin mesh: " + renderer.meshAssetPath + " (handle " + std::to_string(renderer.meshHandle) + ")");
                        loadedCount++;
                    } else {
                        HP_LOG(Warning, "  " + entityName + " references unknown builtin mesh: " + renderer.meshAssetPath);
                    }
                    continue;
                }
                
                // Skip .hpscene files (scene references, not meshes)
                if (renderer.meshAssetPath.find(".hpscene") != std::string::npos) {
                    HP_LOG(Info, "  Skipping " + entityName + " (scene reference, not a mesh)");
                    continue;
                }
                
                std::string meshKey = renderer.meshAssetPath;
                HP_LOG(Info, "  Processing " + entityName + " -> " + meshKey);
                
                // Check if already loaded
                if (g_MeshLookup.count(meshKey) > 0) {
                    size_t cachedIdx = g_MeshLookup[meshKey];
                    renderer.meshHandle = cachedIdx;
                    
                    // Sync submesh count from cached mesh
                    const MeshGPU& cachedMesh = g_MeshLibrary[cachedIdx];
                    int cachedSubmeshCount = cachedMesh.submeshes.empty() ? 1 : static_cast<int>(cachedMesh.submeshes.size());
                    if (renderer.submeshCount != cachedSubmeshCount) {
                        HP_LOG(Info, "    Syncing submeshCount from cached mesh: " + std::to_string(renderer.submeshCount) + " -> " + std::to_string(cachedSubmeshCount));
                        renderer.submeshCount = cachedSubmeshCount;
                    }
                    
                    HP_LOG(Info, "    Reusing cached mesh (submeshes=" + std::to_string(cachedMesh.submeshes.size()) + 
                        ", materialSlots=" + std::to_string(renderer.appliedMaterialPaths.size()) + ")");
                    loadedCount++;
                    continue;
                }
                
                // Load mesh from file
                fs::path meshPath = renderer.meshAssetPath;
                if (meshPath.is_relative()) {
                    meshPath = g_RuntimeRoot / meshPath;
                }
                
                if (!fs::exists(meshPath)) {
                    HP_LOG(Warning, "    Mesh not found: " + meshPath.string());
                    continue;
                }
                
                MeshData meshData;
                std::string importError;
                HP_LOG(Info, "    Loading from: " + meshPath.string());
                
                if (ModelImporter::LoadModel(meshPath, meshData, importError)) {
                    HP_LOG(Info, "    Model imported, uploading to GPU...");
                    size_t idx = LoadMeshToGPU(meshData, meshKey, vkdev.allocator, 
                        vkdev.device, vkdev.commandPool, vkdev.graphicsQueue);
                    if (idx != SIZE_MAX) {
                        renderer.meshHandle = idx;
                        
                        // Update submesh count from the loaded mesh data
                        const MeshGPU& loadedMesh = g_MeshLibrary[idx];
                        int newSubmeshCount = loadedMesh.submeshes.empty() ? 1 : static_cast<int>(loadedMesh.submeshes.size());
                        if (renderer.submeshCount != newSubmeshCount) {
                            HP_LOG(Info, "    Updating submeshCount: " + std::to_string(renderer.submeshCount) + " -> " + std::to_string(newSubmeshCount));
                            renderer.submeshCount = newSubmeshCount;
                        }
                        
                        // Log submesh/material debug info
                        HP_LOG(Info, "    Loaded mesh: " + meshKey + " (submeshes=" + std::to_string(loadedMesh.submeshes.size()) + 
                            ", materialSlots=" + std::to_string(renderer.appliedMaterialPaths.size()) + ")");
                        
                        // Warn if submesh count doesn't match material slot count
                        if (!loadedMesh.submeshes.empty() && 
                            renderer.appliedMaterialPaths.size() != loadedMesh.submeshes.size()) {
                            HP_LOG(Warning, "    Mesh '" + meshKey + "' has " + std::to_string(loadedMesh.submeshes.size()) + 
                                " submeshes but " + std::to_string(renderer.appliedMaterialPaths.size()) + 
                                " material paths assigned. Some sections may use fallback materials.");
                        }
                        
                        loadedCount++;
                    } else {
                        HP_LOG(Warning, "    GPU upload failed for: " + meshKey);
                    }
                } else {
                    HP_LOG(Warning, "    Failed to load mesh " + meshPath.string() + ": " + importError);
                }
            } catch (const std::exception& e) {
                HP_LOG(Error, "  Exception processing entity " + std::to_string(meshEntityCount) + ": " + std::string(e.what()));
            } catch (...) {
                HP_LOG(Error, "  Unknown exception processing entity " + std::to_string(meshEntityCount));
            }
        }
        
        HP_LOG(Info, "Mesh loading complete. Processed " + std::to_string(meshEntityCount) + " entities, loaded " + std::to_string(loadedCount) + " meshes");
    } catch (const std::exception& e) {
        HP_LOG(Error, "Exception during mesh loading: " + std::string(e.what()));
    } catch (...) {
        HP_LOG(Error, "Unknown exception during mesh loading");
    }
    
    HP_LOG(Info, "Loaded " + std::to_string(g_MeshLibrary.size()) + " unique meshes to GPU");
    
    // ── Find Active Camera ────────────────────────
    HP_LOG(Info, "Finding active camera...");
    // gameCamera already declared earlier for Lua binding
    try {
        auto cameraView = scene.GetEntitiesWith<CameraComponent, TransformComponent>();
        bool foundCamera = false;
        
        for (auto entityHandle : cameraView) {
            Entity entity(entityHandle, &scene);
            auto& camComp = entity.GetComponent<CameraComponent>();
            auto& transform = entity.GetComponent<TransformComponent>();
            
            if (camComp.isActive || camComp.isPrimary) {
                gameCamera.position = transform.position;
                
                // Extract rotation from transform
                Vec3 rot = transform.rotation;
                gameCamera.pitchDeg = rot.x * 180.0f / 3.14159265f;
                gameCamera.yawDeg = rot.y * 180.0f / 3.14159265f;
                gameCamera.fovDeg = camComp.fov;
                gameCamera.nearPlane = camComp.nearPlane;
                gameCamera.farPlane = camComp.farPlane;
                
                std::string entityName = "Unknown";
                if (entity.HasComponent<TagComponent>()) {
                    entityName = entity.GetComponent<TagComponent>().tag;
                }
                HP_LOG(Info, "Using camera from entity: " + entityName);
                foundCamera = true;
                break;
            }
        }
        
        if (!foundCamera) {
            HP_LOG(Warning, "No active camera found, using defaults");
        }
    } catch (const std::exception& e) {
        HP_LOG(Error, "Exception finding camera: " + std::string(e.what()));
    } catch (...) {
        HP_LOG(Error, "Unknown exception finding camera");
    }

    Bootstrap::BeginStage(InitStage::Ready);
    HP_LOG(Info, "Game runtime ready!");
    Bootstrap::EndStage(InitStage::Ready, true);

    // ── Debug: Multi-Material Mesh Validation ─────
#ifdef _DEBUG
    HP_LOG(Info, "=== Multi-Material Mesh Debug Info ===");
    {
        auto debugMeshView = scene.GetEntitiesWith<MeshRendererComponent>();
        for (auto entityHandle : debugMeshView) {
            Entity entity(entityHandle, &scene);
            auto& renderer = entity.GetComponent<MeshRendererComponent>();
            
            if (!renderer.HasValidMesh() || renderer.meshHandle >= g_MeshLibrary.size()) {
                continue;
            }
            
            std::string entityName = "Entity";
            if (entity.HasComponent<TagComponent>()) {
                entityName = entity.GetComponent<TagComponent>().tag;
            }
            
            const MeshGPU& mesh = g_MeshLibrary[renderer.meshHandle];
            size_t submeshCount = mesh.submeshes.empty() ? 1 : mesh.submeshes.size();
            size_t materialSlotCount = renderer.appliedMaterialPaths.size();
            
            HP_LOG(Info, "Entity: " + entityName);
            HP_LOG(Info, "  Mesh: " + mesh.name + " (handle=" + std::to_string(renderer.meshHandle) + ")");
            HP_LOG(Info, "  Submeshes: " + std::to_string(submeshCount) + ", Material slots: " + std::to_string(materialSlotCount));
            
            // Log each submesh and its material binding
            if (mesh.submeshes.empty()) {
                std::string matPath = materialSlotCount > 0 ? renderer.appliedMaterialPaths[0] : "";
                std::string matStatus = "none";
                if (!matPath.empty()) {
                    std::string key = NormalizeAssetPath(matPath);
                    matStatus = g_MaterialLibrary.count(key) > 0 ? "loaded" : "NOT FOUND";
                }
                HP_LOG(Info, "  [Single mesh] Material: " + (matPath.empty() ? "(default)" : matPath) + " [" + matStatus + "]");
            } else {
                for (size_t i = 0; i < mesh.submeshes.size(); ++i) {
                    const auto& sub = mesh.submeshes[i];
                    std::string matPath = "";
                    std::string matStatus = "default";
                    
                    if (static_cast<size_t>(sub.materialSlot) < materialSlotCount) {
                        matPath = renderer.appliedMaterialPaths[sub.materialSlot];
                        if (!matPath.empty()) {
                            std::string key = NormalizeAssetPath(matPath);
                            matStatus = g_MaterialLibrary.count(key) > 0 ? "loaded" : "NOT FOUND";
                        }
                    } else {
                        matStatus = "slot missing";
                    }
                    
                    HP_LOG(Info, "  Submesh[" + std::to_string(i) + "] slot=" + std::to_string(sub.materialSlot) + 
                        ", indices=" + std::to_string(sub.indexOffset) + "-" + std::to_string(sub.indexOffset + sub.indexCount) +
                        ", material=" + (matPath.empty() ? "(default)" : matPath) + " [" + matStatus + "]");
                }
            }
            
            // Warn about mismatches
            if (submeshCount > materialSlotCount && materialSlotCount > 0) {
                HP_LOG(Warning, "  WARNING: More submeshes (" + std::to_string(submeshCount) + 
                    ") than material slots (" + std::to_string(materialSlotCount) + 
                    "). Some sections will use fallback material.");
            }
        }
    }
    HP_LOG(Info, "=== End Multi-Material Debug Info ===");
#endif

    // ── Sync Objects ──────────────────────────────
    HP_LOG(Info, "Creating sync objects...");
    const int MAX_FRAMES_IN_FLIGHT = 2;
    
    std::vector<VkCommandBuffer> cmdBufs(swap.framebuffers.size());
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = vkdev.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(cmdBufs.size());
    
    if (vkAllocateCommandBuffers(vkdev.device, &allocInfo, cmdBufs.data()) != VK_SUCCESS) {
        HP_LOG(Error, "Failed to allocate command buffers");
        return 1;
    }

    std::vector<VkSemaphore> imageAvailable(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkSemaphore> renderFinished(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkFence> inFlight(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceCI{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(vkdev.device, &semCI, nullptr, &imageAvailable[i]) != VK_SUCCESS ||
            vkCreateSemaphore(vkdev.device, &semCI, nullptr, &renderFinished[i]) != VK_SUCCESS ||
            vkCreateFence(vkdev.device, &fenceCI, nullptr, &inFlight[i]) != VK_SUCCESS) {
            HP_LOG(Error, "Failed to create synchronization objects");
            return 1;
        }
    }
    HP_LOG(Info, "Sync objects created successfully");

    uint32_t currentFrame = 0;
    auto lastTime = std::chrono::high_resolution_clock::now();

    // ── Main Loop ─────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Calculate delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // Update physics
        physicsSystem.Update(scene, deltaTime);

        // Update scripts
#if HEPHAESTUS_GAME_SCRIPTING
        luaEngine.Update(scene, deltaTime);
#endif

        // Wait for previous frame
        vkWaitForFences(vkdev.device, 1, &inFlight[currentFrame], VK_TRUE, UINT64_MAX);

        // Acquire next image
        uint32_t imageIndex;
        VkResult acquireResult = vkAcquireNextImageKHR(
            vkdev.device, swap.swapchain, UINT64_MAX,
            imageAvailable[currentFrame], VK_NULL_HANDLE, &imageIndex);

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || g_FramebufferResized) {
            g_FramebufferResized = false;
            // Recreate swapchain
            vkDeviceWaitIdle(vkdev.device);
            swap.Cleanup();
            swap.Init(vkdev.physicalDevice, vkdev.device, vkdev.surface, window, vkdev.allocator);
            pipeline.Cleanup();
            pipeline.Init(vkdev.device, swap.renderPass, swap.extent);
            continue;
        }

        vkResetFences(vkdev.device, 1, &inFlight[currentFrame]);

        // Record command buffer
        vkResetCommandBuffer(cmdBufs[imageIndex], 0);
        
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmdBufs[imageIndex], &beginInfo);

        // Gather lighting from scene (before render passes)
        LightingUBO lightingUBO{};
        lightingUBO.dirLightViewProj = Mat4::Identity();
        lightingUBO.dirShadowParams = Vec4(0.005f, 1.0f, 0.001f, 0.0f); // shadows disabled by default
        
        // Default directional light
        lightingUBO.dirLightDirIntensity = Vec4(-0.3f, -0.8f, -0.5f, 1.5f);
        lightingUBO.dirLightColor = Vec4(1.0f, 0.98f, 0.95f, 1.0f);
        lightingUBO.ambientColorIntensity = Vec4(0.4f, 0.45f, 0.5f, 0.4f);
        lightingUBO.pointLightCount = 0;
        lightingUBO.spotLightCount = 0;
        
        // Shadow state
        bool dirShadowEnabled = false;
        Mat4 dirShadowMatrix = Mat4::Identity();
        
        // Gather lights from scene
        // First, try to find lights with TransformComponent
        auto lightView = scene.GetEntitiesWith<LightComponent, TransformComponent>();
        int lightCount = 0;
        for (auto entityHandle : lightView) {
            lightCount++;
            Entity lightEntity(entityHandle, &scene);
            auto& light = lightEntity.GetComponent<LightComponent>();
            auto& lightTransform = lightEntity.GetComponent<TransformComponent>();
            
            std::string lightName = lightEntity.HasComponent<TagComponent>() 
                ? lightEntity.GetComponent<TagComponent>().tag : "Unnamed";
            
            if (light.type == LightType::Directional) {
                HP_LOG(Info, "Found directional light: " + lightName + 
                    " intensity=" + std::to_string(light.intensity) +
                    " castShadows=" + std::to_string(light.castShadows) +
                    " shadowBias=" + std::to_string(light.shadowBias) +
                    " shadowStrength=" + std::to_string(light.shadowStrength) +
                    " shadowDistance=" + std::to_string(light.shadowDistance) +
                    " shadowExtent=" + std::to_string(light.shadowExtent));
                
                // Transform direction by entity rotation (light points in local -Z)
                Mat4 transformMatrix = lightTransform.GetLocalMatrix();
                Vec3 dir = Mat4::TransformDirection(transformMatrix, v3(0.0f, 0.0f, -1.0f));
                if (vlen(dir) < 0.0001f) {
                    dir = v3(0.0f, -1.0f, 0.0f);
                }
                dir = vnorm(dir);
                
                lightingUBO.dirLightDirIntensity = Vec4(dir.x, dir.y, dir.z, light.intensity);
                lightingUBO.dirLightColor = Vec4(light.color.x, light.color.y, light.color.z, 1.0f);
                
                // Shadow mapping setup
                if (light.castShadows && g_DirectionalShadow.IsValid()) {
                    HP_LOG(Info, "Shadow mapping enabled for directional light");
                    float shadowDistance = light.shadowDistance;
                    float shadowExtent = light.shadowExtent;
                    
                    Vec3 sceneCenter = v3(0.0f, 0.0f, 0.0f);
                    Vec3 towardLight = vscale(dir, -1.0f);
                    Vec3 lightPos = vadd(sceneCenter, vscale(towardLight, shadowDistance));
                    
                    Vec3 up = v3(0.0f, 1.0f, 0.0f);
                    if (std::abs(vdot(dir, up)) > 0.99f) {
                        up = v3(1.0f, 0.0f, 0.0f);
                    }
                    
                    Mat4 lightViewMat = Mat4::LookAt(lightPos, sceneCenter, up);
                    float shadowFar = shadowDistance * 3.0f;
                    Mat4 lightProj = Mat4::OrthographicShadow(-shadowExtent, shadowExtent, -shadowExtent, shadowExtent, 0.1f, shadowFar);
                    
                    dirShadowMatrix = Mat4::Multiply(lightProj, lightViewMat);
                    dirShadowEnabled = true;
                    
                    float shadowTexelSize = 1.0f / static_cast<float>(g_DirectionalShadow.resolution);
                    lightingUBO.dirShadowParams = Vec4(light.shadowBias, light.shadowStrength, shadowTexelSize, 1.0f);
                    lightingUBO.dirLightViewProj = dirShadowMatrix;
                }
            } else if (light.type == LightType::Point && lightingUBO.pointLightCount < 16) {
                HP_LOG(Info, "Found point light: " + lightName + " at (" + 
                    std::to_string(lightTransform.position.x) + ", " + 
                    std::to_string(lightTransform.position.y) + ", " + 
                    std::to_string(lightTransform.position.z) + ")");
                int idx = lightingUBO.pointLightCount;
                Vec3 pos = lightTransform.position;
                lightingUBO.pointLightPositions[idx] = Vec4(pos.x, pos.y, pos.z, light.range);
                // Shader expects: colors.w = intensity (shader multiplies color * intensity)
                lightingUBO.pointLightColors[idx] = Vec4(light.color.x, light.color.y, light.color.z, light.intensity);
                lightingUBO.pointLightAttenuation[idx] = Vec4(light.constant, light.linear, light.quadratic, 0.0f);
                lightingUBO.pointLightCount++;
            } else if (light.type == LightType::Spot && lightingUBO.spotLightCount < 16) {
                HP_LOG(Info, "Found spotlight: " + lightName + " at (" + 
                    std::to_string(lightTransform.position.x) + ", " + 
                    std::to_string(lightTransform.position.y) + ", " + 
                    std::to_string(lightTransform.position.z) + ")");
                int idx = lightingUBO.spotLightCount;
                Vec3 pos = lightTransform.position;
                lightingUBO.spotLightPositions[idx] = Vec4(pos.x, pos.y, pos.z, light.range);
                
                // Transform direction by entity rotation (spotlight points in local -Z)
                Mat4 transformMatrix = lightTransform.GetLocalMatrix();
                Vec3 spotDirection = Mat4::TransformDirection(transformMatrix, v3(0.0f, 0.0f, -1.0f));
                spotDirection = vnorm(spotDirection);
                
                // Shader expects: directions.w = innerAngle (raw), colors.w = intensity, attenuation.w = outerAngle (raw)
                lightingUBO.spotLightDirections[idx] = Vec4(spotDirection.x, spotDirection.y, spotDirection.z, 
                    light.innerConeAngle);
                lightingUBO.spotLightColors[idx] = Vec4(light.color.x, light.color.y, light.color.z, light.intensity);
                lightingUBO.spotLightAttenuation[idx] = Vec4(light.constant, light.linear, light.quadratic, light.outerConeAngle);
                lightingUBO.spotLightCount++;
            }
        }
        
        // Log light summary once at startup
        static bool lightSummaryLogged = false;
        if (!lightSummaryLogged) {
            HP_LOG(Info, "Light summary: " + std::to_string(lightCount) + " total lights, " +
                std::to_string(lightingUBO.pointLightCount) + " point, " +
                std::to_string(lightingUBO.spotLightCount) + " spot");
            lightSummaryLogged = true;
        }
        
        // ── Shadow Pass ──────────────────────────────
        auto renderView = scene.GetEntitiesWith<MeshRendererComponent, TransformComponent>();
        
        if (dirShadowEnabled && g_DirectionalShadow.IsValid()) {
            // Transition shadow map to attachment layout
            VkImageMemoryBarrier preBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            preBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            preBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            preBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            preBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            preBarrier.image = g_DirectionalShadow.image;
            preBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            preBarrier.subresourceRange.levelCount = 1;
            preBarrier.subresourceRange.layerCount = 1;
            preBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            preBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(cmdBufs[imageIndex],
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                0, 0, nullptr, 0, nullptr, 1, &preBarrier);

            VkClearValue depthClear{};
            depthClear.depthStencil = {1.0f, 0};
            VkRenderPassBeginInfo shadowBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            shadowBegin.renderPass = g_DirectionalShadow.renderPass;
            shadowBegin.framebuffer = g_DirectionalShadow.framebuffer;
            shadowBegin.renderArea.offset = {0, 0};
            shadowBegin.renderArea.extent = {g_DirectionalShadow.resolution, g_DirectionalShadow.resolution};
            shadowBegin.clearValueCount = 1;
            shadowBegin.pClearValues = &depthClear;

            vkCmdBeginRenderPass(cmdBufs[imageIndex], &shadowBegin, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport shadowViewport{};
            shadowViewport.width = static_cast<float>(g_DirectionalShadow.resolution);
            shadowViewport.height = static_cast<float>(g_DirectionalShadow.resolution);
            shadowViewport.minDepth = 0.0f;
            shadowViewport.maxDepth = 1.0f;
            VkRect2D shadowScissor{{0, 0}, {g_DirectionalShadow.resolution, g_DirectionalShadow.resolution}};
            vkCmdSetViewport(cmdBufs[imageIndex], 0, 1, &shadowViewport);
            vkCmdSetScissor(cmdBufs[imageIndex], 0, 1, &shadowScissor);

            vkCmdBindPipeline(cmdBufs[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, g_DirectionalShadow.pipeline);

            ShadowPushConstants shadowPC{};
            shadowPC.lightViewProj = dirShadowMatrix;

            VkBuffer shadowVertexBuffer = VK_NULL_HANDLE;
            VkBuffer shadowIndexBuffer = VK_NULL_HANDLE;

            for (auto entityHandle : renderView) {
                Entity entity(entityHandle, &scene);
                auto& renderer = entity.GetComponent<MeshRendererComponent>();
                auto& transform = entity.GetComponent<TransformComponent>();

                if (!renderer.visible || renderer.hideInGame) continue;
                if (!renderer.HasValidMesh() || renderer.meshHandle >= g_MeshLibrary.size()) continue;

                MeshGPU& mesh = g_MeshLibrary[renderer.meshHandle];
                if (mesh.indexCount == 0 || mesh.vertexBuffer.buffer == VK_NULL_HANDLE) continue;

                if (mesh.vertexBuffer.buffer != shadowVertexBuffer) {
                    VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(cmdBufs[imageIndex], 0, 1, &mesh.vertexBuffer.buffer, &offset);
                    shadowVertexBuffer = mesh.vertexBuffer.buffer;
                }

                if (mesh.indexBuffer.buffer != shadowIndexBuffer) {
                    vkCmdBindIndexBuffer(cmdBufs[imageIndex], mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                    shadowIndexBuffer = mesh.indexBuffer.buffer;
                }

                shadowPC.model = scene.GetWorldTransform(entity);
                vkCmdPushConstants(cmdBufs[imageIndex], g_DirectionalShadow.pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants), &shadowPC);
                vkCmdDrawIndexed(cmdBufs[imageIndex], mesh.indexCount, 1, 0, 0, 0);
            }

            vkCmdEndRenderPass(cmdBufs[imageIndex]);
            // Render pass transitions to READ_ONLY_OPTIMAL
        }

        // ── Main Render Pass ─────────────────────────
        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = swap.renderPass;
        rpBegin.framebuffer = swap.framebuffers[imageIndex];
        rpBegin.renderArea.offset = {0, 0};
        rpBegin.renderArea.extent = swap.extent;

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.02f, 0.02f, 0.03f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpBegin.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmdBufs[imageIndex], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        // Set viewport and scissor
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swap.extent.width);
        viewport.height = static_cast<float>(swap.extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBufs[imageIndex], 0, 1, &viewport);
        
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swap.extent;
        vkCmdSetScissor(cmdBufs[imageIndex], 0, 1, &scissor);
        
        // Update camera UBO
        UniformBufferObject cameraUBO = gameCamera.MakeUBO(swap.extent);
        void* cameraData;
        vmaMapMemory(vkdev.allocator, uniformBuffers[imageIndex].allocation, &cameraData);
        memcpy(cameraData, &cameraUBO, sizeof(cameraUBO));
        vmaUnmapMemory(vkdev.allocator, uniformBuffers[imageIndex].allocation);
        
        // Update lighting UBO
        void* lightingData;
        vmaMapMemory(vkdev.allocator, lightingBuffers[imageIndex].allocation, &lightingData);
        memcpy(lightingData, &lightingUBO, sizeof(lightingUBO));
        vmaUnmapMemory(vkdev.allocator, lightingBuffers[imageIndex].allocation);
        
        // Bind default pipeline (may be overridden per-entity based on backface setting)
        VkPipeline currentPipeline = VK_NULL_HANDLE;
        
        // Render all mesh entities
        VkBuffer lastVertexBuffer = VK_NULL_HANDLE;
        VkBuffer lastIndexBuffer = VK_NULL_HANDLE;
        uint32_t materialIndex = 0;
        
        for (auto entityHandle : renderView) {
            Entity entity(entityHandle, &scene);
            auto& renderer = entity.GetComponent<MeshRendererComponent>();
            auto& transform = entity.GetComponent<TransformComponent>();
            
            if (!renderer.visible) continue;
            if (renderer.hideInGame) continue;
            if (!renderer.HasValidMesh() || renderer.meshHandle >= g_MeshLibrary.size()) continue;
            
            MeshGPU& mesh = g_MeshLibrary[renderer.meshHandle];
            if (mesh.indexCount == 0 || mesh.vertexBuffer.buffer == VK_NULL_HANDLE) continue;

            VkPipeline desiredPipeline = renderer.backfaceCulling
                ? pipeline.pipelineBackface
                : pipeline.pipeline;
            if (currentPipeline != desiredPipeline) {
                vkCmdBindPipeline(cmdBufs[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, desiredPipeline);
                currentPipeline = desiredPipeline;
            }
            
            // Bind vertex buffer if changed
            if (mesh.vertexBuffer.buffer != lastVertexBuffer) {
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmdBufs[imageIndex], 0, 1, &mesh.vertexBuffer.buffer, &offset);
                lastVertexBuffer = mesh.vertexBuffer.buffer;
            }
            
            // Bind index buffer if changed
            if (mesh.indexBuffer.buffer != lastIndexBuffer) {
                vkCmdBindIndexBuffer(cmdBufs[imageIndex], mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                lastIndexBuffer = mesh.indexBuffer.buffer;
            }
            
            auto resolveMaterialForSlot = [&](int slot) -> const MaterialComponent* {
                if (slot < 0) {
                    slot = 0;
                }
                
                // Try to get material from appliedMaterialPaths
                if (static_cast<size_t>(slot) < renderer.appliedMaterialPaths.size()) {
                    const std::string& slotPath = renderer.appliedMaterialPaths[slot];
                    if (!slotPath.empty()) {
                        std::string key = NormalizeAssetPath(slotPath);
                        auto it = g_MaterialLibrary.find(key);
                        if (it != g_MaterialLibrary.end()) {
                            return &it->second;
                        }
                    }
                }
                
                // Fallback to entity's MaterialComponent (slot 0 only)
                if (slot == 0 && entity.HasComponent<MaterialComponent>()) {
                    return &entity.GetComponent<MaterialComponent>();
                }
                return nullptr;
            };

            auto buildMaterialUBO = [&](const MaterialComponent* mat, MaterialUBO& matUBO) {
                if (!mat) {
                    return;
                }
                matUBO.albedo = mat->albedo;
                matUBO.metallic = mat->metallic;
                matUBO.roughness = mat->roughness;
                matUBO.emissive = mat->emissive;
                matUBO.emissiveStrength = mat->emissiveStrength;
                matUBO.ao = mat->ao;

                if (mat->useAlbedoTexture && !mat->albedoTexturePath.empty()) {
                    std::string key = NormalizeAssetPath(mat->albedoTexturePath);
                    auto it = g_TextureLookup.find(key);
                    if (it != g_TextureLookup.end()) {
                        matUBO.useAlbedoTexture = 1;
                        matUBO.albedoTextureIndex = it->second;
                    }
                }
                if (mat->useNormalTexture && !mat->normalTexturePath.empty()) {
                    std::string key = NormalizeAssetPath(mat->normalTexturePath);
                    auto it = g_TextureLookup.find(key);
                    if (it != g_TextureLookup.end()) {
                        matUBO.useNormalTexture = 1;
                        matUBO.normalTextureIndex = it->second;
                    }
                }
                if (mat->useRoughnessTexture && !mat->roughnessTexturePath.empty()) {
                    std::string key = NormalizeAssetPath(mat->roughnessTexturePath);
                    auto it = g_TextureLookup.find(key);
                    if (it != g_TextureLookup.end()) {
                        matUBO.useRoughnessTexture = 1;
                        matUBO.roughnessTextureIndex = it->second;
                    }
                }
                if (mat->useAoTexture && !mat->aoTexturePath.empty()) {
                    std::string key = NormalizeAssetPath(mat->aoTexturePath);
                    auto it = g_TextureLookup.find(key);
                    if (it != g_TextureLookup.end()) {
                        matUBO.useAoTexture = 1;
                        matUBO.aoTextureIndex = it->second;
                    }
                }

                matUBO.useWorldAlignedTexture = mat->useWorldAlignedTexture ? 1 : 0;
                matUBO.worldTextureScale = mat->worldTextureScale;
            };

            auto drawWithMaterial = [&](const MaterialComponent* mat, uint32_t indexOffset, uint32_t indexCount) {
                MaterialUBO matUBO{};
                buildMaterialUBO(mat, matUBO);

                void* matData;
                vmaMapMemory(vkdev.allocator, materialBuffers[imageIndex].allocation, &matData);
                memcpy(static_cast<char*>(matData) + materialIndex * materialStride, &matUBO, sizeof(matUBO));
                vmaUnmapMemory(vkdev.allocator, materialBuffers[imageIndex].allocation);

                uint32_t dynamicOffset = materialIndex * materialStride;
                vkCmdBindDescriptorSets(cmdBufs[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline.layout, 0, 1, &descriptorSets[imageIndex], 1, &dynamicOffset);

                vkCmdDrawIndexed(cmdBufs[imageIndex], indexCount, 1, indexOffset, 0, 0);
                materialIndex = (materialIndex + 1) % maxMaterialsPerFrame;
            };

            // Push model matrix
            Mat4 modelMatrix = scene.GetWorldTransform(entity);
            vkCmdPushConstants(cmdBufs[imageIndex], pipeline.layout,
                VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), modelMatrix.m);

            if (mesh.submeshes.empty()) {
                drawWithMaterial(resolveMaterialForSlot(0), 0, mesh.indexCount);
            } else {
                for (const auto& submesh : mesh.submeshes) {
                    if (submesh.indexCount == 0) {
                        continue;
                    }
                    drawWithMaterial(resolveMaterialForSlot(submesh.materialSlot),
                        submesh.indexOffset, submesh.indexCount);
                }
            }
        }

        // Render skybox (after meshes, uses depth test to only draw where nothing else rendered)
        if (skybox.hasHDRI) {
            skybox.UpdateDescriptorSet(imageIndex, uniformBuffers[imageIndex].buffer);
            skybox.Draw(cmdBufs[imageIndex], imageIndex);
        }

        vkCmdEndRenderPass(cmdBufs[imageIndex]);
        vkEndCommandBuffer(cmdBufs[imageIndex]);

        // Submit
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable[currentFrame];
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBufs[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished[currentFrame];

        vkQueueSubmit(vkdev.graphicsQueue, 1, &submitInfo, inFlight[currentFrame]);

        // Present
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished[currentFrame];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swap.swapchain;
        presentInfo.pImageIndices = &imageIndex;

        vkQueuePresentKHR(vkdev.graphicsQueue, &presentInfo);

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    // ── Cleanup ───────────────────────────────────
    HP_LOG(Info, "Shutting down...");
    
    vkDeviceWaitIdle(vkdev.device);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(vkdev.device, imageAvailable[i], nullptr);
        vkDestroySemaphore(vkdev.device, renderFinished[i], nullptr);
        vkDestroyFence(vkdev.device, inFlight[i], nullptr);
    }
    
    // Cleanup meshes
    for (auto& mesh : g_MeshLibrary) {
        mesh.Destroy(vkdev.allocator);
    }
    g_MeshLibrary.clear();
    g_MeshLookup.clear();
    
    // Cleanup textures
    for (auto& tex : g_TextureLibrary) {
        tex.Destroy(vkdev.allocator, vkdev.device);
    }
    g_TextureLibrary.clear();
    g_TextureLookup.clear();
    
    // Cleanup uniform buffers
    for (size_t i = 0; i < imageCount; ++i) {
        uniformBuffers[i].Destroy(vkdev.allocator);
        materialBuffers[i].Destroy(vkdev.allocator);
        lightingBuffers[i].Destroy(vkdev.allocator);
    }
    
    // Cleanup descriptor pool
    vkDestroyDescriptorPool(vkdev.device, descriptorPool, nullptr);

    // Cleanup shadow map
    g_DirectionalShadow.Destroy();
    
    skybox.Cleanup(vkdev.allocator);
    pipeline.Cleanup();
    swap.Cleanup();
    vkdev.Cleanup();
    vkDestroyInstance(instance, nullptr);
    
    glfwDestroyWindow(window);
    glfwTerminate();

    Bootstrap::Shutdown();
    
    return 0;
}
