#pragma once

/**
 * @file AutomationToolSupport.h
 * @brief Internal helpers shared by the content-authoring automation modules.
 *
 * These helpers are intentionally Qt-free and depend only on the semantic
 * automation core plus engine content types. They exist to keep the asset,
 * material, character, and script tool groups consistent without duplicating
 * schema/mutation/path logic in each module.
 */

#include "AutomationService.h"
#include "AutomationTypes.h"
#include "EntityRef.h"

#include "Assets/AssetPathPolicy.h"
#include "Assets/AssetTypes.h"
#include "Core/PropertyValue.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Hephaestus::Automation::Content {

// ── Schema / definition helpers ────────────────────────────────────────────

inline Json ObjectSchema(Json properties = Json::object(), Json required = Json::array())
{
    Json schema = {{"type", "object"}, {"properties", std::move(properties)},
                   {"additionalProperties", false}};
    if (!required.empty()) schema["required"] = std::move(required);
    return schema;
}

/**
 * @brief Build an editor-host content tool definition.
 *
 * Content-authoring tools are editor-only for this milestone. Mutations default
 * to EditorOnly so they disappear while Play Mode is active; read-only tools
 * may still run during play.
 */
inline ToolDefinition Definition(std::string name, std::string description, Json schema,
                                 ToolHandler handler, bool mutates = false,
                                 PlayRequirement requirement = PlayRequirement::Any,
                                 HostMask hostMask = HostMask::Editor)
{
    ToolDefinition definition;
    definition.name = std::move(name);
    definition.description = std::move(description);
    definition.inputSchema = std::move(schema);
    definition.hostMask = hostMask;
    definition.playRequirement = mutates ? PlayRequirement::EditorOnly : requirement;
    definition.mutatesProject = mutates;
    definition.handler = std::move(handler);
    return definition;
}

inline void RegisterOrThrow(AutomationService& service, ToolDefinition definition)
{
    std::string error;
    if (!service.Registry().Register(std::move(definition), error)) {
        throw std::runtime_error(error);
    }
}

// ── Entity resolution / mutation helpers ───────────────────────────────────

inline Scene* RequireScene(ToolContext& context)
{
    return context.Backend().ActiveScene().scene;
}

inline std::optional<ResolvedEntity> ResolveEntity(ToolContext& context, const Json& arguments,
                                                   ToolResult& outFailure)
{
    if (!arguments.contains("entity") || !arguments["entity"].is_string()) {
        outFailure = ToolResult::Failure(ErrorCode::InvalidArguments,
                                         "A string entity reference is required");
        return std::nullopt;
    }
    Scene* scene = RequireScene(context);
    if (!scene) {
        outFailure = ToolResult::Failure(ErrorCode::NoActiveScene, "No active scene");
        return std::nullopt;
    }
    ResolvedEntity resolved = context.Entities().Resolve(
        EntityRef{arguments["entity"].get<std::string>()}, scene);
    if (!resolved.IsResolved()) {
        outFailure = ToolResult::Failure(ErrorCode::EntityRefExpired,
                                         "Entity reference is unknown, stale, or destroyed");
        return std::nullopt;
    }
    return resolved;
}

inline bool BeginMutation(ToolContext& context, std::string_view label, ToolResult& outFailure)
{
    std::string error;
    if (context.Backend().PrepareEditorMutation(label, error)) return true;
    outFailure = ToolResult::Failure(ErrorCode::ToolUnavailable, std::move(error));
    return false;
}

// ── PropertyValue <-> JSON ─────────────────────────────────────────────────

inline Json PropertyToJson(const PropertyValue& value)
{
    if (value.IsNull()) return nullptr;
    if (value.IsBool()) return *value.AsBool();
    if (value.IsNumber()) return *value.AsNumber();
    if (value.IsString()) return *value.AsString();
    if (value.IsVec2()) {
        const Vec2& v = *value.AsVec2();
        return Json::array({v.x, v.y});
    }
    if (value.IsVec3()) {
        const Vec3& v = *value.AsVec3();
        return Json::array({v.x, v.y, v.z});
    }
    if (value.IsVec4()) {
        const Vec4& v = *value.AsVec4();
        return Json::array({v.x, v.y, v.z, v.w});
    }
    if (value.IsArray()) {
        Json array = Json::array();
        for (const PropertyValue& element : *value.AsArray()) array.push_back(PropertyToJson(element));
        return array;
    }
    if (value.IsObject()) {
        Json object = Json::object();
        for (const auto& [key, element] : *value.AsObject()) object[key] = PropertyToJson(element);
        return object;
    }
    return nullptr;
}

inline bool TryJsonToProperty(const Json& json, PropertyValue& out)
{
    if (json.is_null()) {
        out = PropertyValue();
        return true;
    }
    if (json.is_boolean()) {
        out = PropertyValue(json.get<bool>());
        return true;
    }
    if (json.is_number()) {
        out = PropertyValue(json.get<double>());
        return true;
    }
    if (json.is_string()) {
        out = PropertyValue(json.get<std::string>());
        return true;
    }
    if (json.is_array()) {
        if (json.size() == 2 && json[0].is_number() && json[1].is_number()) {
            out = PropertyValue(Vec2(json[0].get<float>(), json[1].get<float>()));
            return true;
        }
        if (json.size() == 3 && json[0].is_number() && json[1].is_number() && json[2].is_number()) {
            out = PropertyValue(Vec3(json[0].get<float>(), json[1].get<float>(), json[2].get<float>()));
            return true;
        }
        if (json.size() == 4 && json[0].is_number() && json[1].is_number() &&
            json[2].is_number() && json[3].is_number()) {
            out = PropertyValue(Vec4(json[0].get<float>(), json[1].get<float>(),
                                     json[2].get<float>(), json[3].get<float>()));
            return true;
        }
        PropertyValue::Array array;
        for (const Json& element : json) {
            PropertyValue converted;
            if (!TryJsonToProperty(element, converted)) return false;
            array.push_back(std::move(converted));
        }
        out = PropertyValue(std::move(array));
        return true;
    }
    if (json.is_object()) {
        PropertyValue::Object object;
        for (auto it = json.begin(); it != json.end(); ++it) {
            PropertyValue converted;
            if (!TryJsonToProperty(it.value(), converted)) return false;
            object.emplace(it.key(), std::move(converted));
        }
        out = PropertyValue(std::move(object));
        return true;
    }
    return false;
}

// ── Path helpers ───────────────────────────────────────────────────────────

inline std::string ToLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

inline std::string NormalizePath(const std::string& path)
{
    return AssetPathPolicy::NormalizeSeparators(path);
}

inline bool ContainsParentTraversal(const std::string& normalized)
{
    std::size_t start = 0;
    while (start <= normalized.size()) {
        const std::size_t end = normalized.find('/', start);
        const std::string_view component(normalized.data() + start,
                                         (end == std::string::npos ? normalized.size() : end) - start);
        if (component == "..") return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

/**
 * @brief Convert an authored asset path to a path relative to the asset root.
 *
 * Accepts portable keys ("assets/..."), absolute paths under the project, and
 * asset-root-relative paths. Rejects paths that escape the project asset root.
 */
inline std::optional<std::string> ToAssetRelative(const std::string& projectRoot,
                                                  const std::string& assetRoot,
                                                  const std::string& input,
                                                  std::string& outError)
{
    if (input.empty()) return std::string();
    const std::string normalized = NormalizePath(input);
    if (ContainsParentTraversal(normalized)) {
        outError = "Asset path may not contain '..' traversal: " + input;
        return std::nullopt;
    }

    std::filesystem::path candidate(normalized);
    if (candidate.is_absolute()) {
        std::error_code ec;
        const std::filesystem::path relative = std::filesystem::relative(candidate, assetRoot, ec);
        if (ec || relative.empty() || ContainsParentTraversal(relative.generic_string())) {
            outError = "Path is outside the project asset root: " + input;
            return std::nullopt;
        }
        return relative.generic_string();
    }
    if (normalized == "assets") return std::string();
    if (normalized.rfind("assets/", 0) == 0) return normalized.substr(7);
    return normalized;
}

/**
 * @brief Resolve an authored asset path to an absolute filesystem path.
 */
inline std::optional<std::filesystem::path> ResolveAssetPath(const std::string& projectRoot,
                                                             const std::string& assetRoot,
                                                             const std::string& input,
                                                             std::string& outError)
{
    const auto relative = ToAssetRelative(projectRoot, assetRoot, input, outError);
    if (!relative) return std::nullopt;
    std::filesystem::path resolved = std::filesystem::path(assetRoot) / *relative;
    return resolved.lexically_normal();
}

/**
 * @brief Ensure a destination folder resolves under the project asset root.
 */
inline std::optional<std::filesystem::path> ResolveAssetFolder(const std::string& assetRoot,
                                                               const std::string& input,
                                                               std::string& outError)
{
    const std::string normalized = NormalizePath(input);
    if (ContainsParentTraversal(normalized)) {
        outError = "Destination folder may not contain '..' traversal: " + input;
        return std::nullopt;
    }
    std::filesystem::path folder;
    if (normalized.empty()) {
        folder = std::filesystem::path(assetRoot);
    } else if (std::filesystem::path(normalized).is_absolute()) {
        folder = std::filesystem::path(normalized);
    } else if (normalized == "assets") {
        folder = std::filesystem::path(assetRoot);
    } else if (normalized.rfind("assets/", 0) == 0) {
        folder = std::filesystem::path(assetRoot) / normalized.substr(7);
    } else {
        folder = std::filesystem::path(assetRoot) / normalized;
    }
    folder = folder.lexically_normal();

    std::error_code ec;
    const std::filesystem::path root = std::filesystem::path(assetRoot).lexically_normal();
    const std::filesystem::path relative = std::filesystem::relative(folder, root, ec);
    if (ec || ContainsParentTraversal(relative.generic_string())) {
        outError = "Destination folder is outside the project asset root: " + input;
        return std::nullopt;
    }
    return folder;
}

} // namespace Hephaestus::Automation::Content
