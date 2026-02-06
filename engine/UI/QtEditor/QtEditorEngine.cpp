/**
 * @file QtEditorEngine.cpp
 * @brief Implementation of engine integration for Qt-based editor.
 *
 * NOTE: For the initial implementation, this creates its own Vulkan instance/device.
 * This is a stepping stone - the next iteration should share resources with main.cpp.
 * The architecture is designed to support this consolidation.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "QtEditorEngine.h"
#include "../../Gfx/VulkanDevice.h"
#include "../../Gfx/VulkanSwapchain.h"
#include "../../Gfx/VulkanPipeline.h"
#include "../../Gfx/EngineFrameRenderer.h"
#include "../../Gfx/Vertex.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"
#include "../../Scene/Entity.h"
#include "../../Assets/MeshAsset.h"
#include "../../Assets/ModelImporter.h"
#include "../../Core/ProjectContext.h"

#include <iostream>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

namespace Hephaestus {
namespace QtEditor {

// Debug callback for Vulkan validation
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    Q_UNUSED(messageSeverity);
    Q_UNUSED(messageType);
    Q_UNUSED(pUserData);
    std::cerr << "[VULKAN] " << (pCallbackData ? pCallbackData->pMessage : "") << std::endl;
    return VK_FALSE;
}

QtEditorEngine::QtEditorEngine(QObject* parent)
    : QObject(parent)
{
    // Configure render timer but don't start yet
    m_renderTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_renderTimer, &QTimer::timeout, this, &QtEditorEngine::onTick);
    
    m_frameTimer.start();
}

QtEditorEngine::~QtEditorEngine()
{
    Shutdown();
}

bool QtEditorEngine::InitializeVulkan()
{
    if (m_state != EngineState::Uninitialized) {
        emit engineError("Engine already initialized");
        return false;
    }
    
    m_state = EngineState::InitializingVulkan;
    
    if (!createVulkanInstance()) {
        m_state = EngineState::Uninitialized;
        return false;
    }
    
    if (!initializeVulkanDevice()) {
        cleanupVulkanResources();
        m_state = EngineState::Uninitialized;
        return false;
    }
    
    m_state = EngineState::WaitingForViewport;
    std::cout << "[QtEditorEngine] Vulkan initialized, waiting for viewport\n";
    return true;
}

bool QtEditorEngine::createVulkanInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hephaestus Engine (Qt Editor)";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Hephaestus Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // Extensions needed for Qt (Win32 surface)
    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
    };

#ifdef _DEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

#ifdef _DEBUG
    // Enable validation layers in debug builds
    std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
#else
    createInfo.enabledLayerCount = 0;
#endif

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        emit engineError("Failed to create Vulkan instance");
        return false;
    }

#ifdef _DEBUG
    // Setup debug messenger
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = debugCallback;

    auto createDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
    if (createDebugMessenger) {
        createDebugMessenger(m_instance, &debugCreateInfo, nullptr, &m_debugMessenger);
    }
#endif

    std::cout << "[QtEditorEngine] Vulkan instance created\n";
    return true;
}

bool QtEditorEngine::initializeVulkanDevice()
{
    try {
        m_device = std::make_unique<VulkanDevice>();
        m_device->InitWithoutSurface(m_instance);
        return true;
    } catch (const std::exception& e) {
        emit engineError(QString("Failed to initialize Vulkan device: %1").arg(e.what()));
        return false;
    }
}

bool QtEditorEngine::AttachToViewport(QtViewportWindow* viewport)
{
    if (m_state != EngineState::WaitingForViewport) {
        emit engineError("Engine not in correct state for viewport attachment");
        return false;
    }
    
    if (!viewport || !viewport->IsReadyForRendering()) {
        emit engineError("Viewport not ready for rendering");
        return false;
    }
    
    m_viewport = viewport;
    m_currentSurface = viewport->GetNativeSurface();
    
    m_state = EngineState::CreatingSwapchain;
    
    // Attach device to the native surface
    if (!m_device->AttachToSurface(m_currentSurface)) {
        emit engineError("Failed to attach device to viewport surface");
        m_state = EngineState::WaitingForViewport;
        return false;
    }
    
    // Create swapchain
    if (!createSwapchain()) {
        m_state = EngineState::WaitingForViewport;
        return false;
    }
    
    // Connect viewport signals
    connect(viewport, &QtViewportWindow::viewportResized, this, &QtEditorEngine::OnViewportResized);
    viewport->SetInputEventCallback([this](const InputEvent& event) {
        OnViewportInput(event);
    });
    
    m_state = EngineState::Running;
    emit engineInitialized(true);
    
    std::cout << "[QtEditorEngine] Attached to viewport, ready to render\n";
    return true;
}

bool QtEditorEngine::createSwapchain()
{
    try {
        m_swapchain = std::make_unique<VulkanSwapchain>();
        m_swapchain->InitFromNativeSurface(
            m_device->physicalDevice,
            m_device->device,
            m_device->surface,
            m_currentSurface.width,
            m_currentSurface.height,
            m_device->allocator
        );
        
        // Initialize the frame renderer
        m_frameRenderer = std::make_unique<Hephaestus::EngineFrameRenderer>();
        if (!m_frameRenderer->Init(m_device.get(), m_swapchain.get())) {
            throw std::runtime_error("Failed to initialize frame renderer");
        }
        
        // Create a new empty scene via EditorContext
        m_editorContext.NewScene("New Scene");
        
        // Set up default camera from EditorContext
        const auto& cam = m_editorContext.GetCamera();
        m_camera.positionX = cam.positionX;
        m_camera.positionY = cam.positionY;
        m_camera.positionZ = cam.positionZ;
        m_camera.yawDeg = cam.yawDeg;
        m_camera.pitchDeg = cam.pitchDeg;
        
        std::cout << "[QtEditorEngine] Swapchain and frame renderer created (" 
                  << m_swapchain->extent.width << "x" << m_swapchain->extent.height << ")\n";
        return true;
        
    } catch (const std::exception& e) {
        emit engineError(QString("Failed to create swapchain: %1").arg(e.what()));
        return false;
    }
}

void QtEditorEngine::StartRenderLoop()
{
    if (m_state != EngineState::Running) {
        emit engineError("Engine not ready to start render loop");
        return;
    }
    
    // Use 0ms interval for maximum frame rate (regulated by vsync/fence waits)
    m_renderTimer.setInterval(0);
    m_renderTimer.start();
    
    std::cout << "[QtEditorEngine] Render loop started\n";
}

void QtEditorEngine::Shutdown()
{
    if (m_state == EngineState::Shutdown || m_state == EngineState::Uninitialized) {
        return;
    }
    
    m_state = EngineState::ShuttingDown;
    
    // Stop render timer
    m_renderTimer.stop();
    
    // Wait for GPU to finish
    if (m_device && m_device->device) {
        vkDeviceWaitIdle(m_device->device);
    }
    
    cleanupVulkanResources();
    
    m_state = EngineState::Shutdown;
    std::cout << "[QtEditorEngine] Shutdown complete\n";
}

void QtEditorEngine::cleanupVulkanResources()
{
    // Cleanup frame renderer (handles pipeline, sync objects, command buffers)
    if (m_frameRenderer) {
        m_frameRenderer->Shutdown();
        m_frameRenderer.reset();
    }
    
    // EditorContext owns the scene, no need to cleanup here
    
    // Cleanup swapchain
    if (m_swapchain) {
        m_swapchain->Cleanup();
        m_swapchain.reset();
    }
    
    // Cleanup device
    if (m_device) {
        m_device->Cleanup();
        m_device.reset();
    }
    
    // Cleanup debug messenger
    if (m_debugMessenger) {
        auto destroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyDebugMessenger) {
            destroyDebugMessenger(m_instance, m_debugMessenger, nullptr);
        }
        m_debugMessenger = VK_NULL_HANDLE;
    }
    
    // Cleanup instance
    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

void QtEditorEngine::OnViewportResized(uint32_t width, uint32_t height)
{
    if (m_state != EngineState::Running) {
        return;
    }
    
    // Skip zero-size (minimized window)
    if (width == 0 || height == 0) {
        return;
    }
    
    m_swapchainNeedsRecreation = true;
    m_pendingWidth = width;
    m_pendingHeight = height;
    
    std::cout << "[QtEditorEngine] Resize requested: " << width << "x" << height << "\n";
}

void QtEditorEngine::OnViewportInput(const InputEvent& event)
{
    // TODO: Forward to engine input system
    // For now, just log the events
    switch (event.type) {
    case InputEvent::Type::MousePress:
        // std::cout << "[QtEditorEngine] Mouse press: " << event.button << " at " << event.x << "," << event.y << "\n";
        break;
    case InputEvent::Type::MouseRelease:
        break;
    case InputEvent::Type::MouseMove:
        break;
    case InputEvent::Type::MouseWheel:
        // std::cout << "[QtEditorEngine] Mouse wheel: " << event.wheelDelta << "\n";
        break;
    case InputEvent::Type::KeyPress:
        // std::cout << "[QtEditorEngine] Key press: " << event.key << "\n";
        break;
    case InputEvent::Type::KeyRelease:
        break;
    default:
        break;
    }
}

void QtEditorEngine::onTick()
{
    if (m_state != EngineState::Running || m_renderInProgress) {
        return;
    }
    
    m_renderInProgress = true;
    
    // Calculate delta time
    float currentTime = m_frameTimer.elapsed() / 1000.0f;
    float dt = currentTime - m_lastFrameTime;
    m_lastFrameTime = currentTime;
    
    // Handle pending swapchain recreation
    if (m_swapchainNeedsRecreation) {
        vkDeviceWaitIdle(m_device->device);
        m_swapchain->RecreateFromNativeSurface(m_pendingWidth, m_pendingHeight);
        if (m_frameRenderer) {
            m_frameRenderer->OnResize(m_pendingWidth, m_pendingHeight);
        }
        m_swapchainNeedsRecreation = false;
    }
    
    // Skip rendering if swapchain is in bad state
    if (m_swapchain->extent.width == 0 || m_swapchain->extent.height == 0) {
        m_renderInProgress = false;
        return;
    }
    
    renderFrame();
    
    emit frameRendered(dt);
    m_renderInProgress = false;
}

void QtEditorEngine::renderFrame()
{
    static std::string lastSceneName;
    
    if (!m_frameRenderer) {
        return;
    }
    if (!m_editorContext.HasActiveScene()) {
        return;
    }
    
    Scene* scene = m_editorContext.GetActiveScene();
    std::string sceneName = scene->GetName();
    if (sceneName != lastSceneName) {
        std::cout << "[QtEditorEngine::renderFrame] NEW scene: " << sceneName 
                  << " meshLibrary.size=" << m_frameRenderer->GetMeshCount() << "\n";
        lastSceneName = sceneName;
    }
    
    // Calculate delta time for this frame
    float currentTime = m_frameTimer.elapsed() / 1000.0f;
    float deltaTime = currentTime - m_lastFrameTime;
    
    // Render frame using shared renderer
    bool success = m_frameRenderer->RenderFrame(
        m_editorContext.GetActiveScene(), 
        m_camera, 
        m_renderConfig,
        deltaTime
    );
    
    if (!success) {
        // Swapchain may need recreation
        m_swapchainNeedsRecreation = true;
        m_pendingWidth = m_viewport->width();
        m_pendingHeight = m_viewport->height();
    }
}

void QtEditorEngine::createTestMesh()
{
    // Create a simple entity with a mesh for testing
    // For now, the scene will be empty but renderable
    // Scene content will come from proper asset loading later
    std::cout << "[QtEditorEngine] Test scene initialized\n";
}

// ============================================================================
// Mesh Loading
// ============================================================================

void QtEditorEngine::createBuiltinMeshes()
{
    if (m_builtinMeshesCreated || !m_frameRenderer || !m_device) {
        return;
    }

    std::cout << "[QtEditorEngine] Creating builtin meshes...\n";

    // Create a simple cube mesh
    MeshData cubeMesh;
    
    // Cube vertices (24 vertices for proper normals)
    // Vertex format: {pos[3], normal[3], color[3], texCoord[2]}
    float s = 0.5f;
    // Front face
    cubeMesh.vertices.push_back({{-s, -s,  s}, { 0,  0,  1}, {1, 1, 1}, {0, 0}});
    cubeMesh.vertices.push_back({{ s, -s,  s}, { 0,  0,  1}, {1, 1, 1}, {1, 0}});
    cubeMesh.vertices.push_back({{ s,  s,  s}, { 0,  0,  1}, {1, 1, 1}, {1, 1}});
    cubeMesh.vertices.push_back({{-s,  s,  s}, { 0,  0,  1}, {1, 1, 1}, {0, 1}});
    // Back face
    cubeMesh.vertices.push_back({{ s, -s, -s}, { 0,  0, -1}, {1, 1, 1}, {0, 0}});
    cubeMesh.vertices.push_back({{-s, -s, -s}, { 0,  0, -1}, {1, 1, 1}, {1, 0}});
    cubeMesh.vertices.push_back({{-s,  s, -s}, { 0,  0, -1}, {1, 1, 1}, {1, 1}});
    cubeMesh.vertices.push_back({{ s,  s, -s}, { 0,  0, -1}, {1, 1, 1}, {0, 1}});
    // Top face
    cubeMesh.vertices.push_back({{-s,  s,  s}, { 0,  1,  0}, {1, 1, 1}, {0, 0}});
    cubeMesh.vertices.push_back({{ s,  s,  s}, { 0,  1,  0}, {1, 1, 1}, {1, 0}});
    cubeMesh.vertices.push_back({{ s,  s, -s}, { 0,  1,  0}, {1, 1, 1}, {1, 1}});
    cubeMesh.vertices.push_back({{-s,  s, -s}, { 0,  1,  0}, {1, 1, 1}, {0, 1}});
    // Bottom face
    cubeMesh.vertices.push_back({{-s, -s, -s}, { 0, -1,  0}, {1, 1, 1}, {0, 0}});
    cubeMesh.vertices.push_back({{ s, -s, -s}, { 0, -1,  0}, {1, 1, 1}, {1, 0}});
    cubeMesh.vertices.push_back({{ s, -s,  s}, { 0, -1,  0}, {1, 1, 1}, {1, 1}});
    cubeMesh.vertices.push_back({{-s, -s,  s}, { 0, -1,  0}, {1, 1, 1}, {0, 1}});
    // Right face
    cubeMesh.vertices.push_back({{ s, -s,  s}, { 1,  0,  0}, {1, 1, 1}, {0, 0}});
    cubeMesh.vertices.push_back({{ s, -s, -s}, { 1,  0,  0}, {1, 1, 1}, {1, 0}});
    cubeMesh.vertices.push_back({{ s,  s, -s}, { 1,  0,  0}, {1, 1, 1}, {1, 1}});
    cubeMesh.vertices.push_back({{ s,  s,  s}, { 1,  0,  0}, {1, 1, 1}, {0, 1}});
    // Left face
    cubeMesh.vertices.push_back({{-s, -s, -s}, {-1,  0,  0}, {1, 1, 1}, {0, 0}});
    cubeMesh.vertices.push_back({{-s, -s,  s}, {-1,  0,  0}, {1, 1, 1}, {1, 0}});
    cubeMesh.vertices.push_back({{-s,  s,  s}, {-1,  0,  0}, {1, 1, 1}, {1, 1}});
    cubeMesh.vertices.push_back({{-s,  s, -s}, {-1,  0,  0}, {1, 1, 1}, {0, 1}});

    // Cube indices
    for (int face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        cubeMesh.indices.push_back(base + 0);
        cubeMesh.indices.push_back(base + 1);
        cubeMesh.indices.push_back(base + 2);
        cubeMesh.indices.push_back(base + 0);
        cubeMesh.indices.push_back(base + 2);
        cubeMesh.indices.push_back(base + 3);
    }
    cubeMesh.bounds = AABB(Vec3(-s, -s, -s), Vec3(s, s, s));

    uploadMeshToGPU("Default Cube", cubeMesh, std::filesystem::path("builtin://cube"), false);

    // Create a simple sphere mesh
    MeshData sphereMesh;
    const int stacks = 16;
    const int slices = 32;
    const float radius = 0.5f;

    // Generate vertices
    for (int i = 0; i <= stacks; ++i) {
        float phi = static_cast<float>(i) / stacks * 3.14159265f;
        for (int j = 0; j <= slices; ++j) {
            float theta = static_cast<float>(j) / slices * 2.0f * 3.14159265f;
            float x = radius * sinf(phi) * cosf(theta);
            float y = radius * cosf(phi);
            float z = radius * sinf(phi) * sinf(theta);
            Vec3 normal = vnorm(Vec3(x, y, z));
            float u = static_cast<float>(j) / slices;
            float v = static_cast<float>(i) / stacks;
            sphereMesh.vertices.push_back({{x, y, z}, {normal.x, normal.y, normal.z}, {1, 1, 1}, {u, v}});
        }
    }

    // Generate indices
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint32_t first = i * (slices + 1) + j;
            uint32_t second = first + slices + 1;
            sphereMesh.indices.push_back(first);
            sphereMesh.indices.push_back(second);
            sphereMesh.indices.push_back(first + 1);
            sphereMesh.indices.push_back(second);
            sphereMesh.indices.push_back(second + 1);
            sphereMesh.indices.push_back(first + 1);
        }
    }
    sphereMesh.bounds = AABB(Vec3(-radius, -radius, -radius), Vec3(radius, radius, radius));

    uploadMeshToGPU("Default Sphere", sphereMesh, std::filesystem::path("builtin://sphere"), false);

    m_builtinMeshesCreated = true;
    std::cout << "[QtEditorEngine] Builtin meshes created\n";
}

size_t QtEditorEngine::uploadMeshToGPU(const std::string& name, const MeshData& data,
                                        const std::filesystem::path& sourcePath, bool imported)
{
    if (!m_frameRenderer || !m_device) {
        std::cerr << "[QtEditorEngine] Cannot upload mesh - renderer not initialized\n";
        return MeshRendererComponent::InvalidMesh;
    }

    if (data.vertices.empty() || data.indices.empty()) {
        std::cerr << "[QtEditorEngine] Cannot upload mesh - no geometry\n";
        return MeshRendererComponent::InvalidMesh;
    }

    MeshGPU mesh{};
    mesh.name = name;
    mesh.sourcePath = sourcePath;
    mesh.sourceKey = sourcePath.generic_string();
    mesh.vertexCount = static_cast<uint32_t>(data.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(data.indices.size());
    mesh.submeshes = data.submeshes;
    mesh.bounds = data.bounds;
    mesh.imported = imported;

    // Create vertex buffer
    VkDeviceSize vertexBufferSize = sizeof(Vertex) * data.vertices.size();
    mesh.vertexBuffer.Create(m_device->allocator, vertexBufferSize,
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             VMA_MEMORY_USAGE_CPU_TO_GPU);

    void* mapped = nullptr;
    if (vmaMapMemory(m_device->allocator, mesh.vertexBuffer.allocation, &mapped) == VK_SUCCESS) {
        std::memcpy(mapped, data.vertices.data(), static_cast<size_t>(vertexBufferSize));
        vmaFlushAllocation(m_device->allocator, mesh.vertexBuffer.allocation, 0, VK_WHOLE_SIZE);
        vmaUnmapMemory(m_device->allocator, mesh.vertexBuffer.allocation);
    }

    // Create index buffer
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * data.indices.size();
    mesh.indexBuffer.Create(m_device->allocator, indexBufferSize,
                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                            VMA_MEMORY_USAGE_CPU_TO_GPU);

    if (vmaMapMemory(m_device->allocator, mesh.indexBuffer.allocation, &mapped) == VK_SUCCESS) {
        std::memcpy(mapped, data.indices.data(), static_cast<size_t>(indexBufferSize));
        vmaFlushAllocation(m_device->allocator, mesh.indexBuffer.allocation, 0, VK_WHOLE_SIZE);
        vmaUnmapMemory(m_device->allocator, mesh.indexBuffer.allocation);
    }

    size_t handle = m_frameRenderer->AddMesh(std::move(mesh));
    std::cout << "[QtEditorEngine] Uploaded mesh '" << name << "' as handle " << handle << "\n";
    return handle;
}

std::string QtEditorEngine::normalizeAssetPath(const std::string& assetPath) const
{
    namespace fs = std::filesystem;
    
    if (assetPath.empty()) return "";
    
    // Builtin paths stay as-is
    if (assetPath.rfind("builtin://", 0) == 0) {
        return assetPath;
    }
    
    // Normalize to forward slashes
    fs::path p(assetPath);
    return p.lexically_normal().generic_string();
}

void QtEditorEngine::RebuildSceneMeshes()
{
    if (!m_frameRenderer || !m_device || !m_editorContext.HasActiveScene()) {
        std::cout << "[QtEditorEngine] RebuildSceneMeshes: nothing to rebuild\n";
        return;
    }

    // Ensure builtin meshes exist
    createBuiltinMeshes();

    Scene* scene = m_editorContext.GetActiveScene();
    std::cout << "[QtEditorEngine] Rebuilding meshes for scene: " << scene->GetName() << "\n";

    namespace fs = std::filesystem;

    // Get project root for resolving relative paths
    // We need to access this from somewhere - for now use a simple heuristic
    fs::path projectRoot;
    fs::path assetRoot;
    
    // Try to get paths from scene path if available
    std::string scenePath = m_editorContext.GetScenePath();
    if (!scenePath.empty()) {
        fs::path sp(scenePath);
        // Walk up to find project root (look for .heproject)
        fs::path search = sp.parent_path();
        for (int i = 0; i < 10 && !search.empty(); ++i) {
            for (const auto& entry : fs::directory_iterator(search, fs::directory_options::skip_permission_denied)) {
                if (entry.path().extension() == ".heproject") {
                    projectRoot = search;
                    assetRoot = projectRoot / "assets";
                    break;
                }
            }
            if (!projectRoot.empty()) break;
            search = search.parent_path();
        }
    }

    std::cout << "[QtEditorEngine] Project root: " << projectRoot << "\n";
    std::cout << "[QtEditorEngine] Asset root: " << assetRoot << "\n";

    // Iterate all entities with MeshRendererComponent
    auto renderView = scene->GetEntitiesWith<MeshRendererComponent>();
    
    int meshEntityCount = 0;
    for (auto entityHandle : renderView) {
        meshEntityCount++;
    }
    std::cout << "[QtEditorEngine] Found " << meshEntityCount << " entities with MeshRendererComponent\n";
    
    for (auto entityHandle : renderView) {
        Entity entity(entityHandle, scene);
        auto& renderer = entity.GetComponent<MeshRendererComponent>();

        std::string entityName = entity.HasComponent<TagComponent>()
                                     ? entity.GetComponent<TagComponent>().tag
                                     : std::string("Entity");

        std::cout << "[QtEditorEngine] Entity '" << entityName 
                  << "' meshAssetPath='" << renderer.meshAssetPath << "'\n";

        if (renderer.meshAssetPath.empty()) {
            continue;
        }

        std::string lookupKey = normalizeAssetPath(renderer.meshAssetPath);

        // Check if mesh already in library
        size_t existingHandle = m_frameRenderer->GetMeshHandle(lookupKey);
        if (existingHandle != SIZE_MAX) {
            MeshGPU* mesh = m_frameRenderer->GetMesh(existingHandle);
            if (mesh) {
                int subCount = mesh->submeshes.empty() ? 1 : static_cast<int>(mesh->submeshes.size());
                renderer.SetMesh(existingHandle, mesh->bounds, mesh->sourceKey, mesh->imported, subCount);
                std::cout << "[QtEditorEngine]   -> Found in library: handle " << existingHandle << "\n";
                continue;
            }
        }

        // Handle builtin meshes
        if (renderer.meshAssetPath.rfind("builtin://", 0) == 0) {
            size_t handle = m_frameRenderer->GetMeshHandle(renderer.meshAssetPath);
            if (handle != SIZE_MAX) {
                MeshGPU* mesh = m_frameRenderer->GetMesh(handle);
                if (mesh) {
                    int subCount = mesh->submeshes.empty() ? 1 : static_cast<int>(mesh->submeshes.size());
                    renderer.SetMesh(handle, mesh->bounds, mesh->sourceKey, mesh->imported, subCount);
                    std::cout << "[QtEditorEngine]   -> Builtin mesh: handle " << handle << "\n";
                }
            } else {
                std::cerr << "[QtEditorEngine]   -> Unknown builtin: " << renderer.meshAssetPath << "\n";
                renderer.meshHandle = MeshRendererComponent::InvalidMesh;
            }
            continue;
        }

        // Try to load mesh from file
        fs::path meshPath;
        
        // If path is relative, resolve against asset root
        fs::path assetP(renderer.meshAssetPath);
        if (assetP.is_relative() && !assetRoot.empty()) {
            std::string rel = assetP.generic_string();
            if (rel.rfind("assets/", 0) == 0) {
                rel = rel.substr(std::string("assets/").size());
                meshPath = assetRoot / fs::path(rel);
            } else {
                meshPath = assetRoot / assetP;
            }
        } else if (assetP.is_relative() && !projectRoot.empty()) {
            meshPath = projectRoot / renderer.meshAssetPath;
        } else {
            meshPath = assetP;
        }

        // Normalize
        if (!meshPath.empty()) {
            meshPath = fs::weakly_canonical(meshPath);
        }

        std::cout << "[QtEditorEngine]   -> Resolved path: " << meshPath << "\n";

        if (!fs::exists(meshPath)) {
            std::cerr << "[QtEditorEngine]   -> File not found: " << meshPath << "\n";
            renderer.meshHandle = MeshRendererComponent::InvalidMesh;
            continue;
        }

        // Load mesh
        MeshData meshData;
        std::string loadError;
        if (!ModelImporter::LoadModel(meshPath, meshData, loadError)) {
            std::cerr << "[QtEditorEngine]   -> Load failed: " << loadError << "\n";
            renderer.meshHandle = MeshRendererComponent::InvalidMesh;
            continue;
        }

        // Upload to GPU
        std::string meshName = meshPath.stem().string();
        size_t handle = uploadMeshToGPU(meshName, meshData, meshPath, true);
        
        if (handle != MeshRendererComponent::InvalidMesh) {
            MeshGPU* mesh = m_frameRenderer->GetMesh(handle);
            int subCount = mesh ? (mesh->submeshes.empty() ? 1 : static_cast<int>(mesh->submeshes.size())) : 1;
            renderer.SetMesh(handle, meshData.bounds, lookupKey, true, subCount);
            std::cout << "[QtEditorEngine]   -> Uploaded: handle " << handle << "\n";
        }
    }

    std::cout << "[QtEditorEngine] Mesh rebuild complete. Library size: " 
              << m_frameRenderer->GetMeshCount() << "\n";

    // Debug: list all meshes in library
    std::cout << "[QtEditorEngine] === Mesh Library Contents ===\n";
    for (size_t i = 0; i < m_frameRenderer->GetMeshCount(); i++) {
        MeshGPU* mesh = m_frameRenderer->GetMesh(i);
        if (mesh) {
            std::cout << "  [" << i << "] " << mesh->sourceKey << " - vertices=" << mesh->vertexCount 
                      << " indices=" << mesh->indexCount << "\n";
        }
    }

    // Debug: check what mesh handles entities have
    std::cout << "[QtEditorEngine] === Entity Mesh Assignments ===\n";
    auto debugView = scene->GetEntitiesWith<MeshRendererComponent>();
    for (auto entityHandle : debugView) {
        Entity entity(entityHandle, scene);
        auto& renderer = entity.GetComponent<MeshRendererComponent>();
        std::string entityName = entity.HasComponent<TagComponent>()
                                     ? entity.GetComponent<TagComponent>().tag
                                     : std::string("Entity");
        std::cout << "  " << entityName << " - meshHandle=" << renderer.meshHandle 
                  << " visible=" << renderer.visible << "\n";
    }

    // Auto-frame the camera to the scene bounds so the loaded scene is visible.
    bool hasBounds = false;
    AABB sceneBounds;
    auto boundsView = scene->GetEntitiesWith<TransformComponent, MeshRendererComponent>();
    for (auto entityHandle : boundsView) {
        Entity entity(entityHandle, scene);
        auto& transform = entity.GetComponent<TransformComponent>();
        auto& renderer = entity.GetComponent<MeshRendererComponent>();
        if (!renderer.visible || !renderer.HasValidMesh()) {
            continue;
        }
        AABB worldBounds = renderer.GetWorldBounds(transform);
        if (!hasBounds) {
            sceneBounds = worldBounds;
            hasBounds = true;
        } else {
            sceneBounds.min.x = std::min(sceneBounds.min.x, worldBounds.min.x);
            sceneBounds.min.y = std::min(sceneBounds.min.y, worldBounds.min.y);
            sceneBounds.min.z = std::min(sceneBounds.min.z, worldBounds.min.z);
            sceneBounds.max.x = std::max(sceneBounds.max.x, worldBounds.max.x);
            sceneBounds.max.y = std::max(sceneBounds.max.y, worldBounds.max.y);
            sceneBounds.max.z = std::max(sceneBounds.max.z, worldBounds.max.z);
        }
    }

    if (hasBounds) {
        Vec3 center = vscale(vadd(sceneBounds.min, sceneBounds.max), 0.5f);
        Vec3 extents = vscale(vsub(sceneBounds.max, sceneBounds.min), 0.5f);
        float radius = vlen(extents);
        if (radius < 0.1f) {
            radius = 0.1f;
        }

        constexpr float DEG_TO_RAD = 3.1415926535f / 180.0f;
        float fovRad = 60.0f * DEG_TO_RAD;
        float distance = radius / std::tan(fovRad * 0.5f);
        distance *= 1.5f;

        Vec3 offsetDir = vnorm(Vec3(1.0f, 0.35f, 1.0f));
        Vec3 cameraPos = vadd(center, vscale(offsetDir, distance));
        Vec3 lookDir = vnorm(vsub(center, cameraPos));

        float yawRad = std::atan2(lookDir.z, lookDir.x);
        float pitchRad = std::asin(lookDir.y);

        float yawDeg = yawRad / DEG_TO_RAD;
        float pitchDeg = pitchRad / DEG_TO_RAD;

        m_camera.positionX = cameraPos.x;
        m_camera.positionY = cameraPos.y;
        m_camera.positionZ = cameraPos.z;
        m_camera.yawDeg = yawDeg;
        m_camera.pitchDeg = pitchDeg;

        auto& editorCam = m_editorContext.GetCamera();
        editorCam.positionX = m_camera.positionX;
        editorCam.positionY = m_camera.positionY;
        editorCam.positionZ = m_camera.positionZ;
        editorCam.yawDeg = m_camera.yawDeg;
        editorCam.pitchDeg = m_camera.pitchDeg;

        std::cout << "[QtEditorEngine] Framed scene: center(" << center.x << ", "
                  << center.y << ", " << center.z << ") radius=" << radius << "\n";
    }
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
