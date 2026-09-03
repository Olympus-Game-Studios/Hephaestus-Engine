#include "Scripting/LuaExposeScanner.h"
#include "Scripting/LuaScriptComponent.h"
#include "Scripting/LuaScriptVariableReconciler.h"

#include <iostream>
#include <string>
#include <unordered_map>

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

bool NumberEquals(const LuaScriptComponent& component, const std::string& name, double expected)
{
    const auto value = component.variables.find(name);
    return value != component.variables.end() && value->second.IsNumber() &&
        *value->second.AsNumber() == expected;
}

bool TestScriptReplacement()
{
    LuaScriptComponent component;
    component.variables["speed"] = PropertyValue(9.0);
    component.authoredVariables.insert("speed");
    component.authoredVariablesInitialized = true;

    const auto replacement = LuaExposeScanner::Scan(R"(
Expose.number("speed", 2.0)
Expose.bool("locked", true)
)");
    const auto result = LuaScriptVariableReconciler::OnScriptReplaced(component, replacement);
    return Check(result.changed, "script replacement reports change") &&
           Check(NumberEquals(component, "speed", 2.0), "same-name value does not carry across scripts") &&
           Check(component.variables.at("locked").IsBool(), "replacement defaults seeded") &&
           Check(component.authoredVariables.empty(), "replacement clears authored metadata");
}

bool TestSourceReload()
{
    const auto previous = LuaExposeScanner::Scan(R"(
Expose.number("speed", 5.0)
Expose.bool("canJump", true)
Expose.number("health", 100)
)");
    const auto current = LuaExposeScanner::Scan(R"(
Expose.number("speed", 6.0)
Expose.number("jumpHeight", 1.5)
Expose.string("health", "full")
)");

    LuaScriptComponent component;
    component.variables["speed"] = PropertyValue(7.0);
    component.variables["canJump"] = PropertyValue(true);
    component.variables["health"] = PropertyValue(75.0);
    component.authoredVariables.insert("speed");
    component.authoredVariables.insert("canJump");
    component.authoredVariables.insert("health");
    component.authoredVariablesInitialized = true;

    const auto result = LuaScriptVariableReconciler::OnScriptSourceChanged(
        component, &previous, current);

    return Check(NumberEquals(component, "speed", 7.0), "authored value survives default change") &&
           Check(NumberEquals(component, "jumpHeight", 1.5), "new field receives default") &&
           Check(component.variables.at("health").IsString() &&
                     *component.variables.at("health").AsString() == "full",
                 "incompatible type resets to new default") &&
           Check(component.authoredVariables.count("health") == 0,
                 "type reset clears authored state") &&
           Check(component.variables.count("canJump") == 0 &&
                     component.authoredVariables.count("canJump") == 0,
                 "removed declaration cleans value and metadata") &&
           Check(result.resetTypeNames.size() == 1 && result.removedNames.size() == 1,
                 "reload reports reset and removal");
}

bool TestDefaultFollowingAndOverrides()
{
    const auto previous = LuaExposeScanner::Scan(R"(Expose.number("speed", 5.0))");
    const auto current = LuaExposeScanner::Scan(R"(Expose.number("speed", 8.0))");

    LuaScriptComponent following;
    following.variables["speed"] = PropertyValue(5.0);
    following.authoredVariablesInitialized = true;
    LuaScriptVariableReconciler::OnScriptSourceChanged(following, &previous, current);

    LuaScriptComponent instance;
    instance.variables["speed"] = PropertyValue(7.0);
    instance.authoredVariablesInitialized = true;
    std::unordered_map<std::string, PropertyValue> overrides;
    overrides["speed"] = PropertyValue(7.0);
    LuaScriptVariableReconciler::OnScriptSourceChanged(instance, &previous, current, &overrides);

    return Check(NumberEquals(following, "speed", 8.0), "default-following value updates") &&
           Check(NumberEquals(instance, "speed", 7.0), "instance override survives default change") &&
           Check(overrides.count("speed") == 1, "instance override metadata survives");
}

bool TestLegacyMigration()
{
    const auto current = LuaExposeScanner::Scan(R"(Expose.number("speed", 8.0))");
    LuaScriptComponent component;
    component.variables["speed"] = PropertyValue(3.0);

    LuaScriptVariableReconciler::OnScriptSourceChanged(component, nullptr, current);
    return Check(NumberEquals(component, "speed", 3.0), "legacy value preserved") &&
           Check(component.authoredVariables.count("speed") == 1,
                 "legacy value becomes explicitly authored");
}

} // namespace

int main()
{
    bool ok = true;
    ok &= TestScriptReplacement();
    ok &= TestSourceReload();
    ok &= TestDefaultFollowingAndOverrides();
    ok &= TestLegacyMigration();
    if (ok) {
        std::cout << "LuaScriptVariableTests passed\n";
        return 0;
    }
    return 1;
}
