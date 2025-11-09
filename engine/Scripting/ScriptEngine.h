#pragma once
#include "ScriptBase.h"
#include "ScriptComponent.h"
#include "../Scene/Entity.h"
#include <entt/entt.hpp>
#include <vector>
#include <string>

class Scene;

/**
 * ScriptEngine manages the lifecycle of all scripts in the engine
 * It handles:
 * - Initialization (OnAwake)
 * - Updates (OnUpdate, OnLateUpdate)
 * - Cleanup (OnDestroy)
 * - Script attachment/detachment
 * 
 * The engine should call these methods at appropriate times during its main loop
 */
class ScriptEngine {
public:
    ScriptEngine() = default;
    ~ScriptEngine() = default;

    /**
     * Initialize the script engine with a scene
     * Should be called once when the engine starts or scene is loaded
     */
    void Initialize(Scene* scene);

    /**
     * Cleanup and destroy all scripts
     * Should be called when the scene is unloaded or engine shutdowns
     */
    void Shutdown();

    /**
     * Call OnAwake on all scripts that haven't been awoken yet
     * This is typically called at the start of the first frame
     */
    void AwakeScripts();

    /**
     * Update all scripts - calls OnUpdate with delta time
     * Should be called every frame
     */
    void UpdateScripts(float deltaTime);

    /**
     * Late update all scripts - calls OnLateUpdate with delta time
     * Should be called every frame after regular updates
     */
    void LateUpdateScripts(float deltaTime);

    /**
     * Destroy all scripts and cleanup
     * Should be called before scene unload
     */
    void DestroyScripts();

    /**
     * Attach a script to an entity
     * The script will be awoken on the next AwakeScripts call
     */
    void AttachScript(Entity entity, ScriptPtr script, const std::string& scriptName);

    /**
     * Detach a script from an entity by script name
     */
    void DetachScript(Entity entity, const std::string& scriptName);

    /**
     * Detach all scripts from an entity
     */
    void DetachAllScripts(Entity entity);

    /**
     * Get a script from an entity by name
     */
    ScriptPtr GetScript(Entity entity, const std::string& scriptName);

    /**
     * Get all scripts from an entity
     */
    std::vector<ScriptPtr> GetAllScripts(Entity entity);

    /**
     * Get the count of scripts on an entity
     */
    size_t GetScriptCount(Entity entity);

    /**
     * Handle entity destruction - cleanup its scripts
     */
    void OnEntityDestroyed(Entity entity);

    /**
     * Get the scene this engine is managing
     */
    Scene* GetScene() const { return m_Scene; }

private:
    Scene* m_Scene = nullptr;
    std::vector<ScriptPtr> m_AllScripts;
    std::vector<ScriptPtr> m_ScriptsToAwake; // Scripts waiting to be awakened

    /**
     * Internal helper to find or create ScriptComponent on an entity
     */
    ScriptComponent& GetOrCreateScriptComponent(Entity entity);
};
