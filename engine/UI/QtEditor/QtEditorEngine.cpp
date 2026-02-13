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
#include "../../Physics/JoltPhysicsEngine.h"
#include "../../Physics/JoltPhysicsSystem.h"
#include "../../Scripting/LuaScriptEngine.h"
#include "../../Scripting/ScriptEngine.h"
#include "../../Scripting/LuaScriptComponent.h"

#include <GLFW/glfw3.h>  // For GLFW key code constants used by RuntimeInputState
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>

#include <iostream>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vulkan/vulkan_win32.h>
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
        projMatrix = Mat4::Perspective(60.0f * 3.14159265f / 180.0f,
                                       aspect, cam.nearPlane, cam.farPlane);
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
    };
    
    // Update input state based on event type
    switch (event.type) {
    case InputEvent::Type::MousePress:
        // Update button state
        if (event.button == 1) m_inputState.lmbDown = true;       // Qt::LeftButton
        else if (event.button == 2) m_inputState.rmbDown = true;  // Qt::RightButton
        else if (event.button == 4) m_inputState.mmbDown = true;  // Qt::MiddleButton
        
        // Track for Lua RuntimeInputState
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

        if (event.button == 1 && !m_inputState.altDown && !m_inputState.rmbDown) {
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
                        m_frameRenderer->GetGizmo()->BeginDrag(gizmoRay, hoveredAxis);
                        m_gizmoDragging = true;
                        m_gizmoActiveAxis = hoveredAxis;
                    }
                }
            }
        }
        
        // Notify camera controller
        m_cameraController.OnMousePress(event.button, m_inputState);
        
        // Handle cursor capture for fly mode
        if (m_cameraController.ShouldCaptureCursor() && m_viewport) {
            m_viewport->CaptureCursor();
        }
        
        m_lastMouseX = event.x;
        m_lastMouseY = event.y;
        m_hasLastMousePos = true;
        break;
        
    case InputEvent::Type::MouseRelease:
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
            
            // LMB click without RMB (not fly mode) - try entity picking
            // Suppress picking during play mode  
            if (!m_inputState.rmbDown && !m_playModeManager.ShouldSuppressEditorInteraction() &&
                m_frameRenderer && m_frameRenderer->IsPickingAvailable()) {
                // Initiate pick at mouse position
                float px = event.x;
                float py = event.y;
                toPixelCoords(event.x, event.y, px, py);
                uint32_t pickedEntity = m_frameRenderer->PickEntityAt(
                    static_cast<uint32_t>(px), 
                    static_cast<uint32_t>(py)
                );
                
                // If we got a valid entity, select it (1-frame latency)
                if (pickedEntity != UINT32_MAX && pickedEntity != static_cast<uint32_t>(-1)) {
                    m_editorContext.SelectEntity(static_cast<entt::entity>(pickedEntity));
                } else {
                    // Clicked on empty space - clear selection
                    m_editorContext.ClearSelection();
                }
            }
        }
        else if (event.button == 2) m_inputState.rmbDown = false;
        else if (event.button == 4) m_inputState.mmbDown = false;
        
        // Track for Lua RuntimeInputState (right/middle release)
        if (event.button != 1) {
            int glfwBtn = QtMouseButtonToGLFW(event.button);
            if (glfwBtn >= 0 && glfwBtn < kMaxTrackedMouse)
                m_runtimeMouseButtons[glfwBtn] = false;
        }
        
        // Notify camera controller
        m_cameraController.OnMouseRelease(event.button, m_inputState);
        
        // Release cursor if no longer in fly mode
        if (!m_cameraController.ShouldCaptureCursor() && m_viewport) {
            m_viewport->ReleaseCursor();
        }
        break;
        
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

            // Track for Lua input
            m_runtimeMouseX = static_cast<double>(event.x);
            m_runtimeMouseY = static_cast<double>(event.y);

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
                            Entity entity(selectedEntity, scene);
                            if (entity.HasComponent<TransformComponent>()) {
                                auto& transform = entity.GetComponent<TransformComponent>();
                                float px = event.x;
                                float py = event.y;
                                toPixelCoords(event.x, event.y, px, py);
                                Ray gizmoRay = buildRayFromScreen(px, py, viewMatrix, projMatrix);
                                Mat4 newWorldTransform = m_frameRenderer->GetGizmo()->HandleDrag(gizmoRay);
                                Mat4 localTransform = newWorldTransform;

                                Entity parentEntity = scene->GetParent(entity);
                                if (parentEntity) {
                                    Mat4 parentWorld = scene->GetWorldTransform(parentEntity);
                                    Mat4 parentInv = Mat4::Invert(parentWorld);
                                    localTransform = Mat4::Multiply(parentInv, newWorldTransform);
                                }

                                transform.SetFromMatrix(localTransform);
                                m_editorContext.MarkDirty();

                                m_frameRenderer->GetGizmo()->SetHoveredAxis(m_gizmoActiveAxis);
                                m_frameRenderer->UpdateGizmoState(
                                    static_cast<uint32_t>(selectedEntity),
                                    scene->GetWorldTransform(entity),
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
            if (!m_gizmoDragging && (m_cameraController.IsFlyModeActive() || 
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
        // Check if RMB is held (Qt::RightButton = 2) via event.button or tracked state
        {
            bool rmbHeld = (event.button & 2) != 0 || m_inputState.rmbDown;
            if (rmbHeld && m_cameraController.IsFlyModeActive()) {
                // Adjust fly mode speed instead of zooming
                const float minMoveSpeed = 0.1f;
                const float maxMoveSpeed = 50.0f;
                const float speedStep = 1.1f;
                float speedScale = std::pow(speedStep, event.wheelDelta);
                float newSpeed = std::clamp(m_cameraController.baseMoveSpeed * speedScale, minMoveSpeed, maxMoveSpeed);
                m_cameraController.baseMoveSpeed = newSpeed;
                m_editorContext.GetCamera().moveSpeed = newSpeed;
                break;
            }
        }
        m_cameraController.OnMouseWheel(event.wheelDelta, m_inputState);
        break;
        
    case InputEvent::Type::KeyPress:
        // Update key states
        updateKeyState(event.key, true);
        
        // Update modifiers
        m_inputState.altDown = (event.modifiers & 0x08000000) != 0;
        m_inputState.shiftDown = (event.modifiers & 0x02000000) != 0;
        m_inputState.ctrlDown = (event.modifiers & 0x04000000) != 0;
        
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
        // Release cursor and reset input state when losing focus
        if (m_viewport && m_viewport->IsCursorCaptured()) {
            m_viewport->ReleaseCursor();
        }
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

    // Also track for Lua RuntimeInputState (all keys, GLFW codes)
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
}

void QtEditorEngine::populateRuntimeInputState()
{
    if (!m_luaScriptEngine) return;

    RuntimeInputState& ris = m_luaScriptEngine->GetRuntimeInputState();
    ris.valid = true;

    // Copy key states
    static_assert(kMaxTrackedKeys <= RuntimeInputState::kMaxKeys, "key array size mismatch");
    for (int i = 0; i < kMaxTrackedKeys; ++i) {
        ris.keys[i] = m_runtimeKeys[i];
    }

    // Copy mouse button states
    for (int i = 0; i < kMaxTrackedMouse && i < RuntimeInputState::kMaxMouseButtons; ++i) {
        ris.mouseButtons[i] = m_runtimeMouseButtons[i];
    }

    // Copy mouse position
    ris.mouseX = m_runtimeMouseX;
    ris.mouseY = m_runtimeMouseY;
}

void QtEditorEngine::FocusOnSelection()
{
    if (!m_editorContext.HasActiveScene()) {
        return;
    }
    
    Scene* scene = m_editorContext.GetActiveScene();

    // ── Initialize physics (Qt path) and configure project root ───────────
    if (!m_joltPhysicsEngine) {
        m_joltPhysicsEngine = std::make_unique<Physics::JoltPhysicsEngine>();
        try {
            m_joltPhysicsEngine->Initialize();
            m_joltPhysicsSystem = std::make_unique<Physics::JoltPhysicsSystem>(*m_joltPhysicsEngine);
            m_physicsInitialized = true;
            std::cout << "[QtEditorEngine] Physics engine initialized\n";
        } catch (const std::exception& e) {
            m_physicsInitialized = false;
            std::cerr << "[QtEditorEngine] Failed to initialize physics engine: " << e.what() << "\n";
        }
    }

    if (m_physicsInitialized && m_joltPhysicsSystem) {
        std::filesystem::path projectRoot;
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
                projectRoot = cursor.parent_path();
            }
        }
        if (projectRoot.empty()) {
            projectRoot = std::filesystem::current_path();
        }

        m_joltPhysicsSystem->SetProjectRoot(projectRoot);
        m_joltPhysicsSystem->ClearAllBodies();
        m_joltPhysicsSystem->SetEnabled(true);
    }
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

    // ── Play Mode tick ─────────────────────────────────────────────────────
    const bool isPlaying = m_playModeManager.IsPlaying();
    const bool isPaused  = m_playModeManager.IsPaused();
    const bool isPlayOrPause = !m_playModeManager.IsEditor();

    if (isPlaying) {
        populateRuntimeInputState();
        m_playModeManager.Tick(dt);

        if (m_physicsInitialized && m_joltPhysicsSystem && m_editorContext.HasActiveScene()) {
            try {
                Scene* activeScene = m_editorContext.GetActiveScene();
                m_joltPhysicsSystem->SetEnabled(true);
                m_joltPhysicsSystem->Update(*activeScene, dt);
            } catch (const std::exception& e) {
                std::cerr << "[QtEditorEngine] Physics update error: " << e.what() << "\n";
                m_physicsInitialized = false;
            }
        }
    } else if (m_joltPhysicsSystem) {
        m_joltPhysicsSystem->SetEnabled(false);
    }

    // ── Camera update ──────────────────────────────────────────────────────
    // Only update editor camera when NOT in RuntimeScript mode
    if (!isPlayOrPause || m_playModeManager.GetCameraMode() == Hephaestus::CameraMode::EditorFly) {
        m_cameraController.Update(m_inputState, dt);
        m_cameraController.BuildRenderCamera(m_camera);
    }

    // If a script is driving the camera, apply its state to the render camera
    if (isPlayOrPause && m_playModeManager.GetCameraMode() == Hephaestus::CameraMode::RuntimeScript) {
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
    
    // Sync EditorContext viewport settings to FrameRenderConfig
    const auto& viewportSettings = m_editorContext.GetViewportSettings();
    m_renderConfig.renderGrid = viewportSettings.showGrid;
    m_renderConfig.renderGizmo = viewportSettings.showGizmo && !m_playModeManager.ShouldSuppressEditorInteraction();
    m_renderConfig.renderOutlines = viewportSettings.showSelectionOutline && !m_playModeManager.ShouldSuppressEditorInteraction();
    m_renderConfig.renderShadows = viewportSettings.showShadows;
    m_renderConfig.renderSkybox = viewportSettings.showSkybox;
    m_renderConfig.renderBillboards = viewportSettings.showLightBillboards && !m_playModeManager.ShouldSuppressEditorInteraction();
    
    // Sync selection for outline rendering
    m_renderConfig.selectedEntity = static_cast<uint32_t>(m_editorContext.GetSelectedEntity());
    
    // Sync gizmo settings
    m_renderConfig.gizmoMode = static_cast<int>(m_editorContext.GetGizmoMode());
    m_renderConfig.gizmoSpace = static_cast<int>(m_editorContext.GetGizmoSpace());
    const auto& snapSettings = m_editorContext.GetSnapSettings();
    m_renderConfig.enableSnap = snapSettings.enableSnap;
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
            Mat4 projMatrix = Mat4::Perspective(60.0f * 3.14159265f / 180.0f, 
                                                 aspect, cam.nearPlane, cam.farPlane);
            
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

    // Pass project root to the renderer for HDRI path resolution
    if (m_frameRenderer && !projectRoot.empty()) {
        m_frameRenderer->SetProjectRoot(projectRoot.string());
    }

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

    // Save current editor camera so it can be restored on stop
    m_playModeManager.SaveEditorCamera(m_editorContext.GetCamera());

    // Save gizmo visibility state
    m_editorGizmoVisibleBeforePlay = m_editorContext.GetViewportSettings().showGizmo;
    m_editorContext.GetViewportSettings().showGizmo = false;

    // ── Initialize Lua script engine if not yet created ────────────────────
    if (!m_luaScriptEngine) {
        m_luaScriptEngine = std::make_unique<LuaScriptEngine>();
        m_luaScriptEngine->Initialize(scene, m_glfwWindow);

        // Set camera binding that routes through PlayModeManager
        CameraBinding camBinding;
        camBinding.setActive = [this](bool active) {
            m_playModeManager.SetScriptCameraActive(active);
        };
        camBinding.isActive = [this]() -> bool {
            return m_playModeManager.IsScriptCameraActive();
        };
        camBinding.setPosition = [this](const Vec3& pos) {
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
            auto& cam = m_playModeManager.GetRuntimeCamera();
            cam.pitch = rot.x;  // radians
            cam.yaw   = rot.y;
            cam.roll  = rot.z;
        };
        camBinding.getRotation = [this]() -> Vec3 {
            const auto& cam = m_playModeManager.GetRuntimeCamera();
            return Vec3(cam.pitch, cam.yaw, cam.roll);
        };
        camBinding.translate = [this](const Vec3& delta) {
            auto& cam = m_playModeManager.GetRuntimeCamera();
            cam.posX += delta.x;
            cam.posY += delta.y;
            cam.posZ += delta.z;
        };
        m_luaScriptEngine->SetCameraBinding(camBinding);
    } else {
        // Re-associate with current scene
        m_luaScriptEngine->Initialize(scene, m_glfwWindow);
    }

    // Provide Jolt physics callbacks to Lua scripts in Qt play mode.
    PhysicsBinding physicsBinding{};
    physicsBinding.hasBody = [this](uint32_t entityId) -> bool {
        if (!m_physicsInitialized || !m_joltPhysicsSystem || !m_editorContext.HasActiveScene()) {
            return false;
        }
        Scene* activeScene = m_editorContext.GetActiveScene();
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!activeScene->GetRegistry().valid(handle)) {
            return false;
        }
        return !m_joltPhysicsSystem->GetBodyID(handle).IsInvalid();
    };

    physicsBinding.getLinearVelocity = [this](uint32_t entityId, Vec3& outVelocity) -> bool {
        if (!m_physicsInitialized || !m_joltPhysicsSystem || !m_joltPhysicsEngine || !m_editorContext.HasActiveScene()) {
            return false;
        }
        Scene* activeScene = m_editorContext.GetActiveScene();
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!activeScene->GetRegistry().valid(handle)) {
            return false;
        }
        JPH::BodyID bodyId = m_joltPhysicsSystem->GetBodyID(handle);
        if (bodyId.IsInvalid()) {
            return false;
        }
        JPH::Vec3 vel = m_joltPhysicsEngine->GetBodyInterface().GetLinearVelocity(bodyId);
        outVelocity = Vec3(vel.GetX(), vel.GetY(), vel.GetZ());
        return true;
    };

    physicsBinding.setLinearVelocity = [this](uint32_t entityId, const Vec3& velocity, bool activate) -> bool {
        if (!m_physicsInitialized || !m_joltPhysicsSystem || !m_joltPhysicsEngine || !m_editorContext.HasActiveScene()) {
            return false;
        }
        Scene* activeScene = m_editorContext.GetActiveScene();
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!activeScene->GetRegistry().valid(handle)) {
            return false;
        }
        JPH::BodyID bodyId = m_joltPhysicsSystem->GetBodyID(handle);
        if (bodyId.IsInvalid()) {
            return false;
        }
        m_joltPhysicsEngine->GetBodyInterface().SetLinearVelocity(
            bodyId, JPH::Vec3(velocity.x, velocity.y, velocity.z));
        if (activate) {
            m_joltPhysicsEngine->GetBodyInterface().ActivateBody(bodyId);
        }
        return true;
    };

    physicsBinding.setAngularVelocity = [this](uint32_t entityId, const Vec3& velocity, bool activate) -> bool {
        if (!m_physicsInitialized || !m_joltPhysicsSystem || !m_joltPhysicsEngine || !m_editorContext.HasActiveScene()) {
            return false;
        }
        Scene* activeScene = m_editorContext.GetActiveScene();
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!activeScene->GetRegistry().valid(handle)) {
            return false;
        }
        JPH::BodyID bodyId = m_joltPhysicsSystem->GetBodyID(handle);
        if (bodyId.IsInvalid()) {
            return false;
        }
        m_joltPhysicsEngine->GetBodyInterface().SetAngularVelocity(
            bodyId, JPH::Vec3(velocity.x, velocity.y, velocity.z));
        if (activate) {
            m_joltPhysicsEngine->GetBodyInterface().ActivateBody(bodyId);
        }
        return true;
    };

    physicsBinding.setRotation = [this](uint32_t entityId, const Vec3& rotationEuler, bool activate) -> bool {
        if (!m_physicsInitialized || !m_joltPhysicsSystem || !m_joltPhysicsEngine || !m_editorContext.HasActiveScene()) {
            return false;
        }
        Scene* activeScene = m_editorContext.GetActiveScene();
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!activeScene->GetRegistry().valid(handle)) {
            return false;
        }
        JPH::BodyID bodyId = m_joltPhysicsSystem->GetBodyID(handle);
        if (bodyId.IsInvalid()) {
            return false;
        }

        JPH::Quat qx = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), rotationEuler.x);
        JPH::Quat qy = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), rotationEuler.y);
        JPH::Quat qz = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), rotationEuler.z);
        JPH::Quat rot = qx * qy * qz;

        JPH::BodyInterface& bi = m_joltPhysicsEngine->GetBodyInterface();
        JPH::RVec3 pos = bi.GetCenterOfMassPosition(bodyId);
        bi.SetPositionAndRotation(bodyId, pos, rot,
                                  activate ? JPH::EActivation::Activate
                                           : JPH::EActivation::DontActivate);
        return true;
    };

    physicsBinding.raycast = [this](uint32_t entityId, const Vec3& origin, const Vec3& direction,
                                    float maxDistance, PhysicsRaycastHit& outHit) -> bool {
        if (!m_physicsInitialized || !m_joltPhysicsSystem || !m_joltPhysicsEngine || !m_editorContext.HasActiveScene()) {
            return false;
        }

        entt::entity handle = static_cast<entt::entity>(entityId);
        JPH::BodyID ignoreBody = m_joltPhysicsSystem->GetBodyID(handle);

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
        auto& query = m_joltPhysicsEngine->GetPhysicsSystem()->GetNarrowPhaseQuery();

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

    m_luaScriptEngine->SetPhysicsBinding(std::move(physicsBinding));

    // ── Initialize C++ script engine ───────────────────────────────────────
    if (!m_cppScriptEngine) {
        m_cppScriptEngine = std::make_unique<ScriptEngine>();
    }
    m_cppScriptEngine->Initialize(scene);

    // Wire up engines to the play mode manager
    m_playModeManager.SetScene(scene);
    m_playModeManager.SetLuaScriptEngine(m_luaScriptEngine.get());
    m_playModeManager.SetCppScriptEngine(m_cppScriptEngine.get());
    m_playModeManager.SetLogCallback([](const std::string& msg) {
        std::cout << msg << std::endl;
    });

    // Start!
    m_playModeManager.StartPlayMode();

    emit playStateChanged(true, false);
    std::cout << "[QtEditorEngine] Play Mode started\n";
}

void QtEditorEngine::StopPlayMode()
{
    if (m_playModeManager.IsEditor()) {
        return;  // nothing to stop
    }

    if (m_joltPhysicsSystem) {
        m_joltPhysicsSystem->ClearAllBodies();
        m_joltPhysicsSystem->SetEnabled(false);
    }

    m_playModeManager.StopPlayMode();

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
    m_playModeManager.TogglePause();
    emit playStateChanged(m_playModeManager.IsPlaying(), m_playModeManager.IsPaused());
}

Hephaestus::PlayState QtEditorEngine::GetPlayState() const
{
    return m_playModeManager.GetState();
}

bool QtEditorEngine::IsPlaying() const
{
    return !m_playModeManager.IsEditor();
}

bool QtEditorEngine::IsPaused() const
{
    return m_playModeManager.IsPaused();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
