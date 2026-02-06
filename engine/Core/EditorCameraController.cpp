/**
 * @file EditorCameraController.cpp
 * @brief Implementation of UE5-style editor camera controller.
 */

#include "EditorCameraController.h"
#include "../Gfx/EngineFrameRenderer.h"
#include <algorithm>
#include <cmath>

namespace Hephaestus {

EditorCameraController::EditorCameraController()
{
    // Initialize orbit pivot at world origin
    orbitPivotX = 0.0f;
    orbitPivotY = 0.0f;
    orbitPivotZ = 0.0f;
    
    // Calculate initial orbit distance from camera position to pivot
    float dx = positionX - orbitPivotX;
    float dy = positionY - orbitPivotY;
    float dz = positionZ - orbitPivotZ;
    orbitDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (orbitDistance < 1.0f) {
        orbitDistance = 10.0f;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Update Functions
// ─────────────────────────────────────────────────────────────────────────────

void EditorCameraController::Update(const CameraInputState& input, float deltaTime)
{
    // Update movement in fly mode
    if (m_flyModeActive) {
        updateFlyMovement(input, deltaTime);
    }
    
    // Reset wheel delta after processing
    // (wheel events are instantaneous, not continuous)
}

void EditorCameraController::OnMousePress(int button, const CameraInputState& input)
{
    // Qt::LeftButton = 1, Qt::RightButton = 2, Qt::MiddleButton = 4
    
    if (button == 2) { // RMB
        if (input.altDown) {
            // Alt + RMB = Dolly mode
            m_dollyModeActive = true;
        } else {
            // RMB only = Fly mode
            m_flyModeActive = true;
            // Update orbit pivot to current look-at point when starting fly mode
            UpdateOrbitPivotFromView();
        }
    } else if (button == 1 && input.altDown) { // Alt + LMB
        m_orbitModeActive = true;
    } else if (button == 4 && input.altDown) { // Alt + MMB
        m_panModeActive = true;
    }
    
    // Store initial mouse position for delta calculation
    m_prevMouseX = input.mouseX;
    m_prevMouseY = input.mouseY;
    m_hasMousePosition = true;
}

void EditorCameraController::OnMouseRelease(int button, const CameraInputState& input)
{
    (void)input; // Unused for now
    
    if (button == 2) { // RMB
        m_flyModeActive = false;
        m_dollyModeActive = false;
    } else if (button == 1) { // LMB
        m_orbitModeActive = false;
    } else if (button == 4) { // MMB
        m_panModeActive = false;
    }
}

void EditorCameraController::OnMouseMove(float deltaX, float deltaY, const CameraInputState& input)
{
    (void)input; // May be used for modifier checks
    
    if (m_flyModeActive) {
        // Mouse look: apply yaw and pitch
        yawDeg += deltaX * mouseSensitivity;
        pitchDeg -= deltaY * mouseSensitivity; // Inverted Y
        clampPitch();
    } else if (m_orbitModeActive) {
        updateOrbit(deltaX, deltaY);
    } else if (m_panModeActive) {
        updatePan(deltaX, deltaY);
    } else if (m_dollyModeActive) {
        // Use Y axis for dolly when dragging
        updateDolly(-deltaY * zoomSensitivity * 0.1f);
    }
}

void EditorCameraController::OnMouseWheel(float delta, const CameraInputState& input)
{
    (void)input;
    
    // Wheel always does dolly/zoom - move along view direction
    updateDolly(delta * wheelZoomSensitivity);
}

// ─────────────────────────────────────────────────────────────────────────────
// Navigation Functions
// ─────────────────────────────────────────────────────────────────────────────

void EditorCameraController::FocusOnPoint(float pointX, float pointY, float pointZ, float radius)
{
    // Set orbit pivot
    orbitPivotX = pointX;
    orbitPivotY = pointY;
    orbitPivotZ = pointZ;
    
    // Calculate view distance based on radius and FOV
    float fovRad = fovDeg * DEG_TO_RAD;
    float distance = (radius * 2.5f) / std::tan(fovRad * 0.5f);
    if (distance < 1.0f) {
        distance = 5.0f; // Minimum distance
    }
    orbitDistance = distance;
    
    // Position camera behind the current view direction from pivot
    float fx, fy, fz;
    GetForward(fx, fy, fz);
    
    positionX = pointX - fx * distance;
    positionY = pointY - fy * distance;
    positionZ = pointZ - fz * distance;
    
    // Recalculate yaw/pitch to look at pivot
    float dx = pointX - positionX;
    float dy = pointY - positionY;
    float dz = pointZ - positionZ;
    
    float horizontalDist = std::sqrt(dx * dx + dz * dz);
    yawDeg = std::atan2(dz, dx) * RAD_TO_DEG;
    pitchDeg = std::atan2(dy, horizontalDist) * RAD_TO_DEG;
    clampPitch();
}

void EditorCameraController::SetOrbitPivot(float x, float y, float z)
{
    orbitPivotX = x;
    orbitPivotY = y;
    orbitPivotZ = z;
    
    // Recalculate orbit distance
    float dx = positionX - orbitPivotX;
    float dy = positionY - orbitPivotY;
    float dz = positionZ - orbitPivotZ;
    orbitDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (orbitDistance < 0.1f) {
        orbitDistance = 10.0f;
    }
}

void EditorCameraController::UpdateOrbitPivotFromView()
{
    // Calculate the point the camera is looking at based on current orbit distance
    float fx, fy, fz;
    GetForward(fx, fy, fz);
    
    orbitPivotX = positionX + fx * orbitDistance;
    orbitPivotY = positionY + fy * orbitDistance;
    orbitPivotZ = positionZ + fz * orbitDistance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Direction Vectors
// ─────────────────────────────────────────────────────────────────────────────

void EditorCameraController::GetForward(float& x, float& y, float& z) const
{
    float yawRad = yawDeg * DEG_TO_RAD;
    float pitchRad = pitchDeg * DEG_TO_RAD;
    
    x = std::cos(yawRad) * std::cos(pitchRad);
    y = std::sin(pitchRad);
    z = std::sin(yawRad) * std::cos(pitchRad);
}

void EditorCameraController::GetRight(float& x, float& y, float& z) const
{
    float yawRad = yawDeg * DEG_TO_RAD;
    
    // Right is perpendicular to forward on the XZ plane
    // To get left, negate the perpendicular: forward is (cos, sin), perpendicular is (-sin, cos) or (sin, -cos)
    // We want right to be to the right when looking in forward direction
    x = std::sin(yawRad);
    y = 0.0f;
    z = -std::cos(yawRad);
}

void EditorCameraController::GetUp(float& x, float& y, float& z) const
{
    // Cross product of right and forward
    float fx, fy, fz;
    float rx, ry, rz;
    GetForward(fx, fy, fz);
    GetRight(rx, ry, rz);
    
    // up = forward x right (note: this gives camera-local up)
    // For simple case, we use world up for movement
    x = ry * fz - rz * fy;
    y = rz * fx - rx * fz;
    z = rx * fy - ry * fx;
    
    // Normalize
    float len = std::sqrt(x * x + y * y + z * z);
    if (len > 0.0001f) {
        x /= len;
        y /= len;
        z /= len;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Output
// ─────────────────────────────────────────────────────────────────────────────

void EditorCameraController::BuildRenderCamera(RenderCamera& outCamera) const
{
    outCamera.positionX = positionX;
    outCamera.positionY = positionY;
    outCamera.positionZ = positionZ;
    outCamera.yawDeg = yawDeg;
    outCamera.pitchDeg = pitchDeg;
    outCamera.nearPlane = nearPlane;
    outCamera.farPlane = farPlane;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private Helpers
// ─────────────────────────────────────────────────────────────────────────────

void EditorCameraController::updateFlyMovement(const CameraInputState& input, float deltaTime)
{
    // Calculate movement speed
    float speed = baseMoveSpeed;
    if (input.shiftDown) {
        speed *= sprintMultiplier;
    }
    if (input.ctrlDown) {
        speed *= slowMultiplier;
    }
    
    float velocity = speed * deltaTime;
    
    // Get direction vectors
    float fx, fy, fz;
    float rx, ry, rz;
    GetForward(fx, fy, fz);
    GetRight(rx, ry, rz);
    
    // WASD movement (forward/back/strafe)
    if (input.wDown) {
        positionX += fx * velocity;
        positionY += fy * velocity;
        positionZ += fz * velocity;
    }
    if (input.sDown) {
        positionX -= fx * velocity;
        positionY -= fy * velocity;
        positionZ -= fz * velocity;
    }
    if (input.aDown) {
        positionX -= rx * velocity;
        positionZ -= rz * velocity;
    }
    if (input.dDown) {
        positionX += rx * velocity;
        positionZ += rz * velocity;
    }
    
    // Q/E movement (down/up) - world space vertical
    if (input.eDown) {
        positionY += velocity;
    }
    if (input.qDown) {
        positionY -= velocity;
    }
}

void EditorCameraController::updateOrbit(float deltaX, float deltaY)
{
    // Rotate around the pivot
    yawDeg += deltaX * orbitSensitivity;
    pitchDeg -= deltaY * orbitSensitivity; // Inverted Y
    clampPitch();
    
    // Update camera position to maintain orbit around pivot
    updatePositionFromOrbit();
}

void EditorCameraController::updatePan(float deltaX, float deltaY)
{
    // Move camera and pivot in the view plane
    float rx, ry, rz;
    float ux, uy, uz;
    GetRight(rx, ry, rz);
    GetUp(ux, uy, uz);
    
    float panSpeed = panSensitivity * orbitDistance; // Scale with distance
    
    // Move in view plane (right and up directions)
    float moveX = (-deltaX * rx + deltaY * ux) * panSpeed;
    float moveY = (-deltaX * ry + deltaY * uy) * panSpeed;
    float moveZ = (-deltaX * rz + deltaY * uz) * panSpeed;
    
    positionX += moveX;
    positionY += moveY;
    positionZ += moveZ;
    
    // Also move pivot so orbit center follows
    orbitPivotX += moveX;
    orbitPivotY += moveY;
    orbitPivotZ += moveZ;
}

void EditorCameraController::updateDolly(float delta)
{
    // Move along view direction (forward/back)
    float fx, fy, fz;
    GetForward(fx, fy, fz);
    
    // Scale dolly speed with current distance for consistent feel
    float dollyAmount = delta * orbitDistance * 0.1f;
    
    positionX += fx * dollyAmount;
    positionY += fy * dollyAmount;
    positionZ += fz * dollyAmount;
    
    // Update orbit distance
    orbitDistance -= dollyAmount;
    if (orbitDistance < 0.5f) {
        orbitDistance = 0.5f;
    }
}

void EditorCameraController::clampPitch()
{
    if (pitchDeg > maxPitchDeg) {
        pitchDeg = maxPitchDeg;
    }
    if (pitchDeg < minPitchDeg) {
        pitchDeg = minPitchDeg;
    }
}

void EditorCameraController::updatePositionFromOrbit()
{
    // Position camera at orbitDistance from pivot, oriented by yaw/pitch
    // The camera looks "inward" toward the pivot, so we position it "behind" 
    // the pivot relative to the current view direction
    
    float yawRad = yawDeg * DEG_TO_RAD;
    float pitchRad = pitchDeg * DEG_TO_RAD;
    
    // Direction from pivot to camera (opposite of look direction)
    float backX = -std::cos(yawRad) * std::cos(pitchRad);
    float backY = -std::sin(pitchRad);
    float backZ = -std::sin(yawRad) * std::cos(pitchRad);
    
    positionX = orbitPivotX + backX * orbitDistance;
    positionY = orbitPivotY + backY * orbitDistance;
    positionZ = orbitPivotZ + backZ * orbitDistance;
}

} // namespace Hephaestus
