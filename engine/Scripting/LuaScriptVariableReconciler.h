#pragma once

#include "LuaExposeMetadata.h"

#include <string>
#include <unordered_map>
#include <vector>

struct LuaScriptComponent;

struct LuaScriptReconcileResult {
    bool changed = false;
    std::vector<std::string> resetTypeNames;
    std::vector<std::string> removedNames;
};

class LuaScriptVariableReconciler {
public:
    static LuaScriptReconcileResult OnScriptReplaced(
        LuaScriptComponent& component,
        const LuaExposeScanResult& current);

    static LuaScriptReconcileResult OnScriptSourceChanged(
        LuaScriptComponent& component,
        const LuaExposeScanResult* previous,
        const LuaExposeScanResult& current,
        std::unordered_map<std::string, PropertyValue>* instanceOverrides = nullptr);

private:
    static bool IsCompatible(LuaExposeType type, const PropertyValue& value);
};
