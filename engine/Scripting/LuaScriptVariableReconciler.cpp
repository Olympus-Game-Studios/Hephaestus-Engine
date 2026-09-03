#include "LuaScriptVariableReconciler.h"

#include "LuaScriptComponent.h"

#include <cmath>
#include <unordered_map>
#include <unordered_set>

LuaScriptReconcileResult LuaScriptVariableReconciler::OnScriptReplaced(
    LuaScriptComponent& component,
    const LuaExposeScanResult& current)
{
    LuaScriptReconcileResult result;
    result.changed = !component.variables.empty() || !component.authoredVariables.empty();
    component.variables.clear();
    component.authoredVariables.clear();
    component.authoredVariablesInitialized = true;

    if (!current.valid) {
        return result;
    }
    for (const LuaExposedField& field : current.fields) {
        component.variables.emplace(field.name, field.defaultValue);
        result.changed = true;
    }
    return result;
}

LuaScriptReconcileResult LuaScriptVariableReconciler::OnScriptSourceChanged(
    LuaScriptComponent& component,
    const LuaExposeScanResult* previous,
    const LuaExposeScanResult& current,
    std::unordered_map<std::string, PropertyValue>* instanceOverrides)
{
    LuaScriptReconcileResult result;
    if (!current.valid) {
        return result;
    }

    if (!component.authoredVariablesInitialized) {
        for (const auto& [name, _] : component.variables) {
            component.authoredVariables.insert(name);
        }
        component.authoredVariablesInitialized = true;
        result.changed = !component.variables.empty();
    }

    std::unordered_map<std::string, LuaExposeType> previousTypes;
    if (previous && previous->valid) {
        for (const LuaExposedField& field : previous->fields) {
            previousTypes.emplace(field.name, field.type);
        }
    }

    std::unordered_set<std::string> declaredNames;
    declaredNames.reserve(current.fields.size());
    for (const LuaExposedField& field : current.fields) {
        declaredNames.insert(field.name);

        const auto previousType = previousTypes.find(field.name);
        const bool declarationTypeChanged =
            previousType != previousTypes.end() && previousType->second != field.type;
        const auto currentValue = component.variables.find(field.name);
        const bool valueIncompatible =
            currentValue != component.variables.end() && !IsCompatible(field.type, currentValue->second);
        auto overrideValue = instanceOverrides ? instanceOverrides->find(field.name) :
            std::unordered_map<std::string, PropertyValue>::iterator{};
        const bool overridePresent = instanceOverrides && overrideValue != instanceOverrides->end();
        const bool overrideIncompatible =
            overridePresent && !IsCompatible(field.type, overrideValue->second);

        if (declarationTypeChanged || valueIncompatible || overrideIncompatible) {
            component.variables[field.name] = field.defaultValue;
            component.authoredVariables.erase(field.name);
            if (overridePresent) {
                instanceOverrides->erase(overrideValue);
            }
            result.resetTypeNames.push_back(field.name);
            result.changed = true;
            continue;
        }

        if (overridePresent) {
            if (currentValue == component.variables.end() ||
                !currentValue->second.Equals(overrideValue->second)) {
                component.variables[field.name] = overrideValue->second;
                result.changed = true;
            }
            continue;
        }

        if (component.authoredVariables.count(field.name) != 0 &&
            currentValue != component.variables.end()) {
            continue;
        }
        if (currentValue == component.variables.end() ||
            !currentValue->second.Equals(field.defaultValue)) {
            component.variables[field.name] = field.defaultValue;
            result.changed = true;
        }
    }

    for (auto variable = component.variables.begin(); variable != component.variables.end();) {
        if (declaredNames.count(variable->first) != 0) {
            ++variable;
            continue;
        }
        result.removedNames.push_back(variable->first);
        component.authoredVariables.erase(variable->first);
        if (instanceOverrides) {
            instanceOverrides->erase(variable->first);
        }
        variable = component.variables.erase(variable);
        result.changed = true;
    }

    for (auto authored = component.authoredVariables.begin();
         authored != component.authoredVariables.end();) {
        if (declaredNames.count(*authored) != 0) {
            ++authored;
        } else {
            authored = component.authoredVariables.erase(authored);
            result.changed = true;
        }
    }

    if (instanceOverrides) {
        for (auto overrideValue = instanceOverrides->begin();
             overrideValue != instanceOverrides->end();) {
            if (declaredNames.count(overrideValue->first) != 0) {
                ++overrideValue;
            } else {
                overrideValue = instanceOverrides->erase(overrideValue);
                result.changed = true;
            }
        }
    }
    return result;
}

bool LuaScriptVariableReconciler::IsCompatible(
    LuaExposeType type,
    const PropertyValue& value)
{
    switch (type) {
        case LuaExposeType::Bool:
            return value.IsBool();
        case LuaExposeType::Integer:
            return value.IsNumber() && std::isfinite(*value.AsNumber()) &&
                std::floor(*value.AsNumber()) == *value.AsNumber();
        case LuaExposeType::Number:
            return value.IsNumber();
        case LuaExposeType::String:
            return value.IsString();
        case LuaExposeType::Vec2:
            return value.IsVec2();
        case LuaExposeType::Vec3:
            return value.IsVec3();
        case LuaExposeType::Color:
            return value.IsVec4();
    }
    return false;
}
