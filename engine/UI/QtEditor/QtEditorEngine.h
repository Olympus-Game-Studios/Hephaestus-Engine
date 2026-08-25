#pragma once

/**
 * @file QtEditorEngine.h
 * @brief Engine integration for Qt-based editor.
 *
 * This class manages the engine lifecycle within the Qt editor:
 * - Initializes Vulkan without GLFW
 * - Attaches renderer to Qt viewport's native surface
 * - Runs QTimer-driven tick loop
 * - Forwards input from Qt viewport to engine
 * - Handles clean shutdown
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

#include <entt/entt.hpp>

#include "../../Core/Math.h"

#include "QtViewportWindow.h"
#include "../../Gfx/NativeSurface.h"
#include "../../Gfx/EngineFrameRenderer.h"
#include "../HephaestusUI/UIRenderer.h"
#include "../../Core/EditorContext.h"
#include "../../Core/EditorCameraController.h"
#include "../../Core/PlayModeManager.h"

// Forward declarations (avoid including heavy engine headers)
class VulkanDevice;
struct VulkanSwapchain;
class VulkanPipeline;
class Scene;
class LuaScriptEngine;
class ScriptEngine;
class AudioEngine;
struct GLFWwindow;

namespace Physics {
class JoltPhysicsEngine;
class JoltPhysicsSystem;
}

namespace Hephaestus {
namespace QtEditor {

/**
 * @brief State of the editor engine.
 */
enum class EngineState {
    Uninitialized,
    InitializingVulkan,
    WaitingForViewport,
    CreatingSwapchain,
    Running,
    ShuttingDown,
    Shutdown
};

/**
 * @brief Engine integration for Qt editor.
 *
 * This is the bridge between the Qt UI and the engine renderer.
 * It manages the Vulkan lifecycle and frame ticking independently
 * of GLFW, using only native Qt window handles.
 */
class QtEditorEngine : public QObject
{
    Q_OBJECT

public:
    explicit QtEditorEngine(QObject* parent = nullptr);
    ~QtEditorEngine() override;

    /**
     * @brief Initialize Vulkan instance and device (no surface yet).
     * @return true on success.
     */
    bool InitializeVulkan();

    /**
     * @brief Attach to a Qt viewport window for rendering.
     * 
     * This creates the Vulkan surface and swapchain from the native handle.
     * Should be called when the viewport emits viewportReady().
     * 
     * @param viewport The Qt viewport window to render into.
     * @return true on success.
     */
    bool AttachToViewport(QtViewportWindow* viewport);

    /**
     * @brief Start the render loop timer.
     * 
     * Call this after AttachToViewport() succeeds.
     */
    void StartRenderLoop();

    /**
     * @brief Stop the render loop and shutdown the engine.
     */
    void Shutdown();

    /**
     * @brief Handle viewport resize.
     * @param width New width in pixels.
     * @param height New height in pixels.
     */
    void OnViewportResized(uint32_t width, uint32_t height);

    /**
     * @brief Handle input from the viewport.
     * @param event The input event from QtViewportWindow.
     */
    void OnViewportInput(const InputEvent& event);

    /**
     * @brief Get the current engine state.
     */
    EngineState GetState() const { return m_state; }

    /**
     * @brief Check if the engine is ready to render.
     */
    bool IsReadyToRender() const { return m_state == EngineState::Running; }

    /**
     * @brief Get the editor context (shared state for scene, selection, etc.)
     */
    Hephaestus::EditorContext* GetEditorContext() { return &m_editorContext; }
    const Hephaestus::EditorContext* GetEditorContext() const { return &m_editorContext; }

    /**
     * @brief Set the project root directory for mesh path resolution.
     *
     * Call this when a project is opened so that RebuildSceneMeshes()
     * can resolve relative mesh asset paths without having to
     * re-discover the project root from the scene file path.
     */
    void SetProjectRoot(const std::string& projectRoot);

    /**
     * @brief Rebuild meshes in the renderer for the current scene.
     * 
     * Call this after loading a scene to ensure all meshes are uploaded
     * to the GPU and entity handles are set correctly.
     */
    void RebuildSceneMeshes();

    /**
     * @brief Focus the camera on the currently selected entity.
     * 
     * If no entity is selected, focuses on the world origin.
     * Computes bounds if mesh is present, otherwise uses entity position.
     */
    void FocusOnSelection();

    /**
     * @brief Get the camera controller for external configuration.
     */
    Hephaestus::EditorCameraController* GetCameraController() { return &m_cameraController; }

    /**
     * @brief Get the frame renderer (for direct mesh library access if needed).
     */
    Hephaestus::EngineFrameRenderer* GetFrameRenderer() { return m_frameRenderer.get(); }

    // ── Play Mode API ─────────────────────────────────────────────────────

    /**
     * @brief Start play mode – snapshots scene, inits scripts, transitions camera.
     */
    void StartPlayMode();

    /**
     * @brief Stop play mode – tears down scripts, restores scene + editor camera.
     */
    void StopPlayMode();

    /**
     * @brief Toggle pause while in play mode.
     */
    void TogglePause();

    /**
     * @brief Query current play state.
     */
    Hephaestus::PlayState GetPlayState() const;
    bool IsPlaying() const;
    bool IsPaused() const;

    /**
     * @brief Forward a key event to the Lua runtime input state.
     * 
     * Used by EditorMainWindow to ensure Lua scripts receive keyboard input
     * even when the viewport window doesn't have direct keyboard focus.
     */
    void ForwardKeyEvent(int qtKey, bool pressed);

    /**
     * @brief Forward a mouse event from the UI overlay to the runtime UI system.
     * 
     * Used by EditorMainWindow to route mouse events captured by the overlay
     * to the HephaestusUI system during play mode.
     */
    void ForwardOverlayMouseEvent(int type, float x, float y, int button);

    /**
     * @brief Get runtime UI draw commands produced during play mode.
     */
    const std::vector<::Hephaestus::HephaestusUI::UIDrawCommand>& GetRuntimeUIDrawCommands() const;

    /**
     * @brief Get the PlayModeManager for external queries.
     */
    Hephaestus::PlayModeManager* GetPlayModeManager() { return &m_playModeManager; }

    /**
     * @brief Upload a mesh to the GPU and add it to the mesh library.
     * @return Mesh handle, or InvalidMesh on failure.
     */
    size_t uploadMeshToGPU(const std::string& name, const MeshData& data,
                           const std::filesystem::path& sourcePath, bool imported);

signals:
    /**
     * @brief Emitted when engine initialization completes.
     * @param success true if initialization succeeded.
     */
    void engineInitialized(bool success);

    /**
     * @brief Emitted when a frame has been rendered.
     * @param dt Delta time since last frame in seconds.
     */
    void frameRendered(float dt);

    /**
     * @brief Emitted when an error occurs.
     * @param message Error description.
     */
    void engineError(const QString& message);

    /**
     * @brief Emitted when play mode state changes.
     */
    void playStateChanged(bool playing, bool paused);

private slots:
    /**
     * @brief Called by QTimer to tick the engine.
     */
    void onTick();

private:
    bool createVulkanInstance();
    bool initializeVulkanDevice();
    bool createSwapchain();
    void renderFrame();
    /** Applies GPU viewport pick once readback completes (see EngineFrameRenderer::TakeViewportPickResult). */
    void tryApplyCompletedViewportPick();
    void createTestMesh();
    void cleanupVulkanResources();
    void createBuiltinMeshes();
    std::string normalizeAssetPath(const std::string& assetPath) const;
    
    // Input helper methods
    void updateKeyState(int key, bool pressed);
    void resetInputState();
    void populateRuntimeInputState();
    void syncViewportCursorCapture();
    void captureGizmoMultiDragWorldStarts(Scene* scene);
    void clearGizmoMultiDragWorldStarts();

    EngineState m_state = EngineState::Uninitialized;
    bool m_builtinMeshesCreated = false;

    // Project root directory for mesh path resolution (set by EditorMainWindow)
    std::string m_projectRoot;
    std::string m_assetRoot;

    // Runtime input tracking (for Lua scripts in Qt path)
    // Indexed by GLFW key codes; true = pressed.
    static constexpr int kMaxTrackedKeys = 512;
    bool m_runtimeKeys[kMaxTrackedKeys] = {};
    // Mouse buttons indexed by GLFW button code
    static constexpr int kMaxTrackedMouse = 8;
    bool m_runtimeMouseButtons[kMaxTrackedMouse] = {};
    double m_runtimeMouseX = 0.0;
    double m_runtimeMouseY = 0.0;

    // Vulkan resources (owned by main.cpp in legacy mode, but we need our own here)
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<VulkanSwapchain> m_swapchain;
    
    // Shared frame renderer
    std::unique_ptr<Hephaestus::EngineFrameRenderer> m_frameRenderer;
    
    // Editor context - single source of truth for scene, selection, etc.
    Hephaestus::EditorContext m_editorContext;
    
    // Camera controller - UE5-style navigation
    Hephaestus::EditorCameraController m_cameraController;
    
    // Camera state for rendering
    Hephaestus::RenderCamera m_camera;
    Hephaestus::FrameRenderConfig m_renderConfig;
    
    // Input state tracking
    Hephaestus::CameraInputState m_inputState;
    float m_lastMouseX = 0.0f;
    float m_lastMouseY = 0.0f;
    bool m_hasLastMousePos = false;

    // Gizmo interaction state
    bool m_gizmoDragging = false;
    int m_gizmoActiveAxis = -1;
    /** Per-entity world matrices at gizmo drag start (multi-select). */
    std::vector<std::pair<entt::entity, Mat4>> m_gizmoDragEntityWorldStart;
    Mat4 m_gizmoDragPrimaryWorldStart = Mat4::Identity();

    // Play Mode
    Hephaestus::PlayModeManager m_playModeManager;
    std::unique_ptr<AudioEngine> m_audioEngine;
    std::unique_ptr<LuaScriptEngine> m_luaScriptEngine;
    std::unique_ptr<ScriptEngine>    m_cppScriptEngine;
    std::unique_ptr<Physics::JoltPhysicsEngine> m_joltPhysicsEngine;
    std::unique_ptr<Physics::JoltPhysicsSystem> m_joltPhysicsSystem;
    bool m_physicsInitialized = false;
    GLFWwindow* m_glfwWindow = nullptr;   // borrowed from main if GLFW path exists
    bool m_editorGizmoVisibleBeforePlay = true;

    // Viewport connection
    QtViewportWindow* m_viewport = nullptr;
    NativeSurface m_currentSurface;

    // Render loop
    QTimer m_renderTimer;
    QElapsedTimer m_frameTimer;
    float m_lastFrameTime = 0.0f;
    bool m_renderInProgress = false;
    bool m_reportedFatalRenderError = false;

    // Swapchain state
    bool m_swapchainNeedsRecreation = false;
    uint32_t m_pendingWidth = 0;
    uint32_t m_pendingHeight = 0;

    /** Viewport LMB pick waits one frame for GPU ID readback before updating selection. */
    bool m_viewportPickWaitingReadback = false;
    bool m_viewportPickCtrlHeld = false;
    bool m_viewportPickShiftHeld = false;

    /** Mouse press position saved for picking (use press, not release, to avoid jitter). */
    float m_pickPressX = 0.0f;
    float m_pickPressY = 0.0f;
    bool  m_pickPressValid = false;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
