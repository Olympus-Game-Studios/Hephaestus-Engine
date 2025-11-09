#pragma once
#include "engine/Scripting/ScriptBase.h"
#include "engine/Scene/Components.h"
#include <iostream>

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
            Vec3 movement = m_CurrentDirection * m_MoveSpeed * deltaTime;
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
