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
#include "../../Scripting/LuaDebugger.h"
#include "ToolsCommon/PathUtils.h"
#include "../../Automation/PhysicsHostAdapter.h"
#include "../../Automation/RuntimeUIHostAdapter.h"
#include "AppConfig.h"
#include "EditorUndoCommand.h"
#include "../../Audio/AudioEngine.h"
#include "../../Core/EngineLog.h"
#include "../../Gfx/VulkanDevice.h"
#include "../../Gfx/VulkanSwapchain.h"
#include "../../Gfx/VulkanPipeline.h"
#include "../../Gfx/EngineFrameRenderer.h"
#include "../../Gfx/Vertex.h"
#include "../../Scene/ActiveCameraSync.h"
#include "../../Scene/Scene.h"
#include "../../Scene/SceneSerializer.h"
#include "../../Scene/Components.h"
#include "../../Scene/Entity.h"
#include "../../Scene/PrefabOverrideSystem.h"
#include "../../Assets/MeshAsset.h"
#include "../../Assets/ModelImporter.h"
#include "../../Assets/SkeletalModelImporter.h"
#include "../../Assets/CharacterRuntime.h"
#include "../../Core/ProjectContext.h"
#include "../../Physics/JoltPhysicsEngine.h"
#include "../../Physics/PhysicsLayerConfig.h"
#include "../../Physics/JoltPhysicsSystem.h"
#include "../../Physics/CharacterMotor.h"
#include "../../Physics/CharacterControllerSystem.h"
#include "../../Scripting/LuaScriptEngine.h"
#include "../../Scripting/PluginScriptHost.h"
#include "../../Scripting/ScriptPhysicsBindings.h"
#include "../../Core/SimulationStep.h"
#include "../../Gfx/BuiltinMeshes.h"
#include "../../Scripting/ScriptEngine.h"
#include "../../Scripting/LuaScriptComponent.h"
#include "../HephaestusUI/UISystem.h"
#include "../HephaestusUI/UIContext.h"
#include "../HephaestusUI/UIRenderer.h"

#include <QCoreApplication>
#include <GLFW/glfw3.h>  // Shared physical key/button numbering
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
#endif

namespace Hephaestus {
namespace QtEditor {

// ─────────────────────────────────────────────────────────────────────────────
// Qt key code → GLFW key code translation table
// ─────────────────────────────────────────────────────────────────────────────
static int QtKeyToGLFW(int qtKey) {
    // Printable ASCII keys: Qt::Key_A (0x41) .. Qt::Key_Z (0x5A)
    if (qtKey >= 0x41 && qtKey <= 0x5A) {
        return GLFW_KEY_A + (qtKey - 0x41);
    }
    // Digits: Qt::Key_0 (0x30) .. Qt::Key_9 (0x39)
    if (qtKey >= 0x30 && qtKey <= 0x39) {
        return GLFW_KEY_0 + (qtKey - 0x30);
    }

    // Named keys
    static const std::unordered_map<int, int> kMap = {
        { 0x01000000, GLFW_KEY_ESCAPE },     // Qt::Key_Escape
        { 0x01000001, GLFW_KEY_TAB },         // Qt::Key_Tab
        { 0x01000003, GLFW_KEY_BACKSPACE },   // Qt::Key_Backspace
        { 0x01000004, GLFW_KEY_ENTER },       // Qt::Key_Return
        { 0x01000005, GLFW_KEY_KP_ENTER },    // Qt::Key_Enter  (keypad)
        { 0x01000006, GLFW_KEY_INSERT },       // Qt::Key_Insert
        { 0x01000007, GLFW_KEY_DELETE },       // Qt::Key_Delete
        { 0x01000008, GLFW_KEY_PAUSE },        // Qt::Key_Pause
        { 0x01000009, GLFW_KEY_PRINT_SCREEN }, // Qt::Key_Print
        { 0x01000010, GLFW_KEY_HOME },         // Qt::Key_Home
        { 0x01000011, GLFW_KEY_END },          // Qt::Key_End
        { 0x01000012, GLFW_KEY_LEFT },         // Qt::Key_Left
        { 0x01000013, GLFW_KEY_UP },           // Qt::Key_Up
        { 0x01000014, GLFW_KEY_RIGHT },        // Qt::Key_Right
        { 0x01000015, GLFW_KEY_DOWN },         // Qt::Key_Down
        { 0x01000016, GLFW_KEY_PAGE_UP },      // Qt::Key_PageUp
        { 0x01000017, GLFW_KEY_PAGE_DOWN },    // Qt::Key_PageDown
        { 0x01000020, GLFW_KEY_LEFT_SHIFT },   // Qt::Key_Shift
        { 0x01000021, GLFW_KEY_LEFT_CONTROL }, // Qt::Key_Control
        { 0x01000023, GLFW_KEY_LEFT_ALT },     // Qt::Key_Alt
        { 0x01000024, GLFW_KEY_CAPS_LOCK },    // Qt::Key_CapsLock
        { 0x01000025, GLFW_KEY_NUM_LOCK },     // Qt::Key_NumLock
        { 0x01000026, GLFW_KEY_SCROLL_LOCK },  // Qt::Key_ScrollLock
        { 0x01000030, GLFW_KEY_F1 },           // Qt::Key_F1
        { 0x01000031, GLFW_KEY_F2 },
        { 0x01000032, GLFW_KEY_F3 },
        { 0x01000033, GLFW_KEY_F4 },
        { 0x01000034, GLFW_KEY_F5 },
        { 0x01000035, GLFW_KEY_F6 },
        { 0x01000036, GLFW_KEY_F7 },
        { 0x01000037, GLFW_KEY_F8 },
        { 0x01000038, GLFW_KEY_F9 },
        { 0x01000039, GLFW_KEY_F10 },
        { 0x0100003a, GLFW_KEY_F11 },
        { 0x0100003b, GLFW_KEY_F12 },
        { 0x01000055, GLFW_KEY_MENU },         // Qt::Key_Menu
        { 0x20,       GLFW_KEY_SPACE },        // Qt::Key_Space
        { 0x2c,       GLFW_KEY_COMMA },
        { 0x2d,       GLFW_KEY_MINUS },
        { 0x2e,       GLFW_KEY_PERIOD },
        { 0x2f,       GLFW_KEY_SLASH },
        { 0x3b,       GLFW_KEY_SEMICOLON },
        { 0x3d,       GLFW_KEY_EQUAL },
        { 0x5b,       GLFW_KEY_LEFT_BRACKET },
        { 0x5c,       GLFW_KEY_BACKSLASH },
        { 0x5d,       GLFW_KEY_RIGHT_BRACKET },
        { 0x60,       GLFW_KEY_GRAVE_ACCENT },
    };

    auto it = kMap.find(qtKey);
    return (it != kMap.end()) ? it->second : -1;
}

/// Translate Qt mouse button code to GLFW mouse button code
static int QtMouseButtonToGLFW(int qtButton) {
    if (qtButton == 1)  return GLFW_MOUSE_BUTTON_LEFT;    // Qt::LeftButton
    if (qtButton == 2)  return GLFW_MOUSE_BUTTON_RIGHT;   // Qt::RightButton
    if (qtButton == 4)  return GLFW_MOUSE_BUTTON_MIDDLE;  // Qt::MiddleButton
    return -1;
}

struct RuntimeUIState {
    std::unique_ptr<::Hephaestus::HephaestusUI::UISystem> system;
    std::unique_ptr<::Hephaestus::HephaestusUI::UIContext> context;
    std::unique_ptr<::Hephaestus::HephaestusUI::UIRenderer> renderer;
};

static std::unordered_map<const QtEditorEngine*, RuntimeUIState> g_runtimeUIStates;

static RuntimeUIState& EnsureRuntimeUIState(const QtEditorEngine* engine)
{
    return g_runtimeUIStates[engine];
}

static RuntimeUIState* FindRuntimeUIState(const QtEditorEngine* engine)
{
    auto it = g_runtimeUIStates.find(engine);
    if (it == g_runtimeUIStates.end()) {
        return nullptr;
    }
    return &it->second;
}

static void ClearRuntimeUIState(const QtEditorEngine* engine)
{
    g_runtimeUIStates.erase(engine);
}

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
    m_editorContext.SetRebuildSceneVisualsCallback([this]() { RebuildSceneMeshes(); });
}

QtEditorEngine::~QtEditorEngine()
{
    Shutdown();
}

static const char* VkResultToString(VkResult result)
{
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        default: return "VK_UNKNOWN";
    }
}

bool QtEditorEngine::InitializeVulkan()
{
    if (m_state != EngineState::Uninitialized) {
        emit engineError("Engine already initialized");
        return false;
    }
    
    m_state = EngineState::InitializingVulkan;
    EngineLog::Info("QtEditorEngine: initializing Vulkan");
    
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
    EngineLog::Info("QtEditorEngine: Vulkan initialized, waiting for viewport");
    std::cout << "[QtEditorEngine] Vulkan initialized, waiting for viewport\n";
    return true;
}

bool QtEditorEngine::createVulkanInstance()
{
    EngineLog::Info("createVulkanInstance: preparing VkApplicationInfo (Vulkan 1.3)");
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = HEPHAESTUS_APP_NAME " (Qt Editor)";
    appInfo.applicationVersion = VK_MAKE_VERSION(HEPHAESTUS_VERSION_MAJOR, HEPHAESTUS_VERSION_MINOR, HEPHAESTUS_VERSION_PATCH);
    appInfo.pEngineName = HEPHAESTUS_APP_NAME;
    appInfo.engineVersion = VK_MAKE_VERSION(HEPHAESTUS_VERSION_MAJOR, HEPHAESTUS_VERSION_MINOR, HEPHAESTUS_VERSION_PATCH);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // Extensions needed for the Qt native viewport.
    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(__APPLE__)
        VK_EXT_METAL_SURFACE_EXTENSION_NAME,
#endif
    };

#ifdef _DEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
#ifdef __APPLE__
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
#ifdef __APPLE__
    // Matches the packaged game so MoltenVK, a portability driver, is enumerated.
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

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

#ifdef _DEBUG
    EngineLog::Info("createVulkanInstance: requesting VK_LAYER_KHRONOS_validation");
#else
    EngineLog::Info("createVulkanInstance: validation layers disabled (non-debug build)");
#endif

    EngineLog::Info("createVulkanInstance: calling vkCreateInstance (hangs here usually mean a GPU driver/ICD issue)");
    const VkResult instanceResult = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (instanceResult != VK_SUCCESS) {
        std::string error = std::string("Failed to create Vulkan instance: ") +
                            VkResultToString(instanceResult) + " (" +
                            std::to_string(static_cast<int>(instanceResult)) + ")";
        EngineLog::Error(error);
        emit engineError(QString::fromStdString(error));
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

    EngineLog::Info("createVulkanInstance: instance created");
    std::cout << "[QtEditorEngine] Vulkan instance created\n";
    return true;
}

bool QtEditorEngine::initializeVulkanDevice()
{
    try {
        EngineLog::Info("initializeVulkanDevice: selecting GPU and creating logical device");
        m_device = std::make_unique<VulkanDevice>();
        m_device->InitWithoutSurface(m_instance);
        EngineLog::Info("initializeVulkanDevice: device ready");
        return true;
    } catch (const std::exception& e) {
        EngineLog::Error(std::string("initializeVulkanDevice failed: ") + e.what());
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
    
    EngineLog::Info("AttachToViewport: creating Win32/Vulkan surface and swapchain");
    
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
        if (!m_frameRenderer->Init(m_device.get(), m_swapchain.get(), true)) {
            throw std::runtime_error("Failed to initialize frame renderer");
        }
        
        // Create a default scene only if one wasn't already loaded
        // (e.g. project startup may load a configured default scene first).
        if (!m_editorContext.HasActiveScene()) {
            m_editorContext.NewScene("New Scene");
        }
        
        // Set up default camera from EditorContext
        const auto& cam = m_editorContext.GetCamera();
        m_camera.positionX = cam.positionX;
        m_camera.positionY = cam.positionY;
        m_camera.positionZ = cam.positionZ;
        m_camera.yawDeg = cam.yawDeg;
        m_camera.pitchDeg = cam.pitchDeg;
        
        std::cout << "[QtEditorEngine] Swapchain and frame renderer created (" 
                  << m_swapchain->extent.width << "x" << m_swapchain->extent.height << ")\n";
        m_reportedFatalRenderError = false;
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
    m_reportedFatalRenderError = false;
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

    if (m_joltPhysicsSystem) {
        m_joltPhysicsSystem->ClearAllBodies();
        m_joltPhysicsSystem->SetEnabled(false);
    }
    if (m_joltPhysicsEngine && m_physicsInitialized) {
        m_joltPhysicsEngine->Shutdown();
        m_physicsInitialized = false;
    }
    m_joltPhysicsSystem.reset();
    m_joltPhysicsEngine.reset();
    
    cleanupVulkanResources();
    ClearRuntimeUIState(this);
    
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
    // Handle ignore flag for cursor warp
    if (event.type == InputEvent::Type::MouseMove && m_viewport && m_viewport->ShouldIgnoreNextMouseMove()) {
        m_viewport->SetIgnoreNextMouseMove(false);
        m_lastMouseX = event.x;
        m_lastMouseY = event.y;
        return;
    }

    auto buildViewProj = [&](Mat4& viewMatrix, Mat4& projMatrix, Vec3& camPos) -> bool {
        if (!m_swapchain || m_swapchain->extent.width == 0 || m_swapchain->extent.height == 0) {
            return false;
        }

        const auto& cam = m_camera;
        float yawRad = cam.yawDeg * 3.14159265f / 180.0f;
        float pitchRad = cam.pitchDeg * 3.14159265f / 180.0f;
        Vec3 forward(
            std::cos(pitchRad) * std::cos(yawRad),
            std::sin(pitchRad),
            std::cos(pitchRad) * std::sin(yawRad)
        );
        forward = vnorm(forward);
        camPos = Vec3(cam.positionX, cam.positionY, cam.positionZ);
        Vec3 target = vadd(camPos, forward);
        viewMatrix = Mat4::LookAt(camPos, target, Vec3(0, 1, 0));

        float aspect = static_cast<float>(m_swapchain->extent.width) /
                       static_cast<float>(m_swapchain->extent.height);
        if (cam.projectionMode == Hephaestus::ProjectionMode::Orthographic) {
            const float halfH = std::max(cam.orthoHalfExtent, 0.01f);
            const float halfW = halfH * aspect;
            projMatrix = Mat4::Orthographic(-halfW, halfW, -halfH, halfH, cam.nearPlane, cam.farPlane);
        } else {
            const float fovDeg = (cam.fovDeg > 0.1f) ? cam.fovDeg : 60.0f;
            projMatrix = Mat4::Perspective(fovDeg * 3.14159265f / 180.0f,
                                           aspect, cam.nearPlane, cam.farPlane);
        }
        return true;
    };

    auto buildRayFromScreen = [&](float screenX, float screenY,
                                  const Mat4& viewMatrix,
                                  const Mat4& projMatrix) -> Ray {
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

        return Ray(rayNearWorld, rayDir);
    };

    auto toPixelCoords = [&](float x, float y, float& outX, float& outY) {
        if (!m_swapchain || m_swapchain->extent.width == 0 || m_swapchain->extent.height == 0) {
            outX = x;
            outY = y;
            return;
        }
        float viewW = m_viewport ? static_cast<float>(m_viewport->width()) : 0.0f;
        float viewH = m_viewport ? static_cast<float>(m_viewport->height()) : 0.0f;
        if (viewW <= 0.0f || viewH <= 0.0f) {
            outX = x;
            outY = y;
            return;
        }

        float scaleX = static_cast<float>(m_swapchain->extent.width) / viewW;
        float scaleY = static_cast<float>(m_swapchain->extent.height) / viewH;
        outX = x * scaleX;
        outY = y * scaleY;
    };

    auto toUIPoint = [&](float x, float y) -> ::Hephaestus::HephaestusUI::UIVec2 {
        float px = x;
        float py = y;
        toPixelCoords(x, y, px, py);
        return {px, py};
    };

    auto isRuntimeUICursorMode = [&]() -> bool {
        if (m_playModeManager.IsEditor()) return false;
        RuntimeUIState* rui = FindRuntimeUIState(this);
        return rui && rui->system &&
               rui->system->GetCursorMode() == Hephaestus::HephaestusUI::CursorMode::UI;
    };

    auto canInteractWithGizmo = [&]() -> bool {
        // Suppress gizmo interaction during play mode
        if (m_playModeManager.ShouldSuppressEditorInteraction()) {
            return false;
        }
        if (!m_frameRenderer || !m_frameRenderer->IsGizmoAvailable()) {
            return false;
        }
        if (!m_editorContext.HasActiveScene()) {
            return false;
        }
        if (m_editorContext.GetSelectedEntity() != entt::null &&
            m_editorContext.IsEditorLocked(
                m_editorContext.GetSelectedEntity())) {
            return false;
        }
        const auto& viewportSettings = m_editorContext.GetViewportSettings();
        if (!viewportSettings.showGizmo) {
            return false;
        }
        if (m_editorContext.GetGizmoMode() == Hephaestus::GizmoMode::None) {
            return false;
        }
        if (!m_inputState.viewportFocused && !m_inputState.mouseInViewport) {
            return false;
        }
        if (m_cameraController.IsFlyModeActive() || m_cameraController.IsOrbitModeActive() ||
            m_cameraController.IsPanModeActive() || m_cameraController.IsDollyModeActive()) {
            return false;
        }
        return true;
    };

    auto endGizmoDrag = [&]() {
        if (m_gizmoDragging && m_frameRenderer && m_frameRenderer->IsGizmoAvailable()) {
            m_frameRenderer->GetGizmo()->EndDrag();
            m_frameRenderer->GetGizmo()->SetHoveredAxis(-1);
        }
        m_gizmoDragging = false;
        m_gizmoActiveAxis = -1;
        clearGizmoMultiDragWorldStarts();
    };
    
    // Update input state based on event type
    switch (event.type) {
    case InputEvent::Type::MousePress: {
        // Update button state
        if (event.button == 1) m_inputState.lmbDown = true;       // Qt::LeftButton
        else if (event.button == 2) m_inputState.rmbDown = true;  // Qt::RightButton
        else if (event.button == 4) m_inputState.mmbDown = true;  // Qt::MiddleButton
        
        // Track for shared gameplay input
        {
            int glfwBtn = QtMouseButtonToGLFW(event.button);
            if (glfwBtn >= 0 && glfwBtn < kMaxTrackedMouse) {
                m_runtimeMouseButtons[glfwBtn] = true;
            }
        }
        
        // Update modifiers
        m_inputState.altDown = (event.modifiers & 0x08000000) != 0;   // Qt::AltModifier
        m_inputState.shiftDown = (event.modifiers & 0x02000000) != 0; // Qt::ShiftModifier
        m_inputState.ctrlDown = (event.modifiers & 0x04000000) != 0;  // Qt::ControlModifier
        
        // Update mouse position
        m_inputState.mouseX = event.x;
        m_inputState.mouseY = event.y;
        m_runtimeMouseX = static_cast<double>(event.x);
        m_runtimeMouseY = static_cast<double>(event.y);
        if (event.button == 2) {
            m_rmbPressX = event.x;
            m_rmbPressY = event.y;
            m_rmbMoved = false;
        }

        if (event.button == 1 && m_placementPreview != entt::null) {
            Scene* scene = m_editorContext.GetActiveScene();
            if (scene && scene->GetRegistry().valid(m_placementPreview)) {
                scene->GetRegistry().remove<EditorOnlyComponent>(m_placementPreview);
                m_editorContext.MarkDirty();
                const bool repeat = m_inputState.shiftDown;
                const entt::entity committed = m_placementPreview;
                m_placementPreview = entt::null;
                if (repeat) {
                    EditorUndoManager::PushSnapshot(
                        &m_editorContext, "Repeat placement");
                    const auto duplicates =
                        scene->DuplicateEntities({committed}, true);
                    if (!duplicates.empty()) {
                        BeginPlacement(
                            static_cast<entt::entity>(duplicates.front()));
                    }
                }
                emit sceneHierarchyChanged();
            }
            break;
        }

        RuntimeUIState* runtimeUIState = FindRuntimeUIState(this);
        if (!m_playModeManager.IsEditor() && runtimeUIState && runtimeUIState->context &&
            !(m_viewport && m_viewport->IsCursorCaptured())) {
            const ::Hephaestus::HephaestusUI::UIVec2 uiPos = toUIPoint(event.x, event.y);
            std::cout << "[UI Debug] Mouse Press at (" << uiPos.x << ", " << uiPos.y << ") button=" << event.button << std::endl;
            runtimeUIState->context->QueueMousePress(uiPos, event.button);
        }

        if (event.button == 1 && !m_inputState.rmbDown) {
            // Save press position for picking (release may drift due to jitter)
            m_pickPressX = event.x;
            m_pickPressY = event.y;
            m_pickPressValid = true;

            if (canInteractWithGizmo()) {
                Mat4 viewMatrix;
                Mat4 projMatrix;
                Vec3 camPos;
                if (buildViewProj(viewMatrix, projMatrix, camPos)) {
                    float px = event.x;
                    float py = event.y;
                    toPixelCoords(event.x, event.y, px, py);
                    int hoveredAxis = -1;
                    if (m_frameRenderer->CheckGizmoHover(px, py, viewMatrix, projMatrix, hoveredAxis)) {
                        Ray gizmoRay = buildRayFromScreen(px, py, viewMatrix, projMatrix);
                        EditorUndoManager::PushSnapshot(
                            &m_editorContext,
                            m_inputState.altDown ? "Duplicate and transform" : "Gizmo transform");
                        if (m_inputState.altDown) {
                            Scene* scene = m_editorContext.GetActiveScene();
                            if (scene) {
                                const std::vector<Entity> duplicates =
                                    scene->DuplicateEntities(
                                        m_editorContext.GetSelectedEntities(), true);
                                std::vector<entt::entity> duplicateHandles;
                                duplicateHandles.reserve(duplicates.size());
                                for (const Entity& duplicate : duplicates) {
                                    duplicateHandles.push_back(
                                        static_cast<entt::entity>(duplicate));
                                }
                                if (!duplicateHandles.empty()) {
                                    m_editorContext.SelectEntities(
                                        duplicateHandles, duplicateHandles.back());
                                    m_editorContext.MarkDirty();
                                    emit sceneHierarchyChanged();
                                }
                            }
                        }
                        m_frameRenderer->GetGizmo()->BeginDrag(gizmoRay, hoveredAxis);
                        m_gizmoDragging = true;
                        m_gizmoActiveAxis = hoveredAxis;
                        m_pickPressValid = false; // gizmo owns this click
                        Scene* sceneForGizmo = m_editorContext.GetActiveScene();
                        if (sceneForGizmo) {
                            captureGizmoMultiDragWorldStarts(sceneForGizmo);
                        }
                    }
                }
            }
        }
        
        // Notify camera controller (suppressed when Lua UI has cursor unlocked)
        if (!m_gizmoDragging && !isRuntimeUICursorMode()) {
            m_cameraController.OnMousePress(event.button, m_inputState);
        }
        syncViewportCursorCapture();
        
        if (!(m_viewport && m_viewport->IsCursorCaptured())) {
            m_lastMouseX = event.x;
            m_lastMouseY = event.y;
        }
        m_hasLastMousePos = true;
        break;
    }
        
    case InputEvent::Type::MouseRelease: {
        const bool openContextMenu =
            event.button == 2 && !m_rmbMoved &&
            m_playModeManager.IsEditor();
        // Update button state
        if (event.button == 1) {
            m_inputState.lmbDown = false;
            {
                int glfwBtn = QtMouseButtonToGLFW(event.button);
                if (glfwBtn >= 0 && glfwBtn < kMaxTrackedMouse)
                    m_runtimeMouseButtons[glfwBtn] = false;
            }

            if (m_gizmoDragging) {
                endGizmoDrag();
                break;
            }
            if (m_marqueeSelecting) {
                const bool ctrlHeld = (event.modifiers & 0x04000000) != 0;
                const bool shiftHeld = (event.modifiers & 0x02000000) != 0;
                selectEntitiesInViewportRect(
                    m_pickPressX, m_pickPressY,
                    event.x, event.y, shiftHeld, ctrlHeld);
                m_marqueeSelecting = false;
                m_pickPressValid = false;
                break;
            }
            
            // LMB click without RMB (not fly mode) - try entity picking
            // Suppress picking during play mode; skip while Alt+orbit is active so release does not clear selection
            if (!m_inputState.rmbDown && !m_playModeManager.ShouldSuppressEditorInteraction() &&
                !m_cameraController.IsOrbitModeActive() &&
                m_frameRenderer && m_frameRenderer->IsPickingAvailable() &&
                m_pickPressValid) {
                // Use the press position (not release) for accuracy - avoids jitter drift
                float px = m_pickPressX;
                float py = m_pickPressY;
                toPixelCoords(m_pickPressX, m_pickPressY, px, py);

                const bool ctrlHeld = (event.modifiers & 0x04000000) != 0;  // Qt::ControlModifier
                const bool shiftHeld = (event.modifiers & 0x02000000) != 0; // Qt::ShiftModifier
                const bool altHeld = (event.modifiers & 0x08000000) != 0; // Qt::AltModifier

                const uint32_t pixX = static_cast<uint32_t>(px + 0.5f);
                const uint32_t pixY = static_cast<uint32_t>(py + 0.5f);
                if (!m_swapchain || pixX >= m_swapchain->extent.width ||
                    pixY >= m_swapchain->extent.height) {
                    m_editorContext.SelectFromViewportClick(entt::null, ctrlHeld, shiftHeld);
                } else {
                    m_viewportPickCtrlHeld = ctrlHeld;
                    m_viewportPickShiftHeld = shiftHeld;
                    m_viewportPickAltHeld = altHeld;
                    m_viewportPickWaitingReadback = true;
                    m_frameRenderer->PickEntityAt(pixX, pixY);
                }
            }
            m_pickPressValid = false;
        }
        else if (event.button == 2) m_inputState.rmbDown = false;
        else if (event.button == 4) m_inputState.mmbDown = false;

        RuntimeUIState* runtimeUIState = FindRuntimeUIState(this);
        if (!m_playModeManager.IsEditor() && runtimeUIState && runtimeUIState->context &&
            !(m_viewport && m_viewport->IsCursorCaptured())) {
            const ::Hephaestus::HephaestusUI::UIVec2 uiPos = toUIPoint(event.x, event.y);
            runtimeUIState->context->QueueMouseRelease(uiPos, event.button);
        }
        
        // Track for shared gameplay input (right/middle release)
        if (event.button != 1) {
            int glfwBtn = QtMouseButtonToGLFW(event.button);
            if (glfwBtn >= 0 && glfwBtn < kMaxTrackedMouse)
                m_runtimeMouseButtons[glfwBtn] = false;
        }
        
        if (!isRuntimeUICursorMode()) {
            m_cameraController.OnMouseRelease(event.button, m_inputState);
        }
        syncViewportCursorCapture();
        if (openContextMenu) {
            emit viewportContextMenuRequested(event.x, event.y);
        }
        break;
    }
        
    case InputEvent::Type::MouseMove:
        {
            // Calculate mouse delta
            float deltaX = 0.0f;
            float deltaY = 0.0f;
            if (m_hasLastMousePos) {
                deltaX = event.x - m_lastMouseX;
                deltaY = event.y - m_lastMouseY;
            }
            
            m_inputState.mouseX = event.x;
            m_inputState.mouseY = event.y;
            m_inputState.mouseDeltaX = deltaX;
            m_inputState.mouseDeltaY = deltaY;

            RuntimeUIState* runtimeUI = FindRuntimeUIState(this);
            if (!m_playModeManager.IsEditor() && runtimeUI && runtimeUI->context &&
                !(m_viewport && m_viewport->IsCursorCaptured())) {
                const ::Hephaestus::HephaestusUI::UIVec2 uiPos = toUIPoint(event.x, event.y);
                runtimeUI->context->QueueMouseMove(uiPos);
            }

            // Track for Lua input
            m_runtimeMouseX = static_cast<double>(event.x);
            m_runtimeMouseY = static_cast<double>(event.y);
            if (m_inputState.rmbDown) {
                const float rdx = event.x - m_rmbPressX;
                const float rdy = event.y - m_rmbPressY;
                if (rdx * rdx + rdy * rdy > 16.0f) m_rmbMoved = true;
            }

            if (m_placementPreview != entt::null) {
                Scene* scene = m_editorContext.GetActiveScene();
                if (scene && scene->GetRegistry().valid(m_placementPreview)) {
                    std::unordered_set<entt::entity> ignored{m_placementPreview};
                    SceneAuthoring::SceneRaycastHit hit;
                    if (RaycastViewport(event.x, event.y, hit, ignored)) {
                        Entity preview(m_placementPreview, scene);
                        Mat4 world = scene->GetWorldTransform(preview);
                        Vec3 position = hit.point;
                        if (preview.HasComponent<MeshRendererComponent>()) {
                            const AABB bounds = SceneAuthoring::TransformBounds(
                                preview.GetComponent<MeshRendererComponent>().localBounds,
                                world);
                            const Vec3 half = bounds.HalfSize();
                            const float support =
                                std::abs(hit.normal.x) * half.x +
                                std::abs(hit.normal.y) * half.y +
                                std::abs(hit.normal.z) * half.z;
                            position = vsub(
                                vadd(hit.point, vscale(hit.normal, support)),
                                vsub(bounds.Center(),
                                     Vec3(world.m[12], world.m[13], world.m[14])));
                        }
                        const auto& snap = m_editorContext.GetSnapSettings();
                        if (snap.enableSnap && !m_inputState.ctrlDown) {
                            position = SceneAuthoring::SnapVector(
                                position, snap.translateSnap);
                        }
                        world.m[12] = position.x;
                        world.m[13] = position.y;
                        world.m[14] = position.z;
                        Entity parent = scene->GetParent(preview);
                        const Mat4 local = parent
                            ? Mat4::Multiply(
                                Mat4::Invert(scene->GetWorldTransform(parent)),
                                world)
                            : world;
                        preview.GetComponent<TransformComponent>().SetFromMatrix(local);
                    }
                }
                break;
            }
            if (!m_playModeManager.IsEditor() &&
                !(m_viewport && m_viewport->IsCursorCaptured())) {
                m_runtimeMouseDeltaX += static_cast<double>(deltaX);
                m_runtimeMouseDeltaY += static_cast<double>(deltaY);
            }

            if (m_inputState.lmbDown && m_pickPressValid &&
                !m_inputState.altDown && !m_gizmoDragging) {
                const float dx = event.x - m_pickPressX;
                const float dy = event.y - m_pickPressY;
                if (dx * dx + dy * dy > 36.0f) {
                    m_marqueeSelecting = true;
                    m_marqueeCurrentX = event.x;
                    m_marqueeCurrentY = event.y;
                }
            }

            if (m_gizmoDragging) {
                if (!canInteractWithGizmo()) {
                    endGizmoDrag();
                } else {
                    Mat4 viewMatrix;
                    Mat4 projMatrix;
                    Vec3 camPos;
                    if (buildViewProj(viewMatrix, projMatrix, camPos)) {
                        Scene* scene = m_editorContext.GetActiveScene();
                        entt::entity selectedEntity = m_editorContext.GetSelectedEntity();
                        if (scene && selectedEntity != entt::null) {
                            Entity primaryEntity(selectedEntity, scene);
                            if (primaryEntity.HasComponent<TransformComponent>()) {
                                float px = event.x;
                                float py = event.y;
                                toPixelCoords(event.x, event.y, px, py);
                                Ray gizmoRay = buildRayFromScreen(px, py, viewMatrix, projMatrix);
                                const auto& snap = m_editorContext.GetSnapSettings();
                                float snapIncrement = snap.translateSnap;
                                if (m_editorContext.GetGizmoMode() == Hephaestus::GizmoMode::Rotate) {
                                    snapIncrement = snap.rotateSnapDeg;
                                } else if (m_editorContext.GetGizmoMode() == Hephaestus::GizmoMode::Scale) {
                                    snapIncrement = snap.scaleSnap;
                                }
                                m_frameRenderer->GetGizmo()->SetSnapSettings(
                                    snap.enableSnap, snapIncrement);
                                Mat4 newWorldPrimary =
                                    m_frameRenderer->GetGizmo()->HandleDrag(
                                        gizmoRay, m_inputState.ctrlDown);
                                if (m_editorContext.GetGizmoMode() ==
                                        Hephaestus::GizmoMode::Translate &&
                                    m_inputState.shiftDown) {
                                    std::unordered_set<entt::entity> ignored;
                                    for (entt::entity selected :
                                         m_editorContext.GetSelectedEntities()) {
                                        ignored.insert(selected);
                                    }
                                    SceneAuthoring::SceneRaycastHit surfaceHit;
                                    if (RaycastViewport(event.x, event.y,
                                                        surfaceHit, ignored)) {
                                        Entity primary(selectedEntity, scene);
                                        if (primary.HasComponent<MeshRendererComponent>()) {
                                            const AABB bounds =
                                                SceneAuthoring::TransformBounds(
                                                    primary.GetComponent<MeshRendererComponent>().localBounds,
                                                    newWorldPrimary);
                                            const Vec3 half = bounds.HalfSize();
                                            const float support =
                                                std::abs(surfaceHit.normal.x) * half.x +
                                                std::abs(surfaceHit.normal.y) * half.y +
                                                std::abs(surfaceHit.normal.z) * half.z;
                                            const Vec3 desiredCenter = vadd(
                                                surfaceHit.point,
                                                vscale(surfaceHit.normal, support));
                                            const Vec3 delta = vsub(
                                                desiredCenter, bounds.Center());
                                            newWorldPrimary.m[12] += delta.x;
                                            newWorldPrimary.m[13] += delta.y;
                                            newWorldPrimary.m[14] += delta.z;
                                        } else {
                                            newWorldPrimary.m[12] = surfaceHit.point.x;
                                            newWorldPrimary.m[13] = surfaceHit.point.y;
                                            newWorldPrimary.m[14] = surfaceHit.point.z;
                                        }
                                    }
                                }

                                if (!m_gizmoDragEntityWorldStart.empty()) {
                                    const Mat4 invPrimaryStart = Mat4::Invert(m_gizmoDragPrimaryWorldStart);
                                    const Mat4 deltaWorld = Mat4::Multiply(newWorldPrimary, invPrimaryStart);

                                    std::vector<std::pair<entt::entity, Mat4>> newWorlds;
                                    newWorlds.reserve(m_gizmoDragEntityWorldStart.size());
                                    for (const auto& start : m_gizmoDragEntityWorldStart) {
                                        const Mat4 nw = (start.first == selectedEntity)
                                            ? newWorldPrimary
                                            : Mat4::Multiply(deltaWorld, start.second);
                                        newWorlds.push_back({start.first, nw});
                                    }

                                    auto depthToRoot = [&](entt::entity e) -> int {
                                        int d = 0;
                                        Entity ent(e, scene);
                                        while (ent) {
                                            Entity p = scene->GetParent(ent);
                                            if (!p) {
                                                break;
                                            }
                                            ++d;
                                            ent = p;
                                        }
                                        return d;
                                    };
                                    std::sort(newWorlds.begin(), newWorlds.end(),
                                              [&](const auto& a, const auto& b) {
                                                  return depthToRoot(a.first) < depthToRoot(b.first);
                                              });

                                    for (const auto& nwPair : newWorlds) {
                                        Entity ent(nwPair.first, scene);
                                        if (!ent.HasComponent<TransformComponent>()) {
                                            continue;
                                        }
                                        auto& transform = ent.GetComponent<TransformComponent>();
                                        Mat4 localTransform = nwPair.second;
                                        Entity parentEntity = scene->GetParent(ent);
                                        if (parentEntity) {
                                            Mat4 parentWorld = scene->GetWorldTransform(parentEntity);
                                            Mat4 parentInv = Mat4::Invert(parentWorld);
                                            localTransform = Mat4::Multiply(parentInv, nwPair.second);
                                        }
                                        transform.SetFromMatrix(localTransform);
                                    }
                                } else {
                                    auto& transform = primaryEntity.GetComponent<TransformComponent>();
                                    Mat4 localTransform = newWorldPrimary;
                                    Entity parentEntity = scene->GetParent(primaryEntity);
                                    if (parentEntity) {
                                        Mat4 parentWorld = scene->GetWorldTransform(parentEntity);
                                        Mat4 parentInv = Mat4::Invert(parentWorld);
                                        localTransform = Mat4::Multiply(parentInv, newWorldPrimary);
                                    }
                                    transform.SetFromMatrix(localTransform);
                                }

                                m_editorContext.MarkDirty();

                                m_frameRenderer->GetGizmo()->SetHoveredAxis(m_gizmoActiveAxis);
                                m_frameRenderer->UpdateGizmoState(
                                    static_cast<uint32_t>(selectedEntity),
                                    scene->GetWorldTransform(primaryEntity),
                                    viewMatrix, projMatrix, camPos,
                                    static_cast<int>(m_editorContext.GetGizmoMode()),
                                    static_cast<int>(m_editorContext.GetGizmoSpace())
                                );
                            } else {
                                endGizmoDrag();
                            }
                        } else {
                            endGizmoDrag();
                        }
                    }
                }
            } else if (canInteractWithGizmo()) {
                Mat4 viewMatrix;
                Mat4 projMatrix;
                Vec3 camPos;
                if (buildViewProj(viewMatrix, projMatrix, camPos)) {
                    float px = event.x;
                    float py = event.y;
                    toPixelCoords(event.x, event.y, px, py);
                    int hoveredAxis = -1;
                    m_frameRenderer->CheckGizmoHover(px, py, viewMatrix, projMatrix, hoveredAxis);
                }
            } else if (m_frameRenderer && m_frameRenderer->IsGizmoAvailable()) {
                m_frameRenderer->GetGizmo()->SetHoveredAxis(-1);
            }
            
            // Notify camera controller if any navigation mode is active
            // (suppressed when Lua UI has cursor unlocked for UI interaction)
            if (!m_gizmoDragging && !isRuntimeUICursorMode() &&
                (m_cameraController.IsFlyModeActive() || 
                m_cameraController.IsOrbitModeActive() ||
                m_cameraController.IsPanModeActive() ||
                m_cameraController.IsDollyModeActive())) {
                m_cameraController.OnMouseMove(deltaX, deltaY, m_inputState);
            }
            
            m_lastMouseX = event.x;
            m_lastMouseY = event.y;
            m_hasLastMousePos = true;
        }
        break;
        
    case InputEvent::Type::MouseWheel:
        m_inputState.wheelDelta = event.wheelDelta;
        m_runtimeScrollDeltaY += static_cast<double>(event.wheelDelta);
        if (!isRuntimeUICursorMode()) {
            bool rmbHeld = (event.button & 2) != 0 || m_inputState.rmbDown;
            if (rmbHeld && m_cameraController.IsFlyModeActive()) {
                const float minMoveSpeed = 0.1f;
                const float maxMoveSpeed = 50.0f;
                const float speedStep = 1.1f;
                float speedScale = std::pow(speedStep, event.wheelDelta);
                float newSpeed = std::clamp(m_cameraController.baseMoveSpeed * speedScale, minMoveSpeed, maxMoveSpeed);
                m_cameraController.baseMoveSpeed = newSpeed;
                m_editorContext.GetCamera().moveSpeed = newSpeed;
                emit cameraSpeedChanged(newSpeed);
                break;
            }
            m_cameraController.OnMouseWheel(event.wheelDelta, m_inputState);
        }
        break;
        
    case InputEvent::Type::KeyPress:
        // Update key states
        updateKeyState(event.key, true);
        
        // Update modifiers
        m_inputState.altDown = (event.modifiers & 0x08000000) != 0;
        m_inputState.shiftDown = (event.modifiers & 0x02000000) != 0;
        m_inputState.ctrlDown = (event.modifiers & 0x04000000) != 0;

        if (event.key == 0x01000000 && m_placementPreview != entt::null) {
            m_placementPreview = entt::null;
            std::string error;
            EditorUndoManager::RestoreAndDiscardLastSnapshot(
                &m_editorContext, error);
            emit sceneHierarchyChanged();
            break;
        }
        if (event.key == 0x01000000 && m_gizmoDragging) { // Escape
            endGizmoDrag();
            std::string error;
            EditorUndoManager::RestoreAndDiscardLastSnapshot(
                &m_editorContext, error);
            emit sceneHierarchyChanged();
            break;
        }
        if (event.key == 0x01000007 && // Qt::Key_Delete
            !m_playModeManager.ShouldSuppressEditorInteraction()) {
            emit deleteSelectionRequested();
            break;
        }
        if (event.key == 0x60 && // Qt::Key_QuoteLeft
            !m_playModeManager.ShouldSuppressEditorInteraction()) {
            std::unordered_set<entt::entity> ignored;
            for (entt::entity selected : m_editorContext.GetSelectedEntities()) {
                ignored.insert(selected);
            }
            SceneAuthoring::SceneRaycastHit hit;
            if (!RaycastViewport(m_lastMouseX, m_lastMouseY, hit, ignored)) {
                ignored.clear();
                RaycastViewport(m_lastMouseX, m_lastMouseY, hit, ignored);
            }
            if (hit.entity != entt::null) {
                Entity target(hit.entity, m_editorContext.GetActiveScene());
                if (target.HasComponent<PrefabNodeComponent>()) {
                    Entity root = PrefabOverrideSystem::FindInstanceRoot(
                        *m_editorContext.GetActiveScene(), target);
                    if (root) hit.entity = static_cast<entt::entity>(root);
                }
                m_editorContext.SelectEntity(hit.entity);
            }
            break;
        }

        // Escape exits play mode in the editor (releases cursor clip so Stop is reachable).
        if (event.key == 0x01000000 && !m_playModeManager.IsEditor()) { // Qt::Key_Escape
            if (m_viewport && m_viewport->IsCursorCaptured()) {
                m_viewport->ReleaseCursor();
            }
            m_playModeManager.RequestStop();
            break;
        }
        
        // Handle F key for focus
        if (event.key == 0x46) { // Qt::Key_F
            FocusOnSelection();
        }
        
        // Gizmo mode shortcuts (only when not in fly mode and not in play mode)
        if (!m_inputState.rmbDown && !m_playModeManager.ShouldSuppressEditorInteraction()) {
            switch (event.key) {
                case 0x57: // Qt::Key_W - Translate mode
                    m_editorContext.SetGizmoMode(Hephaestus::GizmoMode::Translate);
                    break;
                case 0x45: // Qt::Key_E - Rotate mode
                    m_editorContext.SetGizmoMode(Hephaestus::GizmoMode::Rotate);
                    break;
                case 0x52: // Qt::Key_R - Scale mode
                    m_editorContext.SetGizmoMode(Hephaestus::GizmoMode::Scale);
                    break;
                case 0x51: // Qt::Key_Q - No gizmo
                    m_editorContext.SetGizmoMode(Hephaestus::GizmoMode::None);
                    break;
                case 0x58: // Qt::Key_X - Toggle local/world space
                    m_editorContext.ToggleGizmoSpace();
                    break;
            }
        }
        break;
        
    case InputEvent::Type::KeyRelease:
        updateKeyState(event.key, false);
        
        // Update modifiers
        m_inputState.altDown = (event.modifiers & 0x08000000) != 0;
        m_inputState.shiftDown = (event.modifiers & 0x02000000) != 0;
        m_inputState.ctrlDown = (event.modifiers & 0x04000000) != 0;
        break;
        
    case InputEvent::Type::FocusIn:
        m_inputState.viewportFocused = true;
        break;
        
    case InputEvent::Type::FocusOut:
        m_inputState.viewportFocused = false;
        // QtViewportWindow suspends an active capture itself on focus loss and
        // restores it on focus return. Releasing it here discarded that state
        // and could leave FPS look uncaptured after an embedded-window focus hop.
        endGizmoDrag();
        resetInputState();
        break;
        
    case InputEvent::Type::Enter:
        m_inputState.mouseInViewport = true;
        break;
        
    case InputEvent::Type::Leave:
        m_inputState.mouseInViewport = false;
        m_hasLastMousePos = false;
        endGizmoDrag();
        break;
    }
}

void QtEditorEngine::captureGizmoMultiDragWorldStarts(Scene* scene)
{
    m_gizmoDragEntityWorldStart.clear();
    m_gizmoDragPrimaryWorldStart = Mat4::Identity();
    if (!scene) {
        return;
    }

    const entt::entity primary = m_editorContext.GetSelectedEntity();
    if (primary == entt::null) {
        return;
    }

    bool capturedPrimary = false;
    for (entt::entity e : m_editorContext.GetSelectedEntities()) {
        Entity ent(e, scene);
        if (!ent || !ent.HasComponent<TransformComponent>()) {
            continue;
        }
        const Mat4 w = scene->GetWorldTransform(ent);
        m_gizmoDragEntityWorldStart.push_back({e, w});
        if (e == primary) {
            m_gizmoDragPrimaryWorldStart = w;
            capturedPrimary = true;
        }
    }

    if (!capturedPrimary) {
        m_gizmoDragEntityWorldStart.clear();
        m_gizmoDragPrimaryWorldStart = Mat4::Identity();
    } else if (m_gizmoDragEntityWorldStart.size() > 1) {
        Vec3 center(0, 0, 0);
        for (const auto& [_, world] : m_gizmoDragEntityWorldStart) {
            center = vadd(center, Vec3(world.m[12], world.m[13], world.m[14]));
        }
        center = vscale(
            center, 1.0f / static_cast<float>(m_gizmoDragEntityWorldStart.size()));
        m_gizmoDragPrimaryWorldStart.m[12] = center.x;
        m_gizmoDragPrimaryWorldStart.m[13] = center.y;
        m_gizmoDragPrimaryWorldStart.m[14] = center.z;
    }
}

void QtEditorEngine::clearGizmoMultiDragWorldStarts()
{
    m_gizmoDragEntityWorldStart.clear();
    m_gizmoDragPrimaryWorldStart = Mat4::Identity();
}

void QtEditorEngine::selectEntitiesInViewportRect(
    float x0, float y0, float x1, float y1, bool add, bool toggle)
{
    Scene* scene = m_editorContext.GetActiveScene();
    if (!scene || !m_viewport) return;

    const float left = std::min(x0, x1);
    const float right = std::max(x0, x1);
    const float top = std::min(y0, y1);
    const float bottom = std::max(y0, y1);
    const float width = std::max(1, m_viewport->width());
    const float height = std::max(1, m_viewport->height());

    const auto& camera = m_camera;
    const float yaw = camera.yawDeg * 3.14159265f / 180.0f;
    const float pitch = camera.pitchDeg * 3.14159265f / 180.0f;
    const Vec3 cameraPosition(camera.positionX, camera.positionY, camera.positionZ);
    const Vec3 forward = vnorm(Vec3(std::cos(pitch) * std::cos(yaw),
                                    std::sin(pitch),
                                    std::cos(pitch) * std::sin(yaw)));
    const Mat4 view = Mat4::LookAt(cameraPosition,
                                   vadd(cameraPosition, forward),
                                   Vec3(0, 1, 0));
    const float aspect = width / height;
    const Mat4 projection = camera.projectionMode == ProjectionMode::Orthographic
        ? Mat4::Orthographic(-camera.orthoHalfExtent * aspect,
                             camera.orthoHalfExtent * aspect,
                             -camera.orthoHalfExtent,
                             camera.orthoHalfExtent,
                             camera.nearPlane, camera.farPlane)
        : Mat4::Perspective(camera.fovDeg * 3.14159265f / 180.0f,
                            aspect, camera.nearPlane, camera.farPlane);
    const Mat4 viewProjection = Mat4::Multiply(projection, view);

    std::vector<entt::entity> inside;
    std::unordered_set<entt::entity> seen;
    auto meshView = scene->GetRegistry().view<TransformComponent, MeshRendererComponent>();
    for (entt::entity handle : meshView) {
        if (m_editorContext.IsEditorHidden(handle) ||
            m_editorContext.IsEditorLocked(handle)) continue;
        Entity entity(handle, scene);
        const auto& renderer = meshView.get<MeshRendererComponent>(handle);
        const AABB bounds = SceneAuthoring::TransformBounds(
            renderer.localBounds, scene->GetWorldTransform(entity));
        const Vec3 corners[] = {
            {bounds.min.x,bounds.min.y,bounds.min.z}, {bounds.max.x,bounds.min.y,bounds.min.z},
            {bounds.min.x,bounds.max.y,bounds.min.z}, {bounds.max.x,bounds.max.y,bounds.min.z},
            {bounds.min.x,bounds.min.y,bounds.max.z}, {bounds.max.x,bounds.min.y,bounds.max.z},
            {bounds.min.x,bounds.max.y,bounds.max.z}, {bounds.max.x,bounds.max.y,bounds.max.z}
        };
        float minX = width, minY = height, maxX = 0.0f, maxY = 0.0f;
        for (const Vec3& corner : corners) {
            const Vec3 ndc = Mat4::TransformPoint(viewProjection, corner);
            const float sx = (ndc.x + 1.0f) * 0.5f * width;
            const float sy = (ndc.y + 1.0f) * 0.5f * height;
            minX = std::min(minX, sx); maxX = std::max(maxX, sx);
            minY = std::min(minY, sy); maxY = std::max(maxY, sy);
        }
        if (minX >= left && maxX <= right && minY >= top && maxY <= bottom) {
            if (entity.HasComponent<PrefabNodeComponent>()) {
                Entity root = PrefabOverrideSystem::FindInstanceRoot(*scene, entity);
                if (root) handle = static_cast<entt::entity>(root);
            }
            if (seen.insert(handle).second) inside.push_back(handle);
        }
    }

    std::vector<entt::entity> result =
        (add || toggle) ? m_editorContext.GetSelectedEntities()
                        : std::vector<entt::entity>{};
    for (entt::entity handle : inside) {
        auto it = std::find(result.begin(), result.end(), handle);
        if (toggle && it != result.end()) result.erase(it);
        else if (it == result.end()) result.push_back(handle);
    }
    if (result.empty()) m_editorContext.ClearSelection();
    else m_editorContext.SelectEntities(result, result.back());
}

void QtEditorEngine::updateKeyState(int key, bool pressed)
{
    // Track for editor camera controller (existing WASDQE)
    switch (key) {
    case 0x57: m_inputState.wDown = pressed; break; // Qt::Key_W
    case 0x41: m_inputState.aDown = pressed; break; // Qt::Key_A
    case 0x53: m_inputState.sDown = pressed; break; // Qt::Key_S
    case 0x44: m_inputState.dDown = pressed; break; // Qt::Key_D
    case 0x51: m_inputState.qDown = pressed; break; // Qt::Key_Q
    case 0x45: m_inputState.eDown = pressed; break; // Qt::Key_E
    default: break;
    }

    // Also track for shared gameplay input (all keys, GLFW codes)
    int glfwKey = QtKeyToGLFW(key);
    if (glfwKey >= 0 && glfwKey < kMaxTrackedKeys) {
        m_runtimeKeys[glfwKey] = pressed;
    }

    // Map Shift/Ctrl/Alt as both left + right variants for GLFW
    if (key == 0x01000020) { // Qt::Key_Shift
        if (GLFW_KEY_LEFT_SHIFT < kMaxTrackedKeys) m_runtimeKeys[GLFW_KEY_LEFT_SHIFT] = pressed;
        if (GLFW_KEY_RIGHT_SHIFT < kMaxTrackedKeys) m_runtimeKeys[GLFW_KEY_RIGHT_SHIFT] = pressed;
    }
    if (key == 0x01000021) { // Qt::Key_Control
        if (GLFW_KEY_LEFT_CONTROL < kMaxTrackedKeys) m_runtimeKeys[GLFW_KEY_LEFT_CONTROL] = pressed;
        if (GLFW_KEY_RIGHT_CONTROL < kMaxTrackedKeys) m_runtimeKeys[GLFW_KEY_RIGHT_CONTROL] = pressed;
    }
    if (key == 0x01000023) { // Qt::Key_Alt
        if (GLFW_KEY_LEFT_ALT < kMaxTrackedKeys) m_runtimeKeys[GLFW_KEY_LEFT_ALT] = pressed;
        if (GLFW_KEY_RIGHT_ALT < kMaxTrackedKeys) m_runtimeKeys[GLFW_KEY_RIGHT_ALT] = pressed;
    }
}

void QtEditorEngine::resetInputState()
{
    m_inputState.lmbDown = false;
    m_inputState.mmbDown = false;
    m_inputState.rmbDown = false;
    m_inputState.wDown = false;
    m_inputState.aDown = false;
    m_inputState.sDown = false;
    m_inputState.dDown = false;
    m_inputState.qDown = false;
    m_inputState.eDown = false;
    m_inputState.shiftDown = false;
    m_inputState.ctrlDown = false;
    m_inputState.altDown = false;
    m_inputState.wheelDelta = 0.0f;
    m_inputState.mouseDeltaX = 0.0f;
    m_inputState.mouseDeltaY = 0.0f;

    // Reset runtime key/mouse tracking
    for (int i = 0; i < kMaxTrackedKeys; ++i) m_runtimeKeys[i] = false;
    for (int i = 0; i < kMaxTrackedMouse; ++i) m_runtimeMouseButtons[i] = false;
    m_runtimeMouseDeltaX = 0.0;
    m_runtimeMouseDeltaY = 0.0;
    m_runtimeScrollDeltaY = 0.0;
    m_gameplayInput.Reset();
}

void QtEditorEngine::populateGameplayInput()
{
    m_gameplayInput.BeginFrame();

    for (int i = 0; i < kMaxTrackedKeys; ++i) {
        m_gameplayInput.SetKeyState(i, m_runtimeKeys[i]);
    }

    for (int i = 0; i < kMaxTrackedMouse; ++i) {
        m_gameplayInput.SetMouseButtonState(i, m_runtimeMouseButtons[i]);
    }

    // Consume all deltas accumulated since the last script tick. Comparing
    // frame-to-frame positions loses captured/warp events between render ticks.
    if (m_viewport && m_viewport->IsCursorCaptured()) {
        float capturedDeltaX = 0.0f;
        float capturedDeltaY = 0.0f;
        if (m_viewport->ConsumeCapturedMouseDelta(capturedDeltaX, capturedDeltaY)) {
            m_runtimeMouseDeltaX += static_cast<double>(capturedDeltaX);
            m_runtimeMouseDeltaY += static_cast<double>(capturedDeltaY);
            m_runtimeMouseX = static_cast<double>(m_viewport->GetVirtualMouseX());
            m_runtimeMouseY = static_cast<double>(m_viewport->GetVirtualMouseY());
        }
    }
    m_gameplayInput.SetMousePosition(m_runtimeMouseX, m_runtimeMouseY);
    m_gameplayInput.AddMouseDelta(m_runtimeMouseDeltaX, m_runtimeMouseDeltaY);
    m_gameplayInput.AddScrollDelta(0.0, m_runtimeScrollDeltaY);
    // GLFW joystick polling is intentionally confined to the GLFW runtime.
    // Qt Play still receives the same keyboard/mouse action map.
    m_gameplayInput.SetGamepadConnected(false);
    m_runtimeMouseDeltaX = 0.0;
    m_runtimeMouseDeltaY = 0.0;
    m_runtimeScrollDeltaY = 0.0;

    syncViewportCursorCapture();
}

void QtEditorEngine::syncViewportCursorCapture()
{
    if (!m_viewport) {
        return;
    }

    bool wantCapture = m_cameraController.ShouldCaptureCursor();

    if (!m_playModeManager.IsEditor()) {
        RuntimeUIState* rui = FindRuntimeUIState(this);
        const bool uiCursor = rui && rui->system &&
            rui->system->GetCursorMode() == Hephaestus::HephaestusUI::CursorMode::UI;
        const bool gameCursor = rui && rui->system &&
            rui->system->GetCursorMode() == Hephaestus::HephaestusUI::CursorMode::Game;
        if (uiCursor) {
            wantCapture = false;
        } else if (gameCursor) {
            // FPS / game look: capture whenever UI reports Game mode.
            wantCapture = true;
        } else if (m_gameplayInput.GetCursorModeRequest() == 1) {
            wantCapture = true;
        }
    }

    const bool wasCaptured = m_viewport->IsCursorCaptured();
    if (wantCapture) {
        m_viewport->CaptureCursor();
        if (!wasCaptured && m_viewport->IsCursorCaptured()) {
            m_runtimeMouseX = static_cast<double>(m_viewport->GetVirtualMouseX());
            m_runtimeMouseY = static_cast<double>(m_viewport->GetVirtualMouseY());
            m_runtimeMouseDeltaX = 0.0;
            m_runtimeMouseDeltaY = 0.0;
            m_lastMouseX = static_cast<float>(m_runtimeMouseX);
            m_lastMouseY = static_cast<float>(m_runtimeMouseY);
            m_hasLastMousePos = true;
            m_gameplayInput.SetMousePosition(m_runtimeMouseX, m_runtimeMouseY);
            m_gameplayInput.SetMouseDelta(0.0, 0.0);
        }
    } else {
        m_viewport->ReleaseCursor();
    }
}

void QtEditorEngine::FocusOnSelection()
{
    if (!m_editorContext.HasActiveScene()) {
        return;
    }
    
    Scene* scene = m_editorContext.GetActiveScene();

    entt::entity selected = m_editorContext.GetSelectedEntity();
    
    float focusX = 0.0f;
    float focusY = 0.0f;
    float focusZ = 0.0f;
    float focusRadius = 5.0f;
    
    if (selected != entt::null && scene->GetRegistry().valid(selected)) {
        // Get entity transform
        auto* transform = scene->GetRegistry().try_get<TransformComponent>(selected);
        if (transform) {
            focusX = transform->position.x;
            focusY = transform->position.y;
            focusZ = transform->position.z;
        }
        
        // Try to get bounds from mesh
        auto* meshRenderer = scene->GetRegistry().try_get<MeshRendererComponent>(selected);
        if (meshRenderer && transform) {
            AABB bounds = meshRenderer->GetWorldBounds(*transform);
            // Calculate center and radius
            focusX = (bounds.min.x + bounds.max.x) * 0.5f;
            focusY = (bounds.min.y + bounds.max.y) * 0.5f;
            focusZ = (bounds.min.z + bounds.max.z) * 0.5f;
            
            float dx = bounds.max.x - bounds.min.x;
            float dy = bounds.max.y - bounds.min.y;
            float dz = bounds.max.z - bounds.min.z;
            focusRadius = std::sqrt(dx*dx + dy*dy + dz*dz) * 0.5f;
            if (focusRadius < 0.5f) {
                focusRadius = 2.0f;
            }
        }
    }
    
    m_cameraController.FocusOnPoint(focusX, focusY, focusZ, focusRadius);
    std::cout << "[QtEditorEngine] Focused on point (" << focusX << ", " << focusY << ", " << focusZ 
              << ") radius=" << focusRadius << "\n";
}

void QtEditorEngine::FocusAll()
{
    Scene* scene = m_editorContext.GetActiveScene();
    if (!scene) return;
    bool hasBounds = false;
    AABB combined;
    auto view = scene->GetRegistry().view<TransformComponent, MeshRendererComponent>();
    for (entt::entity handle : view) {
        if (m_editorContext.IsEditorHidden(handle)) continue;
        Entity entity(handle, scene);
        const auto& renderer = view.get<MeshRendererComponent>(handle);
        const AABB bounds = SceneAuthoring::TransformBounds(
            renderer.localBounds, scene->GetWorldTransform(entity));
        if (!hasBounds) {
            combined = bounds;
            hasBounds = true;
        } else {
            combined.min.x = std::min(combined.min.x, bounds.min.x);
            combined.min.y = std::min(combined.min.y, bounds.min.y);
            combined.min.z = std::min(combined.min.z, bounds.min.z);
            combined.max.x = std::max(combined.max.x, bounds.max.x);
            combined.max.y = std::max(combined.max.y, bounds.max.y);
            combined.max.z = std::max(combined.max.z, bounds.max.z);
        }
    }
    if (!hasBounds) {
        m_cameraController.FocusOnPoint(0, 0, 0, 5.0f);
        return;
    }
    const Vec3 center = combined.Center();
    const Vec3 size = vsub(combined.max, combined.min);
    const float radius = std::max(0.5f, vlen(size) * 0.5f);
    m_cameraController.FocusOnPoint(center.x, center.y, center.z, radius);
}

bool QtEditorEngine::RaycastViewport(
    float x, float y, Hephaestus::SceneAuthoring::SceneRaycastHit& outHit,
    const std::unordered_set<entt::entity>& ignored)
{
    Scene* scene = m_editorContext.GetActiveScene();
    if (!scene || !m_swapchain || m_swapchain->extent.width == 0 ||
        m_swapchain->extent.height == 0) {
        return false;
    }

    const auto& cam = m_camera;
    const float yaw = cam.yawDeg * 3.14159265f / 180.0f;
    const float pitch = cam.pitchDeg * 3.14159265f / 180.0f;
    const Vec3 cameraPosition(cam.positionX, cam.positionY, cam.positionZ);
    const Vec3 forward = vnorm(Vec3(std::cos(pitch) * std::cos(yaw),
                                    std::sin(pitch),
                                    std::cos(pitch) * std::sin(yaw)));
    const Mat4 view = Mat4::LookAt(cameraPosition,
                                   vadd(cameraPosition, forward),
                                   Vec3(0, 1, 0));
    const float aspect = static_cast<float>(m_swapchain->extent.width) /
                         static_cast<float>(m_swapchain->extent.height);
    Mat4 projection;
    if (cam.projectionMode == Hephaestus::ProjectionMode::Orthographic) {
        const float halfHeight = std::max(cam.orthoHalfExtent, 0.01f);
        const float halfWidth = halfHeight * aspect;
        projection = Mat4::Orthographic(-halfWidth, halfWidth,
                                        -halfHeight, halfHeight,
                                        cam.nearPlane, cam.farPlane);
    } else {
        projection = Mat4::Perspective(cam.fovDeg * 3.14159265f / 180.0f,
                                       aspect, cam.nearPlane, cam.farPlane);
    }

    const float viewportWidth = m_viewport
        ? static_cast<float>(m_viewport->width())
        : static_cast<float>(m_swapchain->extent.width);
    const float viewportHeight = m_viewport
        ? static_cast<float>(m_viewport->height())
        : static_cast<float>(m_swapchain->extent.height);
    const float pixelX = x * static_cast<float>(m_swapchain->extent.width) /
                         std::max(viewportWidth, 1.0f);
    const float pixelY = y * static_cast<float>(m_swapchain->extent.height) /
                         std::max(viewportHeight, 1.0f);
    const Vec3 nearNdc(2.0f * pixelX / m_swapchain->extent.width - 1.0f,
                       2.0f * pixelY / m_swapchain->extent.height - 1.0f,
                       0.0f);
    const Vec3 farNdc(nearNdc.x, nearNdc.y, 1.0f);
    const Mat4 inverseProjection = Mat4::Invert(projection);
    const Mat4 inverseView = Mat4::Invert(view);
    const Vec3 nearWorld = Mat4::TransformPoint(
        inverseView, Mat4::TransformPoint(inverseProjection, nearNdc));
    const Vec3 farWorld = Mat4::TransformPoint(
        inverseView, Mat4::TransformPoint(inverseProjection, farNdc));
    const Ray ray(nearWorld, vnorm(vsub(farWorld, nearWorld)));
    if (Hephaestus::SceneAuthoring::RaycastBounds(
            *scene, ray, outHit, ignored)) {
        return true;
    }
    if (std::abs(ray.direction.y) > 1e-6f) {
        const float distance = -ray.origin.y / ray.direction.y;
        if (distance >= 0.0f) {
            outHit.entity = entt::null;
            outHit.point = ray.at(distance);
            outHit.normal = Vec3(0, 1, 0);
            outHit.distance = distance;
            return true;
        }
    }
    return false;
}

void QtEditorEngine::BeginPlacement(entt::entity previewEntity)
{
    Scene* scene = m_editorContext.GetActiveScene();
    if (!scene || previewEntity == entt::null ||
        !scene->GetRegistry().valid(previewEntity)) {
        return;
    }
    m_placementPreview = previewEntity;
    if (!scene->GetRegistry().all_of<EditorOnlyComponent>(previewEntity)) {
        scene->GetRegistry().emplace<EditorOnlyComponent>(previewEntity);
    }
    m_editorContext.SelectEntity(previewEntity);
}

void QtEditorEngine::onTick()
{
    // While the debugger holds Lua at a breakpoint the frame stays frozen.
    if (m_state != EngineState::Running || m_renderInProgress || LuaDebugger::Instance().IsPaused()) {
        return;
    }
    
    m_renderInProgress = true;

    if (m_frameRenderer && m_frameRenderer->HasDeviceLost()) {
        if (!m_reportedFatalRenderError) {
            m_reportedFatalRenderError = true;
            const QString message = "Vulkan device lost during rendering. Rendering has been stopped. Please restart the editor.";
            std::cerr << "[QtEditorEngine] " << message.toStdString() << "\n";
            emit engineError(message);
        }
        m_swapchainNeedsRecreation = false;
        m_renderTimer.stop();
        m_renderInProgress = false;
        return;
    }
    
    // Calculate delta time
    float currentTime = m_frameTimer.elapsed() / 1000.0f;
    float dt = currentTime - m_lastFrameTime;
    m_lastFrameTime = currentTime;
    
    // Handle pending swapchain recreation
    if (m_swapchainNeedsRecreation) {
        if (m_frameRenderer && m_frameRenderer->HasDeviceLost()) {
            m_swapchainNeedsRecreation = false;
            m_renderInProgress = false;
            return;
        }
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

    // ── Play Mode tick ─────────────────────────────────────────────────────
    // A Stop requested while Lua was paused outside the simulation step is still pending here.
    if (m_playModeManager.IsStopRequested()) {
        m_playModeManager.ClearStopRequest();
        StopPlayMode();
    }
    const bool isPlaying = m_playModeManager.IsPlaying();
    const bool isPaused  = m_playModeManager.IsPaused();
    const bool isPlayOrPause = !m_playModeManager.IsEditor();

    if (isPlaying && m_playModeManager.ShouldAdvanceRealtime()) {
        populateGameplayInput();
        simulateGameplayFrame(dt);
        syncViewportCursorCapture();

        if (m_playModeManager.IsStopRequested()) {
            m_playModeManager.ClearStopRequest();
            StopPlayMode();
        }
    } else if (!isPlayOrPause && m_joltPhysicsSystem) {
        m_joltPhysicsSystem->SetEnabled(false);
    }

    RuntimeUIState* runtimeUI = FindRuntimeUIState(this);
    if (isPlayOrPause && (!isPlaying || m_playModeManager.ShouldAdvanceRealtime()) &&
        runtimeUI && runtimeUI->system && runtimeUI->context && runtimeUI->renderer) {
        runtimeUI->context->SetViewportSize({
            static_cast<float>(m_swapchain->extent.width),
            static_cast<float>(m_swapchain->extent.height)
        });
        runtimeUI->context->SetDeltaTimeSeconds(dt);
        runtimeUI->system->Update(*runtimeUI->context);
        ++m_runtimeUIUpdateSerial;
        runtimeUI->renderer->Reset();
        runtimeUI->system->Render(*runtimeUI->renderer);
    } else if (!isPlayOrPause && runtimeUI && runtimeUI->renderer) {
        runtimeUI->renderer->Reset();
    }

    Scene* interpolatedScene = nullptr;
    if (isPlaying && m_playModeManager.ShouldAdvanceRealtime() && m_physicsInitialized &&
        m_joltPhysicsSystem && m_editorContext.HasActiveScene()) {
        interpolatedScene = m_editorContext.GetActiveScene();
        m_joltPhysicsSystem->ApplyRenderInterpolation(*interpolatedScene);
    }

    // ── Camera update ──────────────────────────────────────────────────────
    // Only update editor camera when NOT in RuntimeScript mode
    if (!isPlayOrPause || m_playModeManager.GetCameraMode() == Hephaestus::CameraMode::EditorFly) {
        m_cameraController.Update(m_inputState, dt);
        m_cameraController.BuildRenderCamera(m_camera);
    }

    // If a script is driving the camera, follow the active CameraComponent after LateUpdate.
    // Editor fly control stays in place until Camera.setActive(true).
    if (isPlayOrPause && m_playModeManager.GetCameraMode() == Hephaestus::CameraMode::RuntimeScript) {
        m_playModeManager.SyncRuntimeCameraFromActiveComponent();
        m_playModeManager.ApplyRuntimeCameraToRender(m_camera);
    }

    // Keep editor context camera in sync with the render camera
    auto& editorCam = m_editorContext.GetCamera();
    editorCam.positionX = m_camera.positionX;
    editorCam.positionY = m_camera.positionY;
    editorCam.positionZ = m_camera.positionZ;
    editorCam.yawDeg = m_camera.yawDeg;
    editorCam.pitchDeg = m_camera.pitchDeg;
    editorCam.nearPlane = m_camera.nearPlane;
    editorCam.farPlane = m_camera.farPlane;

    // ── Push play/pause state into render config ───────────────────────────
    m_renderConfig.isPlaying = isPlayOrPause;
    m_renderConfig.isPaused  = isPaused;

    if (m_preRenderCallback) {
        m_preRenderCallback(dt);
    }
    
    renderFrame();
    if (interpolatedScene) {
        m_joltPhysicsSystem->RestoreSimulationPoses(*interpolatedScene);
    }

    emit frameRendered(dt);
    m_renderInProgress = false;
}

static void AppendProbeVolumeDebugDraw(Scene& scene, std::vector<EditorDebugLine>& lines,
                                       std::vector<EditorDebugPoint>& points)
{
    for (entt::entity handle : scene.GetRegistry().view<ProbeVolumeComponent>()) {
        const auto& volume = scene.GetRegistry().get<ProbeVolumeComponent>(handle);
        const Mat4 world = scene.GetWorldTransform(Entity(handle, &scene));
        const Vec3 center(world.m[12], world.m[13], world.m[14]);
        const Vec3 color = volume.enabled ? Vec3(0.3f, 0.8f, 1.0f) : Vec3(0.5f, 0.5f, 0.5f);
        const Vec3 h(volume.size.x * 0.5f, volume.size.y * 0.5f, volume.size.z * 0.5f);
        auto corner = [&](int i) {
            return Vec3(center.x + ((i & 1) ? h.x : -h.x), center.y + ((i & 2) ? h.y : -h.y),
                        center.z + ((i & 4) ? h.z : -h.z));
        };
        for (int i = 0; i < 8; ++i) {
            for (int bit = 1; bit < 8; bit <<= 1) {
                if (!(i & bit)) {
                    lines.push_back({corner(i), corner(i | bit), color});
                }
            }
        }
        for (int z = 0; z < volume.probeCountZ; ++z) {
            for (int y = 0; y < volume.probeCountY; ++y) {
                for (int x = 0; x < volume.probeCountX; ++x) {
                    const Vec3 offset = volume.ProbeOffset(x, y, z);
                    points.push_back({Vec3(center.x + offset.x, center.y + offset.y, center.z + offset.z),
                                      color, 0.08f});
                }
            }
        }
    }
}

void QtEditorEngine::renderFrame()
{
    static std::string lastSceneName;
    
    if (!m_frameRenderer) {
        return;
    }
    if (!m_editorContext.HasActiveScene()) {
        m_viewportPickWaitingReadback = false;
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
    
    // Sync EditorContext viewport settings to FrameRenderConfig
    const auto& viewportSettings = m_editorContext.GetViewportSettings();
    m_renderConfig.isEditorMode = true;
    m_renderConfig.renderGrid = viewportSettings.showGrid && m_playModeManager.IsEditor();
    m_renderConfig.renderGizmo = viewportSettings.showGizmo && !m_playModeManager.ShouldSuppressEditorInteraction();
    m_renderConfig.renderOutlines = viewportSettings.showSelectionOutline && !m_playModeManager.ShouldSuppressEditorInteraction();
    m_renderConfig.renderShadows = viewportSettings.showShadows
        && (m_camera.projectionMode != Hephaestus::ProjectionMode::Orthographic);
    m_renderConfig.renderSkybox = viewportSettings.showSkybox;
    m_renderConfig.renderBillboards = viewportSettings.showLightBillboards && !m_playModeManager.ShouldSuppressEditorInteraction();

    // Viewport shading mode (Lit / Unlit / Wireframe)
    switch (viewportSettings.shadingMode) {
        case 1:
            m_renderConfig.shadingMode = Hephaestus::ViewShadingMode::Unlit;
            break;
        case 2:
            m_renderConfig.shadingMode = (m_frameRenderer && m_frameRenderer->SupportsWireframe())
                ? Hephaestus::ViewShadingMode::Wireframe
                : Hephaestus::ViewShadingMode::Lit;
            break;
        default:
            m_renderConfig.shadingMode = Hephaestus::ViewShadingMode::Lit;
            break;
    }
    
    // Sync selection for outline rendering
    m_renderConfig.selectedEntity = static_cast<uint32_t>(m_editorContext.GetSelectedEntity());
    {
        auto selEntities = m_editorContext.GetSelectedEntities();
        m_renderConfig.selectedEntities.clear();
        m_renderConfig.selectedEntities.reserve(selEntities.size());
        for (entt::entity e : selEntities) {
            m_renderConfig.selectedEntities.push_back(static_cast<uint32_t>(e));
        }
    }
    m_renderConfig.editorHiddenEntities.assign(
        m_editorContext.GetEditorHiddenIds().begin(),
        m_editorContext.GetEditorHiddenIds().end());
    m_renderConfig.editorLockedEntities.assign(
        m_editorContext.GetEditorLockedIds().begin(),
        m_editorContext.GetEditorLockedIds().end());
    m_renderConfig.editorIsolatedEntities.assign(
        m_editorContext.GetIsolatedIds().begin(),
        m_editorContext.GetIsolatedIds().end());
    
    // Sync gizmo settings
    m_renderConfig.gizmoMode = static_cast<int>(m_editorContext.GetGizmoMode());
    m_renderConfig.gizmoSpace = static_cast<int>(m_editorContext.GetGizmoSpace());
    const auto& snapSettings = m_editorContext.GetSnapSettings();
    m_renderConfig.enableSnap = snapSettings.enableSnap;
    m_renderConfig.debugLines = m_debugLines;
    m_renderConfig.debugPoints = m_debugPoints;
    AppendProbeVolumeDebugDraw(*scene, m_renderConfig.debugLines, m_renderConfig.debugPoints);
    // Set snap value based on current gizmo mode
    switch (m_editorContext.GetGizmoMode()) {
        case Hephaestus::GizmoMode::Translate:
            m_renderConfig.snapValue = snapSettings.translateSnap;
            break;
        case Hephaestus::GizmoMode::Rotate:
            m_renderConfig.snapValue = snapSettings.rotateSnapDeg;
            break;
        case Hephaestus::GizmoMode::Scale:
            m_renderConfig.snapValue = snapSettings.scaleSnap;
            break;
        default:
            m_renderConfig.snapValue = 1.0f;
            break;
    }
    
    // Update gizmo state for selected entity
    entt::entity selectedEntity = m_editorContext.GetSelectedEntity();
    if (selectedEntity != entt::null && scene && m_frameRenderer->IsGizmoAvailable()) {
        Entity entity(selectedEntity, scene);
        if (entity.HasComponent<TransformComponent>()) {
            Mat4 worldTransform = scene->GetWorldTransform(entity);
            const auto selected = m_editorContext.GetSelectedEntities();
            if (selected.size() > 1) {
                Vec3 center(0, 0, 0);
                size_t count = 0;
                for (entt::entity handle : selected) {
                    Entity selectedEntityHandle(handle, scene);
                    if (!selectedEntityHandle ||
                        !selectedEntityHandle.HasComponent<TransformComponent>()) continue;
                    const Mat4 selectedWorld =
                        scene->GetWorldTransform(selectedEntityHandle);
                    center = vadd(center, Vec3(selectedWorld.m[12],
                                               selectedWorld.m[13],
                                               selectedWorld.m[14]));
                    ++count;
                }
                if (count > 0) {
                    center = vscale(center, 1.0f / static_cast<float>(count));
                    worldTransform.m[12] = center.x;
                    worldTransform.m[13] = center.y;
                    worldTransform.m[14] = center.z;
                }
            }
            
            // Build view and projection matrices from camera
            const auto& cam = m_camera;
            float yawRad = cam.yawDeg * 3.14159265f / 180.0f;
            float pitchRad = cam.pitchDeg * 3.14159265f / 180.0f;
            Vec3 forward(
                std::cos(pitchRad) * std::cos(yawRad),
                std::sin(pitchRad),
                std::cos(pitchRad) * std::sin(yawRad)
            );
            forward = vnorm(forward);
            Vec3 camPos(cam.positionX, cam.positionY, cam.positionZ);
            Vec3 target = vadd(camPos, forward);
            Mat4 viewMatrix = Mat4::LookAt(camPos, target, Vec3(0, 1, 0));
            
            float aspect = static_cast<float>(m_swapchain->extent.width) / 
                           static_cast<float>(m_swapchain->extent.height);
            Mat4 projMatrix;
            if (cam.projectionMode == Hephaestus::ProjectionMode::Orthographic) {
                const float halfH = std::max(cam.orthoHalfExtent, 0.01f);
                const float halfW = halfH * aspect;
                projMatrix = Mat4::Orthographic(-halfW, halfW, -halfH, halfH, cam.nearPlane, cam.farPlane);
            } else {
                const float fovDeg = (cam.fovDeg > 0.1f) ? cam.fovDeg : 60.0f;
                projMatrix = Mat4::Perspective(fovDeg * 3.14159265f / 180.0f,
                                                 aspect, cam.nearPlane, cam.farPlane);
            }
            
            m_frameRenderer->UpdateGizmoState(
                static_cast<uint32_t>(selectedEntity),
                worldTransform,
                viewMatrix, projMatrix,
                camPos,
                m_renderConfig.gizmoMode,
                m_renderConfig.gizmoSpace
            );
        } else {
            m_frameRenderer->UpdateGizmoState(UINT32_MAX, Mat4::Identity(), 
                                               Mat4::Identity(), Mat4::Identity(),
                                               Vec3(0,0,0), 0, 0);
        }
    } else {
        // No selection - clear gizmo
        if (m_frameRenderer->IsGizmoAvailable()) {
            m_frameRenderer->UpdateGizmoState(UINT32_MAX, Mat4::Identity(), 
                                               Mat4::Identity(), Mat4::Identity(),
                                               Vec3(0,0,0), 0, 0);
        }
    }
    
    // Render frame using shared renderer
    bool success = m_frameRenderer->RenderFrame(
        m_editorContext.GetActiveScene(), 
        m_camera, 
        m_renderConfig,
        deltaTime
    );
    
    if (!success) {
        if (m_frameRenderer && m_frameRenderer->HasDeviceLost()) {
            if (!m_reportedFatalRenderError) {
                m_reportedFatalRenderError = true;
                const QString message = "Vulkan device lost during rendering. Rendering has been stopped. Please restart the editor.";
                std::cerr << "[QtEditorEngine] " << message.toStdString() << "\n";
                emit engineError(message);
            }
            m_swapchainNeedsRecreation = false;
            m_renderTimer.stop();
            m_viewportPickWaitingReadback = false;
            return;
        }

        // Swapchain may need recreation for non-fatal errors (out-of-date/suboptimal).
        m_swapchainNeedsRecreation = true;
        m_pendingWidth = m_viewport->width();
        m_pendingHeight = m_viewport->height();
    }

    tryApplyCompletedViewportPick();
}

void QtEditorEngine::tryApplyCompletedViewportPick()
{
    if (!m_viewportPickWaitingReadback || !m_frameRenderer) {
        return;
    }
    uint32_t raw = UINT32_MAX;
    if (!m_frameRenderer->TakeViewportPickResult(raw)) {
        return;
    }
    entt::entity picked = entt::null;
    if (raw != UINT32_MAX && raw != static_cast<uint32_t>(-1)) {
        picked = static_cast<entt::entity>(raw);
    }
    Scene* scene = m_editorContext.GetActiveScene();
    if (!m_viewportPickAltHeld && scene && picked != entt::null &&
        scene->GetRegistry().valid(picked)) {
        Entity pickedEntity(picked, scene);
        if (pickedEntity.HasComponent<PrefabNodeComponent>()) {
            Entity root = PrefabOverrideSystem::FindInstanceRoot(
                *scene, pickedEntity);
            if (root) picked = static_cast<entt::entity>(root);
        }
    }
    m_editorContext.SelectFromViewportClick(picked, m_viewportPickCtrlHeld, m_viewportPickShiftHeld);
    m_viewportPickWaitingReadback = false;
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

    Hephaestus::AddBuiltinMeshes(*m_frameRenderer);
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

    size_t handle = m_frameRenderer->AddStaticMesh(name, data, sourcePath, imported);
    std::cout << "[QtEditorEngine] Uploaded mesh '" << name << "' as handle " << handle << "\n";
    return handle;
}

void QtEditorEngine::SetProjectRoot(const std::string& projectRoot)
{
    m_projectRoot = projectRoot;
    if (!m_projectRoot.empty()) {
        m_assetRoot = (std::filesystem::path(m_projectRoot) / "assets").lexically_normal().string();
    } else {
        m_assetRoot.clear();
    }
    std::cout << "[QtEditorEngine] Project root set to: " << m_projectRoot << "\n";
    std::cout << "[QtEditorEngine] Asset root set to: " << m_assetRoot << "\n";

    loadProjectInputMap();
    PrefabOverrideSystem::SetProjectRoot(std::filesystem::path(m_projectRoot));

    // Keep Lua search paths in sync so relative script paths keep resolving.
    if (m_luaScriptEngine) {
        m_luaScriptEngine->SetProjectRoot(std::filesystem::path(m_projectRoot));
        m_luaScriptEngine->SetFrameRenderer(m_frameRenderer.get());
    }
    if (m_audioEngine) {
        m_audioEngine->SetProjectRoot(std::filesystem::path(m_projectRoot));
    }
}

void QtEditorEngine::loadProjectInputMap()
{
    if (m_projectRoot.empty()) {
        m_gameplayInput.SetMap(InputMap::CreateDefault());
        return;
    }

    namespace fs = std::filesystem;
    std::string profile = "assets/input/Default.hpinput";
    const fs::path settingsPath =
        fs::path(m_projectRoot) / ".hephaestus" / "project_settings.json";
    std::ifstream input(settingsPath, std::ios::binary);
    if (input) {
        try {
            nlohmann::json settings;
            input >> settings;
            profile = settings.value("input", nlohmann::json::object())
                          .value("defaultInputProfile", profile);
        } catch (const std::exception& error) {
            std::cerr << "[QtEditorEngine] Could not read input profile setting: "
                      << error.what() << '\n';
        }
    }
    if (profile.empty() || profile == "Default") {
        profile = "assets/input/Default.hpinput";
    }

    fs::path profilePath(profile);
    if (profilePath.is_relative()) {
        profilePath = fs::path(m_projectRoot) / profilePath;
    }
    std::string error;
    if (!m_gameplayInput.LoadMap(profilePath, error)) {
        m_gameplayInput.SetMap(InputMap::CreateDefault());
        std::cerr << "[QtEditorEngine] " << error
                  << "; using built-in default input map\n";
    }
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
    std::string normalized = p.lexically_normal().generic_string();

    // Already portable project-relative keys
    if (normalized.rfind("assets/", 0) == 0 || normalized.rfind(".engine/", 0) == 0) {
        return normalized;
    }

    // Remap stale absolute paths saved from another machine/share:
    //   //server/.../Project/assets/models/X.obj  -> assets/models/X.obj
    //   //server/.../Project/.engine/meshes/X.glb -> .engine/meshes/X.glb
    const size_t assetsPos = normalized.find("/assets/");
    if (assetsPos != std::string::npos) {
        return normalized.substr(assetsPos + 1);
    }
    const size_t enginePos = normalized.find("/.engine/");
    if (enginePos != std::string::npos) {
        return normalized.substr(enginePos + 1);
    }

    // If this is inside the active project, convert to project-relative key.
    if (!m_projectRoot.empty()) {
        fs::path projectRoot = fs::path(m_projectRoot).lexically_normal();
        std::error_code ec;
        fs::path canonPath = fs::weakly_canonical(p, ec);
        if (!ec) {
            fs::path rel = canonPath.lexically_relative(projectRoot);
            std::string relStr = rel.generic_string();
            if (!rel.empty() && relStr.find("..") == std::string::npos && !relStr.empty()) {
                return relStr;
            }
        }
    }

    return normalized;
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

    // Get project root for resolving relative paths.
    // Prefer the explicitly-set project root (from ProjectContext via
    // EditorMainWindow) so that mesh resolution works even when the scene
    // path is unavailable or has moved.
    fs::path projectRoot;
    fs::path assetRoot;

    if (!m_projectRoot.empty()) {
        projectRoot = fs::path(m_projectRoot).lexically_normal();
        assetRoot   = !m_assetRoot.empty()
                          ? fs::path(m_assetRoot).lexically_normal()
                          : (projectRoot / "assets").lexically_normal();
    }

    // Fallback: discover project root by walking up from the scene file.
    if (projectRoot.empty()) {
        std::string scenePath = m_editorContext.GetScenePath();
        if (!scenePath.empty()) {
            fs::path sp(scenePath);
            fs::path search = sp.parent_path();
            for (int i = 0; i < 10 && !search.empty(); ++i) {
                std::error_code dirErr;
                for (const auto& entry : fs::directory_iterator(search, fs::directory_options::skip_permission_denied, dirErr)) {
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
    }

    std::cout << "[QtEditorEngine] Project root: " << projectRoot << "\n";
    std::cout << "[QtEditorEngine] Asset root: " << assetRoot << "\n";

    // Pass project root to the renderer for HDRI path resolution
    if (m_frameRenderer && !projectRoot.empty()) {
        m_frameRenderer->SetProjectRoot(projectRoot.string());
    }

    Hephaestus::CharacterRuntime::BindScene(*scene, projectRoot, m_frameRenderer.get());

    // -----------------------------------------------------------------
    // Helper: resolve a mesh asset path (relative key) to an absolute
    // path on disk, trying multiple fallback locations.
    // -----------------------------------------------------------------
    auto resolveAssetPathToFile = [&](const std::string& assetPath) -> fs::path {
        if (assetPath.empty()) return {};
        if (assetPath.rfind("builtin://", 0) == 0) return fs::path(assetPath);

        std::string remapped = normalizeAssetPath(assetPath);
        fs::path assetP(remapped);

        // 0. If remap produced an already-existing absolute path, use it.
        if (assetP.is_absolute() && fs::exists(assetP)) {
            return assetP;
        }

        // 1. projectRoot / key  (works for "assets/meshes/X.obj" and ".engine/meshes/X.glb")
        if (assetP.is_relative() && !projectRoot.empty()) {
            fs::path candidate = (projectRoot / assetP).lexically_normal();
            if (fs::exists(candidate)) return candidate;
        }

        // 2. Backward compat: "assets/meshes/X" might live in ".engine/meshes/X"
        if (assetP.is_relative() && !projectRoot.empty()) {
            std::string rel = assetP.generic_string();
            if (rel.rfind("assets/meshes/", 0) == 0) {
                std::string filename = rel.substr(std::string("assets/meshes/").size());
                fs::path candidate = projectRoot / ".engine" / "meshes" / filename;
                if (fs::exists(candidate)) return candidate;
            }
        }

        // 3. Strip "assets/" prefix and try under assetsRoot
        if (assetP.is_relative() && !assetRoot.empty()) {
            std::string rel = assetP.generic_string();
            if (rel.rfind("assets/", 0) == 0) {
                rel = rel.substr(std::string("assets/").size());
                fs::path candidate = (assetRoot / fs::path(rel)).lexically_normal();
                if (fs::exists(candidate)) return candidate;
            } else {
                fs::path candidate = (assetRoot / assetP).lexically_normal();
                if (fs::exists(candidate)) return candidate;
            }
        }

        // 4. Search by filename in known mesh directories
        fs::path filename = assetP.filename();
        if (!filename.empty()) {
            std::vector<fs::path> searchDirs;
            if (!assetRoot.empty()) {
                searchDirs.push_back(assetRoot / "meshes");
                searchDirs.push_back(assetRoot);
            }
            if (!projectRoot.empty()) {
                searchDirs.push_back(projectRoot / ".engine" / "meshes");
            }
            for (const auto& dir : searchDirs) {
                if (!fs::is_directory(dir)) continue;
                fs::path candidate = dir / filename;
                if (fs::exists(candidate)) {
                    std::cout << "[QtEditorEngine]   -> Fallback found '"
                              << assetPath << "' at " << candidate << "\n";
                    return candidate;
                }
            }

            // 4b. Recursive filename search under assetRoot for legacy scenes
            // that used absolute paths on another machine.
            if (!assetRoot.empty() && fs::is_directory(assetRoot)) {
                std::error_code ec;
                for (const auto& entry : fs::recursive_directory_iterator(
                         assetRoot, fs::directory_options::skip_permission_denied, ec)) {
                    if (ec) break;
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().filename() == filename) {
                        std::cout << "[QtEditorEngine]   -> Recursive fallback found '"
                                  << assetPath << "' at " << entry.path() << "\n";
                        return entry.path();
                    }
                }
            }
        }

        // 5. Try as absolute path (may have been stored from a different machine)
        if (assetP.is_absolute() && fs::exists(assetP)) return assetP;

        // 6. Last resort: next to the executable
        {
            fs::path exeDir = fs::path(Hephaestus::Tools::PathUtils::ExecutableDir().toStdWString());
            fs::path candidate = exeDir / assetPath;
            if (fs::exists(candidate)) return candidate;
        }

        return {};
    };

    auto syncMaterialSlots = [](MeshRendererComponent& renderer, int subCount) {
        const int requiredSlots = std::max(subCount, 1);
        if (static_cast<int>(renderer.appliedMaterialPaths.size()) < requiredSlots) {
            renderer.appliedMaterialPaths.resize(static_cast<size_t>(requiredSlots), "");
        }
        if (static_cast<int>(renderer.materialIndices.size()) < requiredSlots) {
            renderer.materialIndices.resize(static_cast<size_t>(requiredSlots), -1);
        }
    };

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
        if (IsRuntimeGeneratedVisual(renderer)) {
            continue;
        }

        std::string entityName = entity.HasComponent<TagComponent>()
                                     ? entity.GetComponent<TagComponent>().tag
                                     : std::string("Entity");

        std::cout << "[QtEditorEngine] Entity '" << entityName 
                  << "' meshAssetPath='" << renderer.meshAssetPath << "'\n";

        if (renderer.meshAssetPath.empty()) {
            continue;
        }

        // If the entity already has a valid handle that still lives in the
        // library, keep it.  This covers meshes loaded from a path that
        // differs from the stored meshAssetPath (e.g. engine default meshes
        // loaded from the build output while the asset path is stored as a
        // relative string).
        if (renderer.meshHandle != MeshRendererComponent::InvalidMesh) {
            MeshGPU* existing = m_frameRenderer->GetMesh(renderer.meshHandle);
            if (existing) {
                int subCount = existing->submeshes.empty()
                    ? 1 : static_cast<int>(existing->submeshes.size());
                syncMaterialSlots(renderer, subCount);
                std::cout << "[QtEditorEngine]   -> Keeping existing handle "
                          << renderer.meshHandle << "\n";
                continue;
            }
        }

        std::string lookupKey = normalizeAssetPath(renderer.meshAssetPath);
        if (!lookupKey.empty() && lookupKey != renderer.meshAssetPath &&
            lookupKey.rfind("builtin://", 0) != 0) {
            renderer.meshAssetPath = lookupKey;
        }

        // Check if mesh already in library
        size_t existingHandle = m_frameRenderer->GetMeshHandle(lookupKey);
        if (existingHandle != SIZE_MAX) {
            MeshGPU* mesh = m_frameRenderer->GetMesh(existingHandle);
            if (mesh) {
                int subCount = mesh->submeshes.empty() ? 1 : static_cast<int>(mesh->submeshes.size());
                renderer.SetMesh(existingHandle, mesh->bounds, mesh->sourceKey, mesh->imported, subCount);
                syncMaterialSlots(renderer, subCount);
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
                    syncMaterialSlots(renderer, subCount);
                    std::cout << "[QtEditorEngine]   -> Builtin mesh: handle " << handle << "\n";
                }
            } else {
                std::cerr << "[QtEditorEngine]   -> Unknown builtin: " << renderer.meshAssetPath << "\n";
                renderer.meshHandle = MeshRendererComponent::InvalidMesh;
            }
            continue;
        }

        // Try to load mesh from file using the robust resolver
        fs::path meshPath = resolveAssetPathToFile(renderer.meshAssetPath);

        // Normalize
        if (!meshPath.empty() && fs::exists(meshPath)) {
            meshPath = fs::weakly_canonical(meshPath);
        }

        std::cout << "[QtEditorEngine]   -> Resolved path: " << meshPath << "\n";

        if (meshPath.empty() || !fs::exists(meshPath)) {
            std::cerr << "[QtEditorEngine]   -> File not found for '"
                      << renderer.meshAssetPath << "' (tried project root, asset root, "
                      << ".engine/meshes, and filename search)\n";
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
            syncMaterialSlots(renderer, subCount);

            // Register the relative asset path as an alias so that future
            // lookups (e.g. after the next play-mode cycle) find this mesh
            // without hitting disk again.
            m_frameRenderer->AddMeshAlias(lookupKey, handle);

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

// ─────────────────────────────────────────────────────────────────────────────
// Play Mode API
// ─────────────────────────────────────────────────────────────────────────────

void QtEditorEngine::StartPlayMode()
{
    if (!m_editorContext.HasActiveScene()) {
        std::cerr << "[QtEditorEngine] Cannot start Play Mode: no active scene\n";
        return;
    }
    if (m_playModeManager.IsPlaying() || m_playModeManager.IsPaused()) {
        return;  // already playing
    }

    Scene* scene = m_editorContext.GetActiveScene();

    // Resolve project root once and share it across subsystems (physics, Lua, renderer).
    std::filesystem::path projectRootPath;
    if (!m_projectRoot.empty()) {
        projectRootPath = std::filesystem::path(m_projectRoot).lexically_normal();
    } else {
        const std::string& scenePathStr = m_editorContext.GetScenePath();
        if (!scenePathStr.empty()) {
            std::filesystem::path cursor = std::filesystem::path(scenePathStr).parent_path();
            while (!cursor.empty() && cursor.has_filename() && cursor.filename() != "assets") {
                std::filesystem::path parent = cursor.parent_path();
                if (parent == cursor) {
                    break;
                }
                cursor = parent;
            }
            if (!cursor.empty() && cursor.has_filename() && cursor.filename() == "assets") {
                projectRootPath = cursor.parent_path();
            }
        }
        if (projectRootPath.empty()) {
            projectRootPath = std::filesystem::current_path();
        }
    }

    // Ensure Jolt physics is ready in Play Mode (do not depend on FocusOnSelection side-effects).
    if (!m_joltPhysicsEngine) {
        m_joltPhysicsEngine = std::make_unique<Physics::JoltPhysicsEngine>();
        try {
            m_joltPhysicsEngine->Initialize();
            m_joltPhysicsSystem = std::make_unique<Physics::JoltPhysicsSystem>(*m_joltPhysicsEngine);
            m_characterControllers = std::make_unique<Physics::CharacterControllerSystem>(
                *m_joltPhysicsEngine, *m_joltPhysicsSystem);
            m_joltPhysicsSystem->SetCharacterController(m_characterControllers.get());
            m_editorContext.SetCharacterControllers(m_characterControllers.get());
            m_physicsInitialized = true;
            std::cout << "[QtEditorEngine] Physics engine initialized\n";
        } catch (const std::exception& e) {
            m_physicsInitialized = false;
            std::cerr << "[QtEditorEngine] Failed to initialize physics engine: " << e.what() << "\n";
        }
    }

    if (m_physicsInitialized && m_joltPhysicsEngine) {
        Physics::PhysicsLayerConfig layerConfig;
        const Physics::PhysicsLayerLoadResult loaded = Physics::PhysicsLayerConfig::LoadFromFile(
            Physics::PhysicsLayerConfig::ProjectFile(projectRootPath), layerConfig);
        if (!loaded.error.empty()) {
            std::cerr << "[QtEditorEngine] " << loaded.error << "\n";
        }
        if (m_joltPhysicsSystem && m_joltPhysicsEngine->GetPhysicsSystem()) {
            m_joltPhysicsSystem->ClearAllBodies();
        }
        m_joltPhysicsEngine->BeginPlayWorld(layerConfig);
        if (m_joltPhysicsSystem) {
            m_joltPhysicsSystem->SetProjectRoot(projectRootPath);
            m_joltPhysicsSystem->SetEnabled(true);
            m_joltPhysicsSystem->ClearAccumulator();
        }
    }

    if (!m_audioEngine) {
        m_audioEngine = std::make_unique<AudioEngine>();
        m_audioEngine->Initialize();
    }
    m_audioEngine->SetProjectRoot(projectRootPath);

    RuntimeUIState& runtimeUI = EnsureRuntimeUIState(this);
    if (!runtimeUI.system) {
        runtimeUI.system = std::make_unique<::Hephaestus::HephaestusUI::UISystem>();
    }
    if (!runtimeUI.context) {
        runtimeUI.context = std::make_unique<::Hephaestus::HephaestusUI::UIContext>();
    }
    if (!runtimeUI.renderer) {
        runtimeUI.renderer = std::make_unique<::Hephaestus::HephaestusUI::UIRenderer>();
    }
    if (runtimeUI.system) {
        runtimeUI.system->ClearScreens();
    }
    if (runtimeUI.renderer) {
        runtimeUI.renderer->Reset();
    }

    // Save current editor camera so it can be restored on stop
    m_playModeManager.SaveEditorCamera(m_editorContext.GetCamera());

    // Save gizmo visibility state
    m_editorGizmoVisibleBeforePlay = m_editorContext.GetViewportSettings().showGizmo;
    m_editorContext.GetViewportSettings().showGizmo = false;

    loadProjectInputMap();
    m_gameplayInput.Reset();

    // ── Initialize Lua script engine if not yet created ────────────────────
    if (!m_luaScriptEngine) {
        m_luaScriptEngine = std::make_unique<LuaScriptEngine>();
        m_luaScriptEngine->Initialize(scene, m_glfwWindow);
        m_luaScriptEngine->SetProjectRoot(projectRootPath);
        m_luaScriptEngine->SetFrameRenderer(m_frameRenderer.get());
        m_luaScriptEngine->SetAudioEngine(m_audioEngine.get());

    } else {
        // Re-associate with current scene
        m_luaScriptEngine->Initialize(scene, m_glfwWindow);
        m_luaScriptEngine->SetProjectRoot(projectRootPath);
        m_luaScriptEngine->SetFrameRenderer(m_frameRenderer.get());
        m_luaScriptEngine->SetAudioEngine(m_audioEngine.get());
    }
    m_luaScriptEngine->SetInputSystem(&m_gameplayInput);
    if (!m_pluginScriptHost) {
        m_pluginScriptHost = std::make_unique<PluginScriptHost>();
    }
    m_pluginScriptHost->SetLuaEngine(m_luaScriptEngine.get());
    m_pluginScriptHost->SetInputSystem(&m_gameplayInput);
    m_luaScriptEngine->SetPluginScriptHost(m_pluginScriptHost.get());
    populateGameplayInput();

    // Rebind every play session. Initialize() resets simulation state and the
    // binding must always target this editor's current PlayModeManager.
    CameraBinding camBinding;
    camBinding.setActive = [this](bool active) {
        m_playModeManager.SetScriptCameraActive(active);
    };
    camBinding.isActive = [this]() -> bool {
        return m_playModeManager.IsScriptCameraActive();
    };
    camBinding.setPosition = [this](const Vec3& pos) {
        Scene* scene = m_editorContext.HasActiveScene() ? m_editorContext.GetActiveScene() : nullptr;
        if (scene && Hephaestus::WriteActiveCameraLocalPosition(*scene, pos) &&
            m_playModeManager.SyncRuntimeCameraFromActiveComponent()) {
            return;
        }
        auto& cam = m_playModeManager.GetRuntimeCamera();
        cam.posX = pos.x;
        cam.posY = pos.y;
        cam.posZ = pos.z;
    };
    camBinding.getPosition = [this]() -> Vec3 {
        const auto& cam = m_playModeManager.GetRuntimeCamera();
        return Vec3(cam.posX, cam.posY, cam.posZ);
    };
    camBinding.setRotation = [this](const Vec3& rot) {
        Scene* scene = m_editorContext.HasActiveScene() ? m_editorContext.GetActiveScene() : nullptr;
        if (scene && Hephaestus::WriteActiveCameraLocalRotation(*scene, rot) &&
            m_playModeManager.SyncRuntimeCameraFromActiveComponent()) {
            return;
        }
        auto& cam = m_playModeManager.GetRuntimeCamera();
        cam.pitch = rot.x;
        cam.yaw = rot.y;
        cam.roll = rot.z;
    };
    camBinding.getRotation = [this]() -> Vec3 {
        const auto& cam = m_playModeManager.GetRuntimeCamera();
        return Vec3(cam.pitch, cam.yaw, cam.roll);
    };
    camBinding.translate = [this](const Vec3& delta) {
        Scene* scene = m_editorContext.HasActiveScene() ? m_editorContext.GetActiveScene() : nullptr;
        if (scene && Hephaestus::TranslateActiveCameraLocal(*scene, delta) &&
            m_playModeManager.SyncRuntimeCameraFromActiveComponent()) {
            return;
        }
        auto& cam = m_playModeManager.GetRuntimeCamera();
        cam.posX += delta.x;
        cam.posY += delta.y;
        cam.posZ += delta.z;
    };
    m_luaScriptEngine->SetCameraBinding(std::move(camBinding));
    m_luaScriptEngine->SetUIBindings(runtimeUI.system.get(), runtimeUI.context.get(), runtimeUI.renderer.get());

    const ScriptPhysicsAccessor physicsAccess = [this]() {
        ScriptPhysicsAccess access;
        access.characterControllers = m_characterControllers.get();
        if (m_physicsInitialized) {
            access.physicsEngine = m_joltPhysicsEngine.get();
            access.physicsSystem = m_joltPhysicsSystem.get();
            access.scene = m_editorContext.HasActiveScene() ? m_editorContext.GetActiveScene() : nullptr;
        }
        return access;
    };
    m_luaScriptEngine->SetPhysicsBinding(MakeScriptPhysicsBinding(physicsAccess));
    m_luaScriptEngine->SetCharacterBinding(MakeScriptCharacterBinding(physicsAccess));

    // ── Initialize C++ script engine ───────────────────────────────────────
    if (!m_cppScriptEngine) {
        m_cppScriptEngine = std::make_unique<ScriptEngine>();
    }
    m_cppScriptEngine->Initialize(scene);

    // Wire up engines to the play mode manager
    m_playModeManager.SetScene(scene);
    m_playModeManager.SetLuaScriptEngine(m_luaScriptEngine.get());
    m_luaScriptEngine->SetSceneReloadCallback([this]() {
        return m_playModeManager.RequestSceneReload();
    });
    m_luaScriptEngine->SetQuitRequestCallback([this]() {
        m_playModeManager.RequestStop();
    });
    m_playModeManager.SetCppScriptEngine(m_cppScriptEngine.get());
    m_playModeManager.SetPluginScriptHost(m_pluginScriptHost.get());
    m_playModeManager.SetSceneReloadCallback([this](std::string& error) {
        return reloadRuntimeScene(error);
    });
    m_playModeManager.SetLogCallback([](const std::string& msg) {
        std::cout << msg << std::endl;
    });

    // Prefab JSON edits are otherwise stuck in LoadAsset cache from the last Play.
    PrefabOverrideSystem::InvalidateAssetCache();

    // Start!
    m_playModeManager.StartPlayMode();

    // Request keyboard focus on the viewport so Lua scripts can receive input
    // Enable force input forwarding so mouse events reach the UI even when overlay is visible
    if (m_viewport) {
        m_viewport->requestActivate();
        m_viewport->SetForceInputForwarding(true);
        syncViewportCursorCapture();
    }

    emit playStateChanged(true, false);
    std::cout << "[QtEditorEngine] Play Mode started\n";
}

void QtEditorEngine::StopPlayMode()
{
    if (m_playModeManager.IsEditor()) {
        return;  // nothing to stop
    }
    // Lua is suspended inside a hook; unwind it first and stop once the frame has returned.
    if (LuaDebugger::Instance().IsPaused()) {
        LuaDebugger::Instance().RequestResume(LuaDebugger::Action::Stop);
        m_playModeManager.RequestStop();
        return;
    }

    if (m_joltPhysicsSystem && m_joltPhysicsEngine && m_joltPhysicsEngine->GetPhysicsSystem()) {
        Hephaestus::EngineLog::Write(
            Hephaestus::EngineLogLevel::Info,
            "Physics play stopped: bodies=" + std::to_string(m_joltPhysicsSystem->BodyCount())
                + " lastSubsteps=" + std::to_string(m_joltPhysicsSystem->LastSubstepCount())
                + " stalledSteps=" + std::to_string(m_joltPhysicsSystem->StalledStepCount())
                + " lastFrameContacts=" + std::to_string(m_joltPhysicsSystem->FrameContactCount()),
            "Physics");
        m_joltPhysicsSystem->ClearAllBodies();
    }
    if (m_joltPhysicsSystem) {
        m_joltPhysicsSystem->ClearAccumulator();
        m_joltPhysicsSystem->SetEnabled(false);
    }
    if (m_joltPhysicsEngine) {
        m_joltPhysicsEngine->EndPlayWorld();
    }

    m_playModeManager.StopPlayMode();
    LuaDebugger::Instance().StopFinished();
    m_editorContext.SyncSelectionFromScene();

    if (m_luaScriptEngine) {
        m_luaScriptEngine->SetUIBindings(nullptr, nullptr, nullptr);
    }
    RuntimeUIState* runtimeUI = FindRuntimeUIState(this);
    if (runtimeUI && runtimeUI->system) {
        runtimeUI->system->SetCursorMode(Hephaestus::HephaestusUI::CursorMode::Game);
        runtimeUI->system->ConsumeCursorModeChange();
        runtimeUI->system->ClearScreens();
    }
    if (runtimeUI && runtimeUI->renderer) {
        runtimeUI->renderer->Reset();
    }

    if (m_audioEngine) {
        m_audioEngine->StopAll();
    }

    // Release cursor if Lua scripts captured it during play
    // Disable force input forwarding (play mode is ending)
    if (m_viewport) {
        if (m_viewport->IsCursorCaptured()) {
            m_viewport->ReleaseCursor();
        }
        m_viewport->SetForceInputForwarding(false);
    }

    // Restore gizmo visibility
    m_editorContext.GetViewportSettings().showGizmo = m_editorGizmoVisibleBeforePlay;

    // Restore editor camera
    m_playModeManager.RestoreEditorCamera(m_editorContext.GetCamera());

    // Sync camera controller from restored editor camera
    const auto& cam = m_editorContext.GetCamera();
    m_cameraController.positionX = cam.positionX;
    m_cameraController.positionY = cam.positionY;
    m_cameraController.positionZ = cam.positionZ;
    m_cameraController.yawDeg    = cam.yawDeg;
    m_cameraController.pitchDeg  = cam.pitchDeg;
    m_cameraController.baseMoveSpeed = cam.moveSpeed;

    // Rebuild render camera from restored state
    m_cameraController.BuildRenderCamera(m_camera);

    // Rebuild scene meshes since the scene was restored from snapshot
    RebuildSceneMeshes();

    emit playStateChanged(false, false);
    std::cout << "[QtEditorEngine] Play Mode stopped\n";
}

void QtEditorEngine::TogglePause()
{
    const bool wasPaused = m_playModeManager.IsPaused();
    m_playModeManager.TogglePause();
    if (m_joltPhysicsSystem && (wasPaused || m_playModeManager.IsPaused())) {
        m_joltPhysicsSystem->ClearAccumulator();
    }
    // "playing" means a Play session is active, paused or not; the toolbar keeps Stop and Pause enabled while paused.
    emit playStateChanged(!m_playModeManager.IsEditor(), m_playModeManager.IsPaused());
}

Hephaestus::PlayState QtEditorEngine::GetPlayState() const
{
    return m_playModeManager.GetState();
}

bool QtEditorEngine::IsRealtime() const
{
    return m_playModeManager.IsRealtime();
}

void QtEditorEngine::SetRealtime(bool realtime)
{
    m_playModeManager.SetRealtime(realtime);
    m_lastFrameTime = m_frameTimer.elapsed() / 1000.0f;
}

bool QtEditorEngine::reloadRuntimeScene(std::string& outError)
{
    Scene* scene = m_editorContext.GetActiveScene();
    if (!scene || !scene->IsReloadable()) {
        outError = "current scene has no authored source asset";
        return false;
    }

    const std::string sourcePath = scene->GetSourceAssetPath();

    // Validate into a temporary scene before teardown. A missing or malformed
    // source therefore leaves the active play scene intact.
    Scene staged;
    if (!SceneSerializer::Deserialize(staged, std::filesystem::u8path(sourcePath), outError)) {
        outError = "cannot reload '" + sourcePath + "': " + outError;
        return false;
    }

    if (m_luaScriptEngine) {
        m_luaScriptEngine->OnSimulationStopped();
    }
    if (m_pluginScriptHost) {
        m_pluginScriptHost->OnSimulationStopped();
    }
    if (m_cppScriptEngine) {
        m_cppScriptEngine->DestroyScripts();
    }

    m_editorContext.ClearSelection();
    if (m_audioEngine) {
        m_audioEngine->StopAll();
    }
    if (m_joltPhysicsSystem) {
        m_joltPhysicsSystem->ClearAllBodies();
        m_joltPhysicsSystem->ClearAccumulator();
        m_joltPhysicsSystem->SetEnabled(true);
    } else if (m_characterControllers) {
        m_characterControllers->Clear();
    }

    RuntimeUIState* runtimeUI = FindRuntimeUIState(this);
    if (runtimeUI) {
        if (runtimeUI->system) {
            runtimeUI->system->ClearScreens();
            runtimeUI->system->SetCursorMode(Hephaestus::HephaestusUI::CursorMode::Game);
        }
        if (runtimeUI->context) {
            runtimeUI->context->ClearQueuedMouseEvents();
        }
        if (runtimeUI->renderer) {
            runtimeUI->renderer->Reset();
        }
    }
    m_gameplayInput.ResetTransientState();

    if (!SceneSerializer::Deserialize(*scene, std::filesystem::u8path(sourcePath), outError)) {
        scene->Clear();
        outError = "source passed validation but failed during reconstruction: " + outError;
        return false;
    }
    m_editorContext.ClearSelection();

    if (m_luaScriptEngine) {
        m_luaScriptEngine->OnSimulationStarted();
    }
    if (m_cppScriptEngine) {
        m_cppScriptEngine->AwakeScripts();
    }

    namespace fs = std::filesystem;
    fs::path projectRoot = m_projectRoot.empty() ? fs::path{} : fs::path(m_projectRoot);
    if (projectRoot.empty()) {
        projectRoot = fs::path(sourcePath).parent_path();
    }
    Hephaestus::CharacterRuntime::BindScene(*scene, projectRoot, m_frameRenderer.get());
    m_editorContext.RequestRebuildSceneVisuals();
    if (m_joltPhysicsSystem) {
        m_joltPhysicsSystem->PrepareSceneBodies(*scene);
    }
    outError.clear();
    return true;
}

bool QtEditorEngine::ReloadScripts(std::string& outError)
{
    if (!m_luaScriptEngine) {
        outError = "No Lua script runtime is active";
        return false;
    }
    m_luaScriptEngine->RequestReloadAll();
    if (m_pluginScriptHost) {
        m_pluginScriptHost->RequestReloadAll();
    }
    outError.clear();
    return true;
}

void QtEditorEngine::simulateGameplayFrame(float dt)
{
    Scene* scene = m_editorContext.HasActiveScene() ? m_editorContext.GetActiveScene() : nullptr;
    if (!scene) {
        return;
    }
    m_playModeManager.AdvanceFrameTime(dt);
    if (m_physicsInitialized && m_joltPhysicsSystem) {
        m_joltPhysicsSystem->SetEnabled(true);
    }

    Hephaestus::SimulationStepContext step;
    step.scene = scene;
    step.audio = m_audioEngine.get();
    step.physics = m_physicsInitialized ? m_joltPhysicsSystem.get() : nullptr;
    step.lua = m_luaScriptEngine.get();
    step.pluginScripts = m_pluginScriptHost.get();
    step.cppScripts = m_cppScriptEngine.get();
    step.reloadAtSafePoint = [this]() {
        if (!m_playModeManager.HasPendingSceneReload()) {
            return false;
        }
        m_playModeManager.ProcessSceneReload();
        return true;
    };
    step.onPhysicsError = [this](const std::exception& e) {
        Hephaestus::EngineLog::Write(
            Hephaestus::EngineLogLevel::Error,
            std::string("Physics stopped for this Play session after an error: ") + e.what(),
            "Physics");
        m_physicsInitialized = false;
    };
    Hephaestus::RunSimulationStep(step, dt);
}

void QtEditorEngine::runSimulationFrame(float dt)
{
    populateGameplayInput();
    simulateGameplayFrame(dt);
    syncViewportCursorCapture();

    RuntimeUIState* runtimeUI = FindRuntimeUIState(this);
    if (runtimeUI && runtimeUI->system && runtimeUI->context && runtimeUI->renderer) {
        runtimeUI->context->SetViewportSize({
            static_cast<float>(m_swapchain->extent.width),
            static_cast<float>(m_swapchain->extent.height)
        });
        runtimeUI->context->SetDeltaTimeSeconds(dt);
        runtimeUI->system->Update(*runtimeUI->context);
        ++m_runtimeUIUpdateSerial;
        runtimeUI->renderer->Reset();
        runtimeUI->system->Render(*runtimeUI->renderer);
    }
}

bool QtEditorEngine::StepFrames(std::uint32_t count, float fixedDt, std::string& outError)
{
    if (m_playModeManager.IsEditor()) {
        outError = "Play Mode is not active";
        return false;
    }
    if (m_stepping) {
        outError = "A stepping sequence is already in progress";
        return false;
    }
    if (LuaDebugger::Instance().IsPaused()) {
        outError = "The Lua debugger is paused at a breakpoint";
        return false;
    }
    if (fixedDt <= 0.0f) {
        outError = "fixedDt must be positive";
        return false;
    }

    m_stepping = true;
    for (std::uint32_t i = 0; i < count; ++i) {
        runSimulationFrame(fixedDt);
    }
    m_stepping = false;

    // Reset the frame timer so the next free-running tick applies a normal dt
    // instead of a large delta accumulated across the stepped frames.
    m_lastFrameTime = m_frameTimer.elapsed() / 1000.0f;
    return true;
}

bool QtEditorEngine::IsPlaying() const
{
    return !m_playModeManager.IsEditor();
}

bool QtEditorEngine::IsPaused() const
{
    return m_playModeManager.IsPaused();
}

void QtEditorEngine::ForwardKeyEvent(int qtKey, bool pressed)
{
    // Forward the key event to the runtime input state so Lua scripts can read it
    updateKeyState(qtKey, pressed);
}

void QtEditorEngine::ForwardOverlayMouseEvent(int type, float x, float y, int button)
{
    if (m_playModeManager.IsEditor()) {
        return;
    }
    
    RuntimeUIState* runtimeUI = FindRuntimeUIState(this);
    if (!runtimeUI || !runtimeUI->context) {
        return;
    }
    
    const ::Hephaestus::HephaestusUI::UIVec2 pos = {x, y};
    
    switch (type) {
    case 0: // Press
        std::cout << "[UI Debug] ForwardOverlayMouseEvent Press at (" << x << ", " << y << ") button=" << button << std::endl;
        runtimeUI->context->QueueMousePress(pos, button);
        break;
    case 1: // Release
        runtimeUI->context->QueueMouseRelease(pos, button);
        break;
    case 2: // Move
        runtimeUI->context->QueueMouseMove(pos);
        break;
    }
}

const std::vector<::Hephaestus::HephaestusUI::UIDrawCommand>& QtEditorEngine::GetRuntimeUIDrawCommands() const
{
    static const std::vector<::Hephaestus::HephaestusUI::UIDrawCommand> kEmpty;
    RuntimeUIState* runtimeUI = FindRuntimeUIState(this);
    if (!runtimeUI || !runtimeUI->renderer) {
        return kEmpty;
    }
    return runtimeUI->renderer->GetDrawCommands();
}

bool QtEditorEngine::IsRuntimeUICursorMode() const
{
    if (m_playModeManager.IsEditor()) {
        return false;
    }
    RuntimeUIState* runtimeUI = FindRuntimeUIState(this);
    return runtimeUI && runtimeUI->system &&
           runtimeUI->system->GetCursorMode() == Hephaestus::HephaestusUI::CursorMode::UI;
}

namespace {
RuntimeUIState* LiveRuntimeUI(QtEditorEngine* engine)
{
    RuntimeUIState* runtimeUI = FindRuntimeUIState(engine);
    if (!runtimeUI || !runtimeUI->system || !runtimeUI->context) return nullptr;
    return runtimeUI;
}
}

bool QtEditorEngine::CopyRuntimeUI(std::vector<Hephaestus::Automation::AutomationBackend::UIScreenNode>& out,
                                   std::string& outError)
{
    RuntimeUIState* runtimeUI = LiveRuntimeUI(this);
    if (!runtimeUI) {
        outError = "unavailable";
        return false;
    }
    return Hephaestus::Automation::CopyRuntimeUIFrom(*runtimeUI->system, *runtimeUI->context, out, outError);
}

bool QtEditorEngine::ResolveRuntimeUI(const Hephaestus::Automation::AutomationBackend::UISelector& selector,
                                      Hephaestus::Automation::AutomationBackend::UINode& out,
                                      std::string& outError)
{
    RuntimeUIState* runtimeUI = LiveRuntimeUI(this);
    if (!runtimeUI) {
        outError = "unavailable";
        return false;
    }
    return Hephaestus::Automation::ResolveRuntimeUIFrom(*runtimeUI->system, *runtimeUI->context, selector, out, outError);
}

bool QtEditorEngine::QueueRuntimeUIClick(const Hephaestus::Automation::AutomationBackend::UISelector& selector,
                                         Hephaestus::Automation::AutomationBackend::UIBounds& outBounds,
                                         float& outX, float& outY, std::uint64_t& outSerial,
                                         std::uint64_t& outClickToken, std::string& outId, std::string& outScreen,
                                         std::string& outError)
{
    RuntimeUIState* runtimeUI = LiveRuntimeUI(this);
    if (!runtimeUI) {
        outError = "unavailable";
        return false;
    }
    if (!Hephaestus::Automation::QueueRuntimeUIClickFrom(*runtimeUI->system, *runtimeUI->context, selector,
                                                         outBounds, outX, outY, outClickToken, outId, outScreen,
                                                         outError)) {
        return false;
    }
    outSerial = m_runtimeUIUpdateSerial;
    return true;
}

void QtEditorEngine::CancelRuntimeUIClick(std::uint64_t token)
{
    RuntimeUIState* runtimeUI = FindRuntimeUIState(this);
    if (!runtimeUI || !runtimeUI->context) return;
    runtimeUI->context->CancelTaggedMouseEvents(token);
}

bool QtEditorEngine::PhysicsRaycast(const Hephaestus::Automation::AutomationBackend::PhysicsVec3& origin,
                                    const Hephaestus::Automation::AutomationBackend::PhysicsVec3& direction,
                                    float maxDistance, entt::entity ignore,
                                    Hephaestus::Automation::AutomationBackend::PhysicsRaycastResult& out,
                                    std::string& outError, const std::string* layerName)
{
    return Hephaestus::Automation::CastPhysicsRay(
        m_physicsInitialized, m_joltPhysicsEngine.get(), m_joltPhysicsSystem.get(),
        m_editorContext.GetActiveScene(), origin, direction, maxDistance, ignore, out, outError, layerName);
}

bool QtEditorEngine::ListPhysicsBodies(
    std::vector<Hephaestus::Automation::AutomationBackend::PhysicsBodySnapshot>& out, std::string& outError)
{
    return Hephaestus::Automation::ListPhysicsSnapshots(
        m_physicsInitialized, m_joltPhysicsEngine.get(), m_joltPhysicsSystem.get(),
        m_editorContext.GetActiveScene(), out, outError);
}

bool QtEditorEngine::GetPhysicsBody(entt::entity entity,
                                    Hephaestus::Automation::AutomationBackend::PhysicsBodySnapshot& out,
                                    std::string& outError)
{
    return Hephaestus::Automation::ReadPhysicsSnapshot(
        m_physicsInitialized, m_joltPhysicsEngine.get(), m_joltPhysicsSystem.get(),
        m_editorContext.GetActiveScene(), entity, out, outError);
}

bool QtEditorEngine::GetCharacterState(entt::entity entity,
                                       Hephaestus::Automation::AutomationBackend::PhysicsBodySnapshot& pose,
                                       bool& grounded, std::string& outError)
{
    return Hephaestus::Automation::ReadCharacterSnapshot(
        m_physicsInitialized, m_joltPhysicsEngine.get(), m_joltPhysicsSystem.get(),
        m_editorContext.GetActiveScene(), entity, pose, grounded, outError,
        m_characterControllers.get());
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
