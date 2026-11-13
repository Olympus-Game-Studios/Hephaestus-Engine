/**
 * @file EngineFrameRenderer.cpp
 * @brief Implementation of the shared frame rendering system.
 */

#include "EngineFrameRenderer.h"
#include "../Assets/MeshSettings.h"
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "VulkanPipeline.h"
#include "Vertex.h"
#include "../Assets/MeshAsset.h"
#include "../Scene/Scene.h"
#include "../Scene/Components.h"
#include "../Scene/ActiveCameraSync.h"
#include "../Scene/EditorIcons.h"
#include "../UI/HephaestusUI/Rendering/VulkanHUIRenderer.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <cstring>
#include <cmath>
#include <fstream>
#include <array>
#include <algorithm>
#include <deque>
#include <filesystem>
#include <optional>
#include <cassert>

namespace Hephaestus {

// ============================================================================
// UBO Structures (must match main.cpp and shader layouts)
// ============================================================================

struct alignas(16) UniformBufferObject {
    Mat4 view;
    Mat4 proj;
    Vec3 viewPos;
    float shadingMode; // 0 = Lit, 1 = Unlit
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
    uint32_t useMetallicTexture;
    uint32_t metallicTextureIndex;
    uint32_t useAoTexture;
    uint32_t aoTextureIndex;
    uint32_t useWorldAlignedTexture;
    float worldTextureScale;
    float reflectionStrength;
    float specular;
    float opacity;
    float opacityMask;
    float refraction;
    uint32_t blendMode;
    uint32_t textureChannels; // roughness, metallic, AO map channel, 2 bits each
};

constexpr VkDeviceSize kBoneUboSize = 128 * sizeof(Mat4);
constexpr float kLightIconHalfExtent = 0.045f; // fraction of the viewport half-height

// Mixamo-style characters often have the pivot at the hips. Lift the mesh so
// feet sit on the entity origin without moving the physics capsule. Use the
// current pose (Idle/Walk) because bind-pose bounds miss planted feet.
static Mat4 CharacterAwareModelMatrix(Scene* scene, Entity entity, const MeshRendererComponent& mesh)
{
    Mat4 model = scene->GetWorldTransform(entity);
    if (!mesh.runtimeGenerated) {
        return model;
    }
    float minY = mesh.localBounds.min.y;
    if (entity.HasComponent<CharacterComponent>()) {
        const CharacterComponent& character = entity.GetComponent<CharacterComponent>();
        if (character.runtimeReady) {
            for (const Mat4& pose : character.worldPose) {
                minY = std::min(minY, pose.m[13]);
            }
        }
    }
    if (minY >= -1.0e-3f) {
        return model;
    }
    return Mat4::Multiply(model, Mat4::Translation(Vec3(0.0f, -minY, 0.0f)));
}

static VkShaderModule LoadSpvModule(VkDevice device, const std::string& name) {
    const std::string candidates[] = {
        "shaders/" + name,
        "../shaders/" + name,
        "build/Release/shaders/" + name,
        "../build/Release/shaders/" + name
    };
    for (const auto& path : candidates) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) continue;
        const size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> code(fileSize);
        file.seekg(0);
        file.read(code.data(), static_cast<std::streamsize>(fileSize));
        file.close();
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        return module;
    }
    return VK_NULL_HANDLE;
}

struct ToneMapPush {
    uint32_t outputIsSrgb;
    float exposure;
    float contrast;
    float saturation;
    float colorFilter[4];
    float viewportSize[2];
    float vignette;
    float grain;
    float scanlines;
    float time;
    float bloomIntensity;
    float lensRadius;
    float lensDistortion;
    float lensAberration;
    float lensEdge;
    float lensRing;
    float lensOpacity;
    float lensReticle;
};
static_assert(sizeof(ToneMapPush) == 88, "ToneMapPush must match tonemap.frag");

struct BloomPush {
    float sourceTexel[2];
    float threshold;
    float knee;
    float radius;
    uint32_t mode;
};
static_assert(sizeof(BloomPush) == 24, "BloomPush must match bloom.comp");
constexpr uint32_t kBloomMaxMips = 6;

// Inverse of tonemap.frag, so the display-referred clear color shows unchanged after tone mapping.
static float DisplayToSceneLinear(float display) {
    const float toned = std::min(std::pow(std::clamp(display, 0.0f, 1.0f), 2.2f), 0.999f);
    return toned / (1.0f - toned);
}

static void DestroyPipeline(VkDevice device, VkPipeline& pipeline) {
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
}

static void DestroyPipelineLayout(VkDevice device, VkPipelineLayout& layout) {
    if (layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, layout, nullptr);
        layout = VK_NULL_HANDLE;
    }
}

struct LightingUBO {
    Mat4 dirLightViewProj;
    Vec4 dirShadowParams;
    Vec4 dirLightDirIntensity;
    Vec4 dirLightColor;
    Vec4 ambientColorIntensity;
    Vec4 environmentReflectionParams;
    
    struct { float x, y, z, range; } pointLightPositions[16];
    struct { float r, g, b, intensity; } pointLightColors[16];
    struct { float constant, linear, quadratic, unused; } pointLightAttenuation[16];
    struct { float x, y, z, range; } spotLightPositions[16];
    struct { float x, y, z, innerAngle; } spotLightDirections[16];
    struct { float r, g, b, intensity; } spotLightColors[16];
    struct { float constant, linear, quadratic, outerAngle; } spotLightAttenuation[16];
    
    int pointLightCount;
    int spotLightCount;
    int cascadeCount;
    float csmPadding1;
    
    // CSM cascade data (appended to preserve existing layout offsets)
    Mat4 cascadeViewProj[4];
    Vec4 cascadeSplits;  // view-space far plane for each cascade
    Vec4 skyIrradianceSH[9];
    Vec4 cascadeTexelWorld;  // world-space size of one shadow texel per cascade
    Vec4 cascadeDepthRange;  // light-space depth range per cascade, to turn world biases into depth
    Vec4 shadowBiasWorld;    // x = extra depth bias, y = extra normal offset (metres), z = tan(sun half angle), w = 1 when ray traced
    Mat4 localShadowViewProj[32];  // atlas tiles, see EngineFrameRenderer::kLocalShadowTile
    Vec4 pointShadowParams[16];    // x = first of six tiles (+X,-X,+Y,-Y,+Z,-Z), -1 unshadowed or -2 ray traced, y = shadow strength, z = far, w = tan(half fov)
    Vec4 spotShadowParams[16];     // x = tile, -1 unshadowed or -2 ray traced, y = shadow strength, z = far, w = tan(half fov)
};

static bool MeshAllowsHiddenEditorOutline(const MeshRendererComponent& renderer,
                                          const FrameRenderConfig& config)
{
    return !renderer.visible
        && renderer.showHiddenOutline
        && config.isEditorMode
        && !config.isPlaying;
}

static bool ContainsEntityId(const std::vector<uint32_t>& ids, uint32_t id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// editorHiddenEntities is sorted (EditorContext::EditorHiddenIdsForRender) and can hold most of a scene while isolating.
static bool ContainsSortedEntityId(const std::vector<uint32_t>& ids, uint32_t id)
{
    return std::binary_search(ids.begin(), ids.end(), id);
}

static std::vector<size_t> MeshHandlesForDraw(const MeshRendererComponent& renderer)
{
    return RuntimeMeshPartHandles(renderer);
}

static bool IsEditorEntityVisible(uint32_t entityId,
                                  const FrameRenderConfig& config)
{
    if (!config.isEditorMode || config.isPlaying) {
        return true;
    }
    if (ContainsSortedEntityId(config.editorHiddenEntities, entityId)) {
        return false;
    }
    return config.editorIsolatedEntities.empty() ||
           ContainsEntityId(config.editorIsolatedEntities, entityId);
}

constexpr const char* kEditorCameraMeshKey = ".engine/meshes/Camera.glb";
constexpr float kEditorCameraMeshScale = 0.3f; // Camera.glb is about a metre long

// Cameras that show the built-in camera mesh in the editor, posed by their view (the mesh's lens is +X). Cameras
// made before this carry the mesh as their own hidden-in-game MeshRenderer and are left to draw it themselves.
static std::vector<std::pair<entt::entity, Mat4>> EditorCameraMeshes(Scene& scene, const FrameRenderConfig& config)
{
    std::vector<std::pair<entt::entity, Mat4>> out;
    if (!config.isEditorMode || config.isPlaying || !config.renderBillboards) {
        return out;
    }
    auto view = scene.GetRegistry().view<TransformComponent, CameraComponent>();
    for (auto handle : view) {
        if (!view.get<CameraComponent>(handle).showEditorMesh ||
            !IsEditorEntityVisible(static_cast<uint32_t>(handle), config)) {
            continue;
        }
        if (const auto* own = scene.GetRegistry().try_get<MeshRendererComponent>(handle);
            own && own->meshAssetPath == kEditorCameraMeshKey) {
            continue;
        }
        Hephaestus::ActiveCameraView cameraView;
        if (Hephaestus::TryMakeCameraView(scene, Entity(handle, &scene), cameraView)) {
            const Mat4 frame = Hephaestus::MakeCameraViewFrame(cameraView.position, cameraView.yawRadians,
                                                               cameraView.pitchRadians, cameraView.rollRadians);
            out.emplace_back(handle, Mat4::Multiply(frame, Mat4::Scale(Vec3(kEditorCameraMeshScale, kEditorCameraMeshScale,
                                                                            kEditorCameraMeshScale))));
        }
    }
    return out;
}

static bool MeshIncludedInIdPick(const MeshRendererComponent& renderer,
                                 const FrameRenderConfig& config,
                                 uint32_t entityId)
{
    if (!IsEditorEntityVisible(entityId, config) ||
        ContainsEntityId(config.editorLockedEntities, entityId)) {
        return false;
    }
    if (config.isPlaying && renderer.hideInGame) {
        return false;
    }
    if (renderer.visible) {
        return true;
    }
    return MeshAllowsHiddenEditorOutline(renderer, config);
}

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

bool EngineFrameRenderer::SupportsWireframe() const
{
    return m_pipeline && m_pipeline->HasWireframePipeline();
}

// ============================================================================
// EngineFrameRenderer Implementation
// ============================================================================

EngineFrameRenderer::EngineFrameRenderer() = default;

EngineFrameRenderer::~EngineFrameRenderer()
{
    Shutdown();
}

static bool FindShadowCastingSun(Scene* scene, Vec3& outDirection);

bool EngineFrameRenderer::Init(VulkanDevice* device, VulkanSwapchain* swapchain, bool editorOverlays)
{
    if (m_initialized) {
        std::cerr << "[EngineFrameRenderer] Already initialized\n";
        return false;
    }
    m_editorOverlays = editorOverlays;
    
    if (!device || !swapchain) {
        std::cerr << "[EngineFrameRenderer] Invalid device or swapchain\n";
        return false;
    }
    
    m_device = device;
    m_swapchain = swapchain;
    m_deviceLost = false;

    if (!m_probeGI.Init(m_device)) {
        std::cerr << "[EngineFrameRenderer] Failed to create probe GI layouts\n";
        return false;
    }
    
    // Create render resources
    if (!createPipeline()) {
        std::cerr << "[EngineFrameRenderer] Failed to create pipeline\n";
        return false;
    }
    if (!createSkinnedBoneLayout()) {
        std::cerr << "[EngineFrameRenderer] Warning: Failed to create skinned bone layout\n";
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
    
    // Initialize default material texture slot (slot 0) using the dummy texture
    {
        MaterialTextureSlot defaultSlot;
        defaultSlot.texture = {}; // Not owned, the dummy texture is managed separately
        defaultSlot.descriptor.sampler = m_dummyTexture.sampler;
        defaultSlot.descriptor.imageView = m_dummyTexture.imageView;
        defaultSlot.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        defaultSlot.key = "__default__";
        defaultSlot.valid = false; // Do not destroy this slot's texture (it's the dummy)
        m_materialTextureSlots.push_back(std::move(defaultSlot));
        m_defaultMaterialTextureSlot = 0;
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
    if (m_directionalShadow.IsValid() && !createSkinnedShadowPipeline()) {
        std::cerr << "[EngineFrameRenderer] Warning: Failed to create skinned shadow pipeline\n";
    }
    if (m_directionalShadow.IsValid() && !createLocalShadowResources()) {
        std::cerr << "[EngineFrameRenderer] Warning: Failed to create point/spot shadow atlas (those lights will be unshadowed)\n";
    }
    
    if (!createDescriptorSets()) {
        std::cerr << "[EngineFrameRenderer] Failed to create descriptor sets\n";
        return false;
    }
    if (!createSkinnedMainPipelines()) {
        std::cerr << "[EngineFrameRenderer] Warning: Failed to create skinned mesh pipelines\n";
    }
    if (!createToneMapResources()) {
        std::cerr << "[EngineFrameRenderer] Failed to create tone map resources\n";
        return false;
    }
    if (m_directionalShadow.IsValid() && !m_probeGI.InitSunMap(m_directionalShadow.renderPass)) {
        std::cerr << "[EngineFrameRenderer] Warning: Failed to create the probe sun map (probes see the sun unoccluded)\n";
    }
    if (!m_probeGI.CreateSwapchainResources(m_swapchain->renderPass, VulkanSwapchain::kSceneSubpass,
                                            m_pipeline->descriptorSetLayout,
                                            static_cast<uint32_t>(m_swapchain->images.size()))) {
        std::cerr << "[EngineFrameRenderer] Failed to create probe GI resources\n";
        return false;
    }

    // Editor overlay resources are created only for hosts that request them at Init.
    if (m_editorOverlays) {
        if (!createGridResources()) {
            std::cerr << "[EngineFrameRenderer] Warning: Failed to create grid resources (grid will be disabled)\n";
        }
        if (!createDebugOverlayResources()) {
            std::cerr << "[EngineFrameRenderer] Warning: Failed to create debug overlay resources\n";
        }

        // Create outline resources (optional, failure doesn't prevent init)
        if (!createOutlineResources()) {
            std::cerr << "[EngineFrameRenderer] Warning: Failed to create outline resources (outlines will be disabled)\n";
        } else if (!createSkinnedOutlinePipelines()) {
            std::cerr << "[EngineFrameRenderer] Warning: Failed to create skinned outline pipelines\n";
        }
    
        // Create ID picker resources (optional, failure doesn't prevent init)
        if (!createIdPickerResources()) {
            std::cerr << "[EngineFrameRenderer] Warning: Failed to create ID picker resources (picking will be disabled)\n";
        } else if (!createSkinnedIdPipelines()) {
            std::cerr << "[EngineFrameRenderer] Warning: Failed to create skinned ID pipelines\n";
        }

        // Create gizmo resources (optional, failure doesn't prevent init)
        if (!createGizmoResources()) {
            std::cerr << "[EngineFrameRenderer] Warning: Failed to create gizmo resources (gizmo will be disabled)\n";
        }

        // Create directional light gizmo resources (optional)
        if (!createLightGizmoResources()) {
            std::cerr << "[EngineFrameRenderer] Warning: Failed to create light gizmo resources (light gizmos will be disabled)\n";
        }

        // Create billboard resources (optional, failure doesn't prevent init)
        if (!createBillboardResources()) {
            std::cerr << "[EngineFrameRenderer] Warning: Failed to create billboard resources (light icons will be disabled)\n";
        }
    }

    // Create skybox resources (optional, failure doesn't prevent init)
    if (!createSkyboxResources()) {
        std::cerr << "[EngineFrameRenderer] Warning: Failed to create skybox resources (skybox will be disabled)\n";
    }

    // Create the runtime HUI overlay backend (used by packaged games; dormant
    // in the editor where Qt paints the HUI overlay).
    if (!createUIResources()) {
        std::cerr << "[EngineFrameRenderer] Warning: Failed to create HUI overlay resources (runtime UI will be disabled)\n";
    }
    
    m_initialized = true;
    std::cout << "[EngineFrameRenderer] Initialized successfully\n";
    return true;
}

void EngineFrameRenderer::OnResize(uint32_t width, uint32_t height)
{
    finishCapture();
    if (m_capture.requested || m_capture.submitted || !m_capture.pixels.empty()) {
        m_capture.requested = false;
        m_capture.pixels.clear();
        m_capture.error = "ViewportUnavailable: viewport resized during capture";
    }
    if (width == 0 || height == 0) return;
    
    m_swapchainDirty = true;
    m_pendingWidth = width;
    m_pendingHeight = height;
}

bool EngineFrameRenderer::RequestFrameCapture(std::string& outError)
{
    if (!m_initialized || !m_device || !m_swapchain || m_swapchainDirty ||
        !m_swapchain->swapchain || !m_swapchain->extent.width || !m_swapchain->extent.height) {
        outError = "ViewportUnavailable: no usable swapchain";
        return false;
    }
    if (!m_renderedFrameCount) { outError = "NoRenderedFrame"; return false; }
    if (!m_swapchain->supportsCapture) {
        outError = "ViewportUnavailable: surface does not support swapchain transfer-source capture";
        return false;
    }
    if (m_swapchain->format != VK_FORMAT_B8G8R8A8_SRGB &&
        m_swapchain->format != VK_FORMAT_B8G8R8A8_UNORM &&
        m_swapchain->format != VK_FORMAT_R8G8B8A8_SRGB &&
        m_swapchain->format != VK_FORMAT_R8G8B8A8_UNORM) {
        outError = "ViewportUnavailable: swapchain color format is not 8-bit RGBA";
        return false;
    }
    if (m_capture.requested || m_capture.submitted || !m_capture.pixels.empty()) {
        outError = "CaptureInProgress";
        return false;
    }
    CancelFrameCapture();
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(m_swapchain->extent.width) *
                               m_swapchain->extent.height * 4;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = bytes;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    if (vmaCreateBuffer(m_device->allocator, &info, &alloc, &m_capture.buffer,
                        &m_capture.allocation, nullptr) != VK_SUCCESS) {
        outError = "Failed to allocate framebuffer readback buffer";
        return false;
    }
    m_capture.width = m_swapchain->extent.width;
    m_capture.height = m_swapchain->extent.height;
    m_capture.requested = true;
    m_capture.error.clear();
    return true;
}

void EngineFrameRenderer::finishCapture()
{
    if (!m_capture.submitted || !m_device ||
        vkGetFenceStatus(m_device->device, m_capture.fence) != VK_SUCCESS) return;
    if (m_capture.cancelled) {
        m_capture.submitted = false;
        CancelFrameCapture();
        return;
    }
    const size_t bytes = static_cast<size_t>(m_capture.width) * m_capture.height * 4;
    void* mapped = nullptr;
    if (vmaMapMemory(m_device->allocator, m_capture.allocation, &mapped) == VK_SUCCESS) {
        vmaInvalidateAllocation(m_device->allocator, m_capture.allocation, 0, VK_WHOLE_SIZE);
        const auto* source = static_cast<const uint8_t*>(mapped);
        m_capture.pixels.assign(source, source + bytes);
        vmaUnmapMemory(m_device->allocator, m_capture.allocation);
        if (m_swapchain->format == VK_FORMAT_B8G8R8A8_SRGB ||
            m_swapchain->format == VK_FORMAT_B8G8R8A8_UNORM) {
            for (size_t i = 0; i < bytes; i += 4) std::swap(m_capture.pixels[i], m_capture.pixels[i+2]);
        }
    } else m_capture.error = "Failed to map completed framebuffer copy";
    m_capture.submitted = false;
    m_capture.fence = VK_NULL_HANDLE;
}

bool EngineFrameRenderer::PollFrameCapture(FrameCapture& out, std::string& outError)
{
    finishCapture();
    if (!m_capture.error.empty()) {
        outError = m_capture.error;
        CancelFrameCapture();
        return true;
    }
    if (m_capture.pixels.empty()) return false;
    out.width = m_capture.width;
    out.height = m_capture.height;
    out.frame = m_capture.frame;
    out.rgba = std::move(m_capture.pixels);
    CancelFrameCapture();
    return true;
}

void EngineFrameRenderer::CancelFrameCapture()
{
    // A submitted copy must outlive its GPU fence; defer cancellation until completion.
    if (m_capture.submitted) { m_capture.cancelled = true; return; }
    if (m_capture.buffer && m_device) {
        vmaDestroyBuffer(m_device->allocator, m_capture.buffer, m_capture.allocation);
    }
    m_capture = {};
}

void EngineFrameRenderer::SetProjectRoot(const std::string& root)
{
    m_projectRoot = root;
    if (m_uiOverlay) {
        m_uiOverlay->SetProjectRoot(root);
    }
}

void EngineFrameRenderer::SubmitHUICommands(
    const std::vector<Hephaestus::HephaestusUI::UIDrawCommand>& commands)
{
    if (!m_uiOverlay) {
        return;
    }
    m_uiCommands = &commands;
    m_uiCommandsSubmitted = true;
    m_uiOverlay->SetViewportSize({static_cast<float>(m_swapchain->extent.width),
                                  static_cast<float>(m_swapchain->extent.height)});
    m_uiOverlay->SetDrawCommands(commands);
}

bool EngineFrameRenderer::HasHUIOverlay() const
{
    return m_uiOverlay != nullptr;
}

bool EngineFrameRenderer::createUIResources()
{
    if (!m_device || !m_swapchain) {
        return false;
    }
    m_uiOverlay = std::make_unique<Hephaestus::HephaestusUI::VulkanHUIRenderer>();
    const bool ok = m_uiOverlay->Init(m_device, m_swapchain->renderPass,
                                      VulkanSwapchain::kOverlaySubpass, m_swapchain->format,
                                      m_swapchain->extent,
                                      static_cast<uint32_t>(m_swapchain->images.size()));
    if (!ok) {
        m_uiOverlay.reset();
        return false;
    }
    m_uiOverlay->SetProjectRoot(m_projectRoot);
    return true;
}

void EngineFrameRenderer::recordHUIOverlay(VkCommandBuffer cmd, uint32_t imageIndex)
{
    if (!m_uiOverlay || !m_uiCommandsSubmitted) {
        return;
    }
    m_uiOverlay->RecordDraw(cmd, imageIndex);
}

bool EngineFrameRenderer::RenderFrame(Scene* scene, const RenderCamera& camera, 
                                       const FrameRenderConfig& config, float deltaTime)
{
    m_pacedFrameSeconds = 0.0;
    if (!m_initialized) return false;
    if (m_deviceLost) {
        return false;
    }
    
    // Handle pending resize
    if (m_swapchainDirty) {
        vkDeviceWaitIdle(m_device->device);
        finishCapture();
        if (m_capture.requested || !m_capture.pixels.empty()) {
            m_capture.requested = false;
            m_capture.pixels.clear();
            m_capture.error = "ViewportUnavailable: swapchain was recreated before capture";
        }

        const size_t previousImageCount = m_commandBuffers.size();
        const bool recreateSkyboxDescriptors =
            previousImageCount != m_swapchain->images.size();

        cleanupSwapchainResources();

        if (!createPipeline() ||
            !createSyncObjects() ||
            !createCommandBuffers() ||
            !createUniformBuffers()) {
            std::cerr << "[EngineFrameRenderer] Failed to recreate core swapchain resources\n";
            return false;
        }

        if (recreateSkyboxDescriptors) {
            m_skybox.Cleanup(m_device->allocator, m_device->device);
            m_lastSkyboxPath.clear();
            if (!createSkyboxResources()) {
                std::cerr << "[EngineFrameRenderer] Warning: Failed to recreate skybox resources\n";
            }
        } else if (m_skybox.initialized) {
            m_skybox.Recreate(m_device->device, m_swapchain->renderPass, m_swapchain->extent);
        }

        if (!createDescriptorSets()) {
            std::cerr << "[EngineFrameRenderer] Failed to recreate descriptor sets\n";
            return false;
        }
        if (!createToneMapResources()) {
            std::cerr << "[EngineFrameRenderer] Failed to recreate tone map resources\n";
            return false;
        }
        if (!m_probeGI.CreateSwapchainResources(m_swapchain->renderPass, VulkanSwapchain::kSceneSubpass,
                                                m_pipeline->descriptorSetLayout,
                                                static_cast<uint32_t>(m_swapchain->images.size()))) {
            std::cerr << "[EngineFrameRenderer] Failed to recreate probe GI resources\n";
            return false;
        }
        if (!createSkinnedMainPipelines()) {
            std::cerr << "[EngineFrameRenderer] Warning: Failed to recreate skinned mesh pipelines\n";
        }
        for (auto& pending : m_pendingTextureSlotUpdates) {
            for (uint32_t slotIndex = 0;
                 slotIndex < static_cast<uint32_t>(m_materialTextureSlots.size());
                 ++slotIndex) {
                if (m_materialTextureSlots[slotIndex].valid) {
                    pending.push_back(slotIndex);
                }
            }
        }

        if (m_editorOverlays) {
            if (!createGridResources()) {
                std::cerr << "[EngineFrameRenderer] Warning: Failed to recreate grid resources\n";
            }
            if (!createDebugOverlayResources()) {
                std::cerr << "[EngineFrameRenderer] Warning: Failed to recreate debug overlay resources\n";
            }
            if (!createOutlineResources()) {
                std::cerr << "[EngineFrameRenderer] Warning: Failed to recreate outline resources\n";
            } else if (!createSkinnedOutlinePipelines()) {
                std::cerr << "[EngineFrameRenderer] Warning: Failed to recreate skinned outline pipelines\n";
            }
            if (!createIdPickerResources()) {
                std::cerr << "[EngineFrameRenderer] Warning: Failed to recreate ID picker resources\n";
            } else if (!createSkinnedIdPipelines()) {
                std::cerr << "[EngineFrameRenderer] Warning: Failed to recreate skinned ID pipelines\n";
            }
            if (!createGizmoResources()) {
                std::cerr << "[EngineFrameRenderer] Warning: Failed to recreate gizmo resources\n";
            }
            if (!createLightGizmoResources()) {
                std::cerr << "[EngineFrameRenderer] Warning: Failed to recreate light gizmo resources\n";
            }
            if (!createBillboardResources()) {
                std::cerr << "[EngineFrameRenderer] Warning: Failed to recreate billboard resources\n";
            }
        }

        // Recreate the runtime HUI overlay for the new swapchain (the old
        // pipeline/vertex buffers were destroyed in cleanupSwapchainResources).
        if (m_uiOverlay) {
            m_uiOverlay->OnSwapchainRecreated(m_swapchain->renderPass,
                                              VulkanSwapchain::kOverlaySubpass, m_swapchain->format,
                                              m_swapchain->extent,
                                              static_cast<uint32_t>(m_swapchain->images.size()));
        }

        m_currentFrame = 0;
        m_swapchainDirty = false;
    }
    
    // Wait for previous frame
    vkWaitForFences(m_device->device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    finishCapture(); // Before a completed frame's fence can be reset for reuse.
    ++m_frameSerial;
    RetireIdleEntries(m_characterBonePalettes, m_frameSerial, MAX_FRAMES_IN_FLIGHT,
                      [this](CharacterBoneGpu& gpu) { releaseCharacterBonePalette(gpu); });
    
    // Deferred readback fallback: if the immediate readback below didn't run
    // (e.g. submit failed), drain any pending pick on the next frame where the
    // GPU work is guaranteed complete.
    if (m_idPicker.pickReadPending && m_idPicker.mappedStagingBuffer) {
        m_idPicker.lastPickedEntity = resolvePickRegion();
        m_idPicker.pickReadPending = false;
        m_idPicker.pickJustCompleted = true;
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
    } else if (result == VK_ERROR_DEVICE_LOST) {
        m_deviceLost = true;
        std::cerr << "[EngineFrameRenderer] vkAcquireNextImageKHR reported VK_ERROR_DEVICE_LOST\n";
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "[EngineFrameRenderer] Failed to acquire swapchain image\n";
        return false;
    }

    // Per-image synchronization: if this swapchain image is already in flight,
    // wait for the fence that owns it before reusing its command buffer/resources.
    if (imageIndex < m_imagesInFlight.size() && m_imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(m_device->device, 1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    if (imageIndex < m_imagesInFlight.size()) {
        m_imagesInFlight[imageIndex] = m_inFlightFences[m_currentFrame];
    }
    const auto pacedNow = std::chrono::steady_clock::now();
    m_pacedFrameSeconds = m_hasPacedTime ? std::chrono::duration<double>(pacedNow - m_lastPacedTime).count() : 0.0;
    m_lastPacedTime = pacedNow;
    m_hasPacedTime = true;
    flushPendingTextureDescriptorUpdates(imageIndex);
    
    vkResetFences(m_device->device, 1, &m_inFlightFences[m_currentFrame]);
    
    // Update uniform buffers
    updateCameraUBO(imageIndex, camera, config);
    if (scene) {
        updateLightingUBO(imageIndex, scene, camera);
    }
    prepareEnvironment(scene, config, imageIndex);
    {
        Vec3 sunDirection;
        const bool hasSun = scene && FindShadowCastingSun(scene, sunDirection);
        // Hardware tracing sees every visible, non-skinned mesh, so moving objects block and bounce light too.
        auto collectInstances = [&]() {
            std::vector<ProbeTraceInstance> instances;
            auto view = scene->GetRegistry().view<MeshRendererComponent>();
            for (entt::entity handle : view) {
                auto& renderer = view.get<MeshRendererComponent>(handle);
                if (!renderer.visible || renderer.hideInGame || renderer.viewModel || !renderer.HasValidMesh() ||
                    scene->GetRegistry().all_of<CharacterComponent>(handle)) {
                    continue;
                }
                Entity entity(handle, scene);
                const Mat4 world = scene->GetWorldTransform(entity);
                const MaterialComponent* material = resolveMaterialForSlot(entity, renderer, 0);
                for (size_t meshHandle : MeshHandlesForDraw(renderer)) {
                    if (meshHandle >= m_meshLibrary.size() || m_meshLibrary[meshHandle].skinned) continue;
                    ProbeTraceInstance instance;
                    instance.mesh = &m_meshLibrary[meshHandle];
                    instance.world = world;
                    if (material) {
                        instance.albedo = material->albedo;
                        instance.emissive = vscale(material->emissive, material->emissiveStrength);
                    }
                    instances.push_back(instance);
                }
            }
            return instances;
        };
        m_probeGI.Prepare(scene, m_projectRoot, imageIndex, m_lightingBuffers[imageIndex].buffer,
                          hasSun ? &sunDirection : nullptr, scene && m_traceShadows, collectInstances);
    }

    // Prepare the runtime HUI overlay geometry/uploads (queue work is allowed
    // here; the command buffer has not been recorded yet).
    if (m_uiOverlay && m_uiCommandsSubmitted) {
        m_uiOverlay->PrepareFrame();
    }

    // Record command buffer
    VkCommandBuffer cmd = m_commandBuffers[imageIndex];
    vkResetCommandBuffer(cmd, 0);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    // Record ID pass for entity picking (editor only)
    if (config.isEditorMode && m_idPicker.initialized) {
        recordIdPass(cmd, imageIndex, scene, config);
        
        // Copy a small region around the click to staging for multi-pixel sampling.
        if (m_idPicker.pickCopyRequested && m_idPicker.pickX < m_idPicker.width &&
            m_idPicker.pickY < m_idPicker.height) {
            const uint32_t R = IdPicker::SAMPLE_RADIUS;
            uint32_t x0 = (m_idPicker.pickX > R) ? (m_idPicker.pickX - R) : 0;
            uint32_t y0 = (m_idPicker.pickY > R) ? (m_idPicker.pickY - R) : 0;
            uint32_t x1 = std::min(m_idPicker.pickX + R + 1, m_idPicker.width);
            uint32_t y1 = std::min(m_idPicker.pickY + R + 1, m_idPicker.height);
            uint32_t copyW = x1 - x0;
            uint32_t copyH = y1 - y0;

            m_idPicker.sampleOriginX = x0;
            m_idPicker.sampleOriginY = y0;
            m_idPicker.sampleW = copyW;
            m_idPicker.sampleH = copyH;

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = copyW;
            region.bufferImageHeight = copyH;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {static_cast<int32_t>(x0), static_cast<int32_t>(y0), 0};
            region.imageExtent = {copyW, copyH, 1};

            vkCmdCopyImageToBuffer(cmd, m_idPicker.colorImage,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   m_idPicker.stagingBuffer, 1, &region);
            m_idPicker.pickCopyRequested = false;
            m_idPicker.pickReadPending = true;
        }
    } else if (m_idPicker.pickCopyRequested) {
        // No ID pass this frame — drop the request so the UI does not wait forever.
        m_idPicker.pickCopyRequested = false;
    }
    
    // Record shadow pass (before main pass)
    if (config.renderShadows && m_directionalShadow.IsValid() && !(scene && m_sunRayTraced)) {
        recordShadowPass(cmd, scene, camera, config);
    }
    m_probeGI.RecordScene(cmd, imageIndex);
    if (config.renderShadows && m_localShadow.IsValid() && !m_localShadowJobs.empty()) {
        recordLocalShadowPass(cmd, scene, config);
    }
    if (m_probeGI.Active()) {
        m_probeGI.BeginTiming(cmd, imageIndex);
        if (m_probeGI.HasSunMap()) {
            recordProbeGISunPass(cmd, scene);
        }
        m_probeGI.RecordUpdate(cmd, imageIndex);
    }
    
// Record main render pass
    recordMainPass(cmd, imageIndex, scene, config);
    m_lastFrameStats.probeGIGpuMs = m_probeGI.Active() ? m_probeGI.LastGpuMilliseconds() : -1.0f;
    if (m_lastFrameStats.probeGIGpuMs >= 0.0f && m_renderedFrameCount % 600 == 0) {
        std::cout << "[ProbeGI] " << m_probeGI.ProbeCount() << " probes updated in "
                  << m_lastFrameStats.probeGIGpuMs << " ms GPU\n";
    }

    if (m_capture.requested) {
        VkImageMemoryBarrier before{};
        before.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        before.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        before.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        before.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        before.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        before.image = m_swapchain->images[imageIndex];
        before.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &before);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {m_capture.width, m_capture.height, 1};
        vkCmdCopyImageToBuffer(cmd, before.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               m_capture.buffer, 1, &region);
        VkImageMemoryBarrier after = before;
        after.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        after.dstAccessMask = 0;
        after.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        after.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &after);
    }

    VkResult endResult = vkEndCommandBuffer(cmd);
    if (endResult != VK_SUCCESS) {
        std::cerr << "[EngineFrameRenderer] Failed to end command buffer (VkResult: " << endResult << ")\n";
        return false;
    }

    // Drop the HUI command reference now that the frame has consumed it.
    if (m_uiOverlay) {
        m_uiOverlay->ClearDrawCommands();
    }
    m_uiCommands = nullptr;
    m_uiCommandsSubmitted = false;

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
    submitInfo.pSignalSemaphores = &m_renderFinishedSemaphores[imageIndex];
    
    VkResult submitResult = vkQueueSubmit(m_device->graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);
    if (submitResult != VK_SUCCESS) {
        if (m_capture.requested) {
            m_capture.requested = false;
            m_capture.error = "Framebuffer copy submission failed";
        }
        if (submitResult == VK_ERROR_DEVICE_LOST) {
            m_deviceLost = true;
            std::cerr << "[EngineFrameRenderer] vkQueueSubmit reported VK_ERROR_DEVICE_LOST\n";
        }
        std::cerr << "[EngineFrameRenderer] Failed to submit draw command buffer (VkResult: " << submitResult << ")\n";
        return false;
    }
    if (m_capture.requested) {
        m_capture.requested = false;
        m_capture.submitted = true;
        m_capture.fence = m_inFlightFences[m_currentFrame];
        m_capture.frame = ++m_renderedFrameCount;
    } else {
        ++m_renderedFrameCount;
    }
    
    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain->swapchain;
    presentInfo.pImageIndices = &imageIndex;
    
    result = vkQueuePresentKHR(m_device->presentQueue, &presentInfo);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_swapchainDirty = true;
    } else if (result == VK_ERROR_DEVICE_LOST) {
        m_deviceLost = true;
        std::cerr << "[EngineFrameRenderer] vkQueuePresentKHR reported VK_ERROR_DEVICE_LOST\n";
        return false;
    }

    // Immediate pick readback: wait on the fence we just submitted so the
    // staging buffer is safe to read, then resolve the result this frame.
    // Only stalls the CPU on frames where the user actually clicked.
    if (m_idPicker.pickReadPending && m_idPicker.mappedStagingBuffer) {
        vkWaitForFences(m_device->device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
        m_idPicker.lastPickedEntity = resolvePickRegion();
        m_idPicker.pickReadPending = false;
        m_idPicker.pickJustCompleted = true;
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
    finishCapture();
    m_capture.submitted = false;
    CancelFrameCapture();
    
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
    allocateSkinnedBoneDescriptor(m_meshLibrary[handle]);
    
    if (!m_meshLibrary[handle].sourceKey.empty()) {
        m_meshLibraryLookup[m_meshLibrary[handle].sourceKey] = handle;
    }
    
    return handle;
}

size_t EngineFrameRenderer::AddStaticMesh(const std::string& name, const MeshData& data,
                                          const std::filesystem::path& sourcePath, bool imported)
{
    if (!m_device || data.vertices.empty() || data.indices.empty()) {
        return MeshRendererComponent::InvalidMesh;
    }

    VmaAllocator allocator = m_device->allocator;
    VkDevice device = m_device->device;

    MeshGPU mesh{};
    mesh.name = name;
    mesh.sourcePath = sourcePath;
    mesh.sourceKey = sourcePath.generic_string();
    mesh.vertexCount = static_cast<uint32_t>(data.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(data.indices.size());
    mesh.submeshes = data.submeshes;
    mesh.bounds = data.bounds;
    mesh.imported = imported;
    if (m_editorOverlays) {
        mesh.cpuPositions.reserve(data.vertices.size());
        for (const Vertex& v : data.vertices) {
            mesh.cpuPositions.emplace_back(v.pos[0], v.pos[1], v.pos[2]);
        }
        mesh.cpuIndices = data.indices;
    }

    // Ray-traced probes build acceleration structures from these buffers and read triangles by address.
    const VkBufferUsageFlags rayTracingUsage = m_device->SupportsRayQuery()
        ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
              VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
        : 0;
    const VkDeviceSize vbSize = data.vertices.size() * sizeof(Vertex);
    mesh.vertexBuffer.Create(allocator, vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | rayTracingUsage,
        VMA_MEMORY_USAGE_GPU_ONLY);
    const VkDeviceSize ibSize = data.indices.size() * sizeof(uint32_t);
    mesh.indexBuffer.Create(allocator, ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | rayTracingUsage,
        VMA_MEMORY_USAGE_GPU_ONLY);

    VulkanBuffer stagingVB;
    stagingVB.Create(allocator, vbSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void* mapped = nullptr;
    vmaMapMemory(allocator, stagingVB.allocation, &mapped);
    std::memcpy(mapped, data.vertices.data(), static_cast<size_t>(vbSize));
    vmaUnmapMemory(allocator, stagingVB.allocation);

    VulkanBuffer stagingIB;
    stagingIB.Create(allocator, ibSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    vmaMapMemory(allocator, stagingIB.allocation, &mapped);
    std::memcpy(mapped, data.indices.data(), static_cast<size_t>(ibSize));
    vmaUnmapMemory(allocator, stagingIB.allocation);

    VkCommandBufferAllocateInfo cmdAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAlloc.commandPool = m_device->commandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
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
    vkQueueSubmit(m_device->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_device->graphicsQueue);

    vkFreeCommandBuffers(device, m_device->commandPool, 1, &cmd);
    stagingVB.Destroy(allocator);
    stagingIB.Destroy(allocator);

    return AddMesh(std::move(mesh));
}

size_t EngineFrameRenderer::AddSkinnedMesh(const std::string& name,
                                           const std::vector<SkinnedVertex>& vertices,
                                           const std::vector<uint32_t>& indices,
                                           const std::vector<MeshData::Submesh>& submeshes,
                                           const AABB& bounds,
                                           const std::vector<Mat4>& bonePalette,
                                           const std::filesystem::path& sourcePath,
                                           bool imported)
{
    if (!m_device || vertices.empty() || indices.empty()) {
        return SIZE_MAX;
    }

    MeshGPU mesh{};
    mesh.name = name;
    mesh.sourcePath = sourcePath;
    mesh.sourceKey = sourcePath.generic_string();
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.indexCount = static_cast<uint32_t>(indices.size());
    mesh.submeshes = submeshes;
    mesh.bounds = bounds;
    mesh.imported = imported;
    mesh.skinned = true;
    mesh.boneCount = static_cast<uint32_t>(
        std::min(bonePalette.size(), static_cast<size_t>(128)));

    const VkDeviceSize vbSize = sizeof(SkinnedVertex) * vertices.size();
    mesh.vertexBuffer.Create(m_device->allocator, vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             VMA_MEMORY_USAGE_CPU_TO_GPU);
    void* mapped = nullptr;
    if (vmaMapMemory(m_device->allocator, mesh.vertexBuffer.allocation, &mapped) == VK_SUCCESS) {
        std::memcpy(mapped, vertices.data(), static_cast<size_t>(vbSize));
        vmaFlushAllocation(m_device->allocator, mesh.vertexBuffer.allocation, 0, VK_WHOLE_SIZE);
        vmaUnmapMemory(m_device->allocator, mesh.vertexBuffer.allocation);
    }

    const VkDeviceSize ibSize = sizeof(uint32_t) * indices.size();
    mesh.indexBuffer.Create(m_device->allocator, ibSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                            VMA_MEMORY_USAGE_CPU_TO_GPU);
    if (vmaMapMemory(m_device->allocator, mesh.indexBuffer.allocation, &mapped) == VK_SUCCESS) {
        std::memcpy(mapped, indices.data(), static_cast<size_t>(ibSize));
        vmaFlushAllocation(m_device->allocator, mesh.indexBuffer.allocation, 0, VK_WHOLE_SIZE);
        vmaUnmapMemory(m_device->allocator, mesh.indexBuffer.allocation);
    }

    std::vector<Mat4> palette(128, Mat4::Identity());
    const size_t copyCount = std::min(bonePalette.size(), palette.size());
    for (size_t i = 0; i < copyCount; ++i) {
        palette[i] = bonePalette[i];
    }
    mesh.boneBuffer.Create(m_device->allocator, kBoneUboSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                           VMA_MEMORY_USAGE_CPU_TO_GPU);
    if (vmaMapMemory(m_device->allocator, mesh.boneBuffer.allocation, &mapped) == VK_SUCCESS) {
        std::memcpy(mapped, palette.data(), static_cast<size_t>(kBoneUboSize));
        vmaFlushAllocation(m_device->allocator, mesh.boneBuffer.allocation, 0, VK_WHOLE_SIZE);
        vmaUnmapMemory(m_device->allocator, mesh.boneBuffer.allocation);
    }

    return AddMesh(std::move(mesh));
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

void EngineFrameRenderer::AddMeshAlias(const std::string& aliasKey, size_t handle)
{
    if (aliasKey.empty() || handle >= m_meshLibrary.size()) return;
    // Only insert if the key is not already mapped
    m_meshLibraryLookup.emplace(aliasKey, handle);
}

void EngineFrameRenderer::WaitGpuIdle()
{
    if (m_device && m_device->device) {
        vkDeviceWaitIdle(m_device->device);
    }
}

void EngineFrameRenderer::InvalidateMesh(const std::string& sourceKey)
{
    if (sourceKey.empty()) {
        return;
    }
    auto it = m_meshLibraryLookup.find(sourceKey);
    if (it == m_meshLibraryLookup.end()) {
        return;
    }
    const size_t handle = it->second;
    for (auto lookup = m_meshLibraryLookup.begin(); lookup != m_meshLibraryLookup.end();) {
        if (lookup->second == handle) {
            lookup = m_meshLibraryLookup.erase(lookup);
        } else {
            ++lookup;
        }
    }
    if (handle < m_meshLibrary.size() && m_device) {
        MeshGPU& mesh = m_meshLibrary[handle];
        if (mesh.boneDescriptorSet != VK_NULL_HANDLE && m_skinned.bonePool != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(m_device->device, m_skinned.bonePool, 1, &mesh.boneDescriptorSet);
            mesh.boneDescriptorSet = VK_NULL_HANDLE;
        }
        mesh.Destroy(m_device->allocator);
    }
}

// ============================================================================
// Private: Resource Creation
// ============================================================================

bool EngineFrameRenderer::createPipeline()
{
    try {
        m_pipeline = std::make_unique<VulkanPipeline>();
        const bool enableWireframe = m_device && m_device->SupportsFillModeNonSolid();
        m_pipeline->Init(m_device->device, m_swapchain->renderPass, m_swapchain->extent, enableWireframe,
                         {m_probeGI.EmptySetLayout(), m_probeGI.SetLayout()}, surfaceFragmentShader());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[EngineFrameRenderer] Pipeline creation failed: " << e.what() << "\n";
        return false;
    }
}

void EngineFrameRenderer::allocateSkinnedBoneDescriptor(MeshGPU& mesh)
{
    if (!mesh.skinned || mesh.boneBuffer.buffer == VK_NULL_HANDLE ||
        mesh.boneDescriptorSet != VK_NULL_HANDLE || !m_skinned.layoutReady ||
        m_skinned.bonePool == VK_NULL_HANDLE) {
        return;
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = m_skinned.bonePool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &m_skinned.boneSetLayout;
    if (vkAllocateDescriptorSets(m_device->device, &alloc, &mesh.boneDescriptorSet) != VK_SUCCESS) {
        std::cerr << "[EngineFrameRenderer] Failed to allocate bone descriptor set\n";
        mesh.boneDescriptorSet = VK_NULL_HANDLE;
        return;
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = mesh.boneBuffer.buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = kBoneUboSize;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = mesh.boneDescriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(m_device->device, 1, &write, 0, nullptr);
}

void EngineFrameRenderer::destroyCharacterBonePalettes()
{
    if (!m_device) {
        m_characterBonePalettes.clear();
        return;
    }
    for (auto& [id, gpu] : m_characterBonePalettes) {
        releaseCharacterBonePalette(gpu);
    }
    m_characterBonePalettes.clear();
}

void EngineFrameRenderer::releaseCharacterBonePalette(CharacterBoneGpu& gpu)
{
    for (int slot = 0; slot < MAX_FRAMES_IN_FLIGHT; ++slot) {
        if (gpu.sets[slot] != VK_NULL_HANDLE && m_skinned.bonePool != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(m_device->device, m_skinned.bonePool, 1, &gpu.sets[slot]);
        }
        gpu.sets[slot] = VK_NULL_HANDLE;
        gpu.buffers[slot].Destroy(m_device->allocator);
    }
}

VkDescriptorSet EngineFrameRenderer::resolveSkinnedBoneSet(entt::entity entity,
                                                           const CharacterComponent* character,
                                                           const MeshGPU& mesh)
{
    if (!character || !character->runtimeReady || character->bonePalette.empty() ||
        !m_skinned.layoutReady || m_skinned.bonePool == VK_NULL_HANDLE || !m_device) {
        return mesh.boneDescriptorSet;
    }

    const uint32_t id = static_cast<uint32_t>(entity);
    CharacterBoneGpu& gpu = m_characterBonePalettes[id];
    gpu.lastUsedFrame = m_frameSerial;
    const int slot = static_cast<int>(m_currentFrame);
    VulkanBuffer& buffer = gpu.buffers[slot];
    VkDescriptorSet& set = gpu.sets[slot];
    if (buffer.buffer == VK_NULL_HANDLE) {
        buffer.Create(m_device->allocator, kBoneUboSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU);
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = m_skinned.bonePool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &m_skinned.boneSetLayout;
        if (vkAllocateDescriptorSets(m_device->device, &alloc, &set) != VK_SUCCESS) {
            std::cerr << "[EngineFrameRenderer] Failed to allocate a character bone descriptor set\n";
            set = VK_NULL_HANDLE;
            buffer.Destroy(m_device->allocator);
            return mesh.boneDescriptorSet;
        }
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer.buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = kBoneUboSize;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(m_device->device, 1, &write, 0, nullptr);
    }

    void* mapped = nullptr;
    if (gpu.writtenFrame[slot] != m_frameSerial &&
        vmaMapMemory(m_device->allocator, buffer.allocation, &mapped) == VK_SUCCESS) {
        gpu.writtenFrame[slot] = m_frameSerial;
        std::vector<Mat4> palette(128, Mat4::Identity());
        const size_t copyCount = std::min(character->bonePalette.size(), palette.size());
        for (size_t i = 0; i < copyCount; ++i) {
            palette[i] = character->bonePalette[i];
        }
        std::memcpy(mapped, palette.data(), static_cast<size_t>(kBoneUboSize));
        vmaFlushAllocation(m_device->allocator, buffer.allocation, 0, VK_WHOLE_SIZE);
        vmaUnmapMemory(m_device->allocator, buffer.allocation);
    }
    return set != VK_NULL_HANDLE ? set : mesh.boneDescriptorSet;
}

VkPipeline EngineFrameRenderer::getSkinnedPipeline(bool backfaceCulling, bool wireframe) const
{
    if (wireframe) {
        VkPipeline pipe = backfaceCulling ? m_skinned.pipelineWireframeBackface
                                          : m_skinned.pipelineWireframe;
        if (pipe != VK_NULL_HANDLE) {
            return pipe;
        }
    }
    return backfaceCulling ? m_skinned.pipelineBackface : m_skinned.pipeline;
}

bool EngineFrameRenderer::createSkinnedBoneLayout()
{
    if (!m_device || m_skinned.layoutReady) {
        return m_skinned.layoutReady;
    }

    VkDescriptorSetLayoutBinding boneBinding{};
    boneBinding.binding = 0;
    boneBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    boneBinding.descriptorCount = 1;
    boneBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &boneBinding;
    if (vkCreateDescriptorSetLayout(m_device->device, &layoutInfo, nullptr,
                                    &m_skinned.boneSetLayout) != VK_SUCCESS) {
        return false;
    }

    // Mesh bind-pose sets plus one set per frame in flight for each animated character.
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 512;
    if (vkCreateDescriptorPool(m_device->device, &poolInfo, nullptr, &m_skinned.bonePool) !=
        VK_SUCCESS) {
        return false;
    }

    m_skinned.layoutReady = true;
    return true;
}

bool EngineFrameRenderer::createSkinnedMainPipelines()
{
    if (!m_device || !m_swapchain || !m_pipeline || !m_skinned.layoutReady) {
        return false;
    }

    DestroyPipeline(m_device->device, m_skinned.pipeline);
    DestroyPipeline(m_device->device, m_skinned.pipelineBackface);
    DestroyPipeline(m_device->device, m_skinned.pipelineWireframe);
    DestroyPipeline(m_device->device, m_skinned.pipelineWireframeBackface);
    DestroyPipelineLayout(m_device->device, m_skinned.pipelineLayout);

    VkDescriptorSetLayout setLayouts[] = {m_pipeline->descriptorSetLayout, m_skinned.boneSetLayout,
                                          m_probeGI.SetLayout()};
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size = 64;
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 3;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(m_device->device, &layoutInfo, nullptr, &m_skinned.pipelineLayout) !=
        VK_SUCCESS) {
        return false;
    }

    VkShaderModule vert = LoadSpvModule(m_device->device, "triangle_skinned.vert.spv");
    VkShaderModule frag = LoadSpvModule(m_device->device, surfaceFragmentShader());
    if (!vert || !frag) {
        std::cerr << "[EngineFrameRenderer] Failed to load skinned triangle shaders\n";
        if (vert) vkDestroyShaderModule(m_device->device, vert, nullptr);
        if (frag) vkDestroyShaderModule(m_device->device, frag, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    auto binding = SkinnedVertex::getBindingDescription();
    auto attribs = SkinnedVertex::getAttributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInput.pVertexAttributeDescriptions = attribs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAsm{};
    inputAsm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterBase{};
    rasterBase.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterBase.polygonMode = VK_POLYGON_MODE_FILL;
    rasterBase.cullMode = VK_CULL_MODE_NONE;
    rasterBase.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterBase.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorAttach{};
    colorAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &colorAttach;

    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamics;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vertexInput;
    pci.pInputAssemblyState = &inputAsm;
    pci.pViewportState = &viewportState;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &depth;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dyn;
    pci.layout = m_skinned.pipelineLayout;
    pci.renderPass = m_swapchain->renderPass;

    auto build = [&](VkCullModeFlags cull, VkPolygonMode fill) -> VkPipeline {
        VkPipelineRasterizationStateCreateInfo raster = rasterBase;
        raster.cullMode = cull;
        raster.polygonMode = fill;
        pci.pRasterizationState = &raster;
        VkPipeline handle = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(m_device->device, VK_NULL_HANDLE, 1, &pci, nullptr, &handle) !=
            VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        return handle;
    };

    m_skinned.pipeline = build(VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL);
    m_skinned.pipelineBackface = build(VK_CULL_MODE_FRONT_BIT, VK_POLYGON_MODE_FILL);
    if (m_device->SupportsFillModeNonSolid()) {
        m_skinned.pipelineWireframe = build(VK_CULL_MODE_NONE, VK_POLYGON_MODE_LINE);
        m_skinned.pipelineWireframeBackface = build(VK_CULL_MODE_FRONT_BIT, VK_POLYGON_MODE_LINE);
    }

    vkDestroyShaderModule(m_device->device, vert, nullptr);
    vkDestroyShaderModule(m_device->device, frag, nullptr);
    if (m_skinned.pipeline == VK_NULL_HANDLE) {
        std::cerr << "[EngineFrameRenderer] Failed to create skinned graphics pipeline\n";
        return false;
    }
    std::cout << "[EngineFrameRenderer] Skinned mesh pipelines ready\n";
    return true;
}

bool EngineFrameRenderer::createSkinnedShadowPipeline()
{
    if (!m_device || !m_directionalShadow.IsValid() || !m_skinned.layoutReady) {
        return false;
    }
    DestroyPipeline(m_device->device, m_skinned.shadowPipeline);
    DestroyPipelineLayout(m_device->device, m_skinned.shadowLayout);

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size = sizeof(Mat4) * 2;
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_skinned.boneSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(m_device->device, &layoutInfo, nullptr, &m_skinned.shadowLayout) !=
        VK_SUCCESS) {
        return false;
    }

    VkShaderModule vert = LoadSpvModule(m_device->device, "shadowmap_skinned.vert.spv");
    VkShaderModule frag = LoadSpvModule(m_device->device, "shadowmap.frag.spv");
    if (!vert || !frag) {
        if (vert) vkDestroyShaderModule(m_device->device, vert, nullptr);
        if (frag) vkDestroyShaderModule(m_device->device, frag, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    auto binding = SkinnedVertex::getBindingDescription();
    VkVertexInputAttributeDescription attribs[3]{};
    attribs[0].binding = 0;
    attribs[0].location = 0;
    attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribs[0].offset = offsetof(SkinnedVertex, pos);
    attribs[1].binding = 0;
    attribs[1].location = 4;
    attribs[1].format = VK_FORMAT_R16G16B16A16_UINT;
    attribs[1].offset = offsetof(SkinnedVertex, joints);
    attribs[2].binding = 0;
    attribs[2].location = 5;
    attribs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[2].offset = offsetof(SkinnedVertex, weights);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attribs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.5f;
    rasterizer.depthBiasSlopeFactor = 1.5f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_skinned.shadowLayout;
    pipelineInfo.renderPass = m_directionalShadow.renderPass;

    const VkResult result = vkCreateGraphicsPipelines(m_device->device, VK_NULL_HANDLE, 1,
                                                      &pipelineInfo, nullptr, &m_skinned.shadowPipeline);
    vkDestroyShaderModule(m_device->device, vert, nullptr);
    vkDestroyShaderModule(m_device->device, frag, nullptr);
    return result == VK_SUCCESS;
}

bool EngineFrameRenderer::createSkinnedOutlinePipelines()
{
    if (!m_device || !m_swapchain || !m_pipeline || !m_skinned.layoutReady ||
        !m_outlineRenderer.initialized) {
        return false;
    }
    DestroyPipeline(m_device->device, m_skinned.outlinePipeline);
    DestroyPipeline(m_device->device, m_skinned.outlineMaskPipeline);
    DestroyPipelineLayout(m_device->device, m_skinned.outlineLayout);

    VkDescriptorSetLayout setLayouts[] = {m_pipeline->descriptorSetLayout, m_skinned.boneSetLayout};
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size = sizeof(OutlineRenderer::OutlinePushConstants);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(m_device->device, &layoutInfo, nullptr, &m_skinned.outlineLayout) !=
        VK_SUCCESS) {
        return false;
    }

    VkShaderModule vert = LoadSpvModule(m_device->device, "outline_skinned.vert.spv");
    VkShaderModule frag = LoadSpvModule(m_device->device, "outline.frag.spv");
    if (!vert || !frag) {
        if (vert) vkDestroyShaderModule(m_device->device, vert, nullptr);
        if (frag) vkDestroyShaderModule(m_device->device, frag, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    auto binding = SkinnedVertex::getBindingDescription();
    auto attribs = SkinnedVertex::getAttributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInput.pVertexAttributeDescriptions = attribs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAsm{};
    inputAsm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_FRONT_BIT;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_FALSE;
    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState colorAttach{};
    colorAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &colorAttach;

    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamics;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vertexInput;
    pci.pInputAssemblyState = &inputAsm;
    pci.pViewportState = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &depth;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dyn;
    pci.layout = m_skinned.outlineLayout;
    pci.renderPass = m_swapchain->renderPass;
    pci.subpass = VulkanSwapchain::kOverlaySubpass;

    if (vkCreateGraphicsPipelines(m_device->device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                  &m_skinned.outlinePipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(m_device->device, vert, nullptr);
        vkDestroyShaderModule(m_device->device, frag, nullptr);
        return false;
    }

    VkPipelineRasterizationStateCreateInfo maskRaster = raster;
    maskRaster.cullMode = VK_CULL_MODE_NONE;
    VkPipelineDepthStencilStateCreateInfo maskDepth = depth;
    maskDepth.depthTestEnable = VK_TRUE;
    maskDepth.depthWriteEnable = VK_TRUE;
    maskDepth.depthCompareOp = VK_COMPARE_OP_LESS;
    pci.pRasterizationState = &maskRaster;
    pci.pDepthStencilState = &maskDepth;
    const VkResult maskResult = vkCreateGraphicsPipelines(m_device->device, VK_NULL_HANDLE, 1, &pci,
                                                          nullptr, &m_skinned.outlineMaskPipeline);
    vkDestroyShaderModule(m_device->device, vert, nullptr);
    vkDestroyShaderModule(m_device->device, frag, nullptr);
    return maskResult == VK_SUCCESS;
}

bool EngineFrameRenderer::createSkinnedIdPipelines()
{
    if (!m_device || !m_idPicker.initialized || !m_pipeline || !m_skinned.layoutReady) {
        return false;
    }
    DestroyPipeline(m_device->device, m_skinned.idPipeline);
    DestroyPipeline(m_device->device, m_skinned.idPipelineBackface);
    DestroyPipelineLayout(m_device->device, m_skinned.idLayout);

    VkDescriptorSetLayout setLayouts[] = {m_pipeline->descriptorSetLayout, m_skinned.boneSetLayout};
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.size = sizeof(IdPicker::IdPushConstants);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(m_device->device, &layoutInfo, nullptr, &m_skinned.idLayout) !=
        VK_SUCCESS) {
        return false;
    }

    VkShaderModule vert = LoadSpvModule(m_device->device, "entityid_skinned.vert.spv");
    VkShaderModule frag = LoadSpvModule(m_device->device, "entityid.frag.spv");
    if (!vert || !frag) {
        if (vert) vkDestroyShaderModule(m_device->device, vert, nullptr);
        if (frag) vkDestroyShaderModule(m_device->device, frag, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    auto binding = SkinnedVertex::getBindingDescription();
    VkVertexInputAttributeDescription attribs[3]{};
    attribs[0].binding = 0;
    attribs[0].location = 0;
    attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribs[0].offset = offsetof(SkinnedVertex, pos);
    attribs[1].binding = 0;
    attribs[1].location = 4;
    attribs[1].format = VK_FORMAT_R16G16B16A16_UINT;
    attribs[1].offset = offsetof(SkinnedVertex, joints);
    attribs[2].binding = 0;
    attribs[2].location = 5;
    attribs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[2].offset = offsetof(SkinnedVertex, weights);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attribs;

    VkPipelineInputAssemblyStateCreateInfo inputAsm{};
    inputAsm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorAttach{};
    colorAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &colorAttach;

    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamics;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vertexInput;
    pci.pInputAssemblyState = &inputAsm;
    pci.pViewportState = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &depth;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dyn;
    pci.layout = m_skinned.idLayout;
    pci.renderPass = m_idPicker.renderPass;

    if (vkCreateGraphicsPipelines(m_device->device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                  &m_skinned.idPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(m_device->device, vert, nullptr);
        vkDestroyShaderModule(m_device->device, frag, nullptr);
        return false;
    }
    raster.cullMode = VK_CULL_MODE_FRONT_BIT;
    pci.pRasterizationState = &raster;
    const VkResult backface = vkCreateGraphicsPipelines(m_device->device, VK_NULL_HANDLE, 1, &pci,
                                                        nullptr, &m_skinned.idPipelineBackface);
    vkDestroyShaderModule(m_device->device, vert, nullptr);
    vkDestroyShaderModule(m_device->device, frag, nullptr);
    return backface == VK_SUCCESS;
}

void EngineFrameRenderer::cleanupSkinnedGraphicsPipelines()
{
    if (!m_device || !m_device->device) return;
    DestroyPipeline(m_device->device, m_skinned.pipeline);
    DestroyPipeline(m_device->device, m_skinned.pipelineBackface);
    DestroyPipeline(m_device->device, m_skinned.pipelineWireframe);
    DestroyPipeline(m_device->device, m_skinned.pipelineWireframeBackface);
    DestroyPipelineLayout(m_device->device, m_skinned.pipelineLayout);
    DestroyPipeline(m_device->device, m_skinned.outlinePipeline);
    DestroyPipeline(m_device->device, m_skinned.outlineMaskPipeline);
    DestroyPipelineLayout(m_device->device, m_skinned.outlineLayout);
    DestroyPipeline(m_device->device, m_skinned.idPipeline);
    DestroyPipeline(m_device->device, m_skinned.idPipelineBackface);
    DestroyPipelineLayout(m_device->device, m_skinned.idLayout);
}

void EngineFrameRenderer::cleanupSkinnedResources()
{
    cleanupSkinnedGraphicsPipelines();
    if (!m_device || !m_device->device) return;
    destroyCharacterBonePalettes();
    DestroyPipeline(m_device->device, m_skinned.shadowPipeline);
    DestroyPipelineLayout(m_device->device, m_skinned.shadowLayout);
    if (m_skinned.bonePool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->device, m_skinned.bonePool, nullptr);
        m_skinned.bonePool = VK_NULL_HANDLE;
    }
    if (m_skinned.boneSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device->device, m_skinned.boneSetLayout, nullptr);
        m_skinned.boneSetLayout = VK_NULL_HANDLE;
    }
    m_skinned.layoutReady = false;
    for (auto& mesh : m_meshLibrary) {
        mesh.boneDescriptorSet = VK_NULL_HANDLE;
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
    m_renderFinishedSemaphores.resize(m_swapchain->images.size());
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imagesInFlight.resize(m_swapchain->images.size(), VK_NULL_HANDLE);
    
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(m_device->device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device->device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            return false;
        }
    }
    // Presentation holds a signal semaphore until its image is acquired again, so these are per swapchain image.
    for (VkSemaphore& semaphore : m_renderFinishedSemaphores) {
        if (vkCreateSemaphore(m_device->device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
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
    m_materialStride = materialStride; // Store for per-frame use
    
    VkDeviceSize totalMaterialSize = materialStride * kMaxMaterialsPerFrame;
    
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
        defaultMaterial.useMetallicTexture = 0;
        defaultMaterial.metallicTextureIndex = 0;
        defaultMaterial.useAoTexture = 0;
        defaultMaterial.aoTextureIndex = 0;
        defaultMaterial.useWorldAlignedTexture = 0;
        defaultMaterial.worldTextureScale = 1.0f;
        defaultMaterial.reflectionStrength = 1.0f;
        defaultMaterial.specular = 0.5f;
        defaultMaterial.opacity = 1.0f;
        defaultMaterial.opacityMask = 1.0f;
        defaultMaterial.refraction = 0.0f;
        
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
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(imageCount) * (kTextureArraySize + 3) }
    };
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 4;
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
    m_pendingTextureSlotUpdates.clear();
    m_pendingTextureSlotUpdates.resize(imageCount);
    
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
        
        VkDescriptorImageInfo environmentMapInfo{};
        environmentMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        environmentMapInfo.imageView = m_dummyTexture.imageView;
        environmentMapInfo.sampler = m_dummyTexture.sampler;

        VkDescriptorImageInfo localShadowInfo{};
        localShadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        localShadowInfo.imageView = m_localShadow.IsValid() ? m_localShadow.imageView : m_dummyTexture.imageView;
        localShadowInfo.sampler = m_localShadow.IsValid() ? m_directionalShadow.sampler : m_dummyTexture.sampler;

        VkWriteDescriptorSet descriptorWrites[7] = {};
        
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

        // Binding 5: Environment reflection map
        descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5].dstSet = m_descriptorSets[i];
        descriptorWrites[5].dstBinding = 5;
        descriptorWrites[5].dstArrayElement = 0;
        descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[5].descriptorCount = 1;
        descriptorWrites[5].pImageInfo = &environmentMapInfo;
        
        descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[6].dstSet = m_descriptorSets[i];
        descriptorWrites[6].dstBinding = 6;
        descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[6].descriptorCount = 1;
        descriptorWrites[6].pImageInfo = &localShadowInfo;

        vkUpdateDescriptorSets(m_device->device, 7, descriptorWrites, 0, nullptr);
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
    // Frames that skip the shadow pass (no scene, shadows off, ray-traced sun) still bind the map.
    transitionShadowMapForSampling(m_directionalShadow.image);
    
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
    // Match the main mesh pass: no culling so single-sided floors and
    // imported meshes with either winding still write shadow depth.
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
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
    
    // Enable dynamic viewport and scissor for CSM atlas rendering
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
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
    if (!m_device || !m_swapchain || !m_pipeline) return false;

    VkDevice device = m_device->device;
    size_t imageCount = m_swapchain->images.size();

    // ── 1. Load icon textures ────────────────────────────────────────────
    auto loadTex = [&](VulkanTexture& tex, const std::string& filename) -> bool {
        const std::string candidates[] = {
            "editor/assets/billboards/" + filename,
            "../editor/assets/billboards/" + filename,
            "build/Release/editor/assets/billboards/" + filename,
            "../build/Release/editor/assets/billboards/" + filename
        };
        for (const auto& path : candidates) {
            if (tex.LoadFromFile(m_device->allocator, device, m_device->physicalDevice,
                                 m_device->commandPool, m_device->graphicsQueue, path, false)) {
                std::cout << "[Billboard] Loaded texture: " << path << "\n";
                return true;
            }
        }
        return false;
    };

    constexpr size_t kIconCount = static_cast<size_t>(Hephaestus::EditorIcon::Count);
    std::array<bool, kIconCount> iconLoaded{};
    size_t loadedIcons = 0;
    for (size_t i = 0; i < kIconCount; ++i) {
        const char* file = Hephaestus::EditorIconFileName(static_cast<Hephaestus::EditorIcon>(i));
        iconLoaded[i] = loadTex(m_editorIconTextures[i], file);
        if (iconLoaded[i]) {
            ++loadedIcons;
        } else {
            std::cerr << "[Billboard] No icon image " << file << "; that icon is not drawn\n";
        }
    }

    // ── 2. Descriptor set layout for billboard: binding 0 = UBO, binding 1 = sampler
    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCI.bindingCount = 2;
    layoutCI.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &m_billboardPipeline.descriptorLayout) != VK_SUCCESS) {
        std::cerr << "[Billboard] Failed to create descriptor set layout\n";
        return false;
    }

    // ── 3. Descriptor pool (one set per loaded icon per frame)
    const uint32_t totalSets = static_cast<uint32_t>(imageCount * std::max<size_t>(loadedIcons, 1));
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, totalSets };
    poolSizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, totalSets };

    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.poolSizeCount = 2;
    poolCI.pPoolSizes = poolSizes;
    poolCI.maxSets = totalSets;

    if (vkCreateDescriptorPool(device, &poolCI, nullptr, &m_billboardDescriptorPool) != VK_SUCCESS) {
        std::cerr << "[Billboard] Failed to create descriptor pool\n";
        return false;
    }

    // ── 4. Allocate + write descriptor sets per light type per frame
    auto allocAndWrite = [&](std::vector<VkDescriptorSet>& sets, const VulkanTexture& tex) -> bool {
        sets.resize(imageCount);
        std::vector<VkDescriptorSetLayout> layouts(imageCount, m_billboardPipeline.descriptorLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_billboardDescriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
        allocInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(device, &allocInfo, sets.data()) != VK_SUCCESS)
            return false;

        for (size_t i = 0; i < imageCount; ++i) {
            VkDescriptorBufferInfo bufInfo{};
            bufInfo.buffer = m_uniformBuffers[i].buffer;
            bufInfo.offset = 0;
            bufInfo.range  = sizeof(UniformBufferObject);

            VkDescriptorImageInfo imgInfo{};
            imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imgInfo.imageView   = tex.imageView;
            imgInfo.sampler     = tex.sampler;

            VkWriteDescriptorSet writes[2] = {};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = sets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &bufInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = sets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &imgInfo;

            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }
        return true;
    };

    for (size_t i = 0; i < kIconCount; ++i) {
        if (iconLoaded[i] && !allocAndWrite(m_editorIconSets[i], m_editorIconTextures[i])) {
            std::cerr << "[Billboard] Failed to allocate/write descriptor sets\n";
            return false;
        }
    }

    // ── 5. Create the billboard graphics pipeline (reuse VulkanPipeline helper)
    auto bbPipeline = std::make_unique<VulkanPipeline>();
    bbPipeline->CreateBillboardPipeline(device, m_swapchain->renderPass,
                                        m_swapchain->extent, m_billboardPipeline.descriptorLayout);
    m_billboardPipeline.pipeline = bbPipeline->pipeline;
    m_billboardPipeline.layout   = bbPipeline->layout;
    // Prevent the helper from destroying handles we just took ownership of
    bbPipeline->pipeline = VK_NULL_HANDLE;
    bbPipeline->layout = VK_NULL_HANDLE;
    bbPipeline->ownsDescriptorSetLayout = false;

    // ── 6. Create billboard ID pipeline for entity picking ───────────────
    if (m_idPicker.initialized) {
        // Load billboard_id shaders
        auto loadShader = [device](const std::string& name) -> VkShaderModule {
            const std::string candidates[] = {
                "shaders/" + name, "../shaders/" + name,
                "build/Release/shaders/" + name, "../build/Release/shaders/" + name
            };
            for (const auto& path : candidates) {
                std::ifstream file(path, std::ios::ate | std::ios::binary);
                if (!file.is_open()) continue;
                size_t sz = static_cast<size_t>(file.tellg());
                std::vector<char> code(sz);
                file.seekg(0);
                file.read(code.data(), sz);
                file.close();
                VkShaderModuleCreateInfo ci{};
                ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                ci.codeSize = code.size();
                ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
                VkShaderModule sm;
                if (vkCreateShaderModule(device, &ci, nullptr, &sm) == VK_SUCCESS) return sm;
            }
            return VK_NULL_HANDLE;
        };

        VkShaderModule vertSM = loadShader("billboard_id.vert.spv");
        VkShaderModule fragSM = loadShader("billboard_id.frag.spv");

        if (vertSM && fragSM) {
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertSM;
            stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragSM;
            stages[1].pName = "main";

            VkPipelineVertexInputStateCreateInfo vertInput{};
            vertInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

            VkPipelineInputAssemblyStateCreateInfo ia{};
            ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

            VkPipelineViewportStateCreateInfo vps{};
            vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            vps.viewportCount = 1;
            vps.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rast{};
            rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rast.polygonMode = VK_POLYGON_MODE_FILL;
            rast.lineWidth = 1.0f;
            rast.cullMode = VK_CULL_MODE_NONE;
            rast.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

            VkPipelineMultisampleStateCreateInfo ms{};
            ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo ds{};
            ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            ds.depthTestEnable = VK_TRUE;
            ds.depthWriteEnable = VK_TRUE;
            ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

            VkPipelineColorBlendAttachmentState cba{};
            cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            cba.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo cb{};
            cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            cb.attachmentCount = 1;
            cb.pAttachments = &cba;

            VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynState{};
            dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynState.dynamicStateCount = 2;
            dynState.pDynamicStates = dynStates;

            // Push constants: vec3 pos(12) + float size(4) + uint entityId(4) + uint[3] padding(12) = 32
            VkPushConstantRange pcRange{};
            pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pcRange.offset = 0;
            pcRange.size = sizeof(BillboardIdPipeline::PushConstants);

            VkDescriptorSetLayout camLayout = m_pipeline->descriptorSetLayout;
            VkPipelineLayoutCreateInfo plCI{};
            plCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plCI.setLayoutCount = 1;
            plCI.pSetLayouts = &camLayout;
            plCI.pushConstantRangeCount = 1;
            plCI.pPushConstantRanges = &pcRange;

            if (vkCreatePipelineLayout(device, &plCI, nullptr, &m_billboardIdPipeline.layout) == VK_SUCCESS) {
                VkGraphicsPipelineCreateInfo pci{};
                pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pci.stageCount = 2;
                pci.pStages = stages;
                pci.pVertexInputState = &vertInput;
                pci.pInputAssemblyState = &ia;
                pci.pViewportState = &vps;
                pci.pRasterizationState = &rast;
                pci.pMultisampleState = &ms;
                pci.pDepthStencilState = &ds;
                pci.pColorBlendState = &cb;
                pci.pDynamicState = &dynState;
                pci.layout = m_billboardIdPipeline.layout;
                pci.renderPass = m_idPicker.renderPass;
                pci.subpass = 0;

                if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                              &m_billboardIdPipeline.pipeline) == VK_SUCCESS) {
                    m_billboardIdPipeline.initialized = true;
                    std::cout << "[Billboard] ID picking pipeline created\n";
                }
            }
        }
        if (vertSM) vkDestroyShaderModule(device, vertSM, nullptr);
        if (fragSM) vkDestroyShaderModule(device, fragSM, nullptr);
    }

    m_billboardPipeline.initialized = true;
    std::cout << "[Billboard] Resources initialized (" << imageCount << " frames)\n";
    return true;
}

// ============================================================================
// Private: Per-Frame Updates
// ============================================================================

void EngineFrameRenderer::updateCameraUBO(uint32_t imageIndex, const RenderCamera& camera,
                                         const FrameRenderConfig& config)
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
    const float rollR = camera.rollDeg * DEG_TO_RAD;
    const Mat4 viewFrame = Hephaestus::MakeCameraViewFrame(position, yawR, pitchR, rollR);
    const Vec3 up = rollR == 0.0f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{viewFrame.m[4], viewFrame.m[5], viewFrame.m[6]};
    
    UniformBufferObject ubo{};
    ubo.view = Mat4::LookAt(position, vadd(position, front), up);
    
    uint32_t safeWidth = std::max(m_swapchain->extent.width, 1u);
    uint32_t safeHeight = std::max(m_swapchain->extent.height, 1u);
    float aspect = static_cast<float>(safeWidth) / static_cast<float>(safeHeight);
    
    if (camera.projectionMode == ProjectionMode::Orthographic) {
        const float halfH = std::max(camera.orthoHalfExtent, 0.01f);
        const float halfW = halfH * aspect;
        ubo.proj = Mat4::Orthographic(-halfW, halfW, -halfH, halfH, camera.nearPlane, camera.farPlane);
    } else {
        const float fovDeg = (camera.fovDeg > 0.1f) ? camera.fovDeg : 60.0f;
        ubo.proj = Mat4::Perspective(fovDeg * DEG_TO_RAD, aspect, camera.nearPlane, camera.farPlane);
    }
    ubo.viewPos = position;
    m_frameViewMatrix = ubo.view;
    m_frameProjectionMatrix = ubo.proj;
    m_frameCameraPos = position;
    m_frameViewFrame = viewFrame;
    // Wireframe still uses Lit/Unlit shading branch; polygon mode is selected per draw.
    ubo.shadingMode = (config.shadingMode == ViewShadingMode::Unlit) ? 1.0f : 0.0f;
    
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
    lightUBO.environmentReflectionParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    
    // Default directional light
    lightUBO.dirLightDirIntensity = Vec4(0.0f, -1.0f, 0.0f, 0.5f);
    lightUBO.dirLightColor = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    
    // Default shadow parameters - disabled
    lightUBO.dirShadowParams = Vec4(0.005f, 0.5f, 1.0f / m_directionalShadow.resolution, 0.0f);
    lightUBO.dirLightViewProj = Mat4::Identity();
    
    lightUBO.pointLightCount = 0;
    lightUBO.spotLightCount = 0;
    for (int i = 0; i < 16; ++i) {
        lightUBO.pointShadowParams[i] = Vec4(-1.0f, 0.0f, 0.0f, 0.0f);
        lightUBO.spotShadowParams[i] = Vec4(-1.0f, 0.0f, 0.0f, 0.0f);
    }
    m_localShadowJobs.clear();
    m_traceShadows = false;
    m_sunRayTraced = false;
    const bool canTrace = m_probeGI.HardwareRayTracingAvailable();
    uint32_t nextShadowTile = 0;
    int shadowedPoints = 0;
    int shadowedSpots = 0;
    constexpr float kLocalShadowNear = 0.05f;
    auto addShadowTile = [&](const Vec3& pos, const Vec3& forward, const Vec3& up, float fovY, float range) {
        const Mat4 view = Mat4::LookAt(pos, vadd(pos, forward), up);
        const Mat4 proj = Mat4::Perspective(fovY, 1.0f, kLocalShadowNear, std::max(range, kLocalShadowNear * 2.0f));
        const uint32_t tile = nextShadowTile++;
        lightUBO.localShadowViewProj[tile] = Mat4::Multiply(proj, view);
        m_localShadowJobs.push_back({lightUBO.localShadowViewProj[tile], pos, range, tile});
    };

    // If scene is provided, gather lights from it
    if (scene) {
        auto envView = scene->GetEntitiesWith<EnvironmentComponent>();
        for (auto entityHandle : envView) {
            Entity entity(entityHandle, scene);
            const auto& env = entity.GetComponent<EnvironmentComponent>();
            const bool enableReflections =
                env.type == EnvironmentComponent::BackgroundType::HDRI &&
                env.useAsIBL &&
                !env.hdriPath.empty() &&
                m_skybox.hasHDRI;
            if (enableReflections) {
                lightUBO.environmentReflectionParams = Vec4(std::max(0.0f, env.reflectionIntensity), 1.0f,
                                                            1.0f, std::max(0.0f, env.hdriIntensity));
                for (int i = 0; i < 9; ++i) {
                    const auto& c = m_skybox.irradianceSH[i];
                    lightUBO.skyIrradianceSH[i] = Vec4(c[0], c[1], c[2], 0.0f);
                }
            }
            break;
        }

        auto lightView = scene->GetEntitiesWith<LightComponent>();
        int pointLightIndex = 0;
        int spotLightIndex = 0;
        
        for (auto entityHandle : lightView) {
            Entity entity(entityHandle, scene);
            auto& light = entity.GetComponent<LightComponent>();
            
            // Skip disabled lights
            if (!light.enabled) continue;

            Vec3 lightPos{0.0f, 0.0f, 0.0f};
            Vec3 lightDir{0.0f, -1.0f, 0.0f};
            
            if (entity.HasComponent<TransformComponent>()) {
                auto& transform = entity.GetComponent<TransformComponent>();
                lightPos = transform.position;
                
                // Match runtime semantics: directional and spot lights point
                // along transformed local -Z.
                if (light.type == LightType::Directional || light.type == LightType::Spot) {
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
                
                // Set up shadow matrix - CSM cascades
                if (m_directionalShadow.IsValid() && light.castShadows) {
                    constexpr float D2R = 3.1415926535f / 180.0f;
                    uint32_t sw = std::max(m_swapchain->extent.width, 1u);
                    uint32_t sh = std::max(m_swapchain->extent.height, 1u);
                    float aspect = static_cast<float>(sw) / static_cast<float>(sh);
                    const float fovDeg = camera.fovDeg > 0.1f ? camera.fovDeg : 60.0f;
                    float fovY = fovDeg * D2R;
                    
                    float maxRange = light.shadowAutoFit 
                        ? std::max(10.0f, std::min(camera.farPlane, light.shadowMaxDistance))
                        : light.shadowDistance;
                    
                    // Compute cascade splits
                    float splits[CSM_CASCADE_COUNT + 1];
                    ComputeCascadeSplits(camera.nearPlane, maxRange, splits);
                    
                    // Compute VP matrix for each cascade
                    float radii[CSM_CASCADE_COUNT] = {};
                    float depthRanges[CSM_CASCADE_COUNT] = {};
                    for (int c = 0; c < CSM_CASCADE_COUNT; c++) {
                        lightUBO.cascadeViewProj[c] = ComputeCascadeShadowVP(
                            camera.positionX, camera.positionY, camera.positionZ,
                            camera.yawDeg, camera.pitchDeg,
                            splits[c], splits[c + 1],
                            fovY, aspect, lightDir, &radii[c], &depthRanges[c]);
                    }
                    const float tileResolution = static_cast<float>(m_directionalShadow.resolution / 2);
                    lightUBO.cascadeTexelWorld = Vec4(2.0f * radii[0] / tileResolution, 2.0f * radii[1] / tileResolution,
                                                      2.0f * radii[2] / tileResolution, 2.0f * radii[3] / tileResolution);
                    lightUBO.cascadeDepthRange = Vec4(depthRanges[0], depthRanges[1], depthRanges[2], depthRanges[3]);
                    const float sunHalfAngle = std::clamp(light.sunAngularDiameter, 0.0f, 10.0f) * 0.5f * 0.01745329252f;
                    m_sunRayTraced = canTrace && light.rayTracedShadows;
                    m_traceShadows |= m_sunRayTraced;
                    lightUBO.shadowBiasWorld = Vec4(std::max(0.0f, light.shadowBias),
                                                    std::max(0.0f, light.shadowNormalBias), std::tan(sunHalfAngle),
                                                    m_sunRayTraced ? 1.0f : 0.0f);
                    lightUBO.cascadeSplits = Vec4(splits[1], splits[2], splits[3], splits[4]);
                    lightUBO.cascadeCount = CSM_CASCADE_COUNT;
                    
                    // Set first cascade as the legacy dirLightViewProj (vertex shader backward compat)
                    lightUBO.dirLightViewProj = lightUBO.cascadeViewProj[0];
                    
                    // Enable shadows - bias, strength, texel size in atlas UV space (full resolution), enabled
                    float cascadeTexelSize = 1.0f / static_cast<float>(m_directionalShadow.resolution);
                    lightUBO.dirShadowParams = Vec4(light.shadowBias, light.shadowStrength,
                                                    cascadeTexelSize, 1.0f);
                }
            } else if (light.type == LightType::Point && pointLightIndex < 16) {
                lightUBO.pointLightPositions[pointLightIndex] = {lightPos.x, lightPos.y, lightPos.z, light.range};
                lightUBO.pointLightColors[pointLightIndex] = {light.color.x, light.color.y, light.color.z, light.intensity};
                lightUBO.pointLightAttenuation[pointLightIndex] = {
                    light.constant, light.linear, light.quadratic, 0.0f};
                if (light.castShadows && canTrace && light.rayTracedShadows) {
                    lightUBO.pointShadowParams[pointLightIndex] = Vec4(-2.0f, light.shadowStrength, light.range, 0.0f);
                    m_traceShadows = true;
                } else if (light.castShadows && m_localShadow.IsValid() && shadowedPoints < kMaxShadowedPointLights) {
                    const float base = static_cast<float>(nextShadowTile);
                    const Vec3 faces[6] = {Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, 1, 0),
                                           Vec3(0, -1, 0), Vec3(0, 0, 1), Vec3(0, 0, -1)};
                    for (int f = 0; f < 6; ++f) {
                        const Vec3 up = (f == 2 || f == 3) ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
                        addShadowTile(lightPos, faces[f], up, 1.5707963f, light.range);
                    }
                    lightUBO.pointShadowParams[pointLightIndex] = Vec4(base, light.shadowStrength, light.range, 1.0f);
                    ++shadowedPoints;
                }
                ++pointLightIndex;
            } else if (light.type == LightType::Spot && spotLightIndex < 16) {
                lightUBO.spotLightPositions[spotLightIndex] = {lightPos.x, lightPos.y, lightPos.z, light.range};
                lightUBO.spotLightDirections[spotLightIndex] = {
                    lightDir.x, lightDir.y, lightDir.z, light.innerConeAngle};
                lightUBO.spotLightColors[spotLightIndex] = {light.color.x, light.color.y, light.color.z, light.intensity};
                lightUBO.spotLightAttenuation[spotLightIndex] = {
                    light.constant, light.linear, light.quadratic, light.outerConeAngle};
                if (light.castShadows && canTrace && light.rayTracedShadows) {
                    lightUBO.spotShadowParams[spotLightIndex] = Vec4(-2.0f, light.shadowStrength, light.range, 0.0f);
                    m_traceShadows = true;
                } else if (light.castShadows && m_localShadow.IsValid() && shadowedSpots < kMaxShadowedSpotLights) {
                    // outerConeAngle is the full aperture; pad it so filtering at the cone edge stays in the tile.
                    const float fovY = std::min(light.outerConeAngle + 0.1f, 2.8f);
                    const Vec3 up = std::fabs(lightDir.y) > 0.99f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
                    const float tile = static_cast<float>(nextShadowTile);
                    addShadowTile(lightPos, lightDir, up, fovY, light.range);
                    lightUBO.spotShadowParams[spotLightIndex] =
                        Vec4(tile, light.shadowStrength, light.range, std::tan(fovY * 0.5f));
                    ++shadowedSpots;
                }
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

void EngineFrameRenderer::recordShadowPass(VkCommandBuffer cmd, Scene* scene, const RenderCamera& camera,
                                           const FrameRenderConfig& config)
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
    auto& registry = scene->GetRegistry();
    bool hasDirectionalLight = false;
    Mat4 cascadeVPs[CSM_CASCADE_COUNT];
    
    auto lightView = registry.view<TransformComponent, LightComponent>();
    for (auto entity : lightView) {
        auto& light = lightView.get<LightComponent>(entity);
        if (light.type == LightType::Directional && light.castShadows && light.enabled) {
            auto& transform = lightView.get<TransformComponent>(entity);
            
            // Compute light direction from rotation
            Mat4 rotation = Mat4::RotationZ(transform.rotation.z);
            rotation = Mat4::Multiply(Mat4::RotationY(transform.rotation.y), rotation);
            rotation = Mat4::Multiply(Mat4::RotationX(transform.rotation.x), rotation);
            Vec3 lightDir = Mat4::TransformDirection(rotation, Vec3(0, 0, -1));
            lightDir = vnorm(lightDir);
            
            constexpr float D2R = 3.1415926535f / 180.0f;
            uint32_t sw = std::max(m_swapchain->extent.width, 1u);
            uint32_t sh = std::max(m_swapchain->extent.height, 1u);
            float aspect = static_cast<float>(sw) / static_cast<float>(sh);
            const float fovDeg = camera.fovDeg > 0.1f ? camera.fovDeg : 60.0f;
            float fovY = fovDeg * D2R;
            float maxRange = light.shadowAutoFit
                ? std::max(10.0f, std::min(camera.farPlane, light.shadowMaxDistance))
                : light.shadowDistance;
            
            float splits[CSM_CASCADE_COUNT + 1];
            ComputeCascadeSplits(camera.nearPlane, maxRange, splits);
            
            for (int c = 0; c < CSM_CASCADE_COUNT; c++) {
                cascadeVPs[c] = ComputeCascadeShadowVP(
                    camera.positionX, camera.positionY, camera.positionZ,
                    camera.yawDeg, camera.pitchDeg,
                    splits[c], splits[c + 1],
                    fovY, aspect, lightDir);
            }
            
            hasDirectionalLight = true;
            break;
        }
    }
    
    if (!hasDirectionalLight) return;
    
    // Begin shadow render pass (clears entire 4096x4096 atlas)
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
    
    // 2x2 atlas offsets for 4 cascades
    uint32_t halfRes = m_directionalShadow.resolution / 2;
    int atlasOffsetX[4] = { 0, 1, 0, 1 };
    int atlasOffsetY[4] = { 0, 0, 1, 1 };
    
    // Render each cascade into its atlas quadrant
    for (int cascade = 0; cascade < CSM_CASCADE_COUNT; cascade++) {
        // Set viewport and scissor to this cascade's quadrant
        VkViewport vp{};
        vp.x = static_cast<float>(atlasOffsetX[cascade] * halfRes);
        vp.y = static_cast<float>(atlasOffsetY[cascade] * halfRes);
        vp.width = static_cast<float>(halfRes);
        vp.height = static_cast<float>(halfRes);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        
        VkRect2D sc{};
        sc.offset = { static_cast<int32_t>(atlasOffsetX[cascade] * halfRes),
                      static_cast<int32_t>(atlasOffsetY[cascade] * halfRes) };
        sc.extent = { halfRes, halfRes };
        vkCmdSetScissor(cmd, 0, 1, &sc);
        
        drawShadowCasters(cmd, scene, config, cascadeVPs[cascade], nullptr, 0.0f);
    }

    static bool shadowPassDebugPrinted = false;
    if (!shadowPassDebugPrinted) {
        std::cout << "[ShadowPass] CSM: Rendered " << CSM_CASCADE_COUNT << " cascades\n";
        shadowPassDebugPrinted = true;
    }

    vkCmdEndRenderPass(cmd);
}

void EngineFrameRenderer::drawShadowCasters(VkCommandBuffer cmd, Scene* scene, const FrameRenderConfig& config,
                                            const Mat4& viewProj, const Vec3* cullCenter, float cullRadius)
{
    auto& registry = scene->GetRegistry();
    auto meshView = registry.view<TransformComponent, MeshRendererComponent>();
    struct ShadowPushConstants {
        Mat4 lightViewProj;
        Mat4 model;
    };
    {
        VkBuffer currentVertexBuffer = VK_NULL_HANDLE;
        VkBuffer currentIndexBuffer = VK_NULL_HANDLE;
        bool usingSkinnedShadow = false;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_directionalShadow.pipeline);

        for (auto entity : meshView) {
            auto& meshComp = meshView.get<MeshRendererComponent>(entity);
            if (!IsEditorEntityVisible(static_cast<uint32_t>(entity), config)) continue;
            if (!meshComp.HasValidMesh() || !meshComp.visible) continue;
            if (config.isPlaying && meshComp.hideInGame) continue;
            if (meshComp.viewModel || !meshComp.castShadows) continue;
            {
                Entity owner(entity, scene);
                const MaterialComponent* surface = resolveMaterialForSlot(owner, meshComp, 0);
                if (surface && surface->blendMode != MaterialBlendMode::Opaque) continue;
            }

            const CharacterComponent* character = registry.try_get<CharacterComponent>(entity);
            const std::vector<size_t> handles = MeshHandlesForDraw(meshComp);
            for (size_t handle : handles) {
                if (handle >= m_meshLibrary.size()) continue;
                const MeshGPU& mesh = m_meshLibrary[handle];
                VkDescriptorSet boneSet = resolveSkinnedBoneSet(entity, character, mesh);
                const bool skinned = mesh.skinned && boneSet != VK_NULL_HANDLE &&
                                     m_skinned.shadowPipeline != VK_NULL_HANDLE;
                if (skinned != usingSkinnedShadow) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      skinned ? m_skinned.shadowPipeline : m_directionalShadow.pipeline);
                    currentVertexBuffer = VK_NULL_HANDLE;
                    currentIndexBuffer = VK_NULL_HANDLE;
                    usingSkinnedShadow = skinned;
                }
                if (skinned) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skinned.shadowLayout,
                                            0, 1, &boneSet, 0, nullptr);
                }

                if (mesh.vertexBuffer.buffer != currentVertexBuffer) {
                    VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer.buffer, &offset);
                    currentVertexBuffer = mesh.vertexBuffer.buffer;
                }
                if (mesh.indexBuffer.buffer != currentIndexBuffer) {
                    vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                    currentIndexBuffer = mesh.indexBuffer.buffer;
                }

                ShadowPushConstants pc;
                pc.lightViewProj = viewProj;
                pc.model = CharacterAwareModelMatrix(scene, Entity(entity, scene), meshComp);
                if (cullCenter) {
                    // Bounding sphere of the mesh AABB against the light's range.
                    const Vec3 lo = mesh.bounds.min, hi = mesh.bounds.max;
                    const Vec3 local((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f);
                    const float* m = pc.model.m;
                    const Vec3 center(m[0] * local.x + m[4] * local.y + m[8] * local.z + m[12],
                                      m[1] * local.x + m[5] * local.y + m[9] * local.z + m[13],
                                      m[2] * local.x + m[6] * local.y + m[10] * local.z + m[14]);
                    const float scale = std::max({vlen(Vec3(m[0], m[1], m[2])), vlen(Vec3(m[4], m[5], m[6])),
                                                  vlen(Vec3(m[8], m[9], m[10]))});
                    const float radius = 0.5f * vlen(vsub(hi, lo)) * scale;
                    if (vlen(vsub(center, *cullCenter)) > cullRadius + radius) continue;
                }
                const VkPipelineLayout shadowLayout =
                    skinned ? m_skinned.shadowLayout : m_directionalShadow.pipelineLayout;
                vkCmdPushConstants(cmd, shadowLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                  0, sizeof(pc), &pc);

                vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
            }
        }
    }
}

bool EngineFrameRenderer::createLocalShadowResources()
{
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_D32_SFLOAT;
    image.extent = {kLocalShadowTile * kLocalShadowColumns, kLocalShadowTile * kLocalShadowRows, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(m_device->allocator, &image, &alloc, &m_localShadow.image, &m_localShadow.allocation,
                       nullptr) != VK_SUCCESS) {
        m_localShadow = ShadowMap{};
        return false;
    }
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = m_localShadow.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_D32_SFLOAT;
    view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = m_directionalShadow.renderPass;
    fb.attachmentCount = 1;
    fb.width = image.extent.width;
    fb.height = image.extent.height;
    fb.layers = 1;
    if (vkCreateImageView(m_device->device, &view, nullptr, &m_localShadow.imageView) != VK_SUCCESS ||
        (fb.pAttachments = &m_localShadow.imageView,
         vkCreateFramebuffer(m_device->device, &fb, nullptr, &m_localShadow.framebuffer) != VK_SUCCESS)) {
        if (m_localShadow.imageView) vkDestroyImageView(m_device->device, m_localShadow.imageView, nullptr);
        vmaDestroyImage(m_device->allocator, m_localShadow.image, m_localShadow.allocation);
        m_localShadow = ShadowMap{};
        return false;
    }
    // Frames without shadowed point/spot lights never render the atlas but still bind it.
    transitionShadowMapForSampling(m_localShadow.image);
    return true;
}

void EngineFrameRenderer::transitionShadowMapForSampling(VkImage image)
{
    VkCommandBufferAllocateInfo cmdAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAlloc.commandPool = m_device->commandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device->device, &cmdAlloc, &cmd);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(m_device->graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_device->graphicsQueue);
    vkFreeCommandBuffers(m_device->device, m_device->commandPool, 1, &cmd);
}

void EngineFrameRenderer::recordLocalShadowPass(VkCommandBuffer cmd, Scene* scene, const FrameRenderConfig& config)
{
    if (!scene) return;
    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = m_directionalShadow.renderPass;
    begin.framebuffer = m_localShadow.framebuffer;
    begin.renderArea.extent = {kLocalShadowTile * kLocalShadowColumns, kLocalShadowTile * kLocalShadowRows};
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    for (const LocalShadowJob& job : m_localShadowJobs) {
        const uint32_t x = (job.tile % kLocalShadowColumns) * kLocalShadowTile;
        const uint32_t y = (job.tile / kLocalShadowColumns) * kLocalShadowTile;
        const VkViewport viewport{static_cast<float>(x), static_cast<float>(y), static_cast<float>(kLocalShadowTile),
                                  static_cast<float>(kLocalShadowTile), 0.0f, 1.0f};
        const VkRect2D scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {kLocalShadowTile, kLocalShadowTile}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        drawShadowCasters(cmd, scene, config, job.viewProj, &job.lightPos, job.range);
    }
    vkCmdEndRenderPass(cmd);
}

// ============================================================================
// Material system helpers
// ============================================================================

const MaterialComponent* EngineFrameRenderer::resolveMaterialFromPath(const std::string& materialPath)
{
    if (materialPath.empty()) return nullptr;
    const auto now = std::chrono::steady_clock::now();
    auto known = m_materialPathChecks.find(materialPath);
    if (known != m_materialPathChecks.end() && now - known->second.checkedAt < kMaterialRecheckInterval) {
        return known->second.material;
    }
    const MaterialComponent* material = loadMaterialFromPath(materialPath);
    m_materialPathChecks[materialPath] = {material, now};
    return material;
}

const MaterialComponent* EngineFrameRenderer::loadMaterialFromPath(const std::string& materialPath)
{
    namespace fs = std::filesystem;
    fs::path resolvedPath = fs::path(materialPath);

    // Resolve relative paths using project root
    if (!resolvedPath.is_absolute()) {
        if (!m_projectRoot.empty()) {
            // Try: projectRoot/assets/<path>, then projectRoot/<path>
            fs::path assetsCandidate = fs::path(m_projectRoot) / "assets" / resolvedPath;
            fs::path rootCandidate = fs::path(m_projectRoot) / resolvedPath;
            std::error_code ec;
            if (fs::exists(assetsCandidate, ec)) {
                resolvedPath = assetsCandidate.lexically_normal();
            } else if (fs::exists(rootCandidate, ec)) {
                resolvedPath = rootCandidate.lexically_normal();
            } else {
                resolvedPath = resolvedPath.lexically_normal();
            }
        } else {
            resolvedPath = resolvedPath.lexically_normal();
        }
    } else {
        resolvedPath = resolvedPath.lexically_normal();
    }

    std::string cacheKey = resolvedPath.generic_string();

    std::error_code timeError;
    auto timestamp = fs::exists(resolvedPath, timeError)
        ? fs::last_write_time(resolvedPath, timeError)
        : fs::file_time_type{};
    if (timeError) return nullptr;

    // Check cache
    auto cached = m_materialAssetCache.find(cacheKey);
    if (cached != m_materialAssetCache.end()) {
        if (cached->second.lastWriteTime == timestamp) {
            return &cached->second.material;
        }
    }

    // Check failed cache
    auto failed = m_materialAssetCacheFailed.find(cacheKey);
    if (failed != m_materialAssetCacheFailed.end()) {
        if (failed->second == timestamp) return nullptr;
        m_materialAssetCacheFailed.erase(failed);
    }

    // Load material from .hematerial JSON file (standalone, no MaterialEditor dependency)
    MaterialComponent loadedMaterial;
    {
        std::ifstream inFile(resolvedPath);
        if (!inFile.is_open()) {
            std::cerr << "[EngineFrameRenderer] Cannot open material file: " << cacheKey << "\n";
            m_materialAssetCacheFailed[cacheKey] = timestamp;
            return nullptr;
        }
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(inFile);
        } catch (const std::exception& e) {
            std::cerr << "[EngineFrameRenderer] JSON parse error for '" << cacheKey << "': " << e.what() << "\n";
            m_materialAssetCacheFailed[cacheKey] = timestamp;
            return nullptr;
        }

        // Read PBR properties
        if (j.contains("albedo") && j["albedo"].is_array() && j["albedo"].size() >= 3) {
            loadedMaterial.albedo.x = j["albedo"][0].get<float>();
            loadedMaterial.albedo.y = j["albedo"][1].get<float>();
            loadedMaterial.albedo.z = j["albedo"][2].get<float>();
        }
        if (j.contains("metallic")) loadedMaterial.metallic = j["metallic"].get<float>();
        if (j.contains("roughness")) loadedMaterial.roughness = j["roughness"].get<float>();
        if (j.contains("ao")) loadedMaterial.ao = j["ao"].get<float>();
        if (j.contains("emissive") && j["emissive"].is_array() && j["emissive"].size() >= 3) {
            loadedMaterial.emissive.x = j["emissive"][0].get<float>();
            loadedMaterial.emissive.y = j["emissive"][1].get<float>();
            loadedMaterial.emissive.z = j["emissive"][2].get<float>();
        }
        if (j.contains("emissiveStrength")) loadedMaterial.emissiveStrength = j["emissiveStrength"].get<float>();
        if (j.contains("reflectionStrength")) loadedMaterial.reflectionStrength = j["reflectionStrength"].get<float>();
        if (j.contains("specular")) loadedMaterial.specular = j["specular"].get<float>();
        if (j.contains("opacity")) loadedMaterial.opacity = j["opacity"].get<float>();
        if (j.contains("opacityMask")) loadedMaterial.opacityMask = j["opacityMask"].get<float>();
        if (j.contains("refraction")) loadedMaterial.refraction = j["refraction"].get<float>();
        if (j.contains("blendMode") && j["blendMode"].is_string()) {
            const std::string mode = j["blendMode"].get<std::string>();
            loadedMaterial.blendMode = mode == "additive" ? MaterialBlendMode::Additive
                : mode == "alphaBlend" ? MaterialBlendMode::AlphaBlend : MaterialBlendMode::Opaque;
        }

        // Texture paths and flags
        if (j.contains("useAlbedoTexture")) loadedMaterial.useAlbedoTexture = j["useAlbedoTexture"].get<bool>();
        if (j.contains("albedoTexturePath")) loadedMaterial.albedoTexturePath = j["albedoTexturePath"].get<std::string>();
        if (j.contains("useNormalTexture")) loadedMaterial.useNormalTexture = j["useNormalTexture"].get<bool>();
        if (j.contains("normalTexturePath")) loadedMaterial.normalTexturePath = j["normalTexturePath"].get<std::string>();
        if (j.contains("useRoughnessTexture")) loadedMaterial.useRoughnessTexture = j["useRoughnessTexture"].get<bool>();
        if (j.contains("roughnessTexturePath")) loadedMaterial.roughnessTexturePath = j["roughnessTexturePath"].get<std::string>();
        if (j.contains("useMetallicTexture")) loadedMaterial.useMetallicTexture = j["useMetallicTexture"].get<bool>();
        if (j.contains("metallicTexturePath")) loadedMaterial.metallicTexturePath = j["metallicTexturePath"].get<std::string>();
        if (j.contains("useAoTexture")) loadedMaterial.useAoTexture = j["useAoTexture"].get<bool>();
        if (j.contains("aoTexturePath")) loadedMaterial.aoTexturePath = j["aoTexturePath"].get<std::string>();
        loadedMaterial.roughnessChannel = j.value("roughnessChannel", 0u) & 3u;
        loadedMaterial.metallicChannel = j.value("metallicChannel", 0u) & 3u;
        loadedMaterial.aoChannel = j.value("aoChannel", 0u) & 3u;
        if (j.contains("useWorldAlignedTexture")) loadedMaterial.useWorldAlignedTexture = j["useWorldAlignedTexture"].get<bool>();
        if (j.contains("worldTextureScale")) loadedMaterial.worldTextureScale = j["worldTextureScale"].get<float>();
    }

    // Update or insert cache entry
    if (cached != m_materialAssetCache.end()) {
        cached->second.material = loadedMaterial;
        cached->second.lastWriteTime = timestamp;
        return &cached->second.material;
    }

    MaterialCacheEntry entry;
    entry.material = loadedMaterial;
    entry.lastWriteTime = timestamp;
    auto insertResult = m_materialAssetCache.emplace(cacheKey, std::move(entry));
    return &insertResult.first->second.material;
}

uint32_t EngineFrameRenderer::ensureMaterialTextureSlot(const std::string& rawPath, bool linear)
{
    if (rawPath.empty()) return m_defaultMaterialTextureSlot;

    namespace fs = std::filesystem;
    fs::path normalizedInput = fs::path(rawPath).lexically_normal();
    std::string key = normalizedInput.generic_string() + (linear ? "|linear" : "");

    // Check if already loaded
    auto existingIt = m_materialTextureLookup.find(key);
    if (existingIt != m_materialTextureLookup.end()) return existingIt->second;

    if (m_materialTextureSlots.size() >= kMaxMaterialTextureSlots) {
        static bool overflowLogged = false;
        if (!overflowLogged) {
            std::cerr << "[EngineFrameRenderer] Material texture cache full (" << kMaxMaterialTextureSlots << "). Using default.\n";
            overflowLogged = true;
        }
        return m_defaultMaterialTextureSlot;
    }

    // Resolve path relative to project root
    std::vector<fs::path> candidates;
    candidates.push_back(normalizedInput);
    if (!m_projectRoot.empty()) {
        fs::path pr(m_projectRoot);
        candidates.push_back((pr / normalizedInput).lexically_normal());
        // Strip leading "assets/" if present and try under assets root
        std::string normalizedKey = normalizedInput.generic_string();
        bool startsWithAssets = (normalizedKey.rfind("assets/", 0) == 0 || normalizedKey.rfind("assets\\", 0) == 0);
        if (!startsWithAssets) {
            candidates.push_back((pr / "assets" / normalizedInput).lexically_normal());
        }
    }

    fs::path resolvedPath;
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            resolvedPath = candidate;
            break;
        }
    }

    if (resolvedPath.empty()) {
        static int texFailCount = 0;
        if (texFailCount < 10) {
            std::cerr << "[EngineFrameRenderer] Texture not found: " << rawPath << "\n";
            texFailCount++;
        }
        return m_defaultMaterialTextureSlot;
    }

    // Load texture
    MaterialTextureSlot newSlot;
    try {
        if (!newSlot.texture.LoadFromFile(m_device->allocator, m_device->device,
                                           m_device->physicalDevice, m_device->commandPool,
                                           m_device->graphicsQueue, resolvedPath.string(), true, !linear,
                                           m_maxMaterialTextureSize)) {
            std::cerr << "[EngineFrameRenderer] Failed to load texture: " << resolvedPath << "\n";
            return m_defaultMaterialTextureSlot;
        }
    } catch (const std::exception& e) {
        std::cerr << "[EngineFrameRenderer] Exception loading texture '" << resolvedPath << "': " << e.what() << "\n";
        return m_defaultMaterialTextureSlot;
    }

    newSlot.descriptor.sampler = newSlot.texture.sampler;
    newSlot.descriptor.imageView = newSlot.texture.imageView;
    newSlot.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    newSlot.key = key;
    newSlot.resolvedPath = resolvedPath.string();
    newSlot.linear = linear;
    newSlot.valid = true;

    uint32_t slotIndex = static_cast<uint32_t>(m_materialTextureSlots.size());
    m_materialTextureSlots.push_back(std::move(newSlot));
    m_materialTextureLookup.emplace(key, slotIndex);

    // Defer descriptor writes and apply them when each swapchain image is acquired.
    // This avoids updating descriptor sets that may still be in flight on the GPU.
    for (auto& pending : m_pendingTextureSlotUpdates) {
        pending.push_back(slotIndex);
    }

    std::cout << "[EngineFrameRenderer] Loaded material texture [" << slotIndex << "]: " << resolvedPath << "\n";
    return slotIndex;
}

void EngineFrameRenderer::SetMaxMaterialTextureSize(uint32_t pixels)
{
    if (pixels == m_maxMaterialTextureSize) {
        return;
    }
    m_maxMaterialTextureSize = pixels;
    if (!m_device) {
        return;
    }
    bool waited = false;
    uint32_t reloaded = 0;
    for (uint32_t index = 0; index < m_materialTextureSlots.size(); ++index) {
        auto& slot = m_materialTextureSlots[index];
        if (!slot.valid || slot.resolvedPath.empty()) {
            continue;
        }
        uint32_t wantedWidth = 0;
        uint32_t wantedHeight = 0;
        CappedTextureExtent(slot.texture.sourceWidth, slot.texture.sourceHeight, pixels, wantedWidth, wantedHeight);
        if (wantedWidth == slot.texture.width && wantedHeight == slot.texture.height) {
            continue;
        }
        if (!waited) {
            vkDeviceWaitIdle(m_device->device);
            waited = true;
        }
        VulkanTexture resized;
        if (!resized.LoadFromFile(m_device->allocator, m_device->device, m_device->physicalDevice,
                                   m_device->commandPool, m_device->graphicsQueue, slot.resolvedPath, true,
                                   !slot.linear, pixels)) {
            std::cerr << "[EngineFrameRenderer] Failed to reload texture at a new size: " << slot.resolvedPath << "\n";
            continue;
        }
        slot.texture.Destroy(m_device->allocator, m_device->device);
        slot.texture = resized;
        ++reloaded;
        slot.descriptor.sampler = slot.texture.sampler;
        slot.descriptor.imageView = slot.texture.imageView;
        for (auto& pending : m_pendingTextureSlotUpdates) {
            pending.push_back(index);
        }
    }
    std::cout << "[EngineFrameRenderer] Material texture size limit " << pixels << " (0 = full size); reloaded "
              << reloaded << " texture(s)\n";
}

void EngineFrameRenderer::updateEnvironmentReflectionDescriptor(uint32_t imageIndex)
{
    if (imageIndex >= m_descriptorSets.size() || !m_device) {
        return;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (m_skybox.hasHDRI && m_skybox.environmentTexture.imageView != VK_NULL_HANDLE) {
        imageInfo.imageView = m_skybox.environmentTexture.imageView;
        imageInfo.sampler = m_skybox.environmentTexture.sampler;
    } else {
        imageInfo.imageView = m_dummyTexture.imageView;
        imageInfo.sampler = m_dummyTexture.sampler;
    }

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSets[imageIndex];
    write.dstBinding = 5;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_device->device, 1, &write, 0, nullptr);
}

void EngineFrameRenderer::updateTextureSlotInDescriptors(uint32_t slotIndex, uint32_t imageIndex)
{
    if (slotIndex >= kMaxMaterialTextureSlots) return;
    if (slotIndex >= m_materialTextureSlots.size()) return;
    if (!m_materialTextureSlots[slotIndex].valid) return;
    if (imageIndex >= m_descriptorSets.size()) return;

    const auto& slot = m_materialTextureSlots[slotIndex];
    VkDescriptorImageInfo imageInfo = slot.descriptor;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSets[imageIndex];
    write.dstBinding = 3; // Texture array binding
    write.dstArrayElement = slotIndex;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_device->device, 1, &write, 0, nullptr);
}

void EngineFrameRenderer::flushPendingTextureDescriptorUpdates(uint32_t imageIndex)
{
    if (imageIndex >= m_pendingTextureSlotUpdates.size()) return;

    auto& pending = m_pendingTextureSlotUpdates[imageIndex];
    if (pending.empty()) return;

    std::sort(pending.begin(), pending.end());
    pending.erase(std::unique(pending.begin(), pending.end()), pending.end());

    for (uint32_t slotIndex : pending) {
        updateTextureSlotInDescriptors(slotIndex, imageIndex);
    }
    pending.clear();
}

EnvironmentComponent* EngineFrameRenderer::findEnvironment(Scene* scene)
{
    EnvironmentComponent* envComp = nullptr;
    
    // Prefer entity named "Environment"
    auto envView = scene->GetEntitiesWith<EnvironmentComponent>();
    for (auto ent : envView) {
        auto* tagComp = scene->GetRegistry().try_get<TagComponent>(ent);
        if (tagComp && tagComp->tag == "Environment") {
            envComp = &scene->GetRegistry().get<EnvironmentComponent>(ent);
            break;
        }
    }
    // Fallback: first entity with EnvironmentComponent
    if (!envComp) {
        for (auto ent : envView) {
            envComp = &scene->GetRegistry().get<EnvironmentComponent>(ent);
            break;
        }
    }
    return envComp;
}

void EngineFrameRenderer::prepareEnvironment(Scene* scene, const FrameRenderConfig& config,
                                             uint32_t imageIndex)
{
    EnvironmentComponent* envComp =
        (config.renderSkybox && m_skybox.initialized && scene) ? findEnvironment(scene) : nullptr;
    if (envComp && envComp->type == EnvironmentComponent::BackgroundType::HDRI &&
        !envComp->hdriPath.empty()) {
        if (envComp->hdriPath != m_lastSkyboxPath) {
            // Resolve the HDRI path
            namespace fs = std::filesystem;
            std::string resolvedPath;
            
            // Try multiple resolve strategies
            auto tryResolve = [](const std::string& candidate) -> bool {
                return std::filesystem::exists(candidate);
            };
            
            std::vector<std::string> candidates;
            
            // If path is already absolute
            if (fs::path(envComp->hdriPath).is_absolute()) {
                candidates.push_back(envComp->hdriPath);
            }
            
            // Strategy 1: relative to known project root (set by editor)
            if (!m_projectRoot.empty()) {
                fs::path pr(m_projectRoot);
                candidates.push_back((pr / envComp->hdriPath).string());
                candidates.push_back((pr / "assets" / envComp->hdriPath).string());
            }
            
            // Strategy 2: relative to working directory
            candidates.push_back(envComp->hdriPath);
            candidates.push_back("assets/" + envComp->hdriPath);
            candidates.push_back("../assets/" + envComp->hdriPath);
            
            // Strategy 3: walk up from CWD looking for .heproject (fallback)
            {
                fs::path cwd = fs::current_path();
                fs::path search = cwd;
                for (int i = 0; i < 10 && !search.empty(); ++i) {
                    std::error_code ec;
                    for (const auto& entry : fs::directory_iterator(search, fs::directory_options::skip_permission_denied, ec)) {
                        if (entry.path().extension() == ".heproject") {
                            candidates.push_back((search / envComp->hdriPath).string());
                            candidates.push_back((search / "assets" / envComp->hdriPath).string());
                            break;
                        }
                    }
                    auto parent = search.parent_path();
                    if (parent == search) break;
                    search = parent;
                }
            }
            
            for (const auto& candidate : candidates) {
                if (tryResolve(candidate)) {
                    resolvedPath = candidate;
                    break;
                }
            }
            
            if (!resolvedPath.empty()) {
                static bool pathLogPrinted = false;
                if (!pathLogPrinted) {
                    std::cout << "[Skybox] Resolved HDRI path: " << resolvedPath << "\n";
                    pathLogPrinted = true;
                }
                vkDeviceWaitIdle(m_device->device);
                if (m_skybox.LoadHDRI(resolvedPath, m_device->allocator,
                                      m_device->physicalDevice, m_device->commandPool,
                                      m_device->graphicsQueue, m_device->device)) {
                    m_lastSkyboxPath = envComp->hdriPath;
                }
            } else {
                static bool failLogPrinted = false;
                if (!failLogPrinted) {
                    std::cerr << "[Skybox] Could not resolve HDRI path: " << envComp->hdriPath << "\n";
                    failLogPrinted = true;
                }
            }
        }
    }
    updateEnvironmentReflectionDescriptor(imageIndex);
}

const MaterialComponent* EngineFrameRenderer::resolveMaterialForSlot(Entity& entity,
                                                                    const MeshRendererComponent& renderer,
                                                                    int slot)
{
    if (slot < 0) slot = 0;
    // 1) Check per-slot material path assignment (from ImGUI editor)
    if (static_cast<size_t>(slot) < renderer.appliedMaterialPaths.size()) {
        const std::string& slotPath = renderer.appliedMaterialPaths[slot];
        if (!slotPath.empty()) {
            if (const MaterialComponent* mat = resolveMaterialFromPath(slotPath)) {
                return mat;
            }
        }
    }
    // 2) The model's default material for this slot
    if (!renderer.meshAssetPath.empty()) {
        const std::vector<std::string>& defaults = meshDefaultMaterials(renderer.meshAssetPath);
        if (static_cast<size_t>(slot) < defaults.size() && !defaults[slot].empty()) {
            if (const MaterialComponent* mat = resolveMaterialFromPath(defaults[slot])) {
                return mat;
            }
        }
    }
    // 3) Fallback: entity's own MaterialComponent (single material for all submeshes)
    if (entity.HasComponent<MaterialComponent>()) {
        return &entity.GetComponent<MaterialComponent>();
    }
    // 4) No material assigned
    return nullptr;
}

const std::vector<std::string>& EngineFrameRenderer::meshDefaultMaterials(const std::string& meshAssetPath)
{
    const auto now = std::chrono::steady_clock::now();
    MeshDefaultsCheck& entry = m_meshDefaultMaterials[meshAssetPath];
    if (entry.checkedAt != std::chrono::steady_clock::time_point{} && now - entry.checkedAt < kMaterialRecheckInterval) {
        return entry.materials;
    }
    entry.checkedAt = now;
    std::filesystem::path model(meshAssetPath);
    if (meshAssetPath.rfind("builtin://", 0) == 0) {
        entry.materials.clear();
        return entry.materials;
    }
    if (!model.is_absolute() && !m_projectRoot.empty()) {
        std::error_code ec;
        const std::filesystem::path rooted = std::filesystem::path(m_projectRoot) / model;
        model = std::filesystem::exists(rooted, ec) ? rooted : std::filesystem::path(m_projectRoot) / "assets" / model;
    }
    entry.materials = Hephaestus::MeshSettings::LoadDefaultMaterials(model);
    return entry.materials;
}

// Static geometry the probes bake against and the probe sun map draws.
static bool IsProbeStaticMesh(Scene& scene, entt::entity handle, const MeshRendererComponent& renderer)
{
    if (!renderer.visible || renderer.hideInGame || renderer.viewModel || !renderer.HasValidMesh()) {
        return false;
    }
    const auto& registry = scene.GetRegistry();
    if (registry.all_of<CharacterComponent>(handle) || registry.all_of<CharacterControllerComponent>(handle)) {
        return false;
    }
    const auto* body = registry.try_get<RigidBodyComponent>(handle);
    return !body || body->IsStatic();
}

static bool FindShadowCastingSun(Scene* scene, Vec3& outDirection)
{
    auto view = scene->GetRegistry().view<TransformComponent, LightComponent>();
    for (auto entity : view) {
        const auto& light = view.get<LightComponent>(entity);
        if (light.type != LightType::Directional || !light.castShadows || !light.enabled) continue;
        const auto& transform = view.get<TransformComponent>(entity);
        Mat4 rotation = Mat4::RotationZ(transform.rotation.z);
        rotation = Mat4::Multiply(Mat4::RotationY(transform.rotation.y), rotation);
        rotation = Mat4::Multiply(Mat4::RotationX(transform.rotation.x), rotation);
        outDirection = vnorm(Mat4::TransformDirection(rotation, Vec3(0, 0, -1)));
        return true;
    }
    return false;
}

void EngineFrameRenderer::recordProbeGISunPass(VkCommandBuffer cmd, Scene* scene)
{
    // The previous frame's probe update sampled this map.
    VkMemoryBarrier reads{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    reads.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                         1, &reads, 0, nullptr, 0, nullptr);

    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = m_directionalShadow.renderPass;
    begin.framebuffer = m_probeGI.SunFramebuffer();
    begin.renderArea.extent = {ProbeGI::kSunMapResolution, ProbeGI::kSunMapResolution};
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(ProbeGI::kSunMapResolution),
                        static_cast<float>(ProbeGI::kSunMapResolution), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {ProbeGI::kSunMapResolution, ProbeGI::kSunMapResolution}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_directionalShadow.pipeline);

    struct ShadowPushConstants {
        Mat4 lightViewProj;
        Mat4 model;
    } pc;
    pc.lightViewProj = m_probeGI.SunViewProj();
    auto view = scene->GetRegistry().view<MeshRendererComponent>();
    for (entt::entity handle : view) {
        const auto& renderer = view.get<MeshRendererComponent>(handle);
        if (!IsProbeStaticMesh(*scene, handle, renderer)) continue;
        pc.model = scene->GetWorldTransform(Entity(handle, scene));
        for (size_t meshHandle : MeshHandlesForDraw(renderer)) {
            if (meshHandle >= m_meshLibrary.size()) continue;
            const MeshGPU& mesh = m_meshLibrary[meshHandle];
            if (mesh.skinned || mesh.indexCount == 0 || mesh.vertexBuffer.buffer == VK_NULL_HANDLE) continue;
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer.buffer, &offset);
            vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(cmd, m_directionalShadow.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        }
    }
    vkCmdEndRenderPass(cmd);

    VkMemoryBarrier written{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    written.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    written.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &written, 0, nullptr, 0, nullptr);
}

ProbeBakeGeometry EngineFrameRenderer::CollectProbeBakeGeometry(Scene& scene)
{
    ProbeBakeGeometry geometry;
    std::unordered_map<const MaterialComponent*, uint32_t> materialIndex;
    auto addMaterial = [&](const MaterialComponent* material) {
        const auto found = materialIndex.find(material);
        if (found != materialIndex.end()) {
            return found->second;
        }
        ProbeBakeMaterial baked;
        if (material) {
            baked.albedo = material->albedo;
            baked.emissive = vscale(material->emissive, material->emissiveStrength);
        }
        const uint32_t index = static_cast<uint32_t>(geometry.materials.size());
        geometry.materials.push_back(baked);
        materialIndex.emplace(material, index);
        return index;
    };

    auto view = scene.GetRegistry().view<MeshRendererComponent>();
    for (entt::entity handle : view) {
        Entity entity(handle, &scene);
        const MeshRendererComponent& renderer = view.get<MeshRendererComponent>(handle);
        if (!IsProbeStaticMesh(scene, handle, renderer)) {
            continue;
        }
        const Mat4 world = scene.GetWorldTransform(entity);
        auto toWorld = [&](const Vec3& p) {
            const float* m = world.m;
            return Vec3(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                        m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                        m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]);
        };
        for (size_t meshHandle : MeshHandlesForDraw(renderer)) {
            if (meshHandle >= m_meshLibrary.size()) {
                continue;
            }
            const MeshGPU& mesh = m_meshLibrary[meshHandle];
            if (mesh.skinned || mesh.cpuIndices.empty()) {
                continue;
            }
            auto addRange = [&](uint32_t first, uint32_t count, uint32_t material) {
                for (uint32_t i = first; i + 2 < first + count && i + 2 < mesh.cpuIndices.size(); i += 3) {
                    geometry.triangles.push_back({toWorld(mesh.cpuPositions[mesh.cpuIndices[i]]),
                                                  toWorld(mesh.cpuPositions[mesh.cpuIndices[i + 1]]),
                                                  toWorld(mesh.cpuPositions[mesh.cpuIndices[i + 2]]),
                                                  material});
                }
            };
            if (mesh.submeshes.empty()) {
                addRange(0, static_cast<uint32_t>(mesh.cpuIndices.size()),
                         addMaterial(resolveMaterialForSlot(entity, renderer, 0)));
            } else {
                for (const auto& submesh : mesh.submeshes) {
                    addRange(submesh.indexOffset, submesh.indexCount,
                             addMaterial(resolveMaterialForSlot(entity, renderer, submesh.materialSlot)));
                }
            }
        }
    }
    return geometry;
}

void EngineFrameRenderer::recordMainPass(VkCommandBuffer cmd, uint32_t imageIndex, 
                                          Scene* scene, const FrameRenderConfig& config)
{
    VkClearValue clearValues[3];
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    clearValues[2].color = {{DisplayToSceneLinear(config.clearColorR),
                             DisplayToSceneLinear(config.clearColorG),
                             DisplayToSceneLinear(config.clearColorB), 1.0f}};
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    m_toneMap.bloomThisFrame = bloomEnabled(scene);
    // The tone map always samples the bloom image, so it must leave UNDEFINED even in scenes without bloom.
    if (m_bloom.image && !m_bloom.layoutReady) {
        VkImageMemoryBarrier toGeneral{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        toGeneral.srcQueueFamilyIndex = toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image = m_bloom.image;
        toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(m_bloom.mipSizes.size()), 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &toGeneral);
        m_bloom.layoutReady = true;
    }
    renderPassInfo.renderPass =
        m_toneMap.bloomThisFrame ? m_swapchain->sceneOnlyRenderPass : m_swapchain->renderPass;
    renderPassInfo.framebuffer = m_swapchain->framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain->extent;
    renderPassInfo.clearValueCount = 3;
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
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // In play, view models own the nearest slice of the depth range and draw-over-world meshes the next one, so
    // world geometry never covers either and the player's own body never covers the arms and weapon.
    constexpr float kViewModelDepthEnd = 0.025f;
    constexpr float kOverWorldDepthEnd = 0.05f;
    std::optional<Mat4> viewModelRebase;
    if (scene && config.isPlaying) {
        if (const auto activeCamera = Hephaestus::TryReadActiveCameraView(*scene)) {
            const Mat4 cameraWorld = scene->GetWorldTransform(Entity(activeCamera->entity, scene));
            viewModelRebase = Mat4::Multiply(m_frameViewFrame, Mat4::Invert(cameraWorld));
            viewModelRebase = Mat4::Multiply(
                Hephaestus::MakeViewModelFovAdjust(m_frameViewMatrix, activeCamera->fovDegrees,
                                                   activeCamera->viewModelFovDegrees),
                *viewModelRebase);
        }
    }
    enum DepthLayer { kWorldLayer = 0, kOverWorldLayer = 1, kViewModelLayer = 2 };
    VkViewport layerViewports[3] = {viewport, viewport, viewport};
    if (viewModelRebase) {
        layerViewports[kWorldLayer].minDepth = kOverWorldDepthEnd;
        layerViewports[kOverWorldLayer].minDepth = kViewModelDepthEnd;
        layerViewports[kOverWorldLayer].maxDepth = kOverWorldDepthEnd;
        layerViewports[kViewModelLayer].maxDepth = kViewModelDepthEnd;
    }
    int boundDepthLayer = kWorldLayer;
    vkCmdSetViewport(cmd, 0, 1, &layerViewports[kWorldLayer]);

    // ── Skybox / Environment background ──────────────────────────────
    if (config.renderSkybox && m_skybox.initialized && scene) {
        EnvironmentComponent* envComp = findEnvironment(scene);
        
        if (envComp) {
            Skybox::SkyboxPushConstants pc;
            
            if (envComp->type == EnvironmentComponent::BackgroundType::HDRI &&
                !envComp->hdriPath.empty()) {
                
                
                if (m_skybox.hasHDRI) {
                    pc.intensity = envComp->hdriIntensity;
                    pc.useSolidColor = 0;
                } else {
                    // Fallback to solid color if HDRI failed to load
                    pc.solidColorR = envComp->solidColor.x;
                    pc.solidColorG = envComp->solidColor.y;
                    pc.solidColorB = envComp->solidColor.z;
                    pc.useSolidColor = 1;
                    pc.intensity = 1.0f;
                }
            } else {
                // SolidColor mode
                pc.solidColorR = envComp->solidColor.x;
                pc.solidColorG = envComp->solidColor.y;
                pc.solidColorB = envComp->solidColor.z;
                pc.useSolidColor = 1;
                pc.intensity = 1.0f;
            }
            
            m_skybox.UpdateDescriptorSet(imageIndex, m_uniformBuffers[imageIndex].buffer, m_device->device);
            m_skybox.Draw(cmd, imageIndex, pc);
        }
    }
    // ── End Skybox ───────────────────────────────────────────────────
    
    // Bind pipeline dynamically per-object (backface culling toggle)
    VkPipeline currentPipeline = VK_NULL_HANDLE;
    
    // Render scene meshes if we have a scene
    static std::string lastSceneName;
    bool debugPrint = false;
    if (scene) {
        std::string sceneName = scene->GetName();
        if (sceneName != lastSceneName) {
            debugPrint = true;
            lastSceneName = sceneName;
            m_materialDebugLogged = false;
            m_materialDebugDrawCount = 0;
        }
    }
    
    if (scene && !m_meshLibrary.empty()) {
        auto renderView = scene->GetEntitiesWith<TransformComponent, MeshRendererComponent>();
        
        int drawnCount = 0;
        int skippedCount = 0;

        // ── Map the dynamic material UBO for this frame ──────────
        void* matMappedBaseVoid = nullptr;
        vmaMapMemory(m_device->allocator, m_materialBuffers[imageIndex].allocation, &matMappedBaseVoid);
        uint8_t* matMappedBase = reinterpret_cast<uint8_t*>(matMappedBaseVoid);
        uint32_t matSlotNext = 0; // Slot 0 is the default material (already written at init time, but we re-write it each frame for consistency)

        // Write default material at slot 0
        {
            MaterialUBO defaultMat{};
            defaultMat.albedo = Vec3(1.0f, 1.0f, 1.0f);
            defaultMat.albedoPadding = 0.0f;
            defaultMat.metallic = 0.0f;
            defaultMat.metallicPadding0 = 0.0f;
            defaultMat.metallicPadding1 = 0.0f;
            defaultMat.metallicPadding2 = 0.0f;
            defaultMat.emissive = Vec3(0.0f, 0.0f, 0.0f);
            defaultMat.emissivePadding = 0.0f;
            defaultMat.roughness = 0.5f;
            defaultMat.ao = 1.0f;
            defaultMat.emissiveStrength = 0.0f;
            defaultMat.useAlbedoTexture = 0;
            defaultMat.albedoTextureIndex = m_defaultMaterialTextureSlot;
            defaultMat.useNormalTexture = 0;
            defaultMat.normalTextureIndex = m_defaultMaterialTextureSlot;
            defaultMat.useRoughnessTexture = 0;
            defaultMat.roughnessTextureIndex = m_defaultMaterialTextureSlot;
            defaultMat.useMetallicTexture = 0;
            defaultMat.metallicTextureIndex = m_defaultMaterialTextureSlot;
            defaultMat.useAoTexture = 0;
            defaultMat.aoTextureIndex = m_defaultMaterialTextureSlot;
            defaultMat.useWorldAlignedTexture = 0;
            defaultMat.worldTextureScale = 1.0f;
            defaultMat.blendMode = 0u;
            defaultMat.textureChannels = 0u;
            std::memcpy(matMappedBase, &defaultMat, sizeof(defaultMat));
            matSlotNext = 1;
        }

        // Lambda: build MaterialUBO from MaterialComponent
        auto buildMaterialUBO = [&](const MaterialComponent* material, MaterialUBO& matUBO) {
            if (material) {
                const auto& mat = *material;
                matUBO.albedo = mat.albedo;
                matUBO.albedoPadding = 0.0f;
                matUBO.metallic = mat.metallic;
                matUBO.metallicPadding0 = 0.0f;
                matUBO.metallicPadding1 = 0.0f;
                matUBO.metallicPadding2 = 0.0f;
                matUBO.emissive = mat.emissive;
                matUBO.emissivePadding = 0.0f;
                matUBO.roughness = mat.roughness;
                matUBO.ao = mat.ao;
                matUBO.emissiveStrength = mat.emissiveStrength;

                // Albedo texture
                uint32_t albedoSlot = m_defaultMaterialTextureSlot;
                if (mat.useAlbedoTexture && !mat.albedoTexturePath.empty()) {
                    albedoSlot = ensureMaterialTextureSlot(mat.albedoTexturePath);
                    matUBO.useAlbedoTexture = (albedoSlot != m_defaultMaterialTextureSlot) ? 1u : 0u;
                } else {
                    matUBO.useAlbedoTexture = 0u;
                }
                matUBO.albedoTextureIndex = albedoSlot;

                // Normal texture
                uint32_t normalSlot = m_defaultMaterialTextureSlot;
                if (mat.useNormalTexture && !mat.normalTexturePath.empty()) {
                    normalSlot = ensureMaterialTextureSlot(mat.normalTexturePath, true);
                    matUBO.useNormalTexture = (normalSlot != m_defaultMaterialTextureSlot) ? 1u : 0u;
                } else {
                    matUBO.useNormalTexture = 0u;
                }
                matUBO.normalTextureIndex = normalSlot;

                // Roughness texture
                uint32_t roughnessSlot = m_defaultMaterialTextureSlot;
                if (mat.useRoughnessTexture && !mat.roughnessTexturePath.empty()) {
                    roughnessSlot = ensureMaterialTextureSlot(mat.roughnessTexturePath, true);
                    matUBO.useRoughnessTexture = (roughnessSlot != m_defaultMaterialTextureSlot) ? 1u : 0u;
                } else {
                    matUBO.useRoughnessTexture = 0u;
                }
                matUBO.roughnessTextureIndex = roughnessSlot;

                // Metallic texture
                uint32_t metallicSlot = m_defaultMaterialTextureSlot;
                if (mat.useMetallicTexture && !mat.metallicTexturePath.empty()) {
                    metallicSlot = ensureMaterialTextureSlot(mat.metallicTexturePath, true);
                    matUBO.useMetallicTexture = (metallicSlot != m_defaultMaterialTextureSlot) ? 1u : 0u;
                } else {
                    matUBO.useMetallicTexture = 0u;
                }
                matUBO.metallicTextureIndex = metallicSlot;

                // AO texture
                uint32_t aoSlot = m_defaultMaterialTextureSlot;
                if (mat.useAoTexture && !mat.aoTexturePath.empty()) {
                    aoSlot = ensureMaterialTextureSlot(mat.aoTexturePath, true);
                    matUBO.useAoTexture = (aoSlot != m_defaultMaterialTextureSlot) ? 1u : 0u;
                } else {
                    matUBO.useAoTexture = 0u;
                }
                matUBO.aoTextureIndex = aoSlot;

                // World-aligned texture settings
                matUBO.useWorldAlignedTexture = mat.useWorldAlignedTexture ? 1u : 0u;
                matUBO.worldTextureScale = mat.worldTextureScale;
                matUBO.reflectionStrength = mat.reflectionStrength;
                matUBO.specular = mat.specular;
                matUBO.opacity = mat.opacity;
                matUBO.opacityMask = mat.opacityMask;
                matUBO.refraction = mat.refraction;
                matUBO.blendMode = static_cast<uint32_t>(mat.blendMode);
                matUBO.textureChannels = (mat.roughnessChannel & 3u) | ((mat.metallicChannel & 3u) << 2) |
                                         ((mat.aoChannel & 3u) << 4);
            } else {
                // Default white material
                matUBO.albedo = Vec3(1.0f, 1.0f, 1.0f);
                matUBO.albedoPadding = 0.0f;
                matUBO.metallic = 0.0f;
                matUBO.metallicPadding0 = 0.0f;
                matUBO.metallicPadding1 = 0.0f;
                matUBO.metallicPadding2 = 0.0f;
                matUBO.roughness = 0.5f;
                matUBO.ao = 1.0f;
                matUBO.emissive = Vec3(0.0f, 0.0f, 0.0f);
                matUBO.emissivePadding = 0.0f;
                matUBO.emissiveStrength = 0.0f;
                matUBO.useAlbedoTexture = 0u;
                matUBO.albedoTextureIndex = m_defaultMaterialTextureSlot;
                matUBO.useNormalTexture = 0u;
                matUBO.normalTextureIndex = m_defaultMaterialTextureSlot;
                matUBO.useRoughnessTexture = 0u;
                matUBO.roughnessTextureIndex = m_defaultMaterialTextureSlot;
                matUBO.useMetallicTexture = 0u;
                matUBO.metallicTextureIndex = m_defaultMaterialTextureSlot;
                matUBO.useAoTexture = 0u;
                matUBO.aoTextureIndex = m_defaultMaterialTextureSlot;
                matUBO.useWorldAlignedTexture = 0u;
                matUBO.worldTextureScale = 1.0f;
                matUBO.reflectionStrength = 1.0f;
                matUBO.specular = 0.5f;
                matUBO.opacity = 1.0f;
                matUBO.opacityMask = 1.0f;
                matUBO.refraction = 0.0f;
                matUBO.blendMode = 0u;
                matUBO.textureChannels = 0u;
            }
        };
        
        VkBuffer currentVertexBuffer = VK_NULL_HANDLE;
        VkBuffer currentIndexBuffer = VK_NULL_HANDLE;
        bool materialOverflowLogged = false;
        VkPipelineLayout currentDrawLayout = m_pipeline->layout;
        VkPipelineLayout probeSetLayout = VK_NULL_HANDLE;  // layout the probe set was last bound with
        VkDescriptorSet currentBoneSet = VK_NULL_HANDLE;
        
        // Lambda: upload material to dynamic UBO and issue draw call
        struct TranslucentDraw {
            const MeshGPU* mesh = nullptr;
            const MaterialComponent* material = nullptr;
            Mat4 model;
            uint32_t indexOffset = 0;
            uint32_t indexCount = 0;
            int depthLayer = 0;
            float distance = 0.0f;
            const char* name = nullptr;
            int submesh = 0;
            int slot = 0;
        };
        std::vector<TranslucentDraw> translucentDraws;

        auto drawWithMaterial = [&](const MaterialComponent* material,
                                    uint32_t indexOffset, uint32_t indexCount,
                                    const char* entityName, int submeshIdx, int submeshMatSlot) {
            if (indexCount == 0) return;
            if (matSlotNext >= kMaxMaterialsPerFrame) {
                if (!materialOverflowLogged) {
                    std::cerr << "[EngineFrameRenderer] Material UBO capacity exceeded (" 
                              << kMaxMaterialsPerFrame << "). Remaining draws use default material.\n";
                    materialOverflowLogged = true;
                }
                // Fall back to slot 0 (default material)
                uint32_t dynOffset = 0;
                if (currentBoneSet != VK_NULL_HANDLE) {
                    VkDescriptorSet sets[] = {m_descriptorSets[imageIndex], currentBoneSet};
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, currentDrawLayout,
                                            0, 2, sets, 1, &dynOffset);
                } else {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, currentDrawLayout,
                                            0, 1, &m_descriptorSets[imageIndex], 1, &dynOffset);
                }
                vkCmdDrawIndexed(cmd, indexCount, 1, indexOffset, 0, 0);
                drawnCount++;
                return;
            }

            MaterialUBO matUBO = {};
            buildMaterialUBO(material, matUBO);

            VkDeviceSize matOffsetBytes = static_cast<VkDeviceSize>(matSlotNext) * m_materialStride;
            std::memcpy(matMappedBase + matOffsetBytes, &matUBO, sizeof(matUBO));

            uint32_t dynOffset = static_cast<uint32_t>(matOffsetBytes);
            if (currentBoneSet != VK_NULL_HANDLE) {
                VkDescriptorSet sets[] = {m_descriptorSets[imageIndex], currentBoneSet};
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, currentDrawLayout,
                                        0, 2, sets, 1, &dynOffset);
            } else {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, currentDrawLayout,
                                        0, 1, &m_descriptorSets[imageIndex], 1, &dynOffset);
            }
            vkCmdDrawIndexed(cmd, indexCount, 1, indexOffset, 0, 0);

            // Debug logging for first 10 draws when scene changes
            if (!m_materialDebugLogged && m_materialDebugDrawCount < 10) {
                std::cout << "[MaterialDebug] entity='" << (entityName ? entityName : "?") 
                          << "' submesh=" << submeshIdx 
                          << " matSlot=" << submeshMatSlot
                          << " uboSlot=" << matSlotNext 
                          << " dynOffset=" << dynOffset
                          << " albedo=(" << matUBO.albedo.x << "," << matUBO.albedo.y << "," << matUBO.albedo.z << ")"
                          << " albedoTex=" << matUBO.albedoTextureIndex
                          << " useAlbedoTex=" << matUBO.useAlbedoTexture
                          << "\n";
                m_materialDebugDrawCount++;
            }

            matSlotNext++;
            drawnCount++;
        };

        for (auto entityHandle : renderView) {
            Entity entity(entityHandle, scene);
            auto& transform = entity.GetComponent<TransformComponent>();
            auto& renderer = entity.GetComponent<MeshRendererComponent>();
            if (!IsEditorEntityVisible(static_cast<uint32_t>(entityHandle), config)) {
                skippedCount++;
                continue;
            }
            
            if (!renderer.visible) {
                // Load a hidden mesh's textures now (with the scene) rather than on the frame it first shows.
                if (!renderer.texturesWarmed && renderer.HasValidMesh()) {
                    renderer.texturesWarmed = true;
                    const int slots = std::max(1, std::max(renderer.submeshCount,
                                                           static_cast<int>(renderer.appliedMaterialPaths.size())));
                    for (int slot = 0; slot < slots; ++slot) {
                        if (const MaterialComponent* mat = resolveMaterialForSlot(entity, renderer, slot)) {
                            if (mat->useAlbedoTexture && !mat->albedoTexturePath.empty()) {
                                ensureMaterialTextureSlot(mat->albedoTexturePath);
                            }
                            if (mat->useNormalTexture && !mat->normalTexturePath.empty()) {
                                ensureMaterialTextureSlot(mat->normalTexturePath, true);
                            }
                            if (mat->useRoughnessTexture && !mat->roughnessTexturePath.empty()) {
                                ensureMaterialTextureSlot(mat->roughnessTexturePath, true);
                            }
                            if (mat->useMetallicTexture && !mat->metallicTexturePath.empty()) {
                                ensureMaterialTextureSlot(mat->metallicTexturePath, true);
                            }
                            if (mat->useAoTexture && !mat->aoTexturePath.empty()) {
                                ensureMaterialTextureSlot(mat->aoTexturePath, true);
                            }
                        }
                    }
                }
                skippedCount++;
                continue;
            }
            if (config.isPlaying && renderer.hideInGame) {
                skippedCount++;
                continue;
            }
            if (!renderer.HasValidMesh()) {
                skippedCount++;
                continue;
            }

            const CharacterComponent* character = entity.HasComponent<CharacterComponent>()
                ? &entity.GetComponent<CharacterComponent>()
                : nullptr;
            const std::vector<size_t> handles = MeshHandlesForDraw(renderer);
            bool drewAny = false;
            for (size_t handle : handles) {
                if (handle >= m_meshLibrary.size()) {
                    continue;
                }
                MeshGPU& mesh = m_meshLibrary[handle];
                if (mesh.indexCount == 0 || mesh.vertexBuffer.buffer == VK_NULL_HANDLE ||
                    mesh.indexBuffer.buffer == VK_NULL_HANDLE) {
                    continue;
                }

                const bool wantWireframe = (config.shadingMode == ViewShadingMode::Wireframe);
                VkDescriptorSet boneSet = resolveSkinnedBoneSet(entityHandle, character, mesh);
                const bool skinned = mesh.skinned && boneSet != VK_NULL_HANDLE &&
                                     m_skinned.pipeline != VK_NULL_HANDLE;
                VkPipeline desiredPipeline = skinned
                    ? getSkinnedPipeline(renderer.backfaceCulling, wantWireframe)
                    : m_pipeline->GetPipeline(renderer.backfaceCulling, wantWireframe);
                if (desiredPipeline == VK_NULL_HANDLE) {
                    continue;
                }
                if (desiredPipeline != currentPipeline) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, desiredPipeline);
                    currentPipeline = desiredPipeline;
                }
                currentDrawLayout = skinned ? m_skinned.pipelineLayout : m_pipeline->layout;
                currentBoneSet = skinned ? boneSet : VK_NULL_HANDLE;
                if (currentDrawLayout != probeSetLayout) {
                    // Set 2 layouts differ in set 1 between the two pipelines, so switching disturbs it.
                    const VkDescriptorSet probeSet = m_probeGI.DrawSet(imageIndex);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, currentDrawLayout, 2, 1, &probeSet,
                                            0, nullptr);
                    probeSetLayout = currentDrawLayout;
                }

                if (mesh.vertexBuffer.buffer != currentVertexBuffer) {
                    VkBuffer vb = mesh.vertexBuffer.buffer;
                    VkDeviceSize vbOffset = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
                    currentVertexBuffer = vb;
                }

                if (mesh.indexBuffer.buffer != currentIndexBuffer) {
                    vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                    currentIndexBuffer = mesh.indexBuffer.buffer;
                }

                Mat4 modelMatrix = CharacterAwareModelMatrix(scene, entity, renderer);
                const int depthLayer = !viewModelRebase       ? kWorldLayer
                                       : renderer.viewModel     ? kViewModelLayer
                                       : renderer.drawOverWorld ? kOverWorldLayer
                                                                : kWorldLayer;
                if (depthLayer == kViewModelLayer) {
                    modelMatrix = Mat4::Multiply(*viewModelRebase, modelMatrix);
                }
                if (depthLayer != boundDepthLayer) {
                    vkCmdSetViewport(cmd, 0, 1, &layerViewports[depthLayer]);
                    boundDepthLayer = depthLayer;
                }
                vkCmdPushConstants(cmd, currentDrawLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                  0, sizeof(Mat4), modelMatrix.m);

                const char* entityName = nullptr;
                if (entity.HasComponent<TagComponent>()) {
                    entityName = entity.GetComponent<TagComponent>().tag.c_str();
                }

                // Translucent submeshes of unskinned meshes are drawn after every opaque surface.
                auto drawOrDefer = [&](const MaterialComponent* mat, uint32_t offset, uint32_t count, int si, int slot) {
                    if (mat && mat->blendMode != MaterialBlendMode::Opaque && !skinned &&
                        m_pipeline->GetTranslucentPipeline(1) != VK_NULL_HANDLE) {
                        TranslucentDraw draw;
                        draw.mesh = &mesh;
                        draw.material = mat;
                        draw.model = modelMatrix;
                        draw.indexOffset = offset;
                        draw.indexCount = count;
                        draw.depthLayer = depthLayer;
                        draw.distance = vlen(vsub(Vec3(modelMatrix.m[12], modelMatrix.m[13], modelMatrix.m[14]),
                                                  m_frameCameraPos));
                        draw.name = entityName;
                        draw.submesh = si;
                        draw.slot = slot;
                        translucentDraws.push_back(draw);
                        return;
                    }
                    drawWithMaterial(mat, offset, count, entityName, si, slot);
                };
                if (mesh.submeshes.empty()) {
                    drawOrDefer(resolveMaterialForSlot(entity, renderer, 0), 0, mesh.indexCount, 0, 0);
                } else {
                    for (size_t si = 0; si < mesh.submeshes.size(); ++si) {
                        const auto& submesh = mesh.submeshes[si];
                        if (submesh.indexCount == 0) continue;
                        drawOrDefer(resolveMaterialForSlot(entity, renderer, submesh.materialSlot),
                                    submesh.indexOffset, submesh.indexCount, static_cast<int>(si),
                                    submesh.materialSlot);
                    }
                }
                drewAny = true;
            }
            if (!drewAny) {
                skippedCount++;
            }
        }
        
        const size_t cameraMeshHandle = GetMeshHandle(kEditorCameraMeshKey);
        if (cameraMeshHandle < m_meshLibrary.size() && m_meshLibrary[cameraMeshHandle].indexCount > 0) {
            MeshGPU& mesh = m_meshLibrary[cameraMeshHandle];
            static const MaterialComponent cameraMaterial(Vec3(0.6f, 0.6f, 0.7f));
            for (const auto& [handle, model] : EditorCameraMeshes(*scene, config)) {
                VkPipeline pipeline = m_pipeline->GetPipeline(false, config.shadingMode == ViewShadingMode::Wireframe);
                if (pipeline == VK_NULL_HANDLE) break;
                if (pipeline != currentPipeline) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    currentPipeline = pipeline;
                }
                currentDrawLayout = m_pipeline->layout;
                currentBoneSet = VK_NULL_HANDLE;
                if (currentDrawLayout != probeSetLayout) {
                    const VkDescriptorSet probeSet = m_probeGI.DrawSet(imageIndex);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, currentDrawLayout, 2, 1, &probeSet,
                                            0, nullptr);
                    probeSetLayout = currentDrawLayout;
                }
                if (boundDepthLayer != kWorldLayer) {
                    vkCmdSetViewport(cmd, 0, 1, &layerViewports[kWorldLayer]);
                    boundDepthLayer = kWorldLayer;
                }
                if (mesh.vertexBuffer.buffer != currentVertexBuffer) {
                    VkDeviceSize vbOffset = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer.buffer, &vbOffset);
                    currentVertexBuffer = mesh.vertexBuffer.buffer;
                }
                if (mesh.indexBuffer.buffer != currentIndexBuffer) {
                    vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                    currentIndexBuffer = mesh.indexBuffer.buffer;
                }
                vkCmdPushConstants(cmd, currentDrawLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), model.m);
                drawWithMaterial(&cameraMaterial, 0, mesh.indexCount, "camera", 0, 0);
            }
        }

        // Particles: camera-facing quads of the built-in plane, coloured and faded over each particle's life.
        std::deque<MaterialComponent> particleMaterials;
        const size_t planeHandle = GetMeshHandle("builtin://plane");
        if (planeHandle < m_meshLibrary.size() && m_meshLibrary[planeHandle].indexCount > 0) {
            const MeshGPU& plane = m_meshLibrary[planeHandle];
            const float* v = m_frameViewMatrix.m;
            const Vec3 viewRight(v[0], v[4], v[8]);
            const Vec3 viewUp(v[1], v[5], v[9]);
            const Vec3 towardViewer(v[2], v[6], v[10]);
            constexpr float kPlaneSize = 1.5f; // BuildPlane is 1.5 m across
            constexpr size_t kMaxParticleDraws = 768; // leaves material slots for everything else
            size_t particleDraws = 0;
            auto emitters = scene->GetEntitiesWith<ParticleEmitterComponent>();
            for (auto handle : emitters) {
                if (!IsEditorEntityVisible(static_cast<uint32_t>(handle), config)) continue;
                const auto& emitter = emitters.get<ParticleEmitterComponent>(handle);
                if (emitter.particles.empty() || emitter.materialPath.empty()) continue;
                const MaterialComponent* base = resolveMaterialFromPath(emitter.materialPath);
                if (!base) continue;
                for (const Particle& particle : emitter.particles) {
                    if (particleDraws >= kMaxParticleDraws) break;
                    const float t = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
                    auto mix = [t](float a, float b) { return a + (b - a) * t; };
                    MaterialComponent& material = particleMaterials.emplace_back(*base);
                    material.albedo = Vec3(base->albedo.x * mix(emitter.colorStart.x, emitter.colorEnd.x),
                                           base->albedo.y * mix(emitter.colorStart.y, emitter.colorEnd.y),
                                           base->albedo.z * mix(emitter.colorStart.z, emitter.colorEnd.z));
                    material.opacity = base->opacity * std::clamp(mix(emitter.colorStart.w, emitter.colorEnd.w), 0.0f, 1.0f);
                    if (material.blendMode == MaterialBlendMode::Opaque) material.blendMode = MaterialBlendMode::AlphaBlend;
                    const float half = mix(emitter.sizeStart, emitter.sizeEnd) * particle.sizeScale / kPlaneSize;
                    const float c = std::cos(particle.rotation), sn = std::sin(particle.rotation);
                    const Vec3 right = vadd(vscale(viewRight, c), vscale(viewUp, sn));
                    const Vec3 up = vsub(vscale(viewUp, c), vscale(viewRight, sn));
                    TranslucentDraw draw;
                    draw.mesh = &plane;
                    draw.material = &material;
                    draw.model = Mat4::Identity();
                    draw.model.m[0] = right.x * half; draw.model.m[1] = right.y * half; draw.model.m[2] = right.z * half;
                    draw.model.m[4] = towardViewer.x; draw.model.m[5] = towardViewer.y; draw.model.m[6] = towardViewer.z;
                    draw.model.m[8] = up.x * half; draw.model.m[9] = up.y * half; draw.model.m[10] = up.z * half;
                    draw.model.m[12] = particle.position.x;
                    draw.model.m[13] = particle.position.y;
                    draw.model.m[14] = particle.position.z;
                    draw.indexOffset = 0;
                    draw.indexCount = plane.indexCount;
                    draw.distance = vlen(vsub(particle.position, m_frameCameraPos));
                    draw.name = "particle";
                    translucentDraws.push_back(draw);
                    ++particleDraws;
                }
            }
        }

        // World surfaces first, then draw-over-world meshes, then view models (always nearer), each far to near.
        std::stable_sort(translucentDraws.begin(), translucentDraws.end(),
                         [](const TranslucentDraw& a, const TranslucentDraw& b) {
                             if (a.depthLayer != b.depthLayer) return a.depthLayer < b.depthLayer;
                             return a.distance > b.distance;
                         });
        for (const TranslucentDraw& draw : translucentDraws) {
            const VkPipeline translucentPipeline =
                m_pipeline->GetTranslucentPipeline(static_cast<uint32_t>(draw.material->blendMode));
            if (translucentPipeline != currentPipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, translucentPipeline);
                currentPipeline = translucentPipeline;
            }
            currentDrawLayout = m_pipeline->layout;
            currentBoneSet = VK_NULL_HANDLE;
            if (currentDrawLayout != probeSetLayout) {
                const VkDescriptorSet probeSet = m_probeGI.DrawSet(imageIndex);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, currentDrawLayout, 2, 1, &probeSet,
                                        0, nullptr);
                probeSetLayout = currentDrawLayout;
            }
            if (draw.mesh->vertexBuffer.buffer != currentVertexBuffer) {
                VkBuffer vb = draw.mesh->vertexBuffer.buffer;
                VkDeviceSize vbOffset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
                currentVertexBuffer = vb;
            }
            if (draw.mesh->indexBuffer.buffer != currentIndexBuffer) {
                vkCmdBindIndexBuffer(cmd, draw.mesh->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                currentIndexBuffer = draw.mesh->indexBuffer.buffer;
            }
            if (draw.depthLayer != boundDepthLayer) {
                vkCmdSetViewport(cmd, 0, 1, &layerViewports[draw.depthLayer]);
                boundDepthLayer = draw.depthLayer;
            }
            vkCmdPushConstants(cmd, currentDrawLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), draw.model.m);
            drawWithMaterial(draw.material, draw.indexOffset, draw.indexCount, draw.name, draw.submesh, draw.slot);
        }

        // Flush the written material UBO range and unmap
        if (matMappedBaseVoid) {
            VkDeviceSize writtenSize = static_cast<VkDeviceSize>(matSlotNext) * m_materialStride;
            if (writtenSize > 0) {
                vmaFlushAllocation(m_device->allocator, m_materialBuffers[imageIndex].allocation, 0, writtenSize);
            }
            vmaUnmapMemory(m_device->allocator, m_materialBuffers[imageIndex].allocation);
        }

        // Mark debug logging done after first full frame for this scene
        if (!m_materialDebugLogged && m_materialDebugDrawCount >= 10) {
            m_materialDebugLogged = true;
        }
        // Also mark done if we finished the loop (scene may have < 10 draws)
        if (!m_materialDebugLogged && drawnCount > 0) {
            m_materialDebugLogged = true;
        }

        if (debugPrint) {
            std::cout << "[EngineFrameRenderer] Drew " << drawnCount << " submesh draws, skipped " << skippedCount 
                      << " entities, used " << matSlotNext << " material slots\n";
        }

        m_lastFrameStats.drawCalls       = drawnCount;
        m_lastFrameStats.meshesDrawn     = drawnCount;
        m_lastFrameStats.entitiesSkipped = skippedCount;
    } else {
        m_lastFrameStats = {};
        if (debugPrint) {
            std::cout << "[EngineFrameRenderer] No scene or empty mesh library: scene=" << (void*)scene 
                      << " meshLibrary.size=" << m_meshLibrary.size() << "\n";
        }
    }
    
    if (viewModelRebase) {
        vkCmdSetViewport(cmd, 0, 1, &viewport);
    }
    if (config.isEditorMode && !config.isPlaying && m_probeGI.Active()) {
        m_probeGI.RecordDebug(cmd, imageIndex, m_descriptorSets[imageIndex]);
    }
    recordToneMap(cmd, scene, imageIndex);

    // Render editor grid (after opaque meshes, before transparent) — not in play/pause or shipped game
    if (config.isEditorMode && config.renderGrid && !config.isPlaying) {
        recordGrid(cmd, imageIndex, config);
    }
    if (config.isEditorMode && !config.isPlaying) {
        recordDebugOverlay(cmd, imageIndex, config);
    }
    
    // Render billboard icons for lights and other editor icons (before outlines/gizmo)  
    if (config.isEditorMode && config.renderBillboards && !config.isPlaying) {
        recordBillboards(cmd, imageIndex, scene);
    }

    // Render directional light direction gizmos (editor only)
    if (config.isEditorMode && !config.isPlaying) {
        recordLightGizmos(cmd, imageIndex, scene, config);
    }

    // Render selection outlines and hidden-object outlines (editor mode)
    if (config.isEditorMode && config.renderOutlines) {
        recordOutlines(cmd, imageIndex, scene, config);
    }
    
    // Render gizmo on selected entity
    if (config.isEditorMode && config.renderGizmo && m_gizmoInitialized && 
        m_gizmoTargetEntity != UINT32_MAX) {
        m_gizmo.Render(cmd, m_descriptorSets[imageIndex]);
    }

    // Runtime HUI overlay (drawn after the 3D scene and editor overlays, so the
    // UI appears on top; depth test/write are disabled by the UI pipeline).
    recordHUIOverlay(cmd, imageIndex);

    vkCmdEndRenderPass(cmd);
}

void EngineFrameRenderer::recordGizmo(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene)
{
    // Gizmo is rendered directly in recordMainPass when target entity is set
    // This method is kept for future use if separate pass is needed
}

void EngineFrameRenderer::recordBillboards(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene)
{
    if (!m_billboardPipeline.initialized || !scene) return;

    auto& registry = scene->GetRegistry();

    auto iconView = registry.view<TransformComponent>();
    bool anyDrawn = false;

    for (auto entityHandle : iconView) {
        const std::optional<Hephaestus::EditorIcon> icon = Hephaestus::EditorIconFor(registry, entityHandle);
        if (!icon) continue;
        auto& iconSets = m_editorIconSets[static_cast<size_t>(*icon)];
        if (iconSets.size() <= imageIndex) continue;
        VkDescriptorSet* dsPtr = &iconSets[imageIndex];

        if (!anyDrawn) {
            // Bind the billboard pipeline once
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_billboardPipeline.pipeline);

            VkViewport vp{};
            vp.width  = static_cast<float>(m_swapchain->extent.width);
            vp.height = static_cast<float>(m_swapchain->extent.height);
            vp.minDepth = 0.0f;
            vp.maxDepth = 1.0f;
            VkRect2D sc{{0, 0}, m_swapchain->extent};
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            anyDrawn = true;
        }

        // Bind descriptor set for this icon type
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_billboardPipeline.layout, 0, 1, dsPtr, 0, nullptr);

        // Compute world position — use the entity's world transform translation
        Mat4 worldMat = scene->GetWorldTransform(Entity(entityHandle, scene));
        struct {
            float x, y, z, size;
            float r, g, b, a;
        } pc;
        pc.x    = worldMat.m[12];
        pc.y    = worldMat.m[13];
        pc.z    = worldMat.m[14];
        pc.size = kLightIconHalfExtent;

        // A light's icon takes its hue at full brightness, lifted towards white so dim colours stay legible.
        const LightComponent* light = registry.try_get<LightComponent>(entityHandle);
        const float peak = light ? std::max({light->color.x, light->color.y, light->color.z}) : 0.0f;
        const Vec3 hue = peak > 1e-4f ? vscale(light->color, 1.0f / peak) : Vec3(1.0f, 1.0f, 1.0f);
        pc.r = 0.3f + 0.7f * hue.x;
        pc.g = 0.3f + 0.7f * hue.y;
        pc.b = 0.3f + 0.7f * hue.z;
        pc.a = 1.0f;

        vkCmdPushConstants(cmd, m_billboardPipeline.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);

        // Draw a quad (4 verts, triangle strip, generated in vertex shader)
        vkCmdDraw(cmd, 4, 1, 0, 0);
    }
}

void EngineFrameRenderer::recordOutlines(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene,
                                          const FrameRenderConfig& config)
{
    if (!m_outlineRenderer.initialized || !scene) return;

    auto& registry = scene->GetRegistry();

    auto isSelected = [&](entt::entity ent) {
        const uint32_t rawId = static_cast<uint32_t>(ent);
        if (rawId == config.selectedEntity) return true;
        for (uint32_t selectedId : config.selectedEntities) {
            if (selectedId == rawId) return true;
        }
        return false;
    };

    auto drawOutline = [&](entt::entity ent, const MeshRendererComponent& meshComp, bool depthMaskFirst) {
        if (!meshComp.HasValidMesh()) return;
        Entity entity(ent, scene);
        Mat4 modelMatrix = CharacterAwareModelMatrix(scene, entity, meshComp);
        const CharacterComponent* character = entity.HasComponent<CharacterComponent>()
            ? &entity.GetComponent<CharacterComponent>()
            : nullptr;
        const std::vector<size_t> handles = MeshHandlesForDraw(meshComp);
        for (size_t handle : handles) {
            if (handle >= m_meshLibrary.size()) continue;
            const MeshGPU& mesh = m_meshLibrary[handle];
            if (mesh.indexCount == 0 || mesh.vertexBuffer.buffer == VK_NULL_HANDLE ||
                mesh.indexBuffer.buffer == VK_NULL_HANDLE) continue;

            VkDescriptorSet boneSet = resolveSkinnedBoneSet(ent, character, mesh);
            const bool skinned = mesh.skinned && boneSet != VK_NULL_HANDLE &&
                                 m_skinned.outlinePipeline != VK_NULL_HANDLE;
            if (skinned) {
                auto bindSkinned = [&](VkPipeline pipe, float expand) {
                    if (pipe == VK_NULL_HANDLE) return;
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                    uint32_t dynamicOffset = 0;
                    VkDescriptorSet sets[] = {m_descriptorSets[imageIndex], boneSet};
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skinned.outlineLayout,
                                            0, 2, sets, 1, &dynamicOffset);
                    OutlineRenderer::OutlinePushConstants pc{};
                    pc.model = modelMatrix;
                    pc.expand = expand;
                    vkCmdPushConstants(cmd, m_skinned.outlineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                       sizeof(pc), &pc);
                    VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer.buffer, offsets);
                    vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
                };
                if (depthMaskFirst) {
                    bindSkinned(m_skinned.outlineMaskPipeline, 0.0f);
                }
                bindSkinned(m_skinned.outlinePipeline, 1.0f);
                continue;
            }
            m_outlineRenderer.Draw(cmd, m_descriptorSets[imageIndex],
                                   mesh.vertexBuffer.buffer, mesh.indexBuffer.buffer,
                                   mesh.indexCount, modelMatrix, depthMaskFirst);
        }
    };

    auto meshView = registry.view<TransformComponent, MeshRendererComponent>();
    for (auto ent : meshView) {
        if (!IsEditorEntityVisible(static_cast<uint32_t>(ent), config)) continue;
        const auto& meshComp = meshView.get<MeshRendererComponent>(ent);
        const bool hiddenOutline = MeshAllowsHiddenEditorOutline(meshComp, config);
        const bool selectedVisible = isSelected(ent) && meshComp.visible;
        if (!hiddenOutline && !selectedVisible) continue;
        drawOutline(ent, meshComp, hiddenOutline);
    }

    const size_t cameraMeshHandle = GetMeshHandle(kEditorCameraMeshKey);
    if (cameraMeshHandle < m_meshLibrary.size() && m_meshLibrary[cameraMeshHandle].indexCount > 0) {
        const MeshGPU& mesh = m_meshLibrary[cameraMeshHandle];
        for (const auto& [handle, model] : EditorCameraMeshes(*scene, config)) {
            if (isSelected(handle)) {
                m_outlineRenderer.Draw(cmd, m_descriptorSets[imageIndex], mesh.vertexBuffer.buffer,
                                       mesh.indexBuffer.buffer, mesh.indexCount, model, false);
            }
        }
    }
}

// ============================================================================
// Gizmo Integration (Editor-only)
// ============================================================================


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

bool EngineFrameRenderer::createLightGizmoResources()
{
    if (!m_device || !m_swapchain || !m_pipeline) return false;

    try {
        m_lightGizmo.Init(m_device->device, m_swapchain->renderPass, m_swapchain->extent,
                          m_pipeline->descriptorSetLayout, m_device->allocator);
        m_lightGizmoInitialized = true;
        std::cout << "[EngineFrameRenderer] Light gizmo initialized\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[EngineFrameRenderer] Failed to initialize light gizmo: " << e.what() << "\n";
        return false;
    }
}

void EngineFrameRenderer::cleanupLightGizmoResources()
{
    if (m_lightGizmoInitialized && m_device) {
        m_lightGizmo.Cleanup(m_device->device, m_device->allocator);
        m_lightGizmoInitialized = false;
    }
}

void EngineFrameRenderer::recordLightGizmos(VkCommandBuffer cmd, uint32_t imageIndex,
                                             Scene* scene, const FrameRenderConfig& config)
{
    if (!m_lightGizmoInitialized || !scene) return;

    auto& registry = scene->GetRegistry();

    // Query all entities with both LightComponent and TransformComponent
    auto lightView = registry.view<TransformComponent, LightComponent>();

    for (auto entityHandle : lightView) {
        auto& light = registry.get<LightComponent>(entityHandle);
        if (light.type != LightType::Directional) continue;

        bool isSelected = (static_cast<uint32_t>(entityHandle) == config.selectedEntity);
        bool isHovered  = false; // Could wire up hover detection later

        // Build world transform for this entity
        Mat4 worldMat = scene->GetWorldTransform(Entity(entityHandle, scene));

        m_lightGizmo.Draw(cmd, m_descriptorSets[imageIndex],
                          worldMat, m_frameViewMatrix, m_frameProjectionMatrix,
                          m_frameCameraPos, isSelected, isHovered);
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


// ============================================================================
// Private: Cleanup
// ============================================================================

void EngineFrameRenderer::cleanupSwapchainResources()
{
    if (!m_device || !m_device->device) return;

    cleanupSkinnedGraphicsPipelines();
    cleanupToneMapResources();
    m_probeGI.DestroySwapchainResources();

    cleanupBillboardResources();
    cleanupGizmoResources();
    cleanupLightGizmoResources();
    cleanupIdPickerResources();
    cleanupOutlineResources();
    m_editorGrid.Cleanup(m_device->device);
    if (m_debugOverlay.pipeline) {
        vkDestroyPipeline(m_device->device, m_debugOverlay.pipeline, nullptr);
        m_debugOverlay.pipeline = VK_NULL_HANDLE;
    }
    if (m_debugOverlay.pipelineLayout) {
        vkDestroyPipelineLayout(m_device->device, m_debugOverlay.pipelineLayout, nullptr);
        m_debugOverlay.pipelineLayout = VK_NULL_HANDLE;
    }
    m_debugOverlay.vertexBuffer.Destroy(m_device->allocator);
    m_debugOverlay.initialized = false;
    m_debugOverlay.vertexCount = 0;
    m_debugOverlay.vertexCapacity = 0;

    // Destroy swapchain-dependent HUI overlay resources (pipeline + per-image
    // buffers); textures and font atlases survive.
    if (m_uiOverlay) {
        m_uiOverlay->OnSwapchainDestroyed();
    }

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    m_descriptorSets.clear();
    m_pendingTextureSlotUpdates.clear();

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

    if (!m_commandBuffers.empty()) {
        vkFreeCommandBuffers(m_device->device, m_device->commandPool,
            static_cast<uint32_t>(m_commandBuffers.size()), m_commandBuffers.data());
        m_commandBuffers.clear();
    }

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

    if (m_pipeline) {
        m_pipeline->Cleanup();
        m_pipeline.reset();
    }
}

void EngineFrameRenderer::cleanupBillboardResources()
{
    if (!m_device || !m_device->device) return;

    if (m_billboardIdPipeline.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device->device, m_billboardIdPipeline.pipeline, nullptr);
        m_billboardIdPipeline.pipeline = VK_NULL_HANDLE;
    }
    if (m_billboardIdPipeline.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device->device, m_billboardIdPipeline.layout, nullptr);
        m_billboardIdPipeline.layout = VK_NULL_HANDLE;
    }
    m_billboardIdPipeline.initialized = false;

    if (m_billboardPipeline.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device->device, m_billboardPipeline.pipeline, nullptr);
        m_billboardPipeline.pipeline = VK_NULL_HANDLE;
    }
    if (m_billboardPipeline.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device->device, m_billboardPipeline.layout, nullptr);
        m_billboardPipeline.layout = VK_NULL_HANDLE;
    }
    if (m_billboardPipeline.descriptorLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device->device, m_billboardPipeline.descriptorLayout, nullptr);
        m_billboardPipeline.descriptorLayout = VK_NULL_HANDLE;
    }
    if (m_billboardDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->device, m_billboardDescriptorPool, nullptr);
        m_billboardDescriptorPool = VK_NULL_HANDLE;
    }
    for (auto& sets : m_editorIconSets) {
        sets.clear();
    }
    for (auto& texture : m_editorIconTextures) {
        texture.Destroy(m_device->allocator, m_device->device);
    }
    m_billboardPipeline.initialized = false;
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
    
    // Cleanup material texture slots
    for (auto& slot : m_materialTextureSlots) {
        if (slot.valid) {
            slot.texture.Destroy(m_device->allocator, m_device->device);
        }
    }
    m_materialTextureSlots.clear();
    m_materialTextureLookup.clear();
    m_pendingTextureSlotUpdates.clear();
    m_materialAssetCache.clear();
    m_materialAssetCacheFailed.clear();
    m_materialPathChecks.clear();
    
    // Cleanup descriptor pool
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    m_descriptorSets.clear();
    
    cleanupSkinnedResources();

    // Cleanup mesh library
    for (auto& mesh : m_meshLibrary) {
        mesh.Destroy(m_device->allocator);
    }
    m_meshLibrary.clear();
    m_meshLibraryLookup.clear();
    
    cleanupBillboardResources();

    // Cleanup editor grid
    m_editorGrid.Cleanup(m_device->device);
    
    // Cleanup skybox
    m_skybox.Cleanup(m_device->allocator, m_device->device);

    // Shutdown the runtime HUI overlay backend
    if (m_uiOverlay) {
        m_uiOverlay->Shutdown();
        m_uiOverlay.reset();
    }
    m_uiCommands = nullptr;
    m_uiCommandsSubmitted = false;
    
    // Cleanup outline renderer
    cleanupOutlineResources();
    
    // Cleanup ID picker
    cleanupIdPickerResources();
    
    // Cleanup gizmo
    cleanupGizmoResources();
    // Cleanup light direction gizmo
    cleanupLightGizmoResources();

    cleanupToneMapResources();

    if (m_localShadow.framebuffer) vkDestroyFramebuffer(m_device->device, m_localShadow.framebuffer, nullptr);
    if (m_localShadow.imageView) vkDestroyImageView(m_device->device, m_localShadow.imageView, nullptr);
    if (m_localShadow.image) vmaDestroyImage(m_device->allocator, m_localShadow.image, m_localShadow.allocation);
    m_localShadow = ShadowMap{};

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
    m_probeGI.Shutdown();  // after every pipeline layout that uses its set layouts
    
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
    // Note: actual Vulkan destruction happens in destroyAllResources()
    // because we need the VkDevice handle, which this struct doesn't own.
    initialized = false;
}

bool EngineFrameRenderer::Skybox::LoadHDRI(const std::string& path, VmaAllocator allocator,
                                            VkPhysicalDevice physDevice, VkCommandPool cmdPool, VkQueue queue,
                                            VkDevice device)
{
    if (path == loadedHDRIPath && hasHDRI) {
        return true; // Already loaded this exact path
    }

    // Destroy old texture
    environmentTexture.Destroy(allocator, device);

    // Load new HDRI
    std::vector<float> pixels;
    if (!environmentTexture.LoadHDRFromFile(allocator, device, physDevice, cmdPool, queue, path, &pixels, true)) {
        // Fallback to 1x1 white
        environmentTexture.CreateSolidColor(allocator, device, physDevice, cmdPool, queue, 0xFFFFFFFF);
        irradianceSH = {};
        hasHDRI = false;
        loadedHDRIPath.clear();
        std::cerr << "[Skybox] Failed to load HDRI: " << path << " — falling back to solid color\n";
        return false;
    }

    irradianceSH = ProjectEquirectIrradianceSH(pixels.data(), environmentTexture.width,
                                               environmentTexture.height);
    hasHDRI = true;
    loadedHDRIPath = path;
    std::cout << "[Skybox] Loaded HDRI: " << path << " (" << environmentTexture.width << "x" << environmentTexture.height << ")\n";
    return true;
}

void EngineFrameRenderer::Skybox::UpdateDescriptorSet(uint32_t imageIndex, VkBuffer cameraUBO, VkDevice device)
{
    if (descriptorSets.empty()) return;
    imageIndex %= static_cast<uint32_t>(descriptorSets.size());

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = cameraUBO;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = environmentTexture.imageView;
    imageInfo.sampler = environmentTexture.sampler;

    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSets[imageIndex];
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSets[imageIndex];
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);
}

void EngineFrameRenderer::Skybox::Draw(VkCommandBuffer cmd, uint32_t imageIndex,
                                        const SkyboxPushConstants& pc)
{
    if (!initialized || pipeline == VK_NULL_HANDLE) return;
    if (descriptorSets.empty()) return;

    imageIndex %= static_cast<uint32_t>(descriptorSets.size());

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Push constants
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(SkyboxPushConstants), &pc);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, 1, &descriptorSets[imageIndex], 0, nullptr);

    vkCmdDraw(cmd, 36, 1, 0, 0); // 36 vertices for cube
}

void EngineFrameRenderer::Skybox::Cleanup(VmaAllocator allocator, VkDevice device)
{
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    descriptorSets.clear();

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
    if (vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, vertexBuffer, vertexAllocation);
        vertexBuffer = VK_NULL_HANDLE;
        vertexAllocation = VK_NULL_HANDLE;
    }

    environmentTexture.Destroy(allocator, device);

    hasHDRI = false;
    initialized = false;
    loadedHDRIPath.clear();
    m_device = VK_NULL_HANDLE;
}

// --- Skybox internal helpers ---

static const float s_cubeVertices[] = {
    // Back face
    -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,
    // Front face
    -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
    // Left face
    -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,
    // Right face
     1.0f,  1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,
     1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,
    // Bottom face
    -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
    // Top face
    -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f
};

void EngineFrameRenderer::Skybox::CreateCubeVertexBuffer(VmaAllocator allocator)
{
    VkDeviceSize bufferSize = sizeof(s_cubeVertices);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &vertexBuffer, &vertexAllocation, nullptr) != VK_SUCCESS) {
        std::cerr << "[Skybox] Failed to create cube vertex buffer\n";
        return;
    }

    void* data;
    vmaMapMemory(allocator, vertexAllocation, &data);
    memcpy(data, s_cubeVertices, bufferSize);
    vmaUnmapMemory(allocator, vertexAllocation);
}

void EngineFrameRenderer::Skybox::CreateDescriptorSetLayout(VkDevice device)
{
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    // Binding 0: UBO (view/proj matrices)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 1: Environment texture
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout) != VK_SUCCESS) {
        std::cerr << "[Skybox] Failed to create descriptor set layout\n";
    }
}

std::vector<char> EngineFrameRenderer::Skybox::ReadShaderFile(const std::string& name)
{
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
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    }
    return {};
}

VkShaderModule EngineFrameRenderer::Skybox::CreateShaderModule(VkDevice device, const std::vector<char>& code)
{
    if (code.empty()) return VK_NULL_HANDLE;

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

void EngineFrameRenderer::Skybox::CreatePipeline(VkDevice device, VkRenderPass renderPass, VkExtent2D extent)
{
    auto vertCode = ReadShaderFile("skybox.vert.spv");
    auto fragCode = ReadShaderFile("skybox.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::cerr << "[Skybox] Failed to load skybox shaders\n";
        return;
    }

    VkShaderModule vertShader = CreateShaderModule(device, vertCode);
    VkShaderModule fragShader = CreateShaderModule(device, fragCode);
    if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
        if (vertShader) vkDestroyShaderModule(device, vertShader, nullptr);
        if (fragShader) vkDestroyShaderModule(device, fragShader, nullptr);
        std::cerr << "[Skybox] Failed to create shader modules\n";
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

    // Vertex input: vec3 position
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 3 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribute{};
    attribute.binding = 0;
    attribute.location = 0;
    attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute.offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &binding;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  // Don't write depth
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Push constant range for fragment shader
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(SkyboxPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertShader, nullptr);
        vkDestroyShaderModule(device, fragShader, nullptr);
        std::cerr << "[Skybox] Failed to create pipeline layout\n";
        return;
    }

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
        std::cerr << "[Skybox] Failed to create pipeline\n";
    }

    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
}

bool EngineFrameRenderer::Skybox::Init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                                        VmaAllocator allocator, VkPhysicalDevice physDevice,
                                        VkCommandPool cmdPool, VkQueue queue, uint32_t frameCount)
{
    if (initialized) return true;
    m_device = device;

    CreateCubeVertexBuffer(allocator);
    if (vertexBuffer == VK_NULL_HANDLE) return false;

    CreateDescriptorSetLayout(device);
    if (descriptorLayout == VK_NULL_HANDLE) return false;

    CreatePipeline(device, renderPass, extent);
    if (pipeline == VK_NULL_HANDLE) return false;

    // Create descriptor pool
    uint32_t count = std::max(frameCount, 1u);
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = count;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = count;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = count;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        std::cerr << "[Skybox] Failed to create descriptor pool\n";
        return false;
    }

    descriptorSets.resize(count);
    std::vector<VkDescriptorSetLayout> layouts(count, descriptorLayout);
    VkDescriptorSetAllocateInfo allocInfoDS{};
    allocInfoDS.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfoDS.descriptorPool = descriptorPool;
    allocInfoDS.descriptorSetCount = count;
    allocInfoDS.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfoDS, descriptorSets.data()) != VK_SUCCESS) {
        std::cerr << "[Skybox] Failed to allocate descriptor sets\n";
        return false;
    }

    // Create a fallback 1x1 white texture
    environmentTexture.CreateSolidColor(allocator, device, physDevice, cmdPool, queue, 0xFFFFFFFF);

    initialized = true;
    std::cout << "[Skybox] Initialized in EngineFrameRenderer\n";
    return true;
}

void EngineFrameRenderer::Skybox::Recreate(VkDevice device, VkRenderPass renderPass, VkExtent2D extent)
{
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    CreatePipeline(device, renderPass, extent);
}

bool EngineFrameRenderer::createSkyboxResources()
{
    if (!m_device || !m_swapchain) return false;

    uint32_t frameCount = static_cast<uint32_t>(m_swapchain->images.size());
    return m_skybox.Init(m_device->device, m_swapchain->renderPass, m_swapchain->extent,
                         m_device->allocator, m_device->physicalDevice,
                         m_device->commandPool, m_device->graphicsQueue, frameCount);
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
    pipelineInfo.subpass = VulkanSwapchain::kOverlaySubpass;
    
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        std::cerr << "[EditorGrid] Failed to create graphics pipeline\n";
        vkDestroyShaderModule(device, vertShader, nullptr);
        vkDestroyShaderModule(device, fragShader, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
        return;
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
    uint32_t dynamicOffset = 0;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 
                            0, 1, &cameraDescSet, 1, &dynamicOffset);
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
    if (!m_editorGrid.initialized || !config.renderGrid || !config.isEditorMode || config.isPlaying) return;
    
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

bool EngineFrameRenderer::createToneMapResources()
{
    VkDevice device = m_device->device;

    if (!createBloomResources()) {
        return false;
    }

    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2] = bindings[1];
    bindings[2].binding = 2;
    VkDescriptorSetLayoutCreateInfo layoutCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutCI.bindingCount = 3;
    layoutCI.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &m_toneMap.setLayout) != VK_SUCCESS) {
        cleanupToneMapResources();
        return false;
    }

    VkDescriptorPoolSize poolSizes[2] = {{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1},
                                         {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}};
    VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCI.maxSets = 1;
    poolCI.poolSizeCount = 2;
    poolCI.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &poolCI, nullptr, &m_toneMap.descriptorPool) != VK_SUCCESS) {
        cleanupToneMapResources();
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_toneMap.descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_toneMap.setLayout;
    if (vkAllocateDescriptorSets(device, &allocInfo, &m_toneMap.descriptorSet) != VK_SUCCESS) {
        cleanupToneMapResources();
        return false;
    }

    VkDescriptorImageInfo hdrInfo{};
    hdrInfo.imageView = m_swapchain->hdrImageView;
    hdrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_toneMap.descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    write.pImageInfo = &hdrInfo;
    VkDescriptorImageInfo bloomInfo{m_bloom.sampler, m_bloom.mipViews[0], VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet bloomWrite = write;
    bloomWrite.dstBinding = 1;
    bloomWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bloomWrite.pImageInfo = &bloomInfo;
    VkDescriptorImageInfo sceneInfo{m_bloom.sampler, m_swapchain->hdrImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet sceneWrite = bloomWrite;
    sceneWrite.dstBinding = 2;
    sceneWrite.pImageInfo = &sceneInfo;
    const VkWriteDescriptorSet writes[3] = {write, bloomWrite, sceneWrite};
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);

    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ToneMapPush)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &m_toneMap.setLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_toneMap.pipelineLayout) != VK_SUCCESS) {
        cleanupToneMapResources();
        return false;
    }

    VkShaderModule vert = LoadSpvModule(device, "tonemap.vert.spv");
    VkShaderModule frag = LoadSpvModule(device, "tonemap.frag.spv");
    if (!vert || !frag) {
        std::cerr << "[EngineFrameRenderer] Missing tonemap.vert.spv or tonemap.frag.spv\n";
        if (vert) vkDestroyShaderModule(device, vert, nullptr);
        if (frag) vkDestroyShaderModule(device, frag, nullptr);
        cleanupToneMapResources();
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vpState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpState.viewportCount = 1;
    vpState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &cba;
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vpState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dynState;
    pci.layout = m_toneMap.pipelineLayout;
    pci.renderPass = m_swapchain->renderPass;
    pci.subpass = VulkanSwapchain::kOverlaySubpass;

    const VkResult result =
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_toneMap.pipeline);
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    if (result != VK_SUCCESS) {
        cleanupToneMapResources();
        return false;
    }
    return true;
}

void EngineFrameRenderer::cleanupToneMapResources()
{
    VkDevice device = m_device->device;
    DestroyPipeline(device, m_toneMap.pipeline);
    if (m_toneMap.pipelineLayout) {
        vkDestroyPipelineLayout(device, m_toneMap.pipelineLayout, nullptr);
        m_toneMap.pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_toneMap.descriptorPool) {
        vkDestroyDescriptorPool(device, m_toneMap.descriptorPool, nullptr);
        m_toneMap.descriptorPool = VK_NULL_HANDLE;
    }
    m_toneMap.descriptorSet = VK_NULL_HANDLE;
    if (m_toneMap.setLayout) {
        vkDestroyDescriptorSetLayout(device, m_toneMap.setLayout, nullptr);
        m_toneMap.setLayout = VK_NULL_HANDLE;
    }
    cleanupBloomResources();
}

bool EngineFrameRenderer::createBloomResources()
{
    VkDevice device = m_device->device;
    m_bloom.mipSizes.clear();
    VkExtent2D size{std::max(1u, m_swapchain->extent.width / 2), std::max(1u, m_swapchain->extent.height / 2)};
    while (m_bloom.mipSizes.size() < kBloomMaxMips && (m_bloom.mipSizes.empty() || (size.width >= 4 && size.height >= 4))) {
        m_bloom.mipSizes.push_back(size);
        size = {std::max(1u, size.width / 2), std::max(1u, size.height / 2)};
    }
    const uint32_t mips = static_cast<uint32_t>(m_bloom.mipSizes.size());

    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    image.extent = {m_bloom.mipSizes[0].width, m_bloom.mipSizes[0].height, 1};
    image.mipLevels = mips;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(m_device->allocator, &image, &alloc, &m_bloom.image, &m_bloom.allocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    m_bloom.layoutReady = false;
    for (uint32_t mip = 0; mip < mips; ++mip) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = m_bloom.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1};
        VkImageView handle = VK_NULL_HANDLE;
        if (vkCreateImageView(device, &view, nullptr, &handle) != VK_SUCCESS) {
            cleanupBloomResources();
            return false;
        }
        m_bloom.mipViews.push_back(handle);
    }

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &sampler, nullptr, &m_bloom.sampler) != VK_SUCCESS) {
        cleanupBloomResources();
        return false;
    }

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutCI.bindingCount = 2;
    layoutCI.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &m_bloom.setLayout) != VK_SUCCESS) {
        cleanupBloomResources();
        return false;
    }

    const uint32_t steps = 2 * mips - 1;
    VkDescriptorPoolSize poolSizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, steps},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, steps}};
    VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCI.maxSets = steps;
    poolCI.poolSizeCount = 2;
    poolCI.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &poolCI, nullptr, &m_bloom.descriptorPool) != VK_SUCCESS) {
        cleanupBloomResources();
        return false;
    }
    std::vector<VkDescriptorSetLayout> layouts(steps, m_bloom.setLayout);
    m_bloom.sets.resize(steps);
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_bloom.descriptorPool;
    allocInfo.descriptorSetCount = steps;
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &allocInfo, m_bloom.sets.data()) != VK_SUCCESS) {
        cleanupBloomResources();
        return false;
    }
    // Step 0 reads the HDR scene into mip 0; steps 1..mips-1 halve; the rest add each mip back into the one above.
    auto writeStep = [&](uint32_t step, VkImageView source, VkImageLayout sourceLayout, uint32_t targetMip) {
        VkDescriptorImageInfo sourceInfo{m_bloom.sampler, source, sourceLayout};
        VkDescriptorImageInfo targetInfo{VK_NULL_HANDLE, m_bloom.mipViews[targetMip], VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[2]{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_bloom.sets[step], 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &sourceInfo, nullptr, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_bloom.sets[step], 1, 0, 1,
                     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &targetInfo, nullptr, nullptr};
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    };
    writeStep(0, m_swapchain->hdrImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0);
    for (uint32_t mip = 1; mip < mips; ++mip) {
        writeStep(mip, m_bloom.mipViews[mip - 1], VK_IMAGE_LAYOUT_GENERAL, mip);
    }
    for (uint32_t i = 0; i + 1 < mips; ++i) {
        const uint32_t target = mips - 2 - i;
        writeStep(mips + i, m_bloom.mipViews[target + 1], VK_IMAGE_LAYOUT_GENERAL, target);
    }

    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BloomPush)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &m_bloom.setLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_bloom.pipelineLayout) != VK_SUCCESS) {
        cleanupBloomResources();
        return false;
    }
    VkShaderModule module = LoadSpvModule(device, "bloom.comp.spv");
    if (!module) {
        std::cerr << "[EngineFrameRenderer] Missing bloom.comp.spv\n";
        cleanupBloomResources();
        return false;
    }
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT,
                  module, "main", nullptr};
    info.layout = m_bloom.pipelineLayout;
    const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_bloom.pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    if (result != VK_SUCCESS) {
        cleanupBloomResources();
        return false;
    }
    return true;
}

void EngineFrameRenderer::cleanupBloomResources()
{
    VkDevice device = m_device->device;
    if (m_bloom.pipeline) {
        vkDestroyPipeline(device, m_bloom.pipeline, nullptr);
        m_bloom.pipeline = VK_NULL_HANDLE;
    }
    if (m_bloom.pipelineLayout) {
        vkDestroyPipelineLayout(device, m_bloom.pipelineLayout, nullptr);
        m_bloom.pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_bloom.descriptorPool) {
        vkDestroyDescriptorPool(device, m_bloom.descriptorPool, nullptr);
        m_bloom.descriptorPool = VK_NULL_HANDLE;
    }
    m_bloom.sets.clear();
    if (m_bloom.setLayout) {
        vkDestroyDescriptorSetLayout(device, m_bloom.setLayout, nullptr);
        m_bloom.setLayout = VK_NULL_HANDLE;
    }
    if (m_bloom.sampler) {
        vkDestroySampler(device, m_bloom.sampler, nullptr);
        m_bloom.sampler = VK_NULL_HANDLE;
    }
    for (VkImageView view : m_bloom.mipViews) {
        vkDestroyImageView(device, view, nullptr);
    }
    m_bloom.mipViews.clear();
    m_bloom.mipSizes.clear();
    if (m_bloom.image) {
        vmaDestroyImage(m_device->allocator, m_bloom.image, m_bloom.allocation);
        m_bloom.image = VK_NULL_HANDLE;
        m_bloom.allocation = VK_NULL_HANDLE;
    }
    m_bloom.layoutReady = false;
}

bool EngineFrameRenderer::bloomEnabled(Scene* scene)
{
    if (!m_bloom.pipeline || !m_swapchain->sceneOnlyRenderPass || !scene) {
        return false;
    }
    const EnvironmentComponent* env = findEnvironment(scene);
    return env && env->post.bloomIntensity > 0.0f;
}

void EngineFrameRenderer::recordBloom(VkCommandBuffer cmd, const EnvironmentComponent::PostProcess& post)
{
    const uint32_t mips = static_cast<uint32_t>(m_bloom.mipSizes.size());
    const VkImageSubresourceRange allMips{VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};

    // The scene's HDR writes, and the previous frame's tone-map reads of the bloom image, finish first.
    VkImageMemoryBarrier toGeneral{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toGeneral.srcAccessMask = 0;
    toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    toGeneral.oldLayout = m_bloom.layoutReady ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcQueueFamilyIndex = toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.image = m_bloom.image;
    toGeneral.subresourceRange = allMips;
    VkMemoryBarrier sceneWritten{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    sceneWritten.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    sceneWritten.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &sceneWritten, 0, nullptr, 1, &toGeneral);
    m_bloom.layoutReady = true;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_bloom.pipeline);
    const float threshold = std::max(post.bloomThreshold, 0.0f);
    auto dispatch = [&](uint32_t step, VkExtent2D source, uint32_t targetMip, uint32_t mode) {
        BloomPush push{};
        push.sourceTexel[0] = 1.0f / static_cast<float>(source.width);
        push.sourceTexel[1] = 1.0f / static_cast<float>(source.height);
        push.threshold = threshold;
        push.knee = std::max(threshold * 0.5f, 1.0e-3f);
        push.radius = post.bloomRadius;
        push.mode = mode;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_bloom.pipelineLayout, 0, 1,
                                &m_bloom.sets[step], 0, nullptr);
        vkCmdPushConstants(cmd, m_bloom.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        const VkExtent2D target = m_bloom.mipSizes[targetMip];
        vkCmdDispatch(cmd, (target.width + 7) / 8, (target.height + 7) / 8, 1);
        VkMemoryBarrier written{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        written.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        written.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                             &written, 0, nullptr, 0, nullptr);
    };
    dispatch(0, m_swapchain->extent, 0, 0);
    for (uint32_t mip = 1; mip < mips; ++mip) {
        dispatch(mip, m_bloom.mipSizes[mip - 1], mip, 1);
    }
    for (uint32_t i = 0; i + 1 < mips; ++i) {
        const uint32_t target = mips - 2 - i;
        dispatch(mips + i, m_bloom.mipSizes[target + 1], target, 2);
    }
}

void EngineFrameRenderer::recordToneMap(VkCommandBuffer cmd, Scene* scene, uint32_t imageIndex)
{
    const EnvironmentComponent* env = scene ? findEnvironment(scene) : nullptr;
    const EnvironmentComponent::PostProcess post = env ? env->post : EnvironmentComponent::PostProcess{};
    vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
    if (m_toneMap.bloomThisFrame) {
        vkCmdEndRenderPass(cmd);
        recordBloom(cmd, post);
        VkClearValue clearValues[3]{};
        VkRenderPassBeginInfo resume{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        resume.renderPass = m_swapchain->resumeRenderPass;
        resume.framebuffer = m_swapchain->framebuffers[imageIndex];
        resume.renderArea.extent = m_swapchain->extent;
        resume.clearValueCount = 3;
        resume.pClearValues = clearValues;
        vkCmdBeginRenderPass(cmd, &resume, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_toneMap.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_toneMap.pipelineLayout,
                            0, 1, &m_toneMap.descriptorSet, 0, nullptr);
    ToneMapPush push{};
    push.outputIsSrgb = IsSrgbFormat(m_swapchain->format) ? 1u : 0u;
    push.exposure = post.exposure;
    push.contrast = post.contrast;
    push.saturation = post.saturation;
    push.colorFilter[0] = post.colorFilter.x;
    push.colorFilter[1] = post.colorFilter.y;
    push.colorFilter[2] = post.colorFilter.z;
    push.viewportSize[0] = static_cast<float>(m_swapchain->extent.width);
    push.viewportSize[1] = static_cast<float>(m_swapchain->extent.height);
    push.vignette = post.vignette;
    push.grain = post.grain;
    push.scanlines = post.scanlines;
    push.time = static_cast<float>(m_toneMap.frame++ % 997u) / 997.0f;
    push.bloomIntensity = m_toneMap.bloomThisFrame ? post.bloomIntensity : 0.0f;
    push.lensRadius = std::max(m_scopeLens.radius, 0.0f);
    push.lensDistortion = m_scopeLens.distortion;
    push.lensAberration = m_scopeLens.aberration;
    push.lensEdge = m_scopeLens.edgeDarkening;
    push.lensRing = m_scopeLens.ringWidth;
    push.lensOpacity = m_scopeLens.opacity;
    push.lensReticle = m_scopeLens.reticle ? 1.0f : 0.0f;
    vkCmdPushConstants(cmd, m_toneMap.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

bool EngineFrameRenderer::createDebugOverlayResources()
{
    if (m_debugOverlay.initialized) {
        return true;
    }
    if (!m_device || !m_swapchain || !m_pipeline) {
        return false;
    }

    auto loadShader = [this](const std::string& name) -> VkShaderModule {
        const std::string candidates[] = {
            "shaders/" + name,
            "../shaders/" + name,
            "build/Release/shaders/" + name,
            "../build/Release/shaders/" + name
        };
        for (const auto& path : candidates) {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open()) {
                continue;
            }
            const size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> code(fileSize);
            file.seekg(0);
            file.read(code.data(), static_cast<std::streamsize>(fileSize));
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
            VkShaderModule module = VK_NULL_HANDLE;
            if (vkCreateShaderModule(m_device->device, &createInfo, nullptr, &module) == VK_SUCCESS) {
                return module;
            }
        }
        return VK_NULL_HANDLE;
    };

    VkShaderModule vert = loadShader("gizmo.vert.spv");
    VkShaderModule frag = loadShader("gizmo.frag.spv");
    if (!vert || !frag) {
        if (vert) {
            vkDestroyShaderModule(m_device->device, vert, nullptr);
        }
        if (frag) {
            vkDestroyShaderModule(m_device->device, frag, nullptr);
        }
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    auto bindDesc = GizmoVertex::getBindingDescription();
    auto attrDesc = GizmoVertex::getAttributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bindDesc;
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDesc.size());
    vi.pVertexAttributeDescriptions = attrDesc.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo vpState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpState.viewportCount = 1;
    vpState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &cba;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = sizeof(VulkanGizmo::GizmoPushConstants);

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &m_pipeline->descriptorSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(m_device->device, &plci, nullptr, &m_debugOverlay.pipelineLayout) !=
        VK_SUCCESS) {
        vkDestroyShaderModule(m_device->device, vert, nullptr);
        vkDestroyShaderModule(m_device->device, frag, nullptr);
        return false;
    }

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vpState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dynState;
    pci.layout = m_debugOverlay.pipelineLayout;
    pci.renderPass = m_swapchain->renderPass;
    pci.subpass = VulkanSwapchain::kOverlaySubpass;

    const VkResult pipeResult =
        vkCreateGraphicsPipelines(m_device->device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                  &m_debugOverlay.pipeline);
    vkDestroyShaderModule(m_device->device, vert, nullptr);
    vkDestroyShaderModule(m_device->device, frag, nullptr);
    if (pipeResult != VK_SUCCESS) {
        return false;
    }

    m_debugOverlay.initialized = true;
    return true;
}

void EngineFrameRenderer::recordDebugOverlay(VkCommandBuffer cmd, uint32_t imageIndex,
                                             const FrameRenderConfig& config)
{
    if (!m_debugOverlay.initialized || !m_debugOverlay.pipeline || !m_device) {
        return;
    }
    if (config.debugLines.empty() && config.debugPoints.empty()) {
        return;
    }

    std::vector<GizmoVertex> vertices;
    vertices.reserve(config.debugLines.size() * 2 + config.debugPoints.size() * 6);
    auto pushLine = [&](const Vec3& a, const Vec3& b, const Vec3& color) {
        GizmoVertex va{};
        va.position = a;
        va.normal = Vec3(0, 1, 0);
        va.color = color;
        GizmoVertex vb = va;
        vb.position = b;
        vertices.push_back(va);
        vertices.push_back(vb);
    };
    for (const EditorDebugLine& line : config.debugLines) {
        pushLine(line.a, line.b, line.color);
    }
    for (const EditorDebugPoint& point : config.debugPoints) {
        const float s = std::max(0.005f, point.size);
        pushLine(Vec3(point.p.x - s, point.p.y, point.p.z),
                 Vec3(point.p.x + s, point.p.y, point.p.z), point.color);
        pushLine(Vec3(point.p.x, point.p.y - s, point.p.z),
                 Vec3(point.p.x, point.p.y + s, point.p.z), point.color);
        pushLine(Vec3(point.p.x, point.p.y, point.p.z - s),
                 Vec3(point.p.x, point.p.y, point.p.z + s), point.color);
    }
    if (vertices.empty()) {
        return;
    }

    const uint32_t needed = static_cast<uint32_t>(vertices.size());
    if (needed > m_debugOverlay.vertexCapacity) {
        m_debugOverlay.vertexBuffer.Destroy(m_device->allocator);
        const uint32_t cap = std::max(needed, 256u);
        m_debugOverlay.vertexBuffer.Create(m_device->allocator,
                                           sizeof(GizmoVertex) * cap,
                                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                           VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_debugOverlay.vertexCapacity = cap;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(m_device->allocator, m_debugOverlay.vertexBuffer.allocation, &mapped) !=
        VK_SUCCESS) {
        return;
    }
    std::memcpy(mapped, vertices.data(), sizeof(GizmoVertex) * vertices.size());
    vmaFlushAllocation(m_device->allocator, m_debugOverlay.vertexBuffer.allocation, 0,
                       VK_WHOLE_SIZE);
    vmaUnmapMemory(m_device->allocator, m_debugOverlay.vertexBuffer.allocation);
    m_debugOverlay.vertexCount = needed;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugOverlay.pipeline);
    uint32_t dynamicOffset = 0;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugOverlay.pipelineLayout, 0, 1,
                            &m_descriptorSets[imageIndex], 1, &dynamicOffset);
    VulkanGizmo::GizmoPushConstants pc{};
    pc.model = Mat4::Identity();
    pc.state = Vec4(-1, -1, 1, 0);
    vkCmdPushConstants(cmd, m_debugOverlay.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_debugOverlay.vertexBuffer.buffer, &offset);
    vkCmdDraw(cmd, m_debugOverlay.vertexCount, 1, 0, 0);
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
    viewportState.pViewports = nullptr; // dynamic
    viewportState.scissorCount = 1;
    viewportState.pScissors = nullptr; // dynamic
    
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
    depthStencil.depthTestEnable = VK_TRUE;   // Enable depth test so interior is occluded by the original mesh
    depthStencil.depthWriteEnable = VK_FALSE; // Don't write to depth buffer
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
    
    // Dynamic viewport/scissor so outline adapts to window resizes
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateInfo.dynamicStateCount = 2;
    dynamicStateInfo.pDynamicStates = dynamicStates;
    pipelineInfo.pDynamicState = &dynamicStateInfo;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = VulkanSwapchain::kOverlaySubpass;
    
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        std::cerr << "[OutlineRenderer] Failed to create graphics pipeline\n";
        vkDestroyShaderModule(device, vertShader, nullptr);
        vkDestroyShaderModule(device, fragShader, nullptr);
        return;
    }

    // Depth-only mask: write the unexpanded mesh into depth so inverted-hull
    // outlines still clip their interior when the object itself is not drawn.
    VkPipelineRasterizationStateCreateInfo maskRaster = rasterizer;
    maskRaster.cullMode = VK_CULL_MODE_NONE;
    maskRaster.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineDepthStencilStateCreateInfo maskDepth = depthStencil;
    maskDepth.depthTestEnable = VK_TRUE;
    maskDepth.depthWriteEnable = VK_TRUE;
    maskDepth.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState maskBlendAttachment = colorBlendAttachment;
    maskBlendAttachment.colorWriteMask = 0;

    VkPipelineColorBlendStateCreateInfo maskBlending = colorBlending;
    maskBlending.pAttachments = &maskBlendAttachment;

    VkGraphicsPipelineCreateInfo maskInfo = pipelineInfo;
    maskInfo.pRasterizationState = &maskRaster;
    maskInfo.pDepthStencilState = &maskDepth;
    maskInfo.pColorBlendState = &maskBlending;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &maskInfo, nullptr, &maskPipeline) != VK_SUCCESS) {
        std::cerr << "[OutlineRenderer] Failed to create depth-mask pipeline\n";
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
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
    Cleanup(device);
    VmaAllocator dummyAllocator = VK_NULL_HANDLE;
    Init(device, renderPass, extent, cameraDescLayout, dummyAllocator);
}

void EngineFrameRenderer::OutlineRenderer::Draw(VkCommandBuffer cmd, VkDescriptorSet cameraDescSet,
                                                 VkBuffer vertexBuffer, VkBuffer indexBuffer,
                                                 uint32_t indexCount, const Mat4& modelMatrix,
                                                 bool depthMaskFirst)
{
    if (!initialized || pipeline == VK_NULL_HANDLE) return;

    auto bindAndDraw = [&](VkPipeline pipe, float expand) {
        if (pipe == VK_NULL_HANDLE) return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        uint32_t dynamicOffset = 0;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &cameraDescSet, 1, &dynamicOffset);

        OutlinePushConstants pc{};
        pc.model = modelMatrix;
        pc.expand = expand;
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    };

    if (depthMaskFirst) {
        bindAndDraw(maskPipeline, 0.0f);
    }
    bindAndDraw(pipeline, 1.0f);
}

void EngineFrameRenderer::OutlineRenderer::Cleanup(VkDevice device)
{
    if (maskPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, maskPipeline, nullptr);
        maskPipeline = VK_NULL_HANDLE;
    }
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
    
    // Vertex input - must match the actual Vertex struct layout in the vertex buffer
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attribDescs[3]{};
    attribDescs[0].binding = 0;
    attribDescs[0].location = 0;
    attribDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribDescs[0].offset = offsetof(Vertex, pos);
    attribDescs[1].binding = 0;
    attribDescs[1].location = 1;
    attribDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribDescs[1].offset = offsetof(Vertex, normal);
    attribDescs[2].binding = 0;
    attribDescs[2].location = 2;
    attribDescs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attribDescs[2].offset = offsetof(Vertex, texCoord);
    
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
    
    VkPipelineRasterizationStateCreateInfo rasterizerBase{};
    rasterizerBase.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizerBase.depthClampEnable = VK_FALSE;
    rasterizerBase.rasterizerDiscardEnable = VK_FALSE;
    rasterizerBase.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizerBase.lineWidth = 1.0f;
    rasterizerBase.cullMode = VK_CULL_MODE_NONE;
    rasterizerBase.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizerBase.depthBiasEnable = VK_FALSE;
    
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
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_idPicker.pipelineLayout;
    pipelineInfo.renderPass = m_idPicker.renderPass;
    pipelineInfo.subpass = 0;

    auto createIdPipeline = [&](VkCullModeFlags cullMode, VkPipeline& outPipeline) -> bool {
        VkPipelineRasterizationStateCreateInfo rasterizer = rasterizerBase;
        rasterizer.cullMode = cullMode;
        pipelineInfo.pRasterizationState = &rasterizer;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline) != VK_SUCCESS) {
            return false;
        }
        return true;
    };

    if (!createIdPipeline(VK_CULL_MODE_NONE, m_idPicker.pipeline) ||
        !createIdPipeline(VK_CULL_MODE_FRONT_BIT, m_idPicker.pipelineBackface)) {
        std::cerr << "[IdPicker] Failed to create graphics pipeline\n";
        if (m_idPicker.pipeline) {
            vkDestroyPipeline(device, m_idPicker.pipeline, nullptr);
            m_idPicker.pipeline = VK_NULL_HANDLE;
        }
        if (m_idPicker.pipelineBackface) {
            vkDestroyPipeline(device, m_idPicker.pipelineBackface, nullptr);
            m_idPicker.pipelineBackface = VK_NULL_HANDLE;
        }
        vkDestroyShaderModule(device, vertShader, nullptr);
        vkDestroyShaderModule(device, fragShader, nullptr);
        return false;
    }
    
    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
    
    // Create staging buffer for readback (sized for SAMPLE_SIDE x SAMPLE_SIDE region)
    VkBufferCreateInfo stagingBufInfo{};
    stagingBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufInfo.size = IdPicker::STAGING_BYTES;
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

void EngineFrameRenderer::recordIdPass(VkCommandBuffer cmd, uint32_t imageIndex, Scene* scene,
                                       const FrameRenderConfig& config)
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
    
    // Bind camera descriptor set
    uint32_t dynOffset = 0;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_idPicker.pipelineLayout,
                            0, 1, &m_descriptorSets[imageIndex], 1, &dynOffset);
    
    // Render all entities with their IDs
    auto renderView = scene->GetEntitiesWith<TransformComponent, MeshRendererComponent>();
    
    VkBuffer currentVertexBuffer = VK_NULL_HANDLE;
    VkBuffer currentIndexBuffer = VK_NULL_HANDLE;
    VkPipeline currentPipeline = VK_NULL_HANDLE;
    
    for (auto entityHandle : renderView) {
        Entity entity(entityHandle, scene);
        auto& transform = entity.GetComponent<TransformComponent>();
        auto& renderer = entity.GetComponent<MeshRendererComponent>();
        
        if (!MeshIncludedInIdPick(renderer, config,
                                  static_cast<uint32_t>(entityHandle))) continue;
        if (!renderer.HasValidMesh()) continue;

        const CharacterComponent* character = entity.HasComponent<CharacterComponent>()
            ? &entity.GetComponent<CharacterComponent>()
            : nullptr;
        const std::vector<size_t> handles = MeshHandlesForDraw(renderer);
        for (size_t handle : handles) {
            if (handle >= m_meshLibrary.size()) continue;
            MeshGPU& mesh = m_meshLibrary[handle];
            if (mesh.indexCount == 0 || mesh.vertexBuffer.buffer == VK_NULL_HANDLE ||
                mesh.indexBuffer.buffer == VK_NULL_HANDLE) continue;

            VkDescriptorSet boneSet = resolveSkinnedBoneSet(entityHandle, character, mesh);
            const bool skinned = mesh.skinned && boneSet != VK_NULL_HANDLE &&
                                 m_skinned.idPipeline != VK_NULL_HANDLE;
            VkPipeline desiredPipeline = skinned
                ? (renderer.backfaceCulling ? m_skinned.idPipelineBackface : m_skinned.idPipeline)
                : (renderer.backfaceCulling ? m_idPicker.pipelineBackface : m_idPicker.pipeline);
            if (desiredPipeline == VK_NULL_HANDLE) continue;
            if (desiredPipeline != currentPipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, desiredPipeline);
                currentPipeline = desiredPipeline;
            }
            const VkPipelineLayout idLayout = skinned ? m_skinned.idLayout : m_idPicker.pipelineLayout;
            if (skinned) {
                uint32_t dynOff = 0;
                VkDescriptorSet sets[] = {m_descriptorSets[imageIndex], boneSet};
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, idLayout,
                                        0, 2, sets, 1, &dynOff);
            } else {
                uint32_t dynOff = 0;
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, idLayout,
                                        0, 1, &m_descriptorSets[imageIndex], 1, &dynOff);
            }

            if (mesh.vertexBuffer.buffer != currentVertexBuffer) {
                VkBuffer vb = mesh.vertexBuffer.buffer;
                VkDeviceSize vbOffset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
                currentVertexBuffer = vb;
            }

            if (mesh.indexBuffer.buffer != currentIndexBuffer) {
                vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                currentIndexBuffer = mesh.indexBuffer.buffer;
            }

            IdPicker::IdPushConstants pc;
            pc.model = CharacterAwareModelMatrix(scene, entity, renderer);
            pc.entityId = static_cast<uint32_t>(entityHandle);

            vkCmdPushConstants(cmd, idLayout,
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(IdPicker::IdPushConstants), &pc);

            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        }
    }

    const size_t cameraMeshHandle = GetMeshHandle(kEditorCameraMeshKey);
    if (cameraMeshHandle < m_meshLibrary.size() && m_meshLibrary[cameraMeshHandle].indexCount > 0 &&
        m_idPicker.pipeline != VK_NULL_HANDLE) {
        MeshGPU& mesh = m_meshLibrary[cameraMeshHandle];
        for (const auto& [handle, model] : EditorCameraMeshes(*scene, config)) {
            if (ContainsEntityId(config.editorLockedEntities, static_cast<uint32_t>(handle))) continue;
            if (currentPipeline != m_idPicker.pipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_idPicker.pipeline);
                currentPipeline = m_idPicker.pipeline;
                uint32_t dynOff = 0;
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_idPicker.pipelineLayout,
                                        0, 1, &m_descriptorSets[imageIndex], 1, &dynOff);
            }
            if (mesh.vertexBuffer.buffer != currentVertexBuffer) {
                VkDeviceSize vbOffset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer.buffer, &vbOffset);
                currentVertexBuffer = mesh.vertexBuffer.buffer;
            }
            if (mesh.indexBuffer.buffer != currentIndexBuffer) {
                vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                currentIndexBuffer = mesh.indexBuffer.buffer;
            }
            IdPicker::IdPushConstants pc;
            pc.model = model;
            pc.entityId = static_cast<uint32_t>(handle);
            vkCmdPushConstants(cmd, m_idPicker.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(IdPicker::IdPushConstants), &pc);
            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        }
    }

    // ── Billboard ID pass: render editor icon billboards into the ID buffer ──
    if (m_billboardIdPipeline.initialized && config.isEditorMode &&
        config.renderBillboards && !config.isPlaying) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_billboardIdPipeline.pipeline);

        uint32_t dynOff = 0;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_billboardIdPipeline.layout, 0, 1,
                                &m_descriptorSets[imageIndex], 1, &dynOff);

        auto& reg = scene->GetRegistry();
        auto iconView = reg.view<TransformComponent>();
        for (auto ent : iconView) {
            const std::optional<Hephaestus::EditorIcon> icon = Hephaestus::EditorIconFor(reg, ent);
            if (!icon || m_editorIconSets[static_cast<size_t>(*icon)].empty()) continue;
            const uint32_t entityId = static_cast<uint32_t>(ent);
            if (!IsEditorEntityVisible(entityId, config) ||
                ContainsEntityId(config.editorLockedEntities, entityId)) continue;

            Mat4 worldMat = scene->GetWorldTransform(Entity(ent, scene));

            BillboardIdPipeline::PushConstants bpc{};
            bpc.posX     = worldMat.m[12];
            bpc.posY     = worldMat.m[13];
            bpc.posZ     = worldMat.m[14];
            bpc.size     = kLightIconHalfExtent;
            bpc.entityId = static_cast<uint32_t>(ent);

            vkCmdPushConstants(cmd, m_billboardIdPipeline.layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(bpc), &bpc);

            vkCmdDraw(cmd, 4, 1, 0, 0);
        }
    }
    
    vkCmdEndRenderPass(cmd);
}

uint32_t EngineFrameRenderer::PickEntityAt(uint32_t screenX, uint32_t screenY)
{
    if (!m_idPicker.initialized || !m_idPicker.mappedStagingBuffer) {
        return UINT32_MAX;
    }

    if (screenX >= m_idPicker.width || screenY >= m_idPicker.height) {
        return UINT32_MAX;
    }

    m_idPicker.pickCopyRequested = true;
    m_idPicker.pickX = screenX;
    m_idPicker.pickY = screenY;

    return m_idPicker.lastPickedEntity;
}

bool EngineFrameRenderer::TakeViewportPickResult(uint32_t& outEntityId)
{
    if (!m_idPicker.pickJustCompleted) {
        return false;
    }
    m_idPicker.pickJustCompleted = false;
    outEntityId = m_idPicker.lastPickedEntity;
    return true;
}

uint32_t EngineFrameRenderer::resolvePickRegion() const
{
    if (!m_idPicker.mappedStagingBuffer || m_idPicker.sampleW == 0 || m_idPicker.sampleH == 0) {
        return UINT32_MAX;
    }

    const uint8_t* data = static_cast<const uint8_t*>(m_idPicker.mappedStagingBuffer);
    const uint32_t w = m_idPicker.sampleW;
    const uint32_t h = m_idPicker.sampleH;

    // Center of the sample region in local coordinates
    const float cx = static_cast<float>(m_idPicker.pickX - m_idPicker.sampleOriginX);
    const float cy = static_cast<float>(m_idPicker.pickY - m_idPicker.sampleOriginY);

    uint32_t bestEntity = UINT32_MAX;
    float bestDist = 1e9f;

    for (uint32_t sy = 0; sy < h; ++sy) {
        for (uint32_t sx = 0; sx < w; ++sx) {
            const uint32_t offset = (sy * w + sx) * 4;
            uint32_t id = data[offset]
                        | (data[offset + 1] << 8)
                        | (data[offset + 2] << 16)
                        | (data[offset + 3] << 24);

            if (id == UINT32_MAX) continue;

            float dx = static_cast<float>(sx) - cx;
            float dy = static_cast<float>(sy) - cy;
            float dist = dx * dx + dy * dy;
            if (dist < bestDist) {
                bestDist = dist;
                bestEntity = id;
            }
        }
    }

    return bestEntity;
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

    if (m_idPicker.pipelineBackface) {
        vkDestroyPipeline(device, m_idPicker.pipelineBackface, nullptr);
        m_idPicker.pipelineBackface = VK_NULL_HANDLE;
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
    m_idPicker.pickCopyRequested = false;
    m_idPicker.pickReadPending = false;
    m_idPicker.pickJustCompleted = false;
}

} // namespace Hephaestus
