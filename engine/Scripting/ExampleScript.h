#pragma once
#include "ScriptBase.h"
#include "../Scene/Entity.h"
#include <iostream>

/**
 * Example script template showing how to create a custom game script
 * 
 * This is a simple example that prints lifecycle events
 * You can use this as a template for creating your own scripts
 */
class ExampleScript : public ScriptBase {
public:
    ExampleScript(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    virtual ~ExampleScript() = default;

    /**
     * Called when the script is first awoken (usually at scene start)
     */
    void OnAwake() override {
        std::cout << "ExampleScript::OnAwake - Script awakened!\n";
    }

    /**
     * Called every frame with delta time
     */
    void OnUpdate(float deltaTime) override {
        // Your game logic here
        // std::cout << "ExampleScript::OnUpdate - Delta time: " << deltaTime << "\n";
    }

    /**
     * Called after all OnUpdate calls
     */
    void OnLateUpdate(float deltaTime) override {
        // Post-update logic here
    }

    /**
     * Called when the script is enabled
     */
    void OnEnable() override {
        std::cout << "ExampleScript::OnEnable - Script enabled!\n";
    }

    /**
     * Called when the script is disabled
     */
    void OnDisable() override {
        std::cout << "ExampleScript::OnDisable - Script disabled!\n";
    }

    /**
     * Called before the script is destroyed
     */
    void OnDestroy() override {
        std::cout << "ExampleScript::OnDestroy - Script destroyed!\n";
    }

    // You can add custom methods and member variables
private:
    float m_Timer = 0.0f;
};
