#pragma once
#include <string>
#include <memory>

/**
 * Component for holding Lua scripts
 * Complements C++ scripts, can coexist on same entity
 */
struct LuaScriptComponent {
    std::string scriptPath;          // Path to .lua file
    std::string functionOnAwake;     // Function to call on awake
    std::string functionOnUpdate;    // Function to call on update
    std::string functionOnDestroy;   // Function to call on destroy
    bool enabled = true;

    LuaScriptComponent() = default;
    LuaScriptComponent(const std::string& path) : scriptPath(path) {}
};
