#include "ScriptEngine.h"
#include "../Scene/Scene.h"
#include <algorithm>
#include <iostream>

void ScriptEngine::Initialize(Scene* scene) {
    m_Scene = scene;
    std::cout << "ScriptEngine initialized with scene\n";
}

void ScriptEngine::Shutdown() {
    DestroyScripts();
    m_Scene = nullptr;
    std::cout << "ScriptEngine shutdown\n";
}

void ScriptEngine::AwakeScripts() {
    // Awaken all scripts that are waiting
    for (auto& script : m_ScriptsToAwake) {
        if (script && !script->m_IsAwoken) {
            script->m_IsAwoken = true;
            script->OnAwake();
        }
    }
    m_ScriptsToAwake.clear();
}

void ScriptEngine::UpdateScripts(float deltaTime) {
    // Update all active scripts
    for (auto& script : m_AllScripts) {
        if (script && script->m_IsEnabled && script->m_IsAwoken) {
            script->OnUpdate(deltaTime);
        }
    }
}

void ScriptEngine::LateUpdateScripts(float deltaTime) {
    // Late update all active scripts
    for (auto& script : m_AllScripts) {
        if (script && script->m_IsEnabled && script->m_IsAwoken) {
            script->OnLateUpdate(deltaTime);
        }
    }
}

void ScriptEngine::DestroyScripts() {
    // Call OnDestroy on all scripts
    for (auto& script : m_AllScripts) {
        if (script) {
            script->OnDestroy();
        }
    }
    m_AllScripts.clear();
    m_ScriptsToAwake.clear();
    std::cout << "All scripts destroyed\n";
}

void ScriptEngine::AttachScript(Entity entity, ScriptPtr script, const std::string& scriptName) {
    if (!entity || !script || !m_Scene) {
        std::cerr << "Error: Cannot attach script - invalid entity, script, or scene\n";
        return;
    }

    // Set up the script
    script->m_Entity = entity;
    script->m_Scene = m_Scene;
    script->m_IsEnabled = true;
    script->m_IsAwoken = false;

    // Get or create the script component
    ScriptComponent& scriptComp = GetOrCreateScriptComponent(entity);
    scriptComp.AddScript(script, scriptName);

    // Add to all scripts list
    m_AllScripts.push_back(script);

    // Add to scripts to awake
    m_ScriptsToAwake.push_back(script);

    std::cout << "Attached script '" << scriptName << "' to entity\n";
}

void ScriptEngine::DetachScript(Entity entity, const std::string& scriptName) {
    if (!entity || !m_Scene) return;

    if (!entity.HasComponent<ScriptComponent>()) return;
    
    auto& scriptComp = entity.GetComponent<ScriptComponent>();

    ScriptPtr scriptToRemove = scriptComp.GetScriptByName(scriptName);
    if (!scriptToRemove) return;

    // Call OnDestroy
    scriptToRemove->OnDestroy();

    // Remove from script component
    for (size_t i = 0; i < scriptComp.scriptNames.size(); ++i) {
        if (scriptComp.scriptNames[i] == scriptName) {
            scriptComp.RemoveScript(i);
            break;
        }
    }

    // Remove from all scripts list
    auto it = std::find(m_AllScripts.begin(), m_AllScripts.end(), scriptToRemove);
    if (it != m_AllScripts.end()) {
        m_AllScripts.erase(it);
    }

    std::cout << "Detached script '" << scriptName << "' from entity\n";
}

void ScriptEngine::DetachAllScripts(Entity entity) {
    if (!entity || !m_Scene) return;

    if (!entity.HasComponent<ScriptComponent>()) return;
    
    auto& scriptComp = entity.GetComponent<ScriptComponent>();

    std::vector<std::string> scriptNamesToRemove = scriptComp.scriptNames;
    for (const auto& name : scriptNamesToRemove) {
        DetachScript(entity, name);
    }
}

ScriptPtr ScriptEngine::GetScript(Entity entity, const std::string& scriptName) {
    if (!entity) return nullptr;

    if (!entity.HasComponent<ScriptComponent>()) return nullptr;

    auto& scriptComp = entity.GetComponent<ScriptComponent>();

    return scriptComp.GetScriptByName(scriptName);
}

std::vector<ScriptPtr> ScriptEngine::GetAllScripts(Entity entity) {
    std::vector<ScriptPtr> result;
    if (!entity) return result;

    if (!entity.HasComponent<ScriptComponent>()) return result;

    auto& scriptComp = entity.GetComponent<ScriptComponent>();

    return scriptComp.scripts;
}

size_t ScriptEngine::GetScriptCount(Entity entity) {
    if (!entity) return 0;

    if (!entity.HasComponent<ScriptComponent>()) return 0;

    auto& scriptComp = entity.GetComponent<ScriptComponent>();

    return scriptComp.GetScriptCount();
}

void ScriptEngine::OnEntityDestroyed(Entity entity) {
    DetachAllScripts(entity);
}

ScriptComponent& ScriptEngine::GetOrCreateScriptComponent(Entity entity) {
    // This will be implemented in Entity.cpp template instantiation
    // For now, we'll use the pattern that assumes HasComponent and GetComponent work
    if (!entity.HasComponent<ScriptComponent>()) {
        entity.AddComponent<ScriptComponent>();
    }
    return entity.GetComponent<ScriptComponent>();
}
