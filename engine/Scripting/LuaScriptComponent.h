#pragma once
#include <string>
#include <memory>
#include <unordered_map>

#include "../Core/PropertyValue.h"

/**
 * Component for holding Lua scripts
 * Complements C++ scripts, can coexist on same entity
 */
struct LuaScriptComponent {
    std::string scriptPath;          // Path to .lua file
    std::string scriptSource;        // Optional embedded source
    std::string functionOnAwake;     // Function to call on awake
    std::string functionOnUpdate;    // Function to call on update
    std::string functionOnDestroy;   // Function to call on destroy
    bool enabled = true;
    std::unordered_map<std::string, PropertyValue> variables; // Per-instance variables

    LuaScriptComponent() = default;
    LuaScriptComponent(const std::string& path) : scriptPath(path) {}
};
