#pragma once
#include "../Scripting/ScriptBase.h"
#include "../Scene/Components.h"
#include "../Core/Math.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>

/**
 * Simple example: A rotating cube script
 * Demonstrates basic transform manipulation in a script
 */
class RotatingCube : public ScriptBase {
public:
    RotatingCube(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    void OnAwake() override {
        std::cout << "RotatingCube awoken\n";
        m_RotationSpeed = 2.0f; // radians per second
    }

    void OnUpdate(float deltaTime) override {
        // Get the entity's transform
        if (m_Entity.HasComponent<TransformComponent>()) {
            auto& transform = m_Entity.GetComponent<TransformComponent>();
            
            // Rotate around Y axis
            transform.rotation.y += m_RotationSpeed * deltaTime;
            
            // Keep rotation in valid range
            if (transform.rotation.y > 6.28f) { // 2 * PI
                transform.rotation.y -= 6.28f;
            }
        }
    }

    void OnDestroy() override {
        std::cout << "RotatingCube destroyed\n";
    }

private:
    float m_RotationSpeed = 0.0f;
};

/**
 * Example: Movement controller
 * Handles entity movement based on simple input
 */
class SimpleMovement : public ScriptBase {
public:
    SimpleMovement(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    void OnAwake() override {
        std::cout << "SimpleMovement awoken\n";
        m_MoveSpeed = 5.0f; // units per second
        m_CurrentDirection = {0, 0, 0};
    }

    void OnUpdate(float deltaTime) override {
        // Simulate simple WASD input (in real engine, use input system)
        // This is just a demonstration
        m_CurrentDirection = {0, 0, 0};

        // For now, just move forward
        m_CurrentDirection.z = 1.0f;

        if (m_Entity.HasComponent<TransformComponent>()) {
            auto& transform = m_Entity.GetComponent<TransformComponent>();
            
            // Apply movement
            Vec3 movement = vscale(m_CurrentDirection, m_MoveSpeed * deltaTime);
            transform.position.x += movement.x;
            transform.position.y += movement.y;
            transform.position.z += movement.z;
        }
    }

    void OnEnable() override {
        std::cout << "SimpleMovement enabled\n";
    }

    void OnDisable() override {
        std::cout << "SimpleMovement disabled\n";
    }

    void OnDestroy() override {
        std::cout << "SimpleMovement destroyed\n";
    }

private:
    float m_MoveSpeed = 0.0f;
    Vec3 m_CurrentDirection;
};

/**
 * Example: Lifetime component
 * Destroys the entity after a set amount of time
 */
class Lifetime : public ScriptBase {
public:
    Lifetime(Entity entity, Scene* scene, float lifetime = 5.0f) {
        m_Entity = entity;
        m_Scene = scene;
        m_RemainingLifetime = lifetime;
    }

    void OnAwake() override {
        std::cout << "Lifetime awoken: " << m_RemainingLifetime << " seconds\n";
    }

    void OnUpdate(float deltaTime) override {
        m_RemainingLifetime -= deltaTime;

        if (m_RemainingLifetime <= 0.0f) {
            // In a real implementation, you would call scene->DestroyEntity(m_Entity)
            std::cout << "Lifetime expired for entity\n";
            OnDestroy();
        }
    }

    void OnDestroy() override {
        std::cout << "Lifetime destroyed\n";
    }

private:
    float m_RemainingLifetime = 0.0f;
};

/**
 * Example: Billboard effect
 * Makes the object always face the camera (requires camera setup)
 */
class BillboardEffect : public ScriptBase {
public:
    BillboardEffect(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    void OnAwake() override {
        std::cout << "BillboardEffect awoken\n";
        // You would typically cache the camera entity here
    }

    void OnUpdate(float deltaTime) override {
        // In a real implementation:
        // 1. Find the camera entity
        // 2. Calculate direction from entity to camera
        // 3. Rotate the entity to face camera
        
        // Placeholder for demonstration
    }

    void OnDestroy() override {
        std::cout << "BillboardEffect destroyed\n";
    }

private:
    Entity m_CameraEntity;
};

/**
 * Example: Simple FSM (Finite State Machine)
 * Demonstrates more complex script behavior
 */
class SimpleFSM : public ScriptBase {
public:
    enum class State {
        Idle,
        Moving,
        Attacking
    };

    SimpleFSM(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    void OnAwake() override {
        std::cout << "SimpleFSM awoken - Starting in Idle state\n";
        m_CurrentState = State::Idle;
        m_StateTimer = 0.0f;
    }

    void OnUpdate(float deltaTime) override {
        m_StateTimer += deltaTime;

        switch (m_CurrentState) {
            case State::Idle:
                UpdateIdle(deltaTime);
                break;
            case State::Moving:
                UpdateMoving(deltaTime);
                break;
            case State::Attacking:
                UpdateAttacking(deltaTime);
                break;
        }
    }

    void OnDestroy() override {
        std::cout << "SimpleFSM destroyed\n";
    }

private:
    State m_CurrentState = State::Idle;
    float m_StateTimer = 0.0f;

    void UpdateIdle(float deltaTime) {
        // After 2 seconds, transition to moving
        if (m_StateTimer > 2.0f) {
            ChangeState(State::Moving);
        }
    }

    void UpdateMoving(float deltaTime) {
        // Move entity
        if (m_Entity.HasComponent<TransformComponent>()) {
            auto& transform = m_Entity.GetComponent<TransformComponent>();
            transform.position.x += 1.0f * deltaTime;
        }

        // After 3 seconds, transition to attacking
        if (m_StateTimer > 3.0f) {
            ChangeState(State::Attacking);
        }
    }

    void UpdateAttacking(float deltaTime) {
        // Attack logic here
        
        // After 1 second, return to idle
        if (m_StateTimer > 1.0f) {
            ChangeState(State::Idle);
        }
    }

    void ChangeState(State newState) {
        if (m_CurrentState != newState) {
            std::cout << "FSM State Change: " << static_cast<int>(m_CurrentState) 
                      << " -> " << static_cast<int>(newState) << "\n";
            m_CurrentState = newState;
            m_StateTimer = 0.0f;
        }
    }
};

/**
 * PlayerController: Full-featured player movement controller
 * 
 * Features:
 * - WASD movement with forward/backward/strafe
 * - Smooth camera look with mouse (when right mouse button held)
 * - Up/Down movement with Q/E
 * - Speed boosting with Shift
 * - Proper world-space movement
 * 
 * Usage: Attach to a player entity that has a TransformComponent
 */
class PlayerController : public ScriptBase {
public:
    PlayerController(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    void OnAwake() override {
        std::cout << "PlayerController awoken\n";
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
        if (m_glfwWindow && glfwGetKey(m_glfwWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            currentSpeed *= m_SpeedBoostMultiplier;
        }

        // Calculate forward and right vectors from current rotation
        float yaw = m_Transform->rotation.y;
        float pitch = m_Transform->rotation.x;

        Vec3 forward = Vec3(
            -std::cos(yaw),
            0.0f,
            -std::sin(yaw)
        );

        Vec3 right = Vec3(
            -std::sin(yaw),
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
        if (inputDir.y != 0.0f) { // Up/Down
            movement = vadd(movement, vscale(up, inputDir.y * currentSpeed * deltaTime));
        }

        m_Transform->position = vadd(m_Transform->position, movement);

        // Handle camera look with mouse
        if (m_glfwWindow) {
            UpdateCameraLook(deltaTime);
        }
    }

    void OnDestroy() override {
        std::cout << "PlayerController destroyed\n";
    }

    /**
     * Set the GLFW window pointer for input handling
     * This should be called after attaching the script to an entity
     */
    void SetWindow(GLFWwindow* window) {
        m_glfwWindow = window;
    }

    // Configuration
    float m_MoveSpeed = 10.0f;              // Units per second
    float m_LookSpeed = 0.003f;            // Radians per pixel
    float m_SpeedBoostMultiplier = 2.0f;   // Sprint multiplier

private:
    TransformComponent* m_Transform = nullptr;
    GLFWwindow* m_glfwWindow = nullptr;
    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;
    bool m_FirstMouseLook = true;

    /**
     * Get current movement input from keyboard
     * Returns: {right/left, up/down, forward/backward}
     */
    Vec3 GetMovementInput() {
        Vec3 input = {0.0f, 0.0f, 0.0f};

        if (!m_glfwWindow) return input;

        // WASD movement
        if (glfwGetKey(m_glfwWindow, GLFW_KEY_W) == GLFW_PRESS) {
            input.z += 1.0f; // Forward
        }
        if (glfwGetKey(m_glfwWindow, GLFW_KEY_S) == GLFW_PRESS) {
            input.z -= 1.0f; // Backward
        }
        if (glfwGetKey(m_glfwWindow, GLFW_KEY_D) == GLFW_PRESS) {
            input.x += 1.0f; // Right/Strafe
        }
        if (glfwGetKey(m_glfwWindow, GLFW_KEY_A) == GLFW_PRESS) {
            input.x -= 1.0f; // Left/Strafe
        }

        // Vertical movement
        if (glfwGetKey(m_glfwWindow, GLFW_KEY_Q) == GLFW_PRESS) {
            input.y -= 1.0f; // Down
        }
        if (glfwGetKey(m_glfwWindow, GLFW_KEY_E) == GLFW_PRESS) {
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

        // Only look when right mouse is held
        if (glfwGetMouseButton(m_glfwWindow, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS) {
            m_FirstMouseLook = true;
            return;
        }

        double mouseX, mouseY;
        glfwGetCursorPos(m_glfwWindow, &mouseX, &mouseY);

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

/**
 * User script: AndyScript
 * Template for custom game logic
 */
class AndyScript : public ScriptBase {
public:
    AndyScript(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    virtual ~AndyScript() = default;

    void OnAwake() override {
        // Initialize the script
    }

    void OnUpdate(float deltaTime) override {
        // Main game logic - called every frame
        // deltaTime is time elapsed since last frame in seconds
    }

    void OnLateUpdate(float deltaTime) override {
        // Called after OnUpdate - useful for camera or post-processing logic
    }

    void OnEnable() override {
        // Called when the script is enabled
    }

    void OnDisable() override {
        // Called when the script is disabled
    }

    void OnDestroy() override {
        // Called when the script is about to be destroyed - cleanup here
    }

private:
    // Add your script members here
};


