/**
 * @file EditorCameraController.h
 * @brief UE5-style editor camera controller with fly, orbit, pan, and dolly modes.
 *
 * This class handles all camera navigation in the Qt editor:
 * - RMB + WASDQE: Fly mode (mouse look + movement)
 * - Alt + LMB: Orbit around pivot
 * - Alt + MMB: Pan parallel to view plane
 * - Alt + RMB / Wheel: Dolly (zoom)
 * - F: Focus on selection
 */

#pragma once

#include <cmath>

namespace Hephaestus {

// Forward declaration
struct RenderCamera;

/**
 * @brief Input state for camera controller.
 */
struct CameraInputState {
    // Mouse button states
    bool lmbDown = false;
    bool mmbDown = false;
    bool rmbDown = false;
    
    // Key states (WASDQE for movement)
    bool wDown = false;
    bool aDown = false;
    bool sDown = false;
    bool dDown = false;
    bool qDown = false;  // Down
    bool eDown = false;  // Up
    
    // Modifier keys
    bool shiftDown = false;
    bool ctrlDown = false;
    bool altDown = false;
    
    // Mouse position (viewport-local)
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    
    // Mouse delta since last frame
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    
    // Wheel delta (positive = forward/zoom in)
    float wheelDelta = 0.0f;
    
    // Viewport state
    bool viewportFocused = false;
    bool mouseInViewport = false;
};

/**
 * @brief UE5-style editor camera controller.
 *
 * Supports multiple navigation modes:
 * - Fly: RMB + mouse look + WASDQE movement
 * - Orbit: Alt + LMB drag around a pivot point
 * - Pan: Alt + MMB drag parallel to view plane
 * - Dolly: Alt + RMB drag or mouse wheel
 */
class EditorCameraController {
public:
    EditorCameraController();
    ~EditorCameraController() = default;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Camera State
    // ─────────────────────────────────────────────────────────────────────────
    
    // Position
    float positionX = 0.0f;
    float positionY = 2.0f;
    float positionZ = 5.0f;
    
    // Orientation (Euler angles in degrees)
    float yawDeg = -90.0f;    // Horizontal rotation (0 = +X, 90 = +Z)
    float pitchDeg = -15.0f;  // Vertical rotation (-90 = down, 90 = up)
    
    // Projection settings
    float fovDeg = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 10000.0f;
    float orthoHalfExtent = 10.0f;
    bool orthographic = false;
    
    // Movement settings
    float baseMoveSpeed = 5.0f;
    float sprintMultiplier = 3.0f;
    float slowMultiplier = 0.25f;
    float mouseSensitivity = 0.1f;
    float orbitSensitivity = 0.25f;
    float panSensitivity = 0.01f;
    float zoomSensitivity = 1.0f;
    float wheelZoomSensitivity = 0.5f;
    
    // Orbit state
    float orbitPivotX = 0.0f;
    float orbitPivotY = 0.0f;
    float orbitPivotZ = 0.0f;
    float orbitDistance = 10.0f;
    
    // Pitch limits (degrees)
    float minPitchDeg = -89.0f;
    float maxPitchDeg = 89.0f;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Mode Flags
    // ─────────────────────────────────────────────────────────────────────────
    
    /**
     * @brief Check if fly mode is active (RMB held without Alt).
     */
    bool IsFlyModeActive() const { return m_flyModeActive; }
    
    /**
     * @brief Check if orbit mode is active (Alt + LMB).
     */
    bool IsOrbitModeActive() const { return m_orbitModeActive; }
    
    /**
     * @brief Check if pan mode is active (Alt + MMB).
     */
    bool IsPanModeActive() const { return m_panModeActive; }
    
    /**
     * @brief Check if dolly mode is active (Alt + RMB).
     */
    bool IsDollyModeActive() const { return m_dollyModeActive; }
    
    /**
     * @brief Check if any navigation mode is active that should capture cursor.
     */
    bool ShouldCaptureCursor() const { return m_flyModeActive; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Update Functions
    // ─────────────────────────────────────────────────────────────────────────
    
    /**
     * @brief Update camera based on current input state.
     * @param input Current input state.
     * @param deltaTime Time since last update in seconds.
     */
    void Update(const CameraInputState& input, float deltaTime);
    
    /**
     * @brief Process mouse button press.
     * @param button 1=LMB, 2=RMB, 4=MMB (Qt::MouseButton values)
     * @param input Current input state.
     */
    void OnMousePress(int button, const CameraInputState& input);
    
    /**
     * @brief Process mouse button release.
     * @param button 1=LMB, 2=RMB, 4=MMB (Qt::MouseButton values)
     * @param input Current input state.
     */
    void OnMouseRelease(int button, const CameraInputState& input);
    
    /**
     * @brief Process mouse movement.
     * @param deltaX Mouse X delta.
     * @param deltaY Mouse Y delta.
     * @param input Current input state.
     */
    void OnMouseMove(float deltaX, float deltaY, const CameraInputState& input);
    
    /**
     * @brief Process mouse wheel.
     * @param delta Wheel delta (positive = zoom in).
     * @param input Current input state.
     */
    void OnMouseWheel(float delta, const CameraInputState& input);
    
    // ─────────────────────────────────────────────────────────────────────────
    // Navigation Functions
    // ─────────────────────────────────────────────────────────────────────────
    
    /**
     * @brief Focus camera on a point with given radius.
     * @param pointX Target point X.
     * @param pointY Target point Y.
     * @param pointZ Target point Z.
     * @param radius Bounds radius (determines view distance).
     */
    void FocusOnPoint(float pointX, float pointY, float pointZ, float radius);
    
    /**
     * @brief Set the orbit pivot point.
     * @param x Pivot X.
     * @param y Pivot Y.
     * @param z Pivot Z.
     */
    void SetOrbitPivot(float x, float y, float z);
    
    /**
     * @brief Update orbit pivot to be at the current look-at point.
     * 
     * Call this when starting orbit mode to orbit around where the camera
     * is currently looking at the current orbit distance.
     */
    void UpdateOrbitPivotFromView();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Direction Vectors
    // ─────────────────────────────────────────────────────────────────────────
    
    /**
     * @brief Get camera forward direction.
     */
    void GetForward(float& x, float& y, float& z) const;
    
    /**
     * @brief Get camera right direction.
     */
    void GetRight(float& x, float& y, float& z) const;
    
    /**
     * @brief Get camera up direction.
     */
    void GetUp(float& x, float& y, float& z) const;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Output
    // ─────────────────────────────────────────────────────────────────────────
    
    /**
     * @brief Build a RenderCamera from current state.
     * @param outCamera Output camera struct.
     */
    void BuildRenderCamera(RenderCamera& outCamera) const;

private:
    // Navigation mode state
    bool m_flyModeActive = false;
    bool m_orbitModeActive = false;
    bool m_panModeActive = false;
    bool m_dollyModeActive = false;
    
    // Previous mouse position for delta calculation
    float m_prevMouseX = 0.0f;
    float m_prevMouseY = 0.0f;
    bool m_hasMousePosition = false;
    
    // Helper functions
    void updateFlyMovement(const CameraInputState& input, float deltaTime);
    void updateOrbit(float deltaX, float deltaY);
    void updatePan(float deltaX, float deltaY);
    void updateDolly(float delta);
    void clampPitch();
    void updatePositionFromOrbit();
    
    // Constants
    static constexpr float DEG_TO_RAD = 3.1415926535f / 180.0f;
    static constexpr float RAD_TO_DEG = 180.0f / 3.1415926535f;
};

} // namespace Hephaestus
