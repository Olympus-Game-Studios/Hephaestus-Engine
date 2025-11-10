#pragma once
#include "engine/Scripting/ScriptBase.h"
#include "engine/Scene/Components.h"
#include "engine/Core/Math.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>

/**
 * PlayerController - Example player controller script
 * 
 * This is a USER SCRIPT - it lives in the /scripts directory, not the engine!
 * You can modify this directly without changing engine source code.
 * 
 * Features:
 * - WASD movement with forward/backward/strafe
 * - Mouse look with right-click (Unreal-style)
 * - Q/E for up/down movement
 * - Shift to sprint
 * - Pitch clamping to prevent camera flip
 * 
 * Usage:
 * 1. Attach this script to a player entity in the editor
 * 2. The script will automatically find and initialize
 * 3. Control with WASD + mouse when in play mode
 */
class PlayerController : public ScriptBase {
public:
    PlayerController(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    void OnAwake() override {
        std::cout << "PlayerController awoken - WASD to move, Right Mouse to look\n";
        if (m_Entity.HasComponent<TransformComponent>()) {
            m_Transform = &m_Entity.GetComponent<TransformComponent>();
        }
    }

    void OnUpdate(float deltaTime) override {
        if (!m_Transform) return;

        // Get input
        Vec3 inputDir = GetMovementInput();
        
        // Apply speed multiplier
        float currentSpeed = m_MoveSpeed;
        if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            currentSpeed *= m_SpeedBoostMultiplier;
        }

        // Calculate forward and right vectors from current rotation
        float yaw = m_Transform->rotation.y;

        Vec3 forward = Vec3(
            std::cos(yaw),
            0.0f,
            std::sin(yaw)
        );

        Vec3 right = Vec3(
            std::sin(yaw),
            0.0f,
            -std::cos(yaw)
        );

        Vec3 up = Vec3(0.0f, 1.0f, 0.0f);

        // Apply movement in world space
        Vec3 movement = Vec3(0.0f, 0.0f, 0.0f);
        
        if (inputDir.z != 0.0f) { // Forward/Backward (W/S)
            movement = vadd(movement, vscale(forward, inputDir.z * currentSpeed * deltaTime));
        }
        if (inputDir.x != 0.0f) { // Strafe left/right (A/D)
            movement = vadd(movement, vscale(right, inputDir.x * currentSpeed * deltaTime));
        }
        if (inputDir.y != 0.0f) { // Up/Down (Q/E)
            movement = vadd(movement, vscale(up, inputDir.y * currentSpeed * deltaTime));
        }

        m_Transform->position = vadd(m_Transform->position, movement);

        // Handle camera look with mouse
        UpdateCameraLook(deltaTime);
    }

    void OnDestroy() override {
        std::cout << "PlayerController destroyed\n";
    }

private:
    TransformComponent* m_Transform = nullptr;
    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;
    bool m_FirstMouseLook = true;

    // Configuration - modify these to customize behavior
    float m_MoveSpeed = 10.0f;              // Units per second
    float m_LookSpeed = 0.003f;            // Radians per pixel
    float m_SpeedBoostMultiplier = 2.0f;   // Sprint multiplier

    /**
     * Get current movement input from keyboard
     * Returns: {right/left, up/down, forward/backward}
     */
    Vec3 GetMovementInput() {
        Vec3 input = {0.0f, 0.0f, 0.0f};
        GLFWwindow* window = glfwGetCurrentContext();
        if (!window) return input;

        // WASD movement
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            input.z -= 1.0f; // Forward
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            input.z += 1.0f; // Backward
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            input.x += 1.0f; // Right/Strafe
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            input.x -= 1.0f; // Left/Strafe
        }

        // Vertical movement
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            input.y -= 1.0f; // Down
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            input.y += 1.0f; // Up
        }

        // Normalize if multiple keys pressed (for consistent diagonal movement)
        float length = std::sqrt(input.x * input.x + input.y * input.y + input.z * input.z);
        if (length > 0.01f) {
            input.x /= length;
            input.y /= length;
            input.z /= length;
        }

        return input;
    }

    /**
     * Update camera look based on mouse movement
     * Only active when right mouse button is held
     */
    void UpdateCameraLook(float deltaTime) {
        if (!m_Transform) return;

        GLFWwindow* window = glfwGetCurrentContext();
        if (!window) return;

        // Only look when right mouse is held
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS) {
            m_FirstMouseLook = true;
            return;
        }

        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        // First frame of look - just capture position
        if (m_FirstMouseLook) {
            m_LastMouseX = mouseX;
            m_LastMouseY = mouseY;
            m_FirstMouseLook = false;
            return;
        }

        // Calculate mouse delta
        double deltaX = mouseX - m_LastMouseX;
        double deltaY = mouseY - m_LastMouseY;

        m_LastMouseX = mouseX;
        m_LastMouseY = mouseY;

        // Update rotation based on mouse movement
        m_Transform->rotation.y -= static_cast<float>(deltaX) * m_LookSpeed;
        m_Transform->rotation.x -= static_cast<float>(deltaY) * m_LookSpeed;

        // Clamp pitch to prevent flipping
        const float maxPitch = 1.5708f; // ~90 degrees
        if (m_Transform->rotation.x > maxPitch) {
            m_Transform->rotation.x = maxPitch;
        }
        if (m_Transform->rotation.x < -maxPitch) {
            m_Transform->rotation.x = -maxPitch;
        }
    }
};
