#pragma once
#include "ScriptBase.h"
#include <functional>
#include <unordered_map>
#include <string>
#include <memory>
#include <iostream>

/**
 * Factory function type for creating script instances
 * Takes the Entity and Scene as parameters
 */
using ScriptFactory = std::function<ScriptPtr(Entity entity, Scene* scene)>;

/**
 * Registry for managing available script types
 * Scripts must be registered before they can be instantiated
 * 
 * Example usage in your game code:
 * 
 * ScriptRegistry::Register<PlayerController>("PlayerController",
 *     [](Entity entity, Scene* scene) {
 *         return std::make_shared<PlayerController>(entity, scene);
 *     }
 * );
 */
class ScriptRegistry {
public:
    /**
     * Register a script type with a factory function
     * Template version for convenience
     */
    template<typename T>
    static void Register(const std::string& scriptName, ScriptFactory factory) {
        auto& registry = GetInstance().m_Factories;
        if (registry.find(scriptName) != registry.end()) {
            std::cout << "Warning: Script '" << scriptName << "' is already registered. Overwriting.\n";
        }
        registry[scriptName] = factory;
        std::cout << "Registered script: " << scriptName << "\n";
    }

    /**
     * Create a script instance by name
     */
    static ScriptPtr CreateScript(const std::string& scriptName, Entity entity, Scene* scene) {
        auto& registry = GetInstance().m_Factories;
        auto it = registry.find(scriptName);
        if (it == registry.end()) {
            std::cerr << "Error: Script '" << scriptName << "' not found in registry.\n";
            return nullptr;
        }
        return it->second(entity, scene);
    }

    /**
     * Check if a script type is registered
     */
    static bool IsRegistered(const std::string& scriptName) {
        auto& registry = GetInstance().m_Factories;
        return registry.find(scriptName) != registry.end();
    }

    /**
     * Get all registered script names
     */
    static std::vector<std::string> GetRegisteredScripts() {
        auto& registry = GetInstance().m_Factories;
        std::vector<std::string> names;
        for (const auto& pair : registry) {
            names.push_back(pair.first);
        }
        return names;
    }

    /**
     * Unregister a script type
     */
    static void Unregister(const std::string& scriptName) {
        auto& registry = GetInstance().m_Factories;
        registry.erase(scriptName);
    }

    /**
     * Clear all registered scripts (use with caution)
     */
    static void Clear() {
        GetInstance().m_Factories.clear();
    }

private:
    std::unordered_map<std::string, ScriptFactory> m_Factories;

    // Singleton pattern
    static ScriptRegistry& GetInstance() {
        static ScriptRegistry instance;
        return instance;
    }

    ScriptRegistry() = default;
    ~ScriptRegistry() = default;
};
