#pragma once
#include <string>
#include <memory>
#include "../Scene/Entity.h"

class Scene;

/**
 * Base class for all C++ scripts in the engine
 * Developers should inherit from this class to create game logic
 * 
 * Example usage:
 * class PlayerController : public ScriptBase {
 * public:
 *     void OnAwake() override { 
 *         // Initialize 
 *     }
 *     void OnUpdate(float deltaTime) override { 
 *         // Update logic 
 *     }
 * };
 */
class ScriptBase {
public:
    virtual ~ScriptBase() = default;

    /**
     * Called when the script is first attached to an entity or the scene starts
     * Use this for initialization
     */
    virtual void OnAwake() {}

    /**
     * Called when the script is about to be destroyed or disabled
     * Use this for cleanup
     */
    virtual void OnDestroy() {}

    /**
     * Called when the entity or script is enabled
     */
    virtual void OnEnable() {}

    /**
     * Called when the entity or script is disabled
     */
    virtual void OnDisable() {}

    /**
     * Called every frame with delta time (in seconds)
     * Main update loop for your game logic
     */
    virtual void OnUpdate(float deltaTime) {}

    /**
     * Called after OnUpdate, used for physics and rendering
     */
    virtual void OnLateUpdate(float deltaTime) {}

    /**
     * Called when another entity's collider touches this entity's collider
     * (Note: requires physics component setup)
     */
    virtual void OnTriggerEnter(Entity other) {}

    /**
     * Called when another entity's collider stops touching this entity's collider
     */
    virtual void OnTriggerExit(Entity other) {}

    /**
     * Called during collision contact
     */
    virtual void OnTriggerStay(Entity other) {}

    /**
     * Getters for the attached entity and scene
     */
    Entity GetEntity() const { return m_Entity; }
    Scene* GetScene() const { return m_Scene; }
    bool IsAwoken() const { return m_IsAwoken; }
    bool IsEnabled() const { return m_IsEnabled; }

    /**
     * Set enabled state of the script
     */
    void SetEnabled(bool enabled);

protected:
    Entity m_Entity;
    Scene* m_Scene = nullptr;
    bool m_IsAwoken = false;
    bool m_IsEnabled = true;

    friend class ScriptEngine;
};

// Shared pointer type for scripts
using ScriptPtr = std::shared_ptr<ScriptBase>;
