#include "ScriptAutomation.h"

#include "AutomationToolSupport.h"

#include "Assets/AssetManager.h"
#include "Scene/PrefabComponentIO.h"
#include "Scene/PrefabOverrideSystem.h"
#include "Scripting/LuaExposeMetadata.h"
#include "Scripting/LuaExposeScanner.h"
#include "Scripting/LuaScriptComponent.h"
#include "Scripting/LuaScriptVariableReconciler.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Hephaestus::Automation {
namespace {

using namespace Content;

ToolOutcome Failure(ErrorCode code, std::string message)
{
    return ToolResult::Failure(code, std::move(message));
}

const char* LuaExposeTypeName(LuaExposeType type)
{
    switch (type) {
    case LuaExposeType::Bool: return "bool";
    case LuaExposeType::Integer: return "integer";
    case LuaExposeType::Number: return "number";
    case LuaExposeType::String: return "string";
    case LuaExposeType::Vec2: return "vec2";
    case LuaExposeType::Vec3: return "vec3";
    case LuaExposeType::Color: return "color";
    }
    return "number";
}

bool IsCompatible(LuaExposeType type, const PropertyValue& value)
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

bool ScanScriptSource(const LuaScriptComponent& component, const std::string& projectRoot,
                      LuaExposeScanResult& outScan, std::string& outSource, std::string& outError)
{
    if (!component.scriptSource.empty()) {
        outSource = component.scriptSource;
    } else if (!component.scriptPath.empty()) {
        const std::filesystem::path resolved =
            AssetPathPolicy::ResolvePortableKey(projectRoot, component.scriptPath);
        std::ifstream input(resolved, std::ios::binary);
        if (!input) {
            outError = "Unable to read script: " + component.scriptPath;
            return false;
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        outSource = buffer.str();
    } else {
        outError = "Script component has neither a script path nor embedded source";
        return false;
    }
    outScan = LuaExposeScanner::Scan(outSource);
    return true;
}

Json DiagnosticsJson(const LuaExposeScanResult& scan)
{
    Json diagnostics = Json::array();
    for (const LuaExposeDiagnostic& diagnostic : scan.diagnostics) {
        diagnostics.push_back({{"line", diagnostic.sourceLine},
                               {"message", diagnostic.message},
                               {"conflict", diagnostic.conflict}});
    }
    return diagnostics;
}

Json PropertiesJson(const LuaScriptComponent& component, const LuaExposeScanResult& scan)
{
    Json properties = Json::array();
    std::unordered_set<std::string> declared;
    declared.reserve(scan.fields.size());
    for (const LuaExposedField& field : scan.fields) {
        declared.insert(field.name);
        const auto current = component.variables.find(field.name);
        properties.push_back(
            {{"name", field.name},
             {"type", LuaExposeTypeName(field.type)},
             {"value", PropertyToJson(current != component.variables.end() ? current->second
                                                                          : field.defaultValue)},
             {"default", PropertyToJson(field.defaultValue)},
             {"authored", component.authoredVariables.count(field.name) != 0},
             {"sourceLine", field.sourceLine},
             {"declared", true}});
    }
    for (const auto& [name, value] : component.variables) {
        if (declared.count(name) != 0) continue;
        properties.push_back({{"name", name},
                              {"type", "unknown"},
                              {"value", PropertyToJson(value)},
                              {"authored", component.authoredVariables.count(name) != 0},
                              {"declared", false}});
    }
    return properties;
}

Json ScriptJson(const LuaScriptComponent& component)
{
    return Json{{"path", component.scriptPath},
                {"enabled", component.enabled},
                {"embeddedSource", !component.scriptSource.empty()}};
}

Hephaestus::AssetManager* RequireAssets(ToolContext& context, ToolResult& outFailure)
{
    Hephaestus::AssetManager* assets = context.Backend().Assets();
    if (!assets || !assets->HasValidAssetRoot()) {
        outFailure = ToolResult::Failure(ErrorCode::ToolUnavailable,
                                         "Asset management is unavailable for this host");
        return nullptr;
    }
    return assets;
}

std::string ResolveProjectRoot(ToolContext& context)
{
    Hephaestus::AssetManager* assets = context.Backend().Assets();
    return assets ? assets->GetProjectRoot() : std::string();
}

} // namespace

void RegisterScriptAutomation(AutomationService& service)
{
    RegisterOrThrow(service, Definition(
        "script.get",
        "Inspect the Lua script attached to an entity and its Expose.* properties.",
        ObjectSchema({{"entity", {{"type", "string"}}}}, Json::array({"entity"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = ResolveEntity(context, arguments, failure);
            if (!resolved) return failure;
            auto& registry = resolved->scene->GetRegistry();
            const LuaScriptComponent* component = registry.try_get<LuaScriptComponent>(resolved->handle);
            if (!component) {
                return ToolResult::Success({{"attached", false},
                                            {"entity", context.Entities()
                                                           .Bind(*resolved->scene, resolved->handle)
                                                           .value}});
            }

            const std::string projectRoot = ResolveProjectRoot(context);
            LuaExposeScanResult scan;
            std::string source;
            std::string error;
            const bool scanned = ScanScriptSource(*component, projectRoot, scan, source, error);

            Json value = {{"attached", true},
                          {"entity", context.Entities().Bind(*resolved->scene, resolved->handle).value},
                          {"script", ScriptJson(*component)}};
            if (scanned) {
                value["properties"] = PropertiesJson(*component, scan);
                value["diagnostics"] = DiagnosticsJson(scan);
                value["contentHash"] = scan.contentHash;
                value["valid"] = scan.valid;
            } else {
                value["properties"] = Json::array();
                value["diagnostics"] = Json::array();
                value["error"] = error;
            }
            return ToolResult::Success(std::move(value));
        }));

    RegisterOrThrow(service, Definition(
        "script.attach",
        "Attach a project Lua script to an entity through LuaScriptComponent.",
        ObjectSchema({{"entity", {{"type", "string"}}},
                      {"path", {{"type", "string"}}},
                      {"enabled", {{"type", "boolean"}}}},
                     Json::array({"entity", "path"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = ResolveEntity(context, arguments, failure);
            if (!resolved) return failure;
            if (!arguments.contains("path") || !arguments["path"].is_string() ||
                arguments["path"].get<std::string>().empty()) {
                return Failure(ErrorCode::InvalidArguments, "A script asset path is required");
            }
            if (resolved->scene->GetRegistry().all_of<PrefabNodeComponent>(resolved->handle)) {
                return Failure(ErrorCode::PrefabStructuralOverrideUnsupported,
                               "Attaching scripts to linked prefab nodes is not supported");
            }

            Hephaestus::AssetManager* assets = RequireAssets(context, failure);
            if (!assets) return failure;
            const std::string requested = arguments["path"].get<std::string>();
            const std::filesystem::path scriptPath =
                AssetPathPolicy::ResolvePortableKey(assets->GetProjectRoot(), requested);
            std::error_code existsError;
            if (!std::filesystem::exists(scriptPath, existsError)) {
                return Failure(ErrorCode::InvalidArguments,
                               "Script asset does not exist: " + requested);
            }
            const std::string portable =
                AssetPathPolicy::ToPortableKey(assets->GetProjectRoot(), scriptPath.generic_string());

            LuaScriptComponent probe(portable);
            LuaExposeScanResult scan;
            std::string source;
            std::string error;
            ScanScriptSource(probe, assets->GetProjectRoot(), scan, source, error);

            ToolResult mutationFailure;
            if (!BeginMutation(context, "Attach Lua Script", mutationFailure)) return mutationFailure;
            auto& registry = resolved->scene->GetRegistry();
            LuaScriptComponent& component = [&]() -> LuaScriptComponent& {
                if (auto* existing = registry.try_get<LuaScriptComponent>(resolved->handle)) {
                    return *existing;
                }
                return registry.emplace<LuaScriptComponent>(resolved->handle, portable);
            }();
            component.scriptPath = portable;
            component.scriptSource.clear();
            if (arguments.contains("enabled") && arguments["enabled"].is_boolean()) {
                component.enabled = arguments["enabled"].get<bool>();
            }
            LuaScriptVariableReconciler::OnScriptReplaced(component, scan);
            PrefabOverrideSystem::SyncEntityOverrides(
                *resolved->scene, Entity(resolved->handle, resolved->scene));
            context.Backend().CompleteEditorMutation(true);

            Json value = {{"attached", true},
                          {"entity", context.Entities().Bind(*resolved->scene, resolved->handle).value},
                          {"script", ScriptJson(component)},
                          {"properties", PropertiesJson(component, scan)},
                          {"diagnostics", DiagnosticsJson(scan)}};
            if (!scan.valid) value["error"] = error.empty() ? "Script failed to scan" : error;
            return ToolResult::Success(std::move(value));
        }, true));

    RegisterOrThrow(service, Definition(
        "script.remove",
        "Remove the Lua script component from an entity.",
        ObjectSchema({{"entity", {{"type", "string"}}}}, Json::array({"entity"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = ResolveEntity(context, arguments, failure);
            if (!resolved) return failure;
            auto& registry = resolved->scene->GetRegistry();
            if (!registry.all_of<LuaScriptComponent>(resolved->handle)) {
                return Failure(ErrorCode::ComponentMissing,
                               "Entity does not have a Lua script attached");
            }
            if (registry.all_of<PrefabNodeComponent>(resolved->handle)) {
                return Failure(ErrorCode::PrefabStructuralOverrideUnsupported,
                               "Removing scripts from linked prefab nodes is not supported");
            }

            ToolResult mutationFailure;
            if (!BeginMutation(context, "Remove Lua Script", mutationFailure)) return mutationFailure;
            registry.remove<LuaScriptComponent>(resolved->handle);
            PrefabOverrideSystem::SyncEntityOverrides(
                *resolved->scene, Entity(resolved->handle, resolved->scene));
            context.Backend().CompleteEditorMutation(true);
            return ToolResult::Success(
                {{"removed", true},
                 {"entity", context.Entities().Bind(*resolved->scene, resolved->handle).value}});
        }, true));

    RegisterOrThrow(service, Definition(
        "script.set_property",
        "Set an authored Expose.* value on an entity's attached Lua script.",
        ObjectSchema({{"entity", {{"type", "string"}}},
                      {"name", {{"type", "string"}}},
                      {"value", {}}},
                     Json::array({"entity", "name", "value"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = ResolveEntity(context, arguments, failure);
            if (!resolved) return failure;
            if (!arguments.contains("name") || !arguments["name"].is_string()) {
                return Failure(ErrorCode::InvalidArguments, "A property name is required");
            }
            if (!arguments.contains("value")) {
                return Failure(ErrorCode::InvalidArguments, "A property value is required");
            }
            auto& registry = resolved->scene->GetRegistry();
            LuaScriptComponent* component = registry.try_get<LuaScriptComponent>(resolved->handle);
            if (!component) {
                return Failure(ErrorCode::ComponentMissing,
                               "Entity does not have a Lua script attached");
            }

            LuaExposeScanResult scan;
            std::string source;
            std::string error;
            if (!ScanScriptSource(*component, ResolveProjectRoot(context), scan, source, error)) {
                return Failure(ErrorCode::InternalError, error);
            }
            if (!scan.valid) {
                return ToolResult::Failure(
                    ErrorCode::InvalidArguments,
                    "Script has Expose.* declaration conflicts; fix the script first",
                    Json{{"diagnostics", DiagnosticsJson(scan)}});
            }
            const std::string name = arguments["name"].get<std::string>();
            const LuaExposedField* field = nullptr;
            for (const LuaExposedField& candidate : scan.fields) {
                if (candidate.name == name) {
                    field = &candidate;
                    break;
                }
            }
            if (!field) {
                return Failure(ErrorCode::UnknownProperty,
                               "Script does not expose a property named '" + name + "'");
            }

            PropertyValue value;
            if (!TryJsonToProperty(arguments["value"], value)) {
                return Failure(ErrorCode::InvalidArguments,
                               "Value for '" + name + "' is not a supported JSON value");
            }
            if (!IsCompatible(field->type, value)) {
                return Failure(ErrorCode::InvalidPropertyType,
                               "Value for '" + name + "' must be compatible with exposed type " +
                                   std::string(LuaExposeTypeName(field->type)));
            }

            ToolResult mutationFailure;
            if (!BeginMutation(context, "Set Script Property", mutationFailure)) return mutationFailure;
            component->variables[name] = value;
            if (!registry.all_of<PrefabNodeComponent>(resolved->handle)) {
                component->authoredVariables.insert(name);
                component->authoredVariablesInitialized = true;
            }
            PrefabOverrideSystem::SyncEntityOverrides(
                *resolved->scene, Entity(resolved->handle, resolved->scene));
            context.Backend().CompleteEditorMutation(false);

            return ToolResult::Success(
                {{"set", true},
                 {"entity", context.Entities().Bind(*resolved->scene, resolved->handle).value},
                 {"name", name},
                 {"type", LuaExposeTypeName(field->type)},
                 {"value", PropertyToJson(value)},
                 {"authored", component->authoredVariables.count(name) != 0}});
        }, true));

    RegisterOrThrow(service, Definition(
        "script.reload",
        "Re-scan attached scripts, reconcile Expose.* metadata, and request a runtime reload.",
        ObjectSchema({{"entity", {{"type", "string"}}}}),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            Scene* scene = RequireScene(context);
            if (!scene) return Failure(ErrorCode::NoActiveScene, "No active scene");

            std::vector<entt::entity> targets;
            if (arguments.contains("entity")) {
                ToolResult failure;
                const auto resolved = ResolveEntity(context, arguments, failure);
                if (!resolved) return failure;
                if (!resolved->scene->GetRegistry().all_of<LuaScriptComponent>(resolved->handle)) {
                    return Failure(ErrorCode::ComponentMissing,
                                   "Entity does not have a Lua script attached");
                }
                targets.push_back(resolved->handle);
                scene = resolved->scene;
            } else {
                auto view = scene->GetRegistry().view<LuaScriptComponent>();
                for (entt::entity handle : view) targets.push_back(handle);
            }

            const std::string projectRoot = ResolveProjectRoot(context);
            Json updated = Json::array();
            bool anyChanged = false;

            if (!targets.empty()) {
                ToolResult mutationFailure;
                if (!BeginMutation(context, "Reload Scripts", mutationFailure)) return mutationFailure;
                for (entt::entity handle : targets) {
                    LuaScriptComponent* component = scene->GetRegistry().try_get<LuaScriptComponent>(handle);
                    if (!component) continue;
                    LuaExposeScanResult scan;
                    std::string source;
                    std::string error;
                    if (!ScanScriptSource(*component, projectRoot, scan, source, error)) {
                        updated.push_back({{"entity", context.Entities().Bind(*scene, handle).value},
                                           {"ok", false},
                                           {"error", error}});
                        continue;
                    }
                    if (!scan.valid) {
                        updated.push_back({{"entity", context.Entities().Bind(*scene, handle).value},
                                           {"ok", false},
                                           {"diagnostics", DiagnosticsJson(scan)}});
                        continue;
                    }
                    const LuaScriptReconcileResult reconcile =
                        LuaScriptVariableReconciler::OnScriptSourceChanged(*component, nullptr, scan);
                    anyChanged = anyChanged || reconcile.changed;
                    Json entry = {{"entity", context.Entities().Bind(*scene, handle).value},
                                  {"ok", true},
                                  {"changed", reconcile.changed},
                                  {"removed", reconcile.removedNames},
                                  {"reset", reconcile.resetTypeNames},
                                  {"properties", PropertiesJson(*component, scan)}};
                    updated.push_back(std::move(entry));
                    PrefabOverrideSystem::SyncEntityOverrides(*scene, Entity(handle, scene));
                }
                context.Backend().CompleteEditorMutation(false);
            }

            std::string runtimeError;
            const bool runtimeReloaded = context.Backend().RequestScriptReload(runtimeError);
            Json value = {{"reloaded", true},
                          {"updated", std::move(updated)},
                          {"changed", anyChanged},
                          {"runtime", {{"requested", true},
                                       {"available", runtimeReloaded},
                                       {"message", runtimeError}}}};
            return ToolResult::Success(std::move(value));
        }, true));
}

} // namespace Hephaestus::Automation
