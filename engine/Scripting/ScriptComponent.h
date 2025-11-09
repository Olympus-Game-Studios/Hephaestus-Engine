#pragma once
#include "ScriptBase.h"
#include <vector>
#include <string>

/**
 * Component that holds script instances attached to an entity
 * Multiple scripts can be attached to a single entity
 */
struct ScriptComponent {
    std::vector<ScriptPtr> scripts;
    std::vector<std::string> scriptNames; // For serialization and debugging

    ScriptComponent() = default;
    
    /**
     * Add a script instance to this component
     */
    void AddScript(ScriptPtr script, const std::string& scriptName) {
        if (script) {
            scripts.push_back(script);
            scriptNames.push_back(scriptName);
        }
    }

    /**
     * Get a script by index
     */
    ScriptPtr GetScript(size_t index) {
        if (index < scripts.size()) {
            return scripts[index];
        }
        return nullptr;
    }

    /**
     * Get a script by type name
     */
    ScriptPtr GetScriptByName(const std::string& name) {
        for (size_t i = 0; i < scriptNames.size(); ++i) {
            if (scriptNames[i] == name) {
                return scripts[i];
            }
        }
        return nullptr;
    }

    /**
     * Remove a script by index
     */
    void RemoveScript(size_t index) {
        if (index < scripts.size()) {
            scripts.erase(scripts.begin() + index);
            scriptNames.erase(scriptNames.begin() + index);
        }
    }

    /**
     * Remove all scripts
     */
    void RemoveAllScripts() {
        scripts.clear();
        scriptNames.clear();
    }

    /**
     * Get the number of attached scripts
     */
    size_t GetScriptCount() const {
        return scripts.size();
    }

    /**
     * Check if any scripts are attached
     */
    bool HasScripts() const {
        return !scripts.empty();
    }
};
