#include "SceneSerializer.h"

#include "Scene.h"
#include "Components.h"
#include "Json.h"
#include "PrefabSerializer.h"
#include "PrefabInstantiator.h"
#include "PrefabOverrideSystem.h"
#include "PrefabComponentIO.h"
#include "../Assets/AssetPathPolicy.h"
#include "../Scripting/ScriptComponent.h"
#include "../Scripting/ScriptRegistry.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
std::string EscapeString(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);

    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                std::ostringstream oss;
                oss.imbue(std::locale::classic());
                oss << "\\u" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(static_cast<unsigned char>(ch));
                escaped += oss.str();
            } else {
                escaped.push_back(ch);
            }
            break;
        }
    }

    return escaped;
}

std::string FormatFloat(float value)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(6) << value;
    return oss.str();
}

std::string FormatVec3(const Vec3& v)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "[" << FormatFloat(v.x) << ", " << FormatFloat(v.y) << ", " << FormatFloat(v.z) << "]";
    return oss.str();
}

void WritePropertyValue(std::ostream& stream, const PropertyValue& value)
{
    if (value.IsNull()) {
        stream << "null";
        return;
    }
    if (value.IsBool()) {
        stream << (*value.AsBool() ? "true" : "false");
        return;
    }
    if (value.IsNumber()) {
        stream << std::setprecision(10) << *value.AsNumber();
        return;
    }
    if (value.IsString()) {
        stream << "\"" << EscapeString(*value.AsString()) << "\"";
        return;
    }
    if (value.IsVec2()) {
        const Vec2& v = *value.AsVec2();
        stream << "[" << FormatFloat(v.x) << ", " << FormatFloat(v.y) << "]";
        return;
    }
    if (value.IsVec3()) {
        const Vec3& v = *value.AsVec3();
        stream << "[" << FormatFloat(v.x) << ", " << FormatFloat(v.y) << ", " << FormatFloat(v.z) << "]";
        return;
    }
    if (value.IsVec4()) {
        const Vec4& v = *value.AsVec4();
        stream << "[" << FormatFloat(v.x) << ", " << FormatFloat(v.y) << ", " << FormatFloat(v.z) << ", " << FormatFloat(v.w) << "]";
        return;
    }
    if (value.IsArray()) {
        stream << "[";
        const auto& arr = *value.AsArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i) stream << ", ";
            WritePropertyValue(stream, arr[i]);
        }
        stream << "]";
        return;
    }
    if (value.IsObject()) {
        stream << "{";
        bool first = true;
        for (const auto& [key, entry] : *value.AsObject()) {
            if (!first) stream << ", ";
            stream << "\"" << EscapeString(key) << "\": ";
            WritePropertyValue(stream, entry);
            first = false;
        }
        stream << "}";
        return;
    }
}

void WriteStringSet(std::ostream& stream, const std::unordered_set<std::string>& values)
{
    std::vector<std::string> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    stream << "[";
    for (size_t index = 0; index < sorted.size(); ++index) {
        if (index != 0) {
            stream << ", ";
        }
        stream << "\"" << EscapeString(sorted[index]) << "\"";
    }
    stream << "]";
}

PropertyValue ParsePropertyValue(const JsonValue& value);
static bool ParseBoolLike(const JsonValue& value, bool fallback);
static std::string NormalizeAssetPathForSave(const std::string& rawPath);

std::string WriteCharacterControllerJson(const CharacterControllerComponent& component)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "        \"CharacterController\": {\n";
    oss << "          \"enabled\": " << (component.enabled ? "true" : "false") << ",\n";
    oss << "          \"height\": " << FormatFloat(component.height) << ",\n";
    oss << "          \"radius\": " << FormatFloat(component.radius) << ",\n";
    oss << "          \"crouchedHeight\": " << FormatFloat(component.crouchedHeight) << ",\n";
    oss << "          \"maxSlopeAngleDegrees\": " << FormatFloat(component.maxSlopeAngleDegrees) << ",\n";
    oss << "          \"stepHeight\": " << FormatFloat(component.stepHeight) << ",\n";
    oss << "          \"groundSnapDistance\": " << FormatFloat(component.groundSnapDistance) << ",\n";
    oss << "          \"walkSpeed\": " << FormatFloat(component.walkSpeed) << ",\n";
    oss << "          \"acceleration\": " << FormatFloat(component.acceleration) << ",\n";
    oss << "          \"deceleration\": " << FormatFloat(component.deceleration) << ",\n";
    oss << "          \"airAcceleration\": " << FormatFloat(component.airAcceleration) << ",\n";
    oss << "          \"airControl\": " << FormatFloat(component.airControl) << ",\n";
    oss << "          \"jumpSpeed\": " << FormatFloat(component.jumpSpeed) << ",\n";
    oss << "          \"gravityScale\": " << FormatFloat(component.gravityScale) << ",\n";
    oss << "          \"maxFallSpeed\": " << FormatFloat(component.maxFallSpeed) << ",\n";
    oss << "          \"collisionLayer\": " << component.collisionLayer << ",\n";
    oss << "          \"mass\": " << FormatFloat(component.mass) << ",\n";
    oss << "          \"pushStrength\": " << FormatFloat(component.pushStrength) << "\n";
    oss << "        }";
    return oss.str();
}

bool ApplyCharacterControllerJson(CharacterControllerComponent& component, const JsonValue& data, std::string& outError)
{
    if (!data.IsObject()) {
        outError = "CharacterController component is not an object.";
        return false;
    }
    auto readFloat = [&](const char* key, float& out) {
        if (const JsonValue* value = data.Find(key)) {
            if (value->IsNumber()) {
                out = static_cast<float>(value->AsNumber());
            }
        }
    };
    if (const JsonValue* enabled = data.Find("enabled")) {
        component.enabled = ParseBoolLike(*enabled, component.enabled);
    }
    readFloat("height", component.height);
    readFloat("radius", component.radius);
    readFloat("crouchedHeight", component.crouchedHeight);
    readFloat("maxSlopeAngleDegrees", component.maxSlopeAngleDegrees);
    readFloat("stepHeight", component.stepHeight);
    readFloat("groundSnapDistance", component.groundSnapDistance);
    readFloat("walkSpeed", component.walkSpeed);
    readFloat("acceleration", component.acceleration);
    readFloat("deceleration", component.deceleration);
    readFloat("airAcceleration", component.airAcceleration);
    readFloat("airControl", component.airControl);
    readFloat("jumpSpeed", component.jumpSpeed);
    readFloat("gravityScale", component.gravityScale);
    readFloat("maxFallSpeed", component.maxFallSpeed);
    readFloat("mass", component.mass);
    readFloat("pushStrength", component.pushStrength);
    if (const JsonValue* layer = data.Find("collisionLayer")) {
        if (layer->IsNumber()) {
            component.collisionLayer = static_cast<std::uint16_t>(std::max(0.0, layer->AsNumber()));
        }
    }
    return true;
}

std::string WriteCharacterComponentJson(const CharacterComponent& character)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "        \"Character\": {\n";
    oss << "          \"characterPath\": \""
        << EscapeString(NormalizeAssetPathForSave(character.characterPath)) << "\",\n";
    oss << "          \"defaultAnimation\": \"" << EscapeString(character.defaultAnimation)
        << "\",\n";
    oss << "          \"playbackSpeed\": " << FormatFloat(character.playbackSpeed) << ",\n";
    oss << "          \"runController\": " << (character.runController ? "true" : "false") << ",\n";
    oss << "          \"controllerScriptPath\": \""
        << EscapeString(NormalizeAssetPathForSave(character.controllerScriptPath)) << "\",\n";
    oss << "          \"controllerVariables\": {";
    size_t varIndex = 0;
    for (const auto& [key, value] : character.controllerVariables) {
        if (varIndex++) {
            oss << ", ";
        }
        oss << "\"" << EscapeString(key) << "\": ";
        WritePropertyValue(oss, value);
    }
    oss << "},\n";
    oss << "          \"authoredControllerVariables\": ";
    WriteStringSet(oss, character.authoredControllerVariables);
    oss << ",\n";
    oss << "          \"materialOverrides\": [";
    for (size_t i = 0; i < character.materialOverrides.size(); ++i) {
        if (i) {
            oss << ", ";
        }
        oss << "\"" << EscapeString(NormalizeAssetPathForSave(character.materialOverrides[i]))
            << "\"";
    }
    oss << "]\n";
    oss << "        }";
    return oss.str();
}

bool ApplyCharacterJson(CharacterComponent& character, const JsonValue& data, std::string& outError)
{
    if (!data.IsObject()) {
        outError = "Character component is not an object.";
        return false;
    }
    if (const JsonValue* pathValue = data.Find("characterPath")) {
        if (pathValue->IsString()) {
            character.characterPath = pathValue->AsString();
        }
    }
    if (const JsonValue* animValue = data.Find("defaultAnimation")) {
        if (animValue->IsString()) {
            character.defaultAnimation = animValue->AsString();
        }
    }
    if (const JsonValue* speedValue = data.Find("playbackSpeed")) {
        if (speedValue->IsNumber()) {
            character.playbackSpeed = static_cast<float>(speedValue->AsNumber());
        }
    }
    if (const JsonValue* runValue = data.Find("runController")) {
        character.runController = ParseBoolLike(*runValue, character.runController);
    }
    if (const JsonValue* scriptValue = data.Find("controllerScriptPath")) {
        if (scriptValue->IsString()) {
            character.controllerScriptPath = scriptValue->AsString();
        }
    }
    if (const JsonValue* varsValue = data.Find("controllerVariables")) {
        if (varsValue->IsObject()) {
            character.controllerVariables.clear();
            for (const auto& [key, value] : varsValue->AsObject()) {
                character.controllerVariables.emplace(key, ParsePropertyValue(value));
            }
        }
    }
    if (const JsonValue* authoredValue = data.Find("authoredControllerVariables")) {
        character.authoredControllerVariablesInitialized = true;
        character.authoredControllerVariables.clear();
        if (authoredValue->IsArray()) {
            for (const JsonValue& value : authoredValue->AsArray()) {
                if (value.IsString()) {
                    character.authoredControllerVariables.insert(value.AsString());
                }
            }
        }
    }
    if (const JsonValue* materialsValue = data.Find("materialOverrides")) {
        if (materialsValue->IsArray()) {
            character.materialOverrides.clear();
            for (const JsonValue& value : materialsValue->AsArray()) {
                if (value.IsString()) {
                    character.materialOverrides.push_back(value.AsString());
                }
            }
        }
    }
    return true;
}

PropertyValue ParsePropertyValue(const JsonValue& value)
{
    if (value.IsBool()) {
        return PropertyValue(value.AsBool());
    }
    if (value.IsNumber()) {
        return PropertyValue(value.AsNumber());
    }
    if (value.IsString()) {
        return PropertyValue(value.AsString());
    }
    if (value.IsArray()) {
        const auto& arr = value.AsArray();
        if (arr.size() == 2 && arr[0].IsNumber() && arr[1].IsNumber()) {
            return PropertyValue(Vec2(static_cast<float>(arr[0].AsNumber()), static_cast<float>(arr[1].AsNumber())));
        }
        if (arr.size() == 3 && arr[0].IsNumber() && arr[1].IsNumber() && arr[2].IsNumber()) {
            return PropertyValue(Vec3(static_cast<float>(arr[0].AsNumber()), static_cast<float>(arr[1].AsNumber()), static_cast<float>(arr[2].AsNumber())));
        }
        if (arr.size() == 4 && arr[0].IsNumber() && arr[1].IsNumber() && arr[2].IsNumber() && arr[3].IsNumber()) {
            return PropertyValue(Vec4(static_cast<float>(arr[0].AsNumber()), static_cast<float>(arr[1].AsNumber()), static_cast<float>(arr[2].AsNumber()), static_cast<float>(arr[3].AsNumber())));
        }
        PropertyValue::Array arrayOut;
        arrayOut.reserve(arr.size());
        for (const auto& entry : arr) {
            arrayOut.push_back(ParsePropertyValue(entry));
        }
        return PropertyValue(std::move(arrayOut));
    }
    if (value.IsObject()) {
        PropertyValue::Object obj;
        for (const auto& [key, entry] : value.AsObject()) {
            obj.emplace(key, ParsePropertyValue(entry));
        }
        return PropertyValue(std::move(obj));
    }
    return PropertyValue();
}

void WritePrefabInstance(std::ostream& stream, const PrefabInstanceComponent& instance)
{
    stream << "    {\n";
    stream << "      \"prefabPath\": \"" << EscapeString(instance.prefabPath) << "\",\n";
    stream << "      \"prefabGuid\": \"" << EscapeString(instance.prefabGuid) << "\",\n";
    stream << "      \"instanceId\": \"" << EscapeString(instance.instanceId) << "\",\n";
    stream << "      \"rootNodeId\": \"" << EscapeString(instance.rootNodeId) << "\",\n";
    stream << "      \"parent\": ";
    if (instance.parent != entt::null) {
        stream << static_cast<uint32_t>(instance.parent) << ",\n";
    } else {
        stream << "null,\n";
    }

    if (instance.hasRootTransform) {
        stream << "      \"rootTransform\": {\n";
        stream << "        \"position\": " << FormatVec3(instance.rootTransform.position) << ",\n";
        stream << "        \"rotation\": " << FormatVec3(instance.rootTransform.rotation) << ",\n";
        stream << "        \"scale\": " << FormatVec3(instance.rootTransform.scale) << "\n";
        stream << "      },\n";
    }

    if (instance.missingPrefab) {
        stream << "      \"missingPrefab\": true,\n";
        stream << "      \"missingReason\": \"" << EscapeString(instance.missingReason) << "\",\n";
    }

    stream << "      \"overrides\": {\n";
    size_t nodeIndex = 0;
    for (const auto& [nodeId, nodeOverride] : instance.overrides) {
        stream << "        \"" << EscapeString(nodeId) << "\": {\n";
        bool wroteSection = false;

        if (nodeOverride.hasTransform) {
            stream << "          \"transform\": {\n";
            stream << "            \"position\": " << FormatVec3(nodeOverride.transform.position) << ",\n";
            stream << "            \"rotation\": " << FormatVec3(nodeOverride.transform.rotation) << ",\n";
            stream << "            \"scale\": " << FormatVec3(nodeOverride.transform.scale) << "\n";
            stream << "          }";
            wroteSection = true;
        }

        if (!nodeOverride.componentOverrides.empty()) {
            if (wroteSection) {
                stream << ",\n";
            }
            stream << "          \"components\": {\n";
            size_t compIndex = 0;
            for (const auto& [componentType, componentOverride] : nodeOverride.componentOverrides) {
                stream << "            \"" << EscapeString(componentType) << "\": {";
                size_t propIndex = 0;
                for (const auto& [propKey, propValue] : componentOverride.properties) {
                    if (propIndex++) {
                        stream << ", ";
                    }
                    stream << "\"" << EscapeString(propKey) << "\": ";
                    WritePropertyValue(stream, propValue);
                }
                stream << "}";
                if (compIndex + 1 < nodeOverride.componentOverrides.size()) {
                    stream << ",";
                }
                stream << "\n";
                ++compIndex;
            }
            stream << "          }";
            wroteSection = true;
        }

        if (!nodeOverride.scriptVars.empty()) {
            if (wroteSection) {
                stream << ",\n";
            }
            stream << "          \"scriptVars\": {";
            size_t varIndex = 0;
            for (const auto& [key, value] : nodeOverride.scriptVars) {
                if (varIndex++) {
                    stream << ", ";
                }
                stream << "\"" << EscapeString(key) << "\": ";
                WritePropertyValue(stream, value);
            }
            stream << "}";
        }

        stream << "\n        }";
        if (nodeIndex + 1 < instance.overrides.size()) {
            stream << ",";
        }
        stream << "\n";
        ++nodeIndex;
    }
    stream << "      }\n";
    stream << "    }";
}

const char* LightTypeToString(LightType type)
{
    switch (type) {
    case LightType::Directional:
        return "Directional";
    case LightType::Spot:
        return "Spot";
    case LightType::Point:
    default:
        return "Point";
    }
}

const char* PhysicsShapeTypeToString(PhysicsShapeType type)
{
    switch (type) {
    case PhysicsShapeType::Box:
        return "Box";
    case PhysicsShapeType::Sphere:
        return "Sphere";
    case PhysicsShapeType::Capsule:
        return "Capsule";
    case PhysicsShapeType::Cylinder:
        return "Cylinder";
    case PhysicsShapeType::Mesh:
        return "Mesh";
    default:
        return "Box";
    }
}

const char* BackgroundTypeToString(EnvironmentComponent::BackgroundType type)
{
    switch (type) {
    case EnvironmentComponent::BackgroundType::HDRI:
        return "HDRI";
    case EnvironmentComponent::BackgroundType::SolidColor:
    default:
        return "SolidColor";
    }
}

static bool ParseVec3(const JsonValue& value, Vec3& out)
{
    if (!value.IsArray() || value.AsArray().size() != 3) {
        return false;
    }
    const auto& arr = value.AsArray();
    if (!arr[0].IsNumber() || !arr[1].IsNumber() || !arr[2].IsNumber()) {
        return false;
    }
    out.x = static_cast<float>(arr[0].AsNumber());
    out.y = static_cast<float>(arr[1].AsNumber());
    out.z = static_cast<float>(arr[2].AsNumber());
    return true;
}

static bool ParseBoolLike(const JsonValue& value, bool fallback)
{
    if (value.IsBool()) {
        return value.AsBool();
    }
    if (value.IsNumber()) {
        return value.AsNumber() != 0.0;
    }
    if (value.IsString()) {
        std::string text = value.AsString();
        for (char& ch : text) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (text == "true" || text == "1") {
            return true;
        }
        if (text == "false" || text == "0") {
            return false;
        }
    }
    return fallback;
}

static std::string ToLowerCase(const std::string& s)
{
    std::string result = s;
    for (char& ch : result) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return result;
}

// Normalize an asset path for portable serialization via AssetPathPolicy.
// Absolute in-project paths become assets/... /.engine/... / scripts/... keys.
static std::string NormalizeAssetPathForSave(const std::string& rawPath)
{
    if (rawPath.empty() || rawPath.rfind("builtin://", 0) == 0) {
        return rawPath;
    }

    const std::string portable =
        Hephaestus::AssetPathPolicy::ToPortableKey(std::filesystem::path{}, rawPath);
    if (Hephaestus::AssetPathPolicy::IsPortableKey(portable) ||
        !std::filesystem::path(portable).is_absolute()) {
        return portable;
    }

    // Fallback: return just the filename to avoid baking in machine-specific paths
    return std::filesystem::path(rawPath).filename().generic_string();
}

static EnvironmentComponent::BackgroundType ParseBackgroundType(const JsonValue& value)
{
    if (!value.IsString()) {
        return EnvironmentComponent::BackgroundType::SolidColor;
    }
    std::string typeStr = ToLowerCase(value.AsString());
    if (typeStr == "hdri" || typeStr == "hdr") {
        return EnvironmentComponent::BackgroundType::HDRI;
    }
    if (typeStr == "solidcolor" || typeStr == "solid" || typeStr == "color") {
        return EnvironmentComponent::BackgroundType::SolidColor;
    }
    // Unknown type string — caller will use fallback heuristic
    return EnvironmentComponent::BackgroundType::SolidColor;
}

// Check if a type string is explicitly provided vs unknown/missing
static bool IsKnownBackgroundType(const JsonValue& value)
{
    if (!value.IsString()) return false;
    std::string typeStr = ToLowerCase(value.AsString());
    return typeStr == "hdri" || typeStr == "hdr" || typeStr == "solidcolor" || typeStr == "solid" || typeStr == "color";
}

static LightType ParseLightType(const JsonValue& value, std::string& errorOut)
{
    if (!value.IsString()) {
        errorOut = "Light component missing type string.";
        return LightType::Point;
    }
    std::string typeStr = value.AsString();
    if (typeStr == "Directional") {
        return LightType::Directional;
    }
    if (typeStr == "Spot") {
        return LightType::Spot;
    }
    if (typeStr == "Point") {
        return LightType::Point;
    }
    errorOut = "Unknown light type: " + typeStr;
    return LightType::Point;
}

static PhysicsShapeType ParsePhysicsShapeType(const JsonValue& value, std::string& errorOut)
{
    if (!value.IsString()) {
        errorOut = "PhysicsShape component missing type string.";
        return PhysicsShapeType::Box;
    }

    std::string typeStr = value.AsString();
    if (typeStr == "Box") {
        return PhysicsShapeType::Box;
    }
    if (typeStr == "Sphere") {
        return PhysicsShapeType::Sphere;
    }
    if (typeStr == "Capsule") {
        return PhysicsShapeType::Capsule;
    }
    if (typeStr == "Cylinder") {
        return PhysicsShapeType::Cylinder;
    }
    if (typeStr == "Mesh") {
        return PhysicsShapeType::Mesh;
    }

    errorOut = "Unknown physics shape type: " + typeStr;
    return PhysicsShapeType::Box;
}
} // namespace

SceneSerializer::SceneSerializer(const Scene& scene)
    : m_Scene(scene)
{
}

static std::string WriteProbeVolumeJson(const ProbeVolumeComponent& volume)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "        \"ProbeVolume\": {\n";
    oss << "          \"enabled\": " << (volume.enabled ? "true" : "false") << ",\n";
    oss << "          \"size\": " << FormatVec3(volume.size) << ",\n";
    oss << "          \"probeCounts\": [" << volume.probeCountX << ", " << volume.probeCountY << ", "
        << volume.probeCountZ << "],\n";
    oss << "          \"intensity\": " << FormatFloat(volume.intensity) << "\n";
    oss << "        }";
    return oss.str();
}

static bool ApplyProbeVolumeJson(ProbeVolumeComponent& volume, const JsonValue& data, std::string& outError)
{
    if (!data.IsObject()) {
        outError = "ProbeVolume component is not an object.";
        return false;
    }
    if (const JsonValue* enabled = data.Find("enabled")) {
        volume.enabled = ParseBoolLike(*enabled, volume.enabled);
    }
    if (const JsonValue* size = data.Find("size")) {
        ParseVec3(*size, volume.size);
    }
    if (const JsonValue* counts = data.Find("probeCounts")) {
        int* targets[] = {&volume.probeCountX, &volume.probeCountY, &volume.probeCountZ};
        if (counts->IsArray() && counts->AsArray().size() == 3) {
            for (size_t i = 0; i < 3; ++i) {
                if (counts->AsArray()[i].IsNumber()) {
                    *targets[i] = std::max(1, static_cast<int>(counts->AsArray()[i].AsNumber()));
                }
            }
        }
    }
    if (const JsonValue* intensity = data.Find("intensity")) {
        if (intensity->IsNumber()) {
            volume.intensity = static_cast<float>(intensity->AsNumber());
        }
    }
    return true;
}

bool SceneSerializer::Serialize(const std::filesystem::path& filepath) const
{
    std::string ignoredError;
    return Serialize(filepath, ignoredError);
}

bool SceneSerializer::Serialize(const std::filesystem::path& filepath, std::string& outError) const
{
    namespace fs = std::filesystem;

    outError.clear();

    std::error_code dirErr;
    fs::path parent = filepath.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, dirErr);
        if (dirErr) {
            outError = "unable to create directory '" + parent.string() + "': " + dirErr.message();
            return false;
        }
    }

    std::ofstream stream(filepath, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        outError = "unable to open '" + filepath.string() + "' for writing";
        return false;
    }
    stream.imbue(std::locale::classic());

    const entt::registry& registry = m_Scene.GetRegistry();
    std::vector<entt::entity> entities;
    auto taggedEntities = registry.view<TagComponent>();
    for (entt::entity entity : taggedEntities) {
        if (registry.any_of<PrefabNodeComponent, EditorOnlyComponent>(entity)) {
            continue;
        }
        entities.push_back(entity);
    }

    std::sort(entities.begin(), entities.end(), [&](entt::entity a, entt::entity b) {
        const TagComponent* tagA = registry.try_get<TagComponent>(a);
        const TagComponent* tagB = registry.try_get<TagComponent>(b);

        std::string nameA = tagA ? tagA->tag : std::string();
        std::string nameB = tagB ? tagB->tag : std::string();
        if (nameA == nameB) {
            return static_cast<uint32_t>(a) < static_cast<uint32_t>(b);
        }
        return nameA < nameB;
    });

    stream << "{\n";
    stream << "  \"schema\": \"HephaestusScene\",\n";
    stream << "  \"version\": 1,\n";
    stream << "  \"name\": \"" << EscapeString(m_Scene.GetName()) << "\",\n";
    stream << "  \"entityCount\": " << entities.size() << ",\n";

    std::vector<entt::entity> prefabInstances;
    auto prefabView = registry.view<PrefabInstanceComponent>();
    for (entt::entity entity : prefabView) {
        prefabInstances.push_back(entity);
    }
    std::sort(prefabInstances.begin(), prefabInstances.end(), [](entt::entity a, entt::entity b) {
        return static_cast<uint32_t>(a) < static_cast<uint32_t>(b);
    });

    stream << "  \"prefabInstanceCount\": " << prefabInstances.size() << ",\n";
    stream << "  \"prefabInstances\": [\n";
    for (size_t instanceIndex = 0; instanceIndex < prefabInstances.size(); ++instanceIndex) {
        entt::entity handle = prefabInstances[instanceIndex];
        const PrefabInstanceComponent* instance = registry.try_get<PrefabInstanceComponent>(handle);
        if (!instance) {
            continue;
        }

        WritePrefabInstance(stream, *instance);
        if (instanceIndex + 1 < prefabInstances.size()) {
            stream << ",\n";
        } else {
            stream << "\n";
        }
    }
    stream << "  ],\n";

    stream << "  \"entities\": [\n";

    for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
        entt::entity handle = entities[entityIndex];

        const TagComponent* tag = registry.try_get<TagComponent>(handle);
    const TransformComponent* transform = registry.try_get<TransformComponent>(handle);
    const MeshRendererComponent* mesh = registry.try_get<MeshRendererComponent>(handle);
    const MeshColliderComponent* collider = registry.try_get<MeshColliderComponent>(handle);
        const PhysicsShapeComponent* physicsShape = registry.try_get<PhysicsShapeComponent>(handle);
        const RigidBodyComponent* rigidBody = registry.try_get<RigidBodyComponent>(handle);
        const MaterialComponent* material = registry.try_get<MaterialComponent>(handle);
        const LightComponent* light = registry.try_get<LightComponent>(handle);
        const CameraComponent* camera = registry.try_get<CameraComponent>(handle);
        const ScriptComponent* scriptComponent = registry.try_get<ScriptComponent>(handle);
        const LuaScriptComponent* script = registry.try_get<LuaScriptComponent>(handle);
        const AudioSourceComponent* audio = registry.try_get<AudioSourceComponent>(handle);
        const CharacterComponent* character = registry.try_get<CharacterComponent>(handle);
        const CharacterControllerComponent* characterController = registry.try_get<CharacterControllerComponent>(handle);
        const ProbeVolumeComponent* probeVolume = registry.try_get<ProbeVolumeComponent>(handle);
        const SelectionComponent* selection = registry.try_get<SelectionComponent>(handle);
        const FolderComponent* folder = registry.try_get<FolderComponent>(handle);
        const EnvironmentComponent* environment = registry.try_get<EnvironmentComponent>(handle);

        stream << "    {\n";
        stream << "      \"id\": " << static_cast<uint32_t>(handle) << ",\n";
        stream << "      \"name\": \"" << EscapeString(tag ? tag->tag : std::string("Entity")) << "\",\n";

        stream << "      \"parent\": ";
        if (transform && transform->parent != entt::null) {
            stream << static_cast<uint32_t>(transform->parent) << ",\n";
        } else {
            stream << "null,\n";
        }

    std::vector<std::string> componentBlocks;
    componentBlocks.reserve(8);

        if (transform) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Transform\": {\n";
            oss << "          \"position\": " << FormatVec3(transform->position) << ",\n";
            oss << "          \"rotation\": " << FormatVec3(transform->rotation) << ",\n";
            oss << "          \"scale\": " << FormatVec3(transform->scale) << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (mesh && !IsRuntimeGeneratedVisual(mesh)) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"MeshRenderer\": {\n";
            oss << "          \"visible\": " << (mesh->visible ? "true" : "false") << ",\n";
            oss << "          \"hideInGame\": " << (mesh->hideInGame ? "true" : "false") << ",\n";
            oss << "          \"showHiddenOutline\": " << (mesh->showHiddenOutline ? "true" : "false") << ",\n";
            oss << "          \"mesh\": ";
            if (mesh->HasValidMesh()) {
                oss << static_cast<uint64_t>(mesh->meshHandle) << ",\n";
            } else {
                oss << "null,\n";
            }
            oss << "          \"meshSource\": ";
            if (!mesh->meshAssetPath.empty()) {
                oss << "\"" << EscapeString(NormalizeAssetPathForSave(mesh->meshAssetPath)) << "\"";
            } else {
                oss << "null";
            }
            oss << ",\n";
            oss << "          \"meshImported\": " << (mesh->meshImported ? "true" : "false") << ",\n";
            oss << "          \"submeshCount\": " << mesh->submeshCount << ",\n";
            oss << "          \"materialIndices\": [";
            for (size_t i = 0; i < mesh->materialIndices.size(); ++i) {
                if (i) oss << ", ";
                oss << mesh->materialIndices[i];
            }
            oss << "],\n";
            oss << "          \"appliedMaterialPaths\": [";
            for (size_t i = 0; i < mesh->appliedMaterialPaths.size(); ++i) {
                if (i) oss << ", ";
                oss << "\"" << EscapeString(NormalizeAssetPathForSave(mesh->appliedMaterialPaths[i])) << "\"";
            }
            oss << "],\n";
            oss << "          \"backfaceCulling\": " << (mesh->backfaceCulling ? "true" : "false") << ",\n";
            oss << "          \"localBounds\": {\n";
            oss << "            \"min\": " << FormatVec3(mesh->localBounds.min) << ",\n";
            oss << "            \"max\": " << FormatVec3(mesh->localBounds.max) << "\n";
            oss << "          }\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (collider) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"MeshCollider\": {\n";
            oss << "          \"enabled\": " << (collider->enabled ? "true" : "false") << ",\n";
            oss << "          \"isStatic\": " << (collider->isStatic ? "true" : "false") << ",\n";
            oss << "          \"isTrigger\": " << (collider->isTrigger ? "true" : "false") << ",\n";
            oss << "          \"debugDraw\": " << (collider->debugDraw ? "true" : "false") << ",\n";
            oss << "          \"useComplexCollision\": " << (collider->useComplexCollision ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (physicsShape) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"PhysicsShape\": {\n";
            oss << "          \"type\": \"" << PhysicsShapeTypeToString(physicsShape->type) << "\",\n";
            oss << "          \"extents\": " << FormatVec3(physicsShape->extents) << ",\n";
            oss << "          \"radius\": " << FormatFloat(physicsShape->radius) << ",\n";
            oss << "          \"height\": " << FormatFloat(physicsShape->height) << ",\n";
            oss << "          \"offset\": " << FormatVec3(physicsShape->offset) << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (rigidBody) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"RigidBody\": {\n";
            oss << "          \"mass\": " << FormatFloat(rigidBody->mass) << ",\n";
            oss << "          \"motion\": \"" << rigidBody->MotionTypeName() << "\",\n";
            oss << "          \"layer\": " << rigidBody->layer << ",\n";
            oss << "          \"isStatic\": " << (rigidBody->motion == RigidBodyMotion::Static ? "true" : "false") << ",\n";
            oss << "          \"isTrigger\": " << (rigidBody->isTrigger ? "true" : "false") << ",\n";
            oss << "          \"useGravity\": " << (rigidBody->useGravity ? "true" : "false") << ",\n";
            oss << "          \"freezeRotation\": " << (rigidBody->freezeRotation ? "true" : "false") << ",\n";
            oss << "          \"linearDamping\": " << FormatFloat(rigidBody->linearDamping) << ",\n";
            oss << "          \"angularDamping\": " << FormatFloat(rigidBody->angularDamping) << ",\n";
            oss << "          \"friction\": " << FormatFloat(rigidBody->friction) << ",\n";
            oss << "          \"restitution\": " << FormatFloat(rigidBody->restitution) << ",\n";
            oss << "          \"linearVelocity\": " << FormatVec3(rigidBody->linearVelocity) << ",\n";
            oss << "          \"angularVelocity\": " << FormatVec3(rigidBody->angularVelocity) << ",\n";
            oss << "          \"debugDraw\": " << (rigidBody->debugDraw ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (material) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Material\": {\n";
            oss << "          \"albedo\": " << FormatVec3(material->albedo) << ",\n";
            oss << "          \"metallic\": " << FormatFloat(material->metallic) << ",\n";
            oss << "          \"roughness\": " << FormatFloat(material->roughness) << ",\n";
            oss << "          \"ao\": " << FormatFloat(material->ao) << ",\n";
            oss << "          \"emissive\": " << FormatVec3(material->emissive) << ",\n";
            oss << "          \"emissiveStrength\": " << FormatFloat(material->emissiveStrength) << ",\n";
            oss << "          \"reflectionStrength\": " << FormatFloat(material->reflectionStrength) << ",\n";
            oss << "          \"useAlbedoTexture\": " << (material->useAlbedoTexture ? "true" : "false") << ",\n";
            oss << "          \"albedoTexturePath\": \"" << EscapeString(NormalizeAssetPathForSave(material->albedoTexturePath)) << "\",\n";
            oss << "          \"useNormalTexture\": " << (material->useNormalTexture ? "true" : "false") << ",\n";
            oss << "          \"normalTexturePath\": \"" << EscapeString(NormalizeAssetPathForSave(material->normalTexturePath)) << "\",\n";
            oss << "          \"useRoughnessTexture\": " << (material->useRoughnessTexture ? "true" : "false") << ",\n";
            oss << "          \"roughnessTexturePath\": \"" << EscapeString(NormalizeAssetPathForSave(material->roughnessTexturePath)) << "\",\n";
            oss << "          \"useMetallicTexture\": " << (material->useMetallicTexture ? "true" : "false") << ",\n";
            oss << "          \"metallicTexturePath\": \"" << EscapeString(NormalizeAssetPathForSave(material->metallicTexturePath)) << "\",\n";
            oss << "          \"useAoTexture\": " << (material->useAoTexture ? "true" : "false") << ",\n";
            oss << "          \"aoTexturePath\": \"" << EscapeString(NormalizeAssetPathForSave(material->aoTexturePath)) << "\",\n";
            oss << "          \"useWorldAlignedTexture\": " << (material->useWorldAlignedTexture ? "true" : "false") << ",\n";
            oss << "          \"worldTextureScale\": " << FormatFloat(material->worldTextureScale) << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (light) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Light\": {\n";
            oss << "          \"type\": \"" << LightTypeToString(light->type) << "\",\n";
            oss << "          \"enabled\": " << (light->enabled ? "true" : "false") << ",\n";
            oss << "          \"color\": " << FormatVec3(light->color) << ",\n";
            oss << "          \"intensity\": " << FormatFloat(light->intensity) << ",\n";
            oss << "          \"range\": " << FormatFloat(light->range) << ",\n";
            oss << "          \"attenuation\": {\n";
            oss << "            \"constant\": " << FormatFloat(light->constant) << ",\n";
            oss << "            \"linear\": " << FormatFloat(light->linear) << ",\n";
            oss << "            \"quadratic\": " << FormatFloat(light->quadratic) << "\n";
            oss << "          },\n";
            oss << "          \"spotAngles\": {\n";
            oss << "            \"inner\": " << FormatFloat(light->innerConeAngle) << ",\n";
            oss << "            \"outer\": " << FormatFloat(light->outerConeAngle) << "\n";
            oss << "          },\n";
            oss << "          \"direction\": " << FormatVec3(light->direction) << ",\n";
            oss << "          \"shadow\": {\n";
            oss << "            \"castShadows\": " << (light->castShadows ? "true" : "false") << ",\n";
            oss << "            \"autoFit\": " << (light->shadowAutoFit ? "true" : "false") << ",\n";
            oss << "            \"bias\": " << FormatFloat(light->shadowBias) << ",\n";
            oss << "            \"normalBias\": " << FormatFloat(light->shadowNormalBias) << ",\n";
            oss << "            \"strength\": " << FormatFloat(light->shadowStrength) << ",\n";
            oss << "            \"distance\": " << FormatFloat(light->shadowDistance) << ",\n";
            oss << "            \"extent\": " << FormatFloat(light->shadowExtent) << ",\n";
            oss << "            \"maxDistance\": " << FormatFloat(light->shadowMaxDistance) << "\n";
            oss << "          }\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (camera) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Camera\": {\n";
            oss << "          \"fov\": " << FormatFloat(camera->fov) << ",\n";
            oss << "          \"near\": " << FormatFloat(camera->nearPlane) << ",\n";
            oss << "          \"far\": " << FormatFloat(camera->farPlane) << ",\n";
            oss << "          \"projection\": \"" << (camera->projection == CameraProjection::Orthographic ? "Orthographic" : "Perspective") << "\",\n";
            oss << "          \"isActive\": " << (camera->isActive ? "true" : "false") << ",\n";
            oss << "          \"isPrimary\": " << (camera->isPrimary || camera->isActive ? "true" : "false") << ",\n";
            oss << "          \"showEditorMesh\": " << (camera->showEditorMesh ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (scriptComponent && !scriptComponent->scriptNames.empty()) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Scripts\": {\n";
            oss << "          \"names\": [";
            for (size_t i = 0; i < scriptComponent->scriptNames.size(); ++i) {
                oss << "\"" << EscapeString(scriptComponent->scriptNames[i]) << "\"";
                if (i + 1 < scriptComponent->scriptNames.size()) {
                    oss << ", ";
                }
            }
            oss << "]\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (script) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"LuaScript\": {\n";
            oss << "          \"scriptPath\": \"" << EscapeString(NormalizeAssetPathForSave(script->scriptPath)) << "\",\n";
            oss << "          \"scriptSource\": \"" << EscapeString(script->scriptSource) << "\",\n";
            oss << "          \"functionOnAwake\": \"" << EscapeString(script->functionOnAwake) << "\",\n";
            oss << "          \"functionOnUpdate\": \"" << EscapeString(script->functionOnUpdate) << "\",\n";
            oss << "          \"functionOnDestroy\": \"" << EscapeString(script->functionOnDestroy) << "\",\n";
            oss << "          \"enabled\": " << (script->enabled ? "true" : "false") << ",\n";
            oss << "          \"variables\": {";
            size_t varIndex = 0;
            for (const auto& [key, value] : script->variables) {
                if (varIndex++) {
                    oss << ", ";
                }
                oss << "\"" << EscapeString(key) << "\": ";
                WritePropertyValue(oss, value);
            }
            oss << "},\n";
            oss << "          \"authoredVariables\": ";
            WriteStringSet(oss, script->authoredVariables);
            oss << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (audio) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"AudioSource\": {\n";
            oss << "          \"clipPath\": \"" << EscapeString(NormalizeAssetPathForSave(audio->clipPath)) << "\",\n";
            oss << "          \"enabled\": " << (audio->enabled ? "true" : "false") << ",\n";
            oss << "          \"playOnStart\": " << (audio->playOnStart ? "true" : "false") << ",\n";
            oss << "          \"loop\": " << (audio->loop ? "true" : "false") << ",\n";
            oss << "          \"volume\": " << FormatFloat(audio->volume) << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (character) {
            componentBlocks.push_back(WriteCharacterComponentJson(*character));
        }

        if (characterController) {
            componentBlocks.push_back(WriteCharacterControllerJson(*characterController));
        }

        if (probeVolume) {
            componentBlocks.push_back(WriteProbeVolumeJson(*probeVolume));
        }

        if (selection) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Selection\": {\n";
            oss << "          \"selected\": " << (selection->selected ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (folder) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Folder\": {\n";
            oss << "          \"isFolder\": " << (folder->isFolder ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (environment) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Environment\": {\n";
            oss << "          \"type\": \"" << BackgroundTypeToString(environment->type) << "\",\n";
            oss << "          \"solidColor\": " << FormatVec3(environment->solidColor) << ",\n";
            oss << "          \"hdriPath\": \"" << EscapeString(NormalizeAssetPathForSave(environment->hdriPath)) << "\",\n";
            oss << "          \"hdriIntensity\": " << FormatFloat(environment->hdriIntensity) << ",\n";
            oss << "          \"rotationYaw\": " << FormatFloat(environment->rotationYaw) << ",\n";
            oss << "          \"useAsIBL\": " << (environment->useAsIBL ? "true" : "false") << ",\n";
            oss << "          \"reflectionIntensity\": " << FormatFloat(environment->reflectionIntensity) << ",\n";
            oss << "          \"enableRenderDistance\": " << (environment->enableRenderDistance ? "true" : "false") << ",\n";
            oss << "          \"renderDistance\": " << FormatFloat(environment->renderDistance) << ",\n";
            oss << "          \"infiniteRenderDistance\": " << (environment->infiniteRenderDistance ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (componentBlocks.empty()) {
            stream << "      \"components\": {}\n";
        } else {
            stream << "      \"components\": {\n";
            for (size_t compIndex = 0; compIndex < componentBlocks.size(); ++compIndex) {
                stream << componentBlocks[compIndex];
                if (compIndex + 1 < componentBlocks.size()) {
                    stream << ",\n";
                } else {
                    stream << "\n";
                }
            }
            stream << "      }\n";
        }

        stream << "    }";
        if (entityIndex + 1 < entities.size()) {
            stream << ",\n";
        } else {
            stream << "\n";
        }
    }

    stream << "  ]\n";
    stream << "}\n";

    stream.flush();
    if (!stream.good()) {
        outError = "failed while writing '" + filepath.string() + "'";
        return false;
    }

    return true;
}

bool SceneSerializer::Deserialize(Scene& scene, const std::filesystem::path& filepath)
{
    std::string ignored;
    return Deserialize(scene, filepath, ignored);
}

bool SceneSerializer::Deserialize(Scene& scene, const std::filesystem::path& filepath, std::string& outError)
{
    namespace fs = std::filesystem;

    outError.clear();

    std::ifstream stream(filepath, std::ios::binary);
    if (!stream.is_open()) {
        outError = "unable to open '" + filepath.string() + "'";
        return false;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        outError = "failed while reading '" + filepath.string() + "'";
        return false;
    }
    std::string contents = buffer.str();

    JsonParser parser(contents);
    JsonValue root;
    if (!parser.Parse(root, outError)) {
        return false;
    }

    if (!root.IsObject()) {
        outError = "scene file must begin with an object";
        return false;
    }

    const JsonValue* nameValue = root.Find("name");
    std::string sceneName;
    if (nameValue && nameValue->IsString()) {
        sceneName = nameValue->AsString();
    } else {
        sceneName = filepath.stem().string();
    }

    const JsonValue* entitiesValue = root.Find("entities");
    if (!entitiesValue || !entitiesValue->IsArray()) {
        outError = "scene file missing entities array";
        return false;
    }

    scene.Clear();
    scene.SetName(sceneName);

    // Parse root-level "environment" block (legacy format) into an Environment entity
    if (const JsonValue* rootEnvObj = root.Find("environment")) {
        if (rootEnvObj->IsObject()) {
            Entity envEntity = scene.CreateEntity("Environment");
            auto& env = envEntity.AddComponent<EnvironmentComponent>();

            if (const JsonValue* v = rootEnvObj->Find("ambientColor")) {
                ParseVec3(*v, env.solidColor);
            }
            if (const JsonValue* v = rootEnvObj->Find("ambientIntensity")) {
                if (v->IsNumber()) {
                    env.hdriIntensity = static_cast<float>(v->AsNumber());
                }
            }

            // Check for skybox/HDRI path
            for (const char* key : {"skybox", "hdriPath", "skyboxPath"}) {
                if (const JsonValue* v = rootEnvObj->Find(key)) {
                    if (v->IsString() && !v->AsString().empty()) {
                        env.hdriPath = v->AsString();
                        env.type = EnvironmentComponent::BackgroundType::HDRI;
                        break;
                    }
                }
            }

            if (const JsonValue* v = rootEnvObj->Find("type")) {
                if (IsKnownBackgroundType(*v)) {
                    env.type = ParseBackgroundType(*v);
                }
            }
        }
    }

    std::unordered_map<uint32_t, entt::entity> idToEntity;
    std::vector<std::pair<entt::entity, uint32_t>> pendingParents;
    entt::entity selectedHandle = entt::null;

    for (const JsonValue& entityValue : entitiesValue->AsArray()) {
        if (!entityValue.IsObject()) {
            outError = "entity entry must be an object";
            return false;
        }

        const JsonValue* idValue = entityValue.Find("id");
        if (!idValue || !idValue->IsNumber()) {
            outError = "entity missing numeric id";
            return false;
        }
        double rawId = idValue->AsNumber();
        if (rawId < 0.0 || rawId > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
            outError = "entity id out of range";
            return false;
        }
        uint32_t entityId = static_cast<uint32_t>(rawId);

        const JsonValue* nameField = entityValue.Find("name");
        std::string entityName = nameField && nameField->IsString() ? nameField->AsString() : std::string("Entity");
        // Serialized ids are only scene-local keys for parent/component links.
        // Reusing them as live EnTT handles would make stale Lua userdata from
        // a prior reconstruction valid again when an authored id is repeated.
        Entity entity = scene.CreateEntity(entityName);
        idToEntity[entityId] = static_cast<entt::entity>(entity);

        const JsonValue* parentValue = entityValue.Find("parent");
        if (parentValue && parentValue->IsNumber()) {
            double rawParent = parentValue->AsNumber();
            if (rawParent >= 0.0 && rawParent <= static_cast<double>(std::numeric_limits<uint32_t>::max())) {
                pendingParents.emplace_back(static_cast<entt::entity>(entity), static_cast<uint32_t>(rawParent));
            }
        }

        const JsonValue* components = entityValue.Find("components");
        if (!components || !components->IsObject()) {
            continue;
        }

        if (const JsonValue* transformObj = components->Find("Transform")) {
            if (!transformObj->IsObject()) {
                outError = "Transform component is not an object.";
                return false;
            }
            auto& transform = entity.GetComponent<TransformComponent>();
            if (const JsonValue* posValue = transformObj->Find("position")) {
                ParseVec3(*posValue, transform.position);
            }
            if (const JsonValue* rotValue = transformObj->Find("rotation")) {
                ParseVec3(*rotValue, transform.rotation);
            }
            if (const JsonValue* scaleValue = transformObj->Find("scale")) {
                ParseVec3(*scaleValue, transform.scale);
            }
        }

        if (const JsonValue* meshObj = components->Find("MeshRenderer")) {
            if (!meshObj->IsObject()) {
                outError = "MeshRenderer component is not an object.";
                return false;
            }
            auto& mesh = entity.AddComponent<MeshRendererComponent>();
            if (const JsonValue* visibleValue = meshObj->Find("visible")) {
                mesh.visible = visibleValue->IsBool() ? visibleValue->AsBool() : mesh.visible;
            }
            if (const JsonValue* hideInGameValue = meshObj->Find("hideInGame")) {
                mesh.hideInGame = ParseBoolLike(*hideInGameValue, mesh.hideInGame);
            }
            if (const JsonValue* showHiddenOutlineValue = meshObj->Find("showHiddenOutline")) {
                mesh.showHiddenOutline = ParseBoolLike(*showHiddenOutlineValue, mesh.showHiddenOutline);
            }
            // The "mesh" field in the scene file stores a runtime mesh handle
            // index that is only valid for the session in which the scene was
            // saved.  Across sessions the mesh library is rebuilt from scratch
            // so the old index is meaningless (and using it causes meshes to
            // swap or disappear).  Always start with InvalidMesh here —
            // rebuildSceneMeshesFromComponents() will resolve the correct
            // handle from meshAssetPath / "meshSource".
            mesh.meshHandle = MeshRendererComponent::InvalidMesh;
            if (const JsonValue* meshSourceValue = meshObj->Find("meshSource")) {
                if (meshSourceValue->IsString()) {
                    mesh.meshAssetPath = meshSourceValue->AsString();
                }
            }
            if (const JsonValue* meshImportedValue = meshObj->Find("meshImported")) {
                if (meshImportedValue->IsBool()) {
                    mesh.meshImported = meshImportedValue->AsBool();
                } else if (meshImportedValue->IsNumber()) {
                    mesh.meshImported = meshImportedValue->AsNumber() != 0.0;
                }
            }
            if (const JsonValue* materialIndicesValue = meshObj->Find("materialIndices")) {
                if (materialIndicesValue->IsArray()) {
                    mesh.materialIndices.clear();
                    for (const auto& entry : materialIndicesValue->AsArray()) {
                        if (entry.IsNumber()) {
                            mesh.materialIndices.push_back(static_cast<int>(entry.AsNumber()));
                        }
                    }
                }
            }
            if (const JsonValue* submeshCountValue = meshObj->Find("submeshCount")) {
                if (submeshCountValue->IsNumber()) {
                    mesh.submeshCount = static_cast<int>(submeshCountValue->AsNumber());
                }
            }
            if (const JsonValue* appliedPathsValue = meshObj->Find("appliedMaterialPaths")) {
                if (appliedPathsValue->IsArray()) {
                    mesh.appliedMaterialPaths.clear();
                    for (const auto& entry : appliedPathsValue->AsArray()) {
                        if (entry.IsString()) {
                            mesh.appliedMaterialPaths.push_back(entry.AsString());
                        }
                    }
                }
            }
            if (const JsonValue* backfaceValue = meshObj->Find("backfaceCulling")) {
                if (backfaceValue->IsBool()) {
                    mesh.backfaceCulling = backfaceValue->AsBool();
                } else if (backfaceValue->IsNumber()) {
                    mesh.backfaceCulling = backfaceValue->AsNumber() != 0.0;
                }
            }
            if (const JsonValue* boundsObj = meshObj->Find("localBounds")) {
                if (boundsObj->IsObject()) {
                    if (const JsonValue* minValue = boundsObj->Find("min")) {
                        ParseVec3(*minValue, mesh.localBounds.min);
                    }
                    if (const JsonValue* maxValue = boundsObj->Find("max")) {
                        ParseVec3(*maxValue, mesh.localBounds.max);
                    }
                }
            }
        }

        if (const JsonValue* colliderObj = components->Find("MeshCollider")) {
            if (!colliderObj->IsObject()) {
                outError = "MeshCollider component is not an object.";
                return false;
            }
            auto& collider = entity.AddComponent<MeshColliderComponent>();
            if (const JsonValue* enabledValue = colliderObj->Find("enabled")) {
                collider.enabled = ParseBoolLike(*enabledValue, collider.enabled);
            }
            if (const JsonValue* staticValue = colliderObj->Find("isStatic")) {
                collider.isStatic = ParseBoolLike(*staticValue, collider.isStatic);
            }
            if (const JsonValue* triggerValue = colliderObj->Find("isTrigger")) {
                collider.isTrigger = ParseBoolLike(*triggerValue, collider.isTrigger);
            }
            if (const JsonValue* debugValue = colliderObj->Find("debugDraw")) {
                collider.debugDraw = ParseBoolLike(*debugValue, collider.debugDraw);
            }
            if (const JsonValue* complexValue = colliderObj->Find("useComplexCollision")) {
                collider.useComplexCollision = ParseBoolLike(*complexValue, collider.useComplexCollision);
            }
        }

        if (const JsonValue* shapeObj = components->Find("PhysicsShape")) {
            if (!shapeObj->IsObject()) {
                outError = "PhysicsShape component is not an object.";
                return false;
            }

            PhysicsShapeComponent& shape = entity.HasComponent<PhysicsShapeComponent>()
                ? entity.GetComponent<PhysicsShapeComponent>()
                : entity.AddComponent<PhysicsShapeComponent>();

            if (const JsonValue* typeValue = shapeObj->Find("type")) {
                std::string parseError;
                shape.type = ParsePhysicsShapeType(*typeValue, parseError);
                if (!parseError.empty()) {
                    outError = parseError;
                    return false;
                }
            }
            if (const JsonValue* extentsValue = shapeObj->Find("extents")) {
                ParseVec3(*extentsValue, shape.extents);
            }
            if (const JsonValue* radiusValue = shapeObj->Find("radius")) {
                if (radiusValue->IsNumber()) {
                    shape.radius = static_cast<float>(radiusValue->AsNumber());
                }
            }
            if (const JsonValue* heightValue = shapeObj->Find("height")) {
                if (heightValue->IsNumber()) {
                    shape.height = static_cast<float>(heightValue->AsNumber());
                }
            }
            if (const JsonValue* offsetValue = shapeObj->Find("offset")) {
                ParseVec3(*offsetValue, shape.offset);
            }
        }

        if (const JsonValue* rigidBodyObj = components->Find("RigidBody")) {
            if (!rigidBodyObj->IsObject()) {
                outError = "RigidBody component is not an object.";
                return false;
            }

            RigidBodyComponent& rigidBody = entity.HasComponent<RigidBodyComponent>()
                ? entity.GetComponent<RigidBodyComponent>()
                : entity.AddComponent<RigidBodyComponent>();

            if (const JsonValue* massValue = rigidBodyObj->Find("mass")) {
                if (massValue->IsNumber()) {
                    rigidBody.mass = std::max(0.0f, static_cast<float>(massValue->AsNumber()));
                }
            }
            if (const JsonValue* staticValue = rigidBodyObj->Find("isStatic")) {
                rigidBody.isStatic = ParseBoolLike(*staticValue, rigidBody.isStatic);
            }
            bool motionExplicit = false;
            if (const JsonValue* motionValue = rigidBodyObj->Find("motion")) {
                if (motionValue->IsString()) {
                    const std::string& motionName = motionValue->AsString();
                    if (motionName == "static") {
                        rigidBody.motion = RigidBodyMotion::Static;
                        motionExplicit = true;
                    } else if (motionName == "kinematic") {
                        rigidBody.motion = RigidBodyMotion::Kinematic;
                        motionExplicit = true;
                    } else if (motionName == "dynamic") {
                        rigidBody.motion = RigidBodyMotion::Dynamic;
                        motionExplicit = true;
                    }
                }
            }
            if (const JsonValue* layerValue = rigidBodyObj->Find("layer")) {
                if (layerValue->IsNumber()) {
                    rigidBody.layer = static_cast<std::uint16_t>(std::max(0.0, layerValue->AsNumber()));
                }
            }
            if (const JsonValue* triggerValue = rigidBodyObj->Find("isTrigger")) {
                rigidBody.isTrigger = ParseBoolLike(*triggerValue, rigidBody.isTrigger);
            }
            if (const JsonValue* gravityValue = rigidBodyObj->Find("useGravity")) {
                rigidBody.useGravity = ParseBoolLike(*gravityValue, rigidBody.useGravity);
            }
            if (const JsonValue* freezeValue = rigidBodyObj->Find("freezeRotation")) {
                rigidBody.freezeRotation = ParseBoolLike(*freezeValue, rigidBody.freezeRotation);
            }
            if (const JsonValue* frictionValue = rigidBodyObj->Find("friction")) {
                if (frictionValue->IsNumber()) {
                    rigidBody.friction = static_cast<float>(frictionValue->AsNumber());
                }
            }
            if (const JsonValue* restitutionValue = rigidBodyObj->Find("restitution")) {
                if (restitutionValue->IsNumber()) {
                    rigidBody.restitution = static_cast<float>(restitutionValue->AsNumber());
                }
            }
            if (const JsonValue* linearDampingValue = rigidBodyObj->Find("linearDamping")) {
                if (linearDampingValue->IsNumber()) {
                    rigidBody.linearDamping = static_cast<float>(linearDampingValue->AsNumber());
                }
            }
            if (const JsonValue* angularDampingValue = rigidBodyObj->Find("angularDamping")) {
                if (angularDampingValue->IsNumber()) {
                    rigidBody.angularDamping = static_cast<float>(angularDampingValue->AsNumber());
                }
            }
            if (const JsonValue* debugValue = rigidBodyObj->Find("debugDraw")) {
                rigidBody.debugDraw = ParseBoolLike(*debugValue, rigidBody.debugDraw);
            }
            if (const JsonValue* linearVelValue = rigidBodyObj->Find("linearVelocity")) {
                ParseVec3(*linearVelValue, rigidBody.linearVelocity);
            }
            if (const JsonValue* angularVelValue = rigidBodyObj->Find("angularVelocity")) {
                ParseVec3(*angularVelValue, rigidBody.angularVelocity);
            }

            rigidBody.ApplyAuthoredMotion(motionExplicit);

            rigidBody.InvalidateBodyHandle();
        }

        if (const JsonValue* materialObj = components->Find("Material")) {
            if (!materialObj->IsObject()) {
                outError = "Material component is not an object.";
                return false;
            }
            auto& material = entity.AddComponent<MaterialComponent>();
            if (const JsonValue* albedoValue = materialObj->Find("albedo")) {
                ParseVec3(*albedoValue, material.albedo);
            }
            if (const JsonValue* metallicValue = materialObj->Find("metallic")) {
                if (metallicValue->IsNumber()) {
                    material.metallic = static_cast<float>(metallicValue->AsNumber());
                }
            }
            if (const JsonValue* roughnessValue = materialObj->Find("roughness")) {
                if (roughnessValue->IsNumber()) {
                    material.roughness = static_cast<float>(roughnessValue->AsNumber());
                }
            }
            if (const JsonValue* aoValue = materialObj->Find("ao")) {
                if (aoValue->IsNumber()) {
                    material.ao = static_cast<float>(aoValue->AsNumber());
                }
            }
            if (const JsonValue* emissiveValue = materialObj->Find("emissive")) {
                ParseVec3(*emissiveValue, material.emissive);
            }
            if (const JsonValue* emissiveStrengthValue = materialObj->Find("emissiveStrength")) {
                if (emissiveStrengthValue->IsNumber()) {
                    material.emissiveStrength = static_cast<float>(emissiveStrengthValue->AsNumber());
                }
            }
            if (const JsonValue* reflectionStrengthValue = materialObj->Find("reflectionStrength")) {
                if (reflectionStrengthValue->IsNumber()) {
                    material.reflectionStrength = static_cast<float>(reflectionStrengthValue->AsNumber());
                }
            }
            if (const JsonValue* useTexValue = materialObj->Find("useAlbedoTexture")) {
                material.useAlbedoTexture = ParseBoolLike(*useTexValue, material.useAlbedoTexture);
            }
            if (const JsonValue* texPathValue = materialObj->Find("albedoTexturePath")) {
                if (texPathValue->IsString()) {
                    material.albedoTexturePath = texPathValue->AsString();
                }
            }
            if (const JsonValue* useNormalValue = materialObj->Find("useNormalTexture")) {
                material.useNormalTexture = ParseBoolLike(*useNormalValue, material.useNormalTexture);
            }
            if (const JsonValue* normalPathValue = materialObj->Find("normalTexturePath")) {
                if (normalPathValue->IsString()) {
                    material.normalTexturePath = normalPathValue->AsString();
                }
            }
            if (const JsonValue* useRoughValue = materialObj->Find("useRoughnessTexture")) {
                material.useRoughnessTexture = ParseBoolLike(*useRoughValue, material.useRoughnessTexture);
            }
            if (const JsonValue* roughPathValue = materialObj->Find("roughnessTexturePath")) {
                if (roughPathValue->IsString()) {
                    material.roughnessTexturePath = roughPathValue->AsString();
                }
            }
            if (const JsonValue* useMetallicValue = materialObj->Find("useMetallicTexture")) {
                material.useMetallicTexture = ParseBoolLike(*useMetallicValue, material.useMetallicTexture);
            }
            if (const JsonValue* metallicPathValue = materialObj->Find("metallicTexturePath")) {
                if (metallicPathValue->IsString()) {
                    material.metallicTexturePath = metallicPathValue->AsString();
                }
            }
            if (const JsonValue* useAoValue = materialObj->Find("useAoTexture")) {
                material.useAoTexture = ParseBoolLike(*useAoValue, material.useAoTexture);
            }
            if (const JsonValue* aoPathValue = materialObj->Find("aoTexturePath")) {
                if (aoPathValue->IsString()) {
                    material.aoTexturePath = aoPathValue->AsString();
                }
            }
            if (const JsonValue* useWorldAlignedValue = materialObj->Find("useWorldAlignedTexture")) {
                material.useWorldAlignedTexture = ParseBoolLike(*useWorldAlignedValue, material.useWorldAlignedTexture);
            }
            if (const JsonValue* worldScaleValue = materialObj->Find("worldTextureScale")) {
                if (worldScaleValue->IsNumber()) {
                    material.worldTextureScale = static_cast<float>(worldScaleValue->AsNumber());
                }
            }
        }

        if (const JsonValue* lightObj = components->Find("Light")) {
            if (!lightObj->IsObject()) {
                outError = "Light component is not an object.";
                return false;
            }
            LightComponent light;
            std::string lightError;
            if (const JsonValue* typeValue = lightObj->Find("type")) {
                light.type = ParseLightType(*typeValue, lightError);
                if (!lightError.empty()) {
                    outError = lightError;
                    return false;
                }
            }
            // enabled (default true for backwards compatibility)
            if (const JsonValue* enabledValue = lightObj->Find("enabled")) {
                if (enabledValue->IsBool()) {
                    light.enabled = enabledValue->AsBool();
                }
            }
            if (const JsonValue* colorValue = lightObj->Find("color")) {
                ParseVec3(*colorValue, light.color);
            }
            if (const JsonValue* intensityValue = lightObj->Find("intensity")) {
                if (intensityValue->IsNumber()) {
                    light.intensity = static_cast<float>(intensityValue->AsNumber());
                }
            }
            if (const JsonValue* rangeValue = lightObj->Find("range")) {
                if (rangeValue->IsNumber()) {
                    light.range = static_cast<float>(rangeValue->AsNumber());
                }
            }
            if (const JsonValue* attenuation = lightObj->Find("attenuation")) {
                if (attenuation->IsObject()) {
                    if (const JsonValue* constantValue = attenuation->Find("constant")) {
                        if (constantValue->IsNumber()) {
                            light.constant = static_cast<float>(constantValue->AsNumber());
                        }
                    }
                    if (const JsonValue* linearValue = attenuation->Find("linear")) {
                        if (linearValue->IsNumber()) {
                            light.linear = static_cast<float>(linearValue->AsNumber());
                        }
                    }
                    if (const JsonValue* quadValue = attenuation->Find("quadratic")) {
                        if (quadValue->IsNumber()) {
                            light.quadratic = static_cast<float>(quadValue->AsNumber());
                        }
                    }
                }
            }
            if (const JsonValue* spotAngles = lightObj->Find("spotAngles")) {
                if (spotAngles->IsObject()) {
                    if (const JsonValue* innerValue = spotAngles->Find("inner")) {
                        if (innerValue->IsNumber()) {
                            light.innerConeAngle = static_cast<float>(innerValue->AsNumber());
                        }
                    }
                    if (const JsonValue* outerValue = spotAngles->Find("outer")) {
                        if (outerValue->IsNumber()) {
                            light.outerConeAngle = static_cast<float>(outerValue->AsNumber());
                        }
                    }
                }
            }
            if (const JsonValue* directionValue = lightObj->Find("direction")) {
                ParseVec3(*directionValue, light.direction);
            }
            // Load shadow settings
            if (const JsonValue* shadowObj = lightObj->Find("shadow")) {
                if (shadowObj->IsObject()) {
                    if (const JsonValue* castValue = shadowObj->Find("castShadows")) {
                        if (castValue->IsBool()) {
                            light.castShadows = castValue->AsBool();
                        }
                    }
                    if (const JsonValue* autoFitValue = shadowObj->Find("autoFit")) {
                        if (autoFitValue->IsBool()) {
                            light.shadowAutoFit = autoFitValue->AsBool();
                        }
                    }
                    if (const JsonValue* biasValue = shadowObj->Find("bias")) {
                        if (biasValue->IsNumber()) {
                            light.shadowBias = static_cast<float>(biasValue->AsNumber());
                        }
                    }
                    if (const JsonValue* normalBiasValue = shadowObj->Find("normalBias")) {
                        if (normalBiasValue->IsNumber()) {
                            light.shadowNormalBias = static_cast<float>(normalBiasValue->AsNumber());
                        }
                    }
                    if (const JsonValue* strengthValue = shadowObj->Find("strength")) {
                        if (strengthValue->IsNumber()) {
                            light.shadowStrength = static_cast<float>(strengthValue->AsNumber());
                        }
                    }
                    if (const JsonValue* distanceValue = shadowObj->Find("distance")) {
                        if (distanceValue->IsNumber()) {
                            light.shadowDistance = static_cast<float>(distanceValue->AsNumber());
                        }
                    }
                    if (const JsonValue* extentValue = shadowObj->Find("extent")) {
                        if (extentValue->IsNumber()) {
                            light.shadowExtent = static_cast<float>(extentValue->AsNumber());
                        }
                    }
                    if (const JsonValue* maxDistValue = shadowObj->Find("maxDistance")) {
                        if (maxDistValue->IsNumber()) {
                            light.shadowMaxDistance = static_cast<float>(maxDistValue->AsNumber());
                        }
                    }
                }
            }
            entity.AddComponent<LightComponent>(light);
            std::cout << "[SceneSerializer] Loaded Light component on entity: " 
                << (entity.HasComponent<TagComponent>() ? entity.GetComponent<TagComponent>().tag : "Unknown")
                << " type=" << static_cast<int>(light.type)
                << " intensity=" << light.intensity << std::endl;
        }

        if (const JsonValue* cameraObj = components->Find("Camera")) {
            if (!cameraObj->IsObject()) {
                outError = "Camera component is not an object.";
                return false;
            }
            auto& camera = entity.AddComponent<CameraComponent>();
            if (const JsonValue* fovValue = cameraObj->Find("fov")) {
                if (fovValue->IsNumber()) {
                    camera.fov = static_cast<float>(fovValue->AsNumber());
                }
            }
            if (const JsonValue* nearValue = cameraObj->Find("near")) {
                if (nearValue->IsNumber()) {
                    camera.nearPlane = static_cast<float>(nearValue->AsNumber());
                }
            }
            if (const JsonValue* farValue = cameraObj->Find("far")) {
                if (farValue->IsNumber()) {
                    camera.farPlane = static_cast<float>(farValue->AsNumber());
                }
            }
            if (const JsonValue* projectionValue = cameraObj->Find("projection")) {
                if (projectionValue->IsString()) {
                    std::string projStr = projectionValue->AsString();
                    if (projStr == "Orthographic") {
                        camera.projection = CameraProjection::Orthographic;
                    } else {
                        camera.projection = CameraProjection::Perspective;
                    }
                }
            }
            if (const JsonValue* activeValue = cameraObj->Find("isActive")) {
                camera.isActive = ParseBoolLike(*activeValue, camera.isActive);
            }
            if (const JsonValue* primaryValue = cameraObj->Find("isPrimary")) {
                camera.isPrimary = ParseBoolLike(*primaryValue, camera.isPrimary);
                // Backward compatibility: isPrimary maps to isActive if isActive not specified
                if (!cameraObj->Find("isActive") && camera.isPrimary) {
                    camera.isActive = true;
                }
            }
            if (const JsonValue* showMeshValue = cameraObj->Find("showEditorMesh")) {
                camera.showEditorMesh = ParseBoolLike(*showMeshValue, camera.showEditorMesh);
            }
        }

        if (const JsonValue* scriptsObj = components->Find("Scripts")) {
            if (!scriptsObj->IsObject()) {
                outError = "Scripts component is not an object.";
                return false;
            }
            const JsonValue* namesArray = scriptsObj->Find("names");
            if (!namesArray || !namesArray->IsArray()) {
                outError = "Scripts component missing names array.";
                return false;
            }

            ScriptComponent& scriptComp = entity.HasComponent<ScriptComponent>()
                ? entity.GetComponent<ScriptComponent>()
                : entity.AddComponent<ScriptComponent>();
            scriptComp.RemoveAllScripts();
            for (const JsonValue& nameValue : namesArray->AsArray()) {
                if (!nameValue.IsString()) {
                    continue;
                }
                std::string scriptName = nameValue.AsString();
                if (scriptName.empty()) {
                    continue;
                }
                scriptComp.scriptNames.push_back(scriptName);
                scriptComp.scripts.push_back(nullptr);
            }
        }

        if (const JsonValue* scriptObj = components->Find("LuaScript")) {
            if (!scriptObj->IsObject()) {
                outError = "LuaScript component is not an object.";
                return false;
            }
            auto& script = entity.AddComponent<LuaScriptComponent>();
            if (const JsonValue* pathValue = scriptObj->Find("scriptPath")) {
                if (pathValue->IsString()) {
                    script.scriptPath = pathValue->AsString();
                }
            }
            if (const JsonValue* sourceValue = scriptObj->Find("scriptSource")) {
                if (sourceValue->IsString()) {
                    script.scriptSource = sourceValue->AsString();
                }
            }
            if (const JsonValue* awakeValue = scriptObj->Find("functionOnAwake")) {
                if (awakeValue->IsString()) {
                    script.functionOnAwake = awakeValue->AsString();
                }
            }
            if (const JsonValue* updateValue = scriptObj->Find("functionOnUpdate")) {
                if (updateValue->IsString()) {
                    script.functionOnUpdate = updateValue->AsString();
                }
            }
            if (const JsonValue* destroyValue = scriptObj->Find("functionOnDestroy")) {
                if (destroyValue->IsString()) {
                    script.functionOnDestroy = destroyValue->AsString();
                }
            }
            if (const JsonValue* enabledValue = scriptObj->Find("enabled")) {
                script.enabled = ParseBoolLike(*enabledValue, script.enabled);
            }
            if (const JsonValue* varsValue = scriptObj->Find("variables")) {
                if (varsValue->IsObject()) {
                    script.variables.clear();
                    for (const auto& [key, value] : varsValue->AsObject()) {
                        script.variables.emplace(key, ParsePropertyValue(value));
                    }
                }
            }
            if (const JsonValue* authoredValue = scriptObj->Find("authoredVariables")) {
                script.authoredVariablesInitialized = true;
                if (authoredValue->IsArray()) {
                    for (const JsonValue& value : authoredValue->AsArray()) {
                        if (value.IsString()) {
                            script.authoredVariables.insert(value.AsString());
                        }
                    }
                }
            }
        }

        if (const JsonValue* audioObj = components->Find("AudioSource")) {
            if (!audioObj->IsObject()) {
                outError = "AudioSource component is not an object.";
                return false;
            }

            auto& audio = entity.AddComponent<AudioSourceComponent>();
            if (const JsonValue* clipPathValue = audioObj->Find("clipPath")) {
                if (clipPathValue->IsString()) {
                    audio.clipPath = clipPathValue->AsString();
                }
            }
            if (const JsonValue* enabledValue = audioObj->Find("enabled")) {
                audio.enabled = ParseBoolLike(*enabledValue, audio.enabled);
            }
            if (const JsonValue* playOnStartValue = audioObj->Find("playOnStart")) {
                audio.playOnStart = ParseBoolLike(*playOnStartValue, audio.playOnStart);
            }
            if (const JsonValue* loopValue = audioObj->Find("loop")) {
                audio.loop = ParseBoolLike(*loopValue, audio.loop);
            }
            if (const JsonValue* volumeValue = audioObj->Find("volume")) {
                if (volumeValue->IsNumber()) {
                    audio.volume = static_cast<float>(volumeValue->AsNumber());
                }
            }
            audio.runtimeHandle = 0;
            audio.runtimeStarted = false;
            audio.runtimeLastClip.clear();
        }

        if (const JsonValue* characterObj = components->Find("Character")) {
            auto& character = entity.AddComponent<CharacterComponent>();
            if (!ApplyCharacterJson(character, *characterObj, outError)) {
                return false;
            }
        }

        if (const JsonValue* controllerObj = components->Find("CharacterController")) {
            auto& controller = entity.HasComponent<CharacterControllerComponent>()
                ? entity.GetComponent<CharacterControllerComponent>()
                : entity.AddComponent<CharacterControllerComponent>();
            if (!ApplyCharacterControllerJson(controller, *controllerObj, outError)) {
                return false;
            }
        }

        if (const JsonValue* probeVolumeObj = components->Find("ProbeVolume")) {
            auto& volume = entity.HasComponent<ProbeVolumeComponent>()
                ? entity.GetComponent<ProbeVolumeComponent>()
                : entity.AddComponent<ProbeVolumeComponent>();
            if (!ApplyProbeVolumeJson(volume, *probeVolumeObj, outError)) {
                return false;
            }
        }

        if (const JsonValue* selectionObj = components->Find("Selection")) {
            if (!selectionObj->IsObject()) {
                outError = "Selection component is not an object.";
                return false;
            }
            bool selected = false;
            if (const JsonValue* selectedValue = selectionObj->Find("selected")) {
                selected = selectedValue->IsBool() ? selectedValue->AsBool() : false;
            }
            if (!entity.HasComponent<SelectionComponent>()) {
                entity.AddComponent<SelectionComponent>(selected);
            } else {
                entity.GetComponent<SelectionComponent>().selected = selected;
            }
            if (selected) {
                selectedHandle = static_cast<entt::entity>(entity);
            }
        }

        if (const JsonValue* folderObj = components->Find("Folder")) {
            if (!folderObj->IsObject()) {
                outError = "Folder component is not an object.";
                return false;
            }
            bool isFolder = true;
            if (const JsonValue* isFolderValue = folderObj->Find("isFolder")) {
                isFolder = isFolderValue->IsBool() ? isFolderValue->AsBool() : true;
            }
            if (!entity.HasComponent<FolderComponent>()) {
                entity.AddComponent<FolderComponent>(isFolder);
            } else {
                entity.GetComponent<FolderComponent>().isFolder = isFolder;
            }
        }

        if (const JsonValue* envObj = components->Find("Environment")) {
            if (!envObj->IsObject()) {
                outError = "Environment component is not an object.";
                return false;
            }
            auto& env = entity.AddComponent<EnvironmentComponent>();
            bool usedLegacy = false;
            bool hasExplicitType = false;

            // --- Parse type (case-insensitive) ---
            if (const JsonValue* typeValue = envObj->Find("type")) {
                hasExplicitType = IsKnownBackgroundType(*typeValue);
                env.type = ParseBackgroundType(*typeValue);
            }

            // --- Parse solidColor with legacy aliases ---
            bool gotColor = false;
            for (const char* key : {"solidColor", "color", "skyColor"}) {
                if (gotColor) break;
                if (const JsonValue* v = envObj->Find(key)) {
                    if (ParseVec3(*v, env.solidColor)) {
                        gotColor = true;
                        if (std::string(key) != "solidColor") usedLegacy = true;
                    }
                }
            }

            // --- Parse hdriPath with legacy aliases ---
            bool gotPath = false;
            for (const char* key : {"hdriPath", "hdrPath", "environmentPath", "skyboxPath"}) {
                if (gotPath) break;
                if (const JsonValue* v = envObj->Find(key)) {
                    if (v->IsString() && !v->AsString().empty()) {
                        env.hdriPath = v->AsString();
                        gotPath = true;
                        if (std::string(key) != "hdriPath") usedLegacy = true;
                    }
                }
            }

            // --- Parse hdriIntensity with legacy aliases ---
            bool gotIntensity = false;
            for (const char* key : {"hdriIntensity", "intensity", "exposure"}) {
                if (gotIntensity) break;
                if (const JsonValue* v = envObj->Find(key)) {
                    if (v->IsNumber()) {
                        env.hdriIntensity = static_cast<float>(v->AsNumber());
                        gotIntensity = true;
                        if (std::string(key) != "hdriIntensity") usedLegacy = true;
                    }
                }
            }

            // --- Parse render distance fields (with defaults on absence) ---
            if (const JsonValue* v = envObj->Find("enableRenderDistance")) {
                env.enableRenderDistance = ParseBoolLike(*v, false);
            }
            if (const JsonValue* v = envObj->Find("renderDistance")) {
                if (v->IsNumber()) env.renderDistance = static_cast<float>(v->AsNumber());
            }
            if (const JsonValue* v = envObj->Find("infiniteRenderDistance")) {
                env.infiniteRenderDistance = ParseBoolLike(*v, true);
            }

            // --- Parse new fields (backwards-compatible: defaults preserved if absent) ---
            if (const JsonValue* v = envObj->Find("rotationYaw")) {
                if (v->IsNumber()) env.rotationYaw = static_cast<float>(v->AsNumber());
            }
            if (const JsonValue* v = envObj->Find("useAsIBL")) {
                env.useAsIBL = ParseBoolLike(*v, true);
            }
            if (const JsonValue* v = envObj->Find("reflectionIntensity")) {
                if (v->IsNumber()) env.reflectionIntensity = static_cast<float>(v->AsNumber());
            }

            // --- Heuristic: if no explicit type, infer from hdriPath ---
            if (!hasExplicitType) {
                if (gotPath) {
                    env.type = EnvironmentComponent::BackgroundType::HDRI;
                    usedLegacy = true;
                } else {
                    env.type = EnvironmentComponent::BackgroundType::SolidColor;
                }
            }

            if (usedLegacy) {
                static bool legacyWarnPrinted = false;
                if (!legacyWarnPrinted) {
                    std::cout << "[SceneSerializer] Loaded legacy environment format: applied defaults/aliases\n";
                    legacyWarnPrinted = true;
                }
            }
        }
        // --- Legacy component names mapped to EnvironmentComponent ---
        else {
            const JsonValue* legacyEnvObj = nullptr;
            const char* legacyName = nullptr;
            for (const char* name : {"Sky", "Skybox", "SceneEnvironment"}) {
                if (const JsonValue* v = components->Find(name)) {
                    if (v->IsObject()) {
                        legacyEnvObj = v;
                        legacyName = name;
                        break;
                    }
                }
            }
            if (legacyEnvObj) {
                auto& env = entity.AddComponent<EnvironmentComponent>();
                // Parse with same alias logic
                bool gotPath = false;
                for (const char* key : {"hdriPath", "hdrPath", "environmentPath", "skyboxPath"}) {
                    if (gotPath) break;
                    if (const JsonValue* v = legacyEnvObj->Find(key)) {
                        if (v->IsString() && !v->AsString().empty()) {
                            env.hdriPath = v->AsString();
                            gotPath = true;
                        }
                    }
                }
                for (const char* key : {"solidColor", "color", "skyColor"}) {
                    if (const JsonValue* v = legacyEnvObj->Find(key)) {
                        if (ParseVec3(*v, env.solidColor)) break;
                    }
                }
                for (const char* key : {"hdriIntensity", "intensity", "exposure"}) {
                    if (const JsonValue* v = legacyEnvObj->Find(key)) {
                        if (v->IsNumber()) {
                            env.hdriIntensity = static_cast<float>(v->AsNumber());
                            break;
                        }
                    }
                }
                if (const JsonValue* v = legacyEnvObj->Find("type")) {
                    if (IsKnownBackgroundType(*v)) {
                        env.type = ParseBackgroundType(*v);
                    } else if (gotPath) {
                        env.type = EnvironmentComponent::BackgroundType::HDRI;
                    }
                } else if (gotPath) {
                    env.type = EnvironmentComponent::BackgroundType::HDRI;
                }
                static bool legacyNameWarnPrinted = false;
                if (!legacyNameWarnPrinted) {
                    std::cout << "[SceneSerializer] Loaded legacy environment component '" << legacyName
                              << "': mapped to EnvironmentComponent with defaults/aliases\n";
                    legacyNameWarnPrinted = true;
                }
            }
        }
    }

    // Load prefab instances (new format: "prefabInstances", legacy: "blueprintInstances")
    const JsonValue* prefabInstancesValue = root.Find("prefabInstances");
    if (!prefabInstancesValue) {
        prefabInstancesValue = root.Find("blueprintInstances");
    }
    if (prefabInstancesValue) {
        if (!prefabInstancesValue->IsArray()) {
            outError = "prefabInstances is not an array";
            return false;
        }

        for (const JsonValue& instanceValue : prefabInstancesValue->AsArray()) {
            if (!instanceValue.IsObject()) {
                outError = "prefab instance entry must be an object";
                return false;
            }

            PrefabInstanceComponent instanceData;
            // New format: prefabPath / prefabGuid; legacy: blueprintPath / blueprintGuid
            if (const JsonValue* pathValue = instanceValue.Find("prefabPath")) {
                if (pathValue->IsString()) {
                    instanceData.prefabPath = pathValue->AsString();
                }
            }
            if (instanceData.prefabPath.empty()) {
                if (const JsonValue* pathValue = instanceValue.Find("blueprintPath")) {
                    if (pathValue->IsString()) {
                        instanceData.prefabPath = pathValue->AsString();
                    }
                }
            }
            if (const JsonValue* guidValue = instanceValue.Find("prefabGuid")) {
                if (guidValue->IsString()) {
                    instanceData.prefabGuid = guidValue->AsString();
                }
            }
            if (instanceData.prefabGuid.empty()) {
                if (const JsonValue* guidValue = instanceValue.Find("blueprintGuid")) {
                    if (guidValue->IsString()) {
                        instanceData.prefabGuid = guidValue->AsString();
                    }
                }
            }
            if (instanceData.prefabPath.empty()) {
                outError = "prefab instance missing prefabPath";
                return false;
            }
            if (const JsonValue* idValue = instanceValue.Find("instanceId")) {
                if (idValue->IsString()) {
                    instanceData.instanceId = idValue->AsString();
                }
            }
            if (const JsonValue* rootIdValue = instanceValue.Find("rootNodeId")) {
                if (rootIdValue->IsString()) {
                    instanceData.rootNodeId = rootIdValue->AsString();
                }
            }
            if (const JsonValue* parentValue = instanceValue.Find("parent")) {
                if (parentValue->IsNumber()) {
                    double rawParent = parentValue->AsNumber();
                    if (rawParent >= 0.0 && rawParent <= static_cast<double>(std::numeric_limits<uint32_t>::max())) {
                        uint32_t parentId = static_cast<uint32_t>(rawParent);
                        auto parentIt = idToEntity.find(parentId);
                        if (parentIt != idToEntity.end()) {
                            instanceData.parent = parentIt->second;
                        }
                    }
                }
            }

            if (const JsonValue* rootTransformValue = instanceValue.Find("rootTransform")) {
                if (rootTransformValue->IsObject()) {
                    if (const JsonValue* posValue = rootTransformValue->Find("position")) {
                        ParseVec3(*posValue, instanceData.rootTransform.position);
                    }
                    if (const JsonValue* rotValue = rootTransformValue->Find("rotation")) {
                        ParseVec3(*rotValue, instanceData.rootTransform.rotation);
                    }
                    if (const JsonValue* scaleValue = rootTransformValue->Find("scale")) {
                        ParseVec3(*scaleValue, instanceData.rootTransform.scale);
                    }
                    instanceData.hasRootTransform = true;
                }
            }
            if (const JsonValue* missingValue = instanceValue.Find("missingPrefab")) {
                instanceData.missingPrefab = ParseBoolLike(*missingValue, false);
            }
            if (const JsonValue* missingReasonValue = instanceValue.Find("missingReason")) {
                if (missingReasonValue->IsString()) {
                    instanceData.missingReason = missingReasonValue->AsString();
                }
            }

            if (const JsonValue* overridesValue = instanceValue.Find("overrides")) {
                if (!overridesValue->IsObject()) {
                    outError = "prefab overrides must be an object";
                    return false;
                }

                for (const auto& [nodeId, nodeValue] : overridesValue->AsObject()) {
                    if (!nodeValue.IsObject()) {
                        continue;
                    }
                    PrefabNodeOverride nodeOverride;

                    if (const JsonValue* transformValue = nodeValue.Find("transform")) {
                        if (transformValue->IsObject()) {
                            if (const JsonValue* posValue = transformValue->Find("position")) {
                                ParseVec3(*posValue, nodeOverride.transform.position);
                            }
                            if (const JsonValue* rotValue = transformValue->Find("rotation")) {
                                ParseVec3(*rotValue, nodeOverride.transform.rotation);
                            }
                            if (const JsonValue* scaleValue = transformValue->Find("scale")) {
                                ParseVec3(*scaleValue, nodeOverride.transform.scale);
                            }
                            nodeOverride.hasTransform = true;
                        }
                    }

                    if (const JsonValue* componentsValue = nodeValue.Find("components")) {
                        if (componentsValue->IsObject()) {
                            for (const auto& [componentType, componentValue] : componentsValue->AsObject()) {
                                if (!componentValue.IsObject()) {
                                    continue;
                                }
                                PrefabComponentOverride componentOverride;
                                for (const auto& [propKey, propValue] : componentValue.AsObject()) {
                                    componentOverride.properties.emplace(propKey, ParsePropertyValue(propValue));
                                }
                                nodeOverride.componentOverrides.emplace(componentType, std::move(componentOverride));
                            }
                        }
                    }

                    if (const JsonValue* scriptVarsValue = nodeValue.Find("scriptVars")) {
                        if (scriptVarsValue->IsObject()) {
                            for (const auto& [varKey, varValue] : scriptVarsValue->AsObject()) {
                                nodeOverride.scriptVars.emplace(varKey, ParsePropertyValue(varValue));
                            }
                        }
                    }

                    instanceData.overrides.emplace(nodeId, std::move(nodeOverride));
                }
            }

            PrefabAsset asset;
            std::string prefabError;
            if (!PrefabOverrideSystem::LoadAsset(instanceData.prefabPath, asset, prefabError)) {
                std::cerr << "[SceneSerializer] Missing prefab '" << instanceData.prefabPath
                          << "': " << prefabError << "\n";
                PrefabOverrideSystem::CreateMissingStub(scene, instanceData, prefabError);
                continue;
            }

            instanceData.missingPrefab = false;
            instanceData.missingReason.clear();
            PrefabInstantiateOptions options;
            options.prefabPath = instanceData.prefabPath;
            options.instanceId = instanceData.instanceId;
            options.parent = instanceData.parent;
            options.instanceData = &instanceData;

            PrefabInstantiator::Instantiate(scene, asset, options);
        }
    }

    for (const auto& link : pendingParents) {
        entt::entity childHandle = link.first;
        uint32_t parentId = link.second;
        auto parentIt = idToEntity.find(parentId);
        if (parentIt == idToEntity.end()) {
            continue;
        }
        Entity child(childHandle, &scene);
        Entity parent(parentIt->second, &scene);
        scene.SetParent(child, parent);
    }

    if (selectedHandle != entt::null) {
        scene.SetSelectedEntity(Entity(selectedHandle, &scene));
    } else {
        scene.ClearSelection();
    }

    scene.SetSourceAssetPath(filepath.string());
    scene.MarkReconstructedFromSource();
    return true;
}

std::string SceneSerializer::SerializeToString() const
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());

    const entt::registry& registry = m_Scene.GetRegistry();
    std::vector<entt::entity> entities;
    auto taggedEntities = registry.view<TagComponent>();
    for (entt::entity entity : taggedEntities) {
        if (registry.any_of<PrefabNodeComponent, EditorOnlyComponent>(entity)) {
            continue;
        }
        entities.push_back(entity);
    }

    std::sort(entities.begin(), entities.end(), [&](entt::entity a, entt::entity b) {
        const TagComponent* tagA = registry.try_get<TagComponent>(a);
        const TagComponent* tagB = registry.try_get<TagComponent>(b);

        std::string nameA = tagA ? tagA->tag : std::string();
        std::string nameB = tagB ? tagB->tag : std::string();
        if (nameA == nameB) {
            return static_cast<uint32_t>(a) < static_cast<uint32_t>(b);
        }
        return nameA < nameB;
    });

    stream << "{\n";
    stream << "  \"schema\": \"HephaestusScene\",\n";
    stream << "  \"version\": 1,\n";
    stream << "  \"name\": \"" << EscapeString(m_Scene.GetName()) << "\",\n";
    stream << "  \"entityCount\": " << entities.size() << ",\n";

    std::vector<entt::entity> prefabInstances;
    auto prefabView = registry.view<PrefabInstanceComponent>();
    for (entt::entity entity : prefabView) {
        prefabInstances.push_back(entity);
    }
    std::sort(prefabInstances.begin(), prefabInstances.end(), [](entt::entity a, entt::entity b) {
        return static_cast<uint32_t>(a) < static_cast<uint32_t>(b);
    });

    stream << "  \"prefabInstanceCount\": " << prefabInstances.size() << ",\n";
    stream << "  \"prefabInstances\": [\n";
    for (size_t instanceIndex = 0; instanceIndex < prefabInstances.size(); ++instanceIndex) {
        entt::entity handle = prefabInstances[instanceIndex];
        const PrefabInstanceComponent* instance = registry.try_get<PrefabInstanceComponent>(handle);
        if (!instance) {
            continue;
        }

        WritePrefabInstance(stream, *instance);
        if (instanceIndex + 1 < prefabInstances.size()) {
            stream << ",\n";
        } else {
            stream << "\n";
        }
    }
    stream << "  ],\n";

    stream << "  \"entities\": [\n";

    for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
        entt::entity handle = entities[entityIndex];

        const TagComponent* tag = registry.try_get<TagComponent>(handle);
        const TransformComponent* transform = registry.try_get<TransformComponent>(handle);
        const MeshRendererComponent* mesh = registry.try_get<MeshRendererComponent>(handle);
        const MeshColliderComponent* collider = registry.try_get<MeshColliderComponent>(handle);
    const PhysicsShapeComponent* physicsShape = registry.try_get<PhysicsShapeComponent>(handle);
    const RigidBodyComponent* rigidBody = registry.try_get<RigidBodyComponent>(handle);
        const MaterialComponent* material = registry.try_get<MaterialComponent>(handle);
        const LightComponent* light = registry.try_get<LightComponent>(handle);
        const CameraComponent* camera = registry.try_get<CameraComponent>(handle);
        const LuaScriptComponent* script = registry.try_get<LuaScriptComponent>(handle);
        const AudioSourceComponent* audio = registry.try_get<AudioSourceComponent>(handle);
        const CharacterComponent* character = registry.try_get<CharacterComponent>(handle);
        const CharacterControllerComponent* characterController = registry.try_get<CharacterControllerComponent>(handle);
        const ProbeVolumeComponent* probeVolume = registry.try_get<ProbeVolumeComponent>(handle);
        const SelectionComponent* selection = registry.try_get<SelectionComponent>(handle);
        const FolderComponent* folder = registry.try_get<FolderComponent>(handle);
        const EnvironmentComponent* environment = registry.try_get<EnvironmentComponent>(handle);

        stream << "    {\n";
        stream << "      \"id\": " << static_cast<uint32_t>(handle) << ",\n";
        stream << "      \"name\": \"" << EscapeString(tag ? tag->tag : std::string("Entity")) << "\",\n";

        stream << "      \"parent\": ";
        if (transform && transform->parent != entt::null) {
            stream << static_cast<uint32_t>(transform->parent) << ",\n";
        } else {
            stream << "null,\n";
        }

        std::vector<std::string> componentBlocks;
        componentBlocks.reserve(8);

        if (transform) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Transform\": {\n";
            oss << "          \"position\": " << FormatVec3(transform->position) << ",\n";
            oss << "          \"rotation\": " << FormatVec3(transform->rotation) << ",\n";
            oss << "          \"scale\": " << FormatVec3(transform->scale) << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (mesh && !IsRuntimeGeneratedVisual(mesh)) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"MeshRenderer\": {\n";
            oss << "          \"visible\": " << (mesh->visible ? "true" : "false") << ",\n";
            oss << "          \"hideInGame\": " << (mesh->hideInGame ? "true" : "false") << ",\n";
            oss << "          \"showHiddenOutline\": " << (mesh->showHiddenOutline ? "true" : "false") << ",\n";
            oss << "          \"mesh\": ";
            if (mesh->HasValidMesh()) {
                oss << static_cast<uint64_t>(mesh->meshHandle) << ",\n";
            } else {
                oss << "null,\n";
            }
            oss << "          \"meshSource\": ";
            if (!mesh->meshAssetPath.empty()) {
                oss << "\"" << EscapeString(NormalizeAssetPathForSave(mesh->meshAssetPath)) << "\"";
            } else {
                oss << "null";
            }
            oss << ",\n";
            oss << "          \"meshImported\": " << (mesh->meshImported ? "true" : "false") << ",\n";
            oss << "          \"submeshCount\": " << mesh->submeshCount << ",\n";
            oss << "          \"materialIndices\": [";
            for (size_t i = 0; i < mesh->materialIndices.size(); ++i) {
                if (i) oss << ", ";
                oss << mesh->materialIndices[i];
            }
            oss << "],\n";
            oss << "          \"appliedMaterialPaths\": [";
            for (size_t i = 0; i < mesh->appliedMaterialPaths.size(); ++i) {
                if (i) oss << ", ";
                oss << "\"" << EscapeString(NormalizeAssetPathForSave(mesh->appliedMaterialPaths[i])) << "\"";
            }
            oss << "],\n";
            oss << "          \"backfaceCulling\": " << (mesh->backfaceCulling ? "true" : "false") << ",\n";
            oss << "          \"localBounds\": {\n";
            oss << "            \"min\": " << FormatVec3(mesh->localBounds.min) << ",\n";
            oss << "            \"max\": " << FormatVec3(mesh->localBounds.max) << "\n";
            oss << "          }\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (collider) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"MeshCollider\": {\n";
            oss << "          \"enabled\": " << (collider->enabled ? "true" : "false") << ",\n";
            oss << "          \"isStatic\": " << (collider->isStatic ? "true" : "false") << ",\n";
            oss << "          \"isTrigger\": " << (collider->isTrigger ? "true" : "false") << ",\n";
            oss << "          \"debugDraw\": " << (collider->debugDraw ? "true" : "false") << ",\n";
            oss << "          \"useComplexCollision\": " << (collider->useComplexCollision ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (physicsShape) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"PhysicsShape\": {\n";
            oss << "          \"type\": \"" << PhysicsShapeTypeToString(physicsShape->type) << "\",\n";
            oss << "          \"extents\": " << FormatVec3(physicsShape->extents) << ",\n";
            oss << "          \"radius\": " << FormatFloat(physicsShape->radius) << ",\n";
            oss << "          \"height\": " << FormatFloat(physicsShape->height) << ",\n";
            oss << "          \"offset\": " << FormatVec3(physicsShape->offset) << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (rigidBody) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"RigidBody\": {\n";
            oss << "          \"mass\": " << FormatFloat(rigidBody->mass) << ",\n";
            oss << "          \"motion\": \"" << rigidBody->MotionTypeName() << "\",\n";
            oss << "          \"layer\": " << rigidBody->layer << ",\n";
            oss << "          \"isStatic\": " << (rigidBody->motion == RigidBodyMotion::Static ? "true" : "false") << ",\n";
            oss << "          \"isTrigger\": " << (rigidBody->isTrigger ? "true" : "false") << ",\n";
            oss << "          \"useGravity\": " << (rigidBody->useGravity ? "true" : "false") << ",\n";
            oss << "          \"freezeRotation\": " << (rigidBody->freezeRotation ? "true" : "false") << ",\n";
            oss << "          \"linearDamping\": " << FormatFloat(rigidBody->linearDamping) << ",\n";
            oss << "          \"angularDamping\": " << FormatFloat(rigidBody->angularDamping) << ",\n";
            oss << "          \"friction\": " << FormatFloat(rigidBody->friction) << ",\n";
            oss << "          \"restitution\": " << FormatFloat(rigidBody->restitution) << ",\n";
            oss << "          \"linearVelocity\": " << FormatVec3(rigidBody->linearVelocity) << ",\n";
            oss << "          \"angularVelocity\": " << FormatVec3(rigidBody->angularVelocity) << ",\n";
            oss << "          \"debugDraw\": " << (rigidBody->debugDraw ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (material) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Material\": {\n";
            oss << "          \"albedo\": " << FormatVec3(material->albedo) << ",\n";
            oss << "          \"metallic\": " << FormatFloat(material->metallic) << ",\n";
            oss << "          \"roughness\": " << FormatFloat(material->roughness) << ",\n";
            oss << "          \"ao\": " << FormatFloat(material->ao) << ",\n";
            oss << "          \"emissive\": " << FormatVec3(material->emissive) << ",\n";
            oss << "          \"emissiveStrength\": " << FormatFloat(material->emissiveStrength) << ",\n";
            oss << "          \"reflectionStrength\": " << FormatFloat(material->reflectionStrength) << ",\n";
            oss << "          \"useAlbedoTexture\": " << (material->useAlbedoTexture ? "true" : "false") << ",\n";
            oss << "          \"albedoTexturePath\": \"" << EscapeString(NormalizeAssetPathForSave(material->albedoTexturePath)) << "\",\n";
            oss << "          \"useNormalTexture\": " << (material->useNormalTexture ? "true" : "false") << ",\n";
            oss << "          \"normalTexturePath\": \"" << EscapeString(NormalizeAssetPathForSave(material->normalTexturePath)) << "\",\n";
            oss << "          \"useRoughnessTexture\": " << (material->useRoughnessTexture ? "true" : "false") << ",\n";
            oss << "          \"roughnessTexturePath\": \"" << EscapeString(NormalizeAssetPathForSave(material->roughnessTexturePath)) << "\",\n";
            oss << "          \"useMetallicTexture\": " << (material->useMetallicTexture ? "true" : "false") << ",\n";
            oss << "          \"metallicTexturePath\": \"" << EscapeString(NormalizeAssetPathForSave(material->metallicTexturePath)) << "\",\n";
            oss << "          \"useAoTexture\": " << (material->useAoTexture ? "true" : "false") << ",\n";
            oss << "          \"aoTexturePath\": \"" << EscapeString(NormalizeAssetPathForSave(material->aoTexturePath)) << "\",\n";
            oss << "          \"useWorldAlignedTexture\": " << (material->useWorldAlignedTexture ? "true" : "false") << ",\n";
            oss << "          \"worldTextureScale\": " << FormatFloat(material->worldTextureScale) << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (light) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Light\": {\n";
            oss << "          \"type\": \"" << LightTypeToString(light->type) << "\",\n";
            oss << "          \"enabled\": " << (light->enabled ? "true" : "false") << ",\n";
            oss << "          \"color\": " << FormatVec3(light->color) << ",\n";
            oss << "          \"intensity\": " << FormatFloat(light->intensity) << ",\n";
            oss << "          \"range\": " << FormatFloat(light->range) << ",\n";
            oss << "          \"attenuation\": {\n";
            oss << "            \"constant\": " << FormatFloat(light->constant) << ",\n";
            oss << "            \"linear\": " << FormatFloat(light->linear) << ",\n";
            oss << "            \"quadratic\": " << FormatFloat(light->quadratic) << "\n";
            oss << "          },\n";
            oss << "          \"spotAngles\": {\n";
            oss << "            \"inner\": " << FormatFloat(light->innerConeAngle) << ",\n";
            oss << "            \"outer\": " << FormatFloat(light->outerConeAngle) << "\n";
            oss << "          },\n";
            oss << "          \"direction\": " << FormatVec3(light->direction) << ",\n";
            oss << "          \"shadow\": {\n";
            oss << "            \"castShadows\": " << (light->castShadows ? "true" : "false") << ",\n";
            oss << "            \"autoFit\": " << (light->shadowAutoFit ? "true" : "false") << ",\n";
            oss << "            \"bias\": " << FormatFloat(light->shadowBias) << ",\n";
            oss << "            \"normalBias\": " << FormatFloat(light->shadowNormalBias) << ",\n";
            oss << "            \"strength\": " << FormatFloat(light->shadowStrength) << ",\n";
            oss << "            \"distance\": " << FormatFloat(light->shadowDistance) << ",\n";
            oss << "            \"extent\": " << FormatFloat(light->shadowExtent) << ",\n";
            oss << "            \"maxDistance\": " << FormatFloat(light->shadowMaxDistance) << "\n";
            oss << "          }\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (camera) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Camera\": {\n";
            oss << "          \"fov\": " << FormatFloat(camera->fov) << ",\n";
            oss << "          \"near\": " << FormatFloat(camera->nearPlane) << ",\n";
            oss << "          \"far\": " << FormatFloat(camera->farPlane) << ",\n";
            oss << "          \"projection\": \"" << (camera->projection == CameraProjection::Orthographic ? "Orthographic" : "Perspective") << "\",\n";
            oss << "          \"isActive\": " << (camera->isActive ? "true" : "false") << ",\n";
            oss << "          \"isPrimary\": " << (camera->isPrimary || camera->isActive ? "true" : "false") << ",\n";
            oss << "          \"showEditorMesh\": " << (camera->showEditorMesh ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (script) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"LuaScript\": {\n";
            oss << "          \"scriptPath\": \"" << EscapeString(NormalizeAssetPathForSave(script->scriptPath)) << "\",\n";
            oss << "          \"scriptSource\": \"" << EscapeString(script->scriptSource) << "\",\n";
            oss << "          \"functionOnAwake\": \"" << EscapeString(script->functionOnAwake) << "\",\n";
            oss << "          \"functionOnUpdate\": \"" << EscapeString(script->functionOnUpdate) << "\",\n";
            oss << "          \"functionOnDestroy\": \"" << EscapeString(script->functionOnDestroy) << "\",\n";
            oss << "          \"enabled\": " << (script->enabled ? "true" : "false") << ",\n";
            oss << "          \"variables\": {";
            size_t varIndex = 0;
            for (const auto& [key, value] : script->variables) {
                if (varIndex++) {
                    oss << ", ";
                }
                oss << "\"" << EscapeString(key) << "\": ";
                WritePropertyValue(oss, value);
            }
            oss << "},\n";
            oss << "          \"authoredVariables\": ";
            WriteStringSet(oss, script->authoredVariables);
            oss << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (audio) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"AudioSource\": {\n";
            oss << "          \"clipPath\": \"" << EscapeString(NormalizeAssetPathForSave(audio->clipPath)) << "\",\n";
            oss << "          \"enabled\": " << (audio->enabled ? "true" : "false") << ",\n";
            oss << "          \"playOnStart\": " << (audio->playOnStart ? "true" : "false") << ",\n";
            oss << "          \"loop\": " << (audio->loop ? "true" : "false") << ",\n";
            oss << "          \"volume\": " << FormatFloat(audio->volume) << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (character) {
            componentBlocks.push_back(WriteCharacterComponentJson(*character));
        }

        if (characterController) {
            componentBlocks.push_back(WriteCharacterControllerJson(*characterController));
        }

        if (probeVolume) {
            componentBlocks.push_back(WriteProbeVolumeJson(*probeVolume));
        }

        if (selection) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Selection\": {\n";
            oss << "          \"selected\": " << (selection->selected ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (folder) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Folder\": {\n";
            oss << "          \"isFolder\": " << (folder->isFolder ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (environment) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Environment\": {\n";
            oss << "          \"type\": \"" << BackgroundTypeToString(environment->type) << "\",\n";
            oss << "          \"solidColor\": " << FormatVec3(environment->solidColor) << ",\n";
            oss << "          \"hdriPath\": \"" << EscapeString(NormalizeAssetPathForSave(environment->hdriPath)) << "\",\n";
            oss << "          \"hdriIntensity\": " << FormatFloat(environment->hdriIntensity) << ",\n";
            oss << "          \"rotationYaw\": " << FormatFloat(environment->rotationYaw) << ",\n";
            oss << "          \"useAsIBL\": " << (environment->useAsIBL ? "true" : "false") << ",\n";
            oss << "          \"reflectionIntensity\": " << FormatFloat(environment->reflectionIntensity) << ",\n";
            oss << "          \"enableRenderDistance\": " << (environment->enableRenderDistance ? "true" : "false") << ",\n";
            oss << "          \"renderDistance\": " << FormatFloat(environment->renderDistance) << ",\n";
            oss << "          \"infiniteRenderDistance\": " << (environment->infiniteRenderDistance ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        stream << "      \"components\": {\n";
        for (size_t i = 0; i < componentBlocks.size(); ++i) {
            stream << componentBlocks[i];
            if (i + 1 < componentBlocks.size()) {
                stream << ",\n";
            } else {
                stream << "\n";
            }
        }
        stream << "      }\n";

        if (entityIndex + 1 < entities.size()) {
            stream << "    },\n";
        } else {
            stream << "    }\n";
        }
    }

    stream << "  ]\n";
    stream << "}\n";

    return stream.str();
}

bool SceneSerializer::DeserializeFromString(Scene& scene, const std::string& data)
{
    // Unique temp path avoids races when multiple deserializations run concurrently.
    // (Shared fixed name previously clobbered peer callers.)
    namespace fs = std::filesystem;
    const std::string sourceAssetPath = scene.GetSourceAssetPath();
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto unique = std::to_string(stamp) + "_" +
                        std::to_string(reinterpret_cast<std::uintptr_t>(&data));
    const fs::path tempPath =
        fs::temp_directory_path() / ("hephaestus_snapshot_" + unique + ".hpscene");

    {
        std::ofstream tempFile(tempPath, std::ios::binary | std::ios::trunc);
        if (!tempFile.is_open()) {
            return false;
        }
        tempFile.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!tempFile.good()) {
            tempFile.close();
            fs::remove(tempPath);
            return false;
        }
    }

    const bool result = Deserialize(scene, tempPath);
    scene.SetSourceAssetPath(sourceAssetPath);
    std::error_code ec;
    fs::remove(tempPath, ec);
    return result;
}

