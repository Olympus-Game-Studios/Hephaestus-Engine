#include "PrefabSerializer.h"

#include "PrefabComponentIO.h"

#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace {
std::string EscapeString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);

    for (char ch : value) {
        switch (ch) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

std::string FormatFloat(float value) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(6) << value;
    return oss.str();
}

std::string FormatVec3(const Vec3& v) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "[" << FormatFloat(v.x) << ", " << FormatFloat(v.y) << ", " << FormatFloat(v.z) << "]";
    return oss.str();
}

bool ParseVec3(const JsonValue& value, Vec3& out) {
    if (!value.IsArray()) {
        return false;
    }
    const auto& arr = value.AsArray();
    if (arr.size() < 3) {
        return false;
    }
    if (!arr[0].IsNumber() || !arr[1].IsNumber() || !arr[2].IsNumber()) {
        return false;
    }
    out.x = static_cast<float>(arr[0].AsNumber());
    out.y = static_cast<float>(arr[1].AsNumber());
    out.z = static_cast<float>(arr[2].AsNumber());
    return true;
}

PropertyValue ParsePropertyValue(const JsonValue& value) {
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

void WritePropertyValue(std::ostream& out, const PropertyValue& value, int indent);

void WriteJsonIndent(std::ostream& out, int indent) {
    for (int i = 0; i < indent; ++i) {
        out << ' ';
    }
}

void WriteJsonValue(std::ostream& out, const JsonValue& value, int indent) {
    switch (value.type) {
        case JsonValue::Type::Null:
            out << "null";
            return;
        case JsonValue::Type::Bool:
            out << (value.boolValue ? "true" : "false");
            return;
        case JsonValue::Type::Number:
            out << std::setprecision(10) << value.numberValue;
            return;
        case JsonValue::Type::String:
            out << "\"" << EscapeString(value.stringValue) << "\"";
            return;
        case JsonValue::Type::Array: {
            out << "[";
            for (size_t i = 0; i < value.arrayValue.size(); ++i) {
                if (i) out << ", ";
                WriteJsonValue(out, value.arrayValue[i], indent);
            }
            out << "]";
            return;
        }
        case JsonValue::Type::Object: {
            out << "{";
            std::vector<std::string> keys;
            keys.reserve(value.objectValue.size());
            for (const auto& [key, _] : value.objectValue) {
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());
            bool first = true;
            for (const auto& key : keys) {
                const auto it = value.objectValue.find(key);
                if (it == value.objectValue.end()) {
                    continue;
                }
                if (!first) out << ", ";
                out << "\"" << EscapeString(key) << "\": ";
                WriteJsonValue(out, it->second, indent);
                first = false;
            }
            out << "}";
            return;
        }
    }
}

void WritePropertyValue(std::ostream& out, const PropertyValue& value, int indent) {
    if (value.IsNull()) {
        out << "null";
        return;
    }
    if (value.IsBool()) {
        out << (*value.AsBool() ? "true" : "false");
        return;
    }
    if (value.IsNumber()) {
        out << std::setprecision(10) << *value.AsNumber();
        return;
    }
    if (value.IsString()) {
        out << "\"" << EscapeString(*value.AsString()) << "\"";
        return;
    }
    if (value.IsVec2()) {
        const Vec2& v = *value.AsVec2();
        out << "[" << FormatFloat(v.x) << ", " << FormatFloat(v.y) << "]";
        return;
    }
    if (value.IsVec3()) {
        const Vec3& v = *value.AsVec3();
        out << "[" << FormatFloat(v.x) << ", " << FormatFloat(v.y) << ", " << FormatFloat(v.z) << "]";
        return;
    }
    if (value.IsVec4()) {
        const Vec4& v = *value.AsVec4();
        out << "[" << FormatFloat(v.x) << ", " << FormatFloat(v.y) << ", " << FormatFloat(v.z) << ", " << FormatFloat(v.w) << "]";
        return;
    }
    if (value.IsArray()) {
        out << "[";
        const auto& arr = *value.AsArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i) out << ", ";
            WritePropertyValue(out, arr[i], indent);
        }
        out << "]";
        return;
    }
    if (value.IsObject()) {
        out << "{";
        std::vector<std::string> keys;
        keys.reserve(value.AsObject()->size());
        for (const auto& [key, _] : *value.AsObject()) {
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        bool first = true;
        for (const auto& key : keys) {
            const auto it = value.AsObject()->find(key);
            if (it == value.AsObject()->end()) {
                continue;
            }
            if (!first) out << ", ";
            out << "\"" << EscapeString(key) << "\": ";
            WritePropertyValue(out, it->second, indent);
            first = false;
        }
        out << "}";
        return;
    }
}
}

bool PrefabSerializer::Deserialize(PrefabAsset& asset, const std::filesystem::path& filepath, std::string& outError) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        outError = "Unable to open prefab file.";
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    if (!DeserializeFromString(asset, content, outError)) {
        return false;
    }

    if (asset.name.empty()) {
        asset.name = filepath.stem().string();
    }

    outError.clear();
    return true;
}

bool PrefabSerializer::DeserializeFromString(PrefabAsset& asset, const std::string& data, std::string& outError) {
    JsonParser parser(data);
    JsonValue root;
    if (!parser.Parse(root, outError)) {
        return false;
    }
    if (!root.IsObject()) {
        outError = "Prefab root must be an object.";
        return false;
    }

    asset = PrefabAsset{};

    // Accept both "HephaestusPrefab" and legacy "HephaestusBlueprint" schemas
    if (const JsonValue* versionValue = root.Find("assetVersion")) {
        if (versionValue->IsNumber()) {
            asset.assetVersion = static_cast<int>(versionValue->AsNumber());
        }
    } else if (const JsonValue* legacyVersionValue = root.Find("version")) {
        if (legacyVersionValue->IsNumber()) {
            asset.assetVersion = static_cast<int>(legacyVersionValue->AsNumber());
        }
    }

    if (const JsonValue* guidValue = root.Find("guid")) {
        if (guidValue->IsString()) {
            asset.guid = guidValue->AsString();
        }
    } else if (const JsonValue* legacyId = root.Find("assetId")) {
        if (legacyId->IsString()) {
            asset.guid = legacyId->AsString();
        }
    }

    if (const JsonValue* nameValue = root.Find("name")) {
        if (nameValue->IsString()) {
            asset.name = nameValue->AsString();
        }
    }

    if (const JsonValue* rootNodeValue = root.Find("rootNodeId")) {
        if (rootNodeValue->IsString()) {
            asset.rootNodeId = rootNodeValue->AsString();
        }
    }

    const JsonValue* nodesValue = root.Find("nodes");
    if (!nodesValue || !nodesValue->IsArray()) {
        outError = "Prefab missing nodes array.";
        return false;
    }

    for (const JsonValue& nodeValue : nodesValue->AsArray()) {
        if (!nodeValue.IsObject()) {
            outError = "Prefab node entry must be an object.";
            return false;
        }

        PrefabNode node;
        if (const JsonValue* idValue = nodeValue.Find("id")) {
            if (idValue->IsString()) {
                node.id = idValue->AsString();
            }
        }
        if (node.id.empty()) {
            outError = "Prefab node missing id.";
            return false;
        }

        if (const JsonValue* nameValue = nodeValue.Find("name")) {
            if (nameValue->IsString()) {
                node.name = nameValue->AsString();
            }
        }
        if (node.name.empty()) {
            node.name = node.id;
        }

        if (const JsonValue* parentValue = nodeValue.Find("parentId")) {
            if (parentValue->IsString()) {
                node.parentId = parentValue->AsString();
            } else if (parentValue->IsNull()) {
                node.parentId.clear();
            }
        }

        if (const JsonValue* prefabValue = nodeValue.Find("prefabPath"); prefabValue && prefabValue->IsString()) {
            node.prefabPath = prefabValue->AsString();
            if (const JsonValue* overridesValue = nodeValue.Find("overrides")) {
                PrefabComponentIO::NodeOverridesFromJson(*overridesValue, node.nestedOverrides);
            }
        }
        if (const JsonValue* fromValue = nodeValue.Find("nestedFrom"); fromValue && fromValue->IsString()) {
            node.nestedFrom = fromValue->AsString();
        }
        if (const JsonValue* nestedIdValue = nodeValue.Find("nestedNodeId"); nestedIdValue && nestedIdValue->IsString()) {
            node.nestedNodeId = nestedIdValue->AsString();
        }

        if (const JsonValue* transformValue = nodeValue.Find("transform")) {
            if (transformValue->IsObject()) {
                if (const JsonValue* posValue = transformValue->Find("position")) {
                    ParseVec3(*posValue, node.localTransform.position);
                }
                if (const JsonValue* rotValue = transformValue->Find("rotation")) {
                    ParseVec3(*rotValue, node.localTransform.rotation);
                }
                if (const JsonValue* scaleValue = transformValue->Find("scale")) {
                    ParseVec3(*scaleValue, node.localTransform.scale);
                }
            }
        }

        if (const JsonValue* componentsValue = nodeValue.Find("components")) {
            if (componentsValue->IsArray()) {
                for (const auto& compEntry : componentsValue->AsArray()) {
                    if (!compEntry.IsObject()) {
                        continue;
                    }
                    PrefabComponentEntry entry;
                    if (const JsonValue* typeValue = compEntry.Find("type")) {
                        if (typeValue->IsString()) {
                            entry.type = typeValue->AsString();
                        }
                    }
                    if (const JsonValue* enabledValue = compEntry.Find("enabled")) {
                        if (enabledValue->IsBool()) {
                            entry.enabled = enabledValue->AsBool();
                        } else if (enabledValue->IsNumber()) {
                            entry.enabled = (enabledValue->AsNumber() != 0.0);
                        }
                    }
                    if (const JsonValue* dataValue = compEntry.Find("data")) {
                        entry.data = *dataValue;
                    }
                    if (!entry.type.empty()) {
                        node.components.push_back(std::move(entry));
                    }
                }
            } else if (componentsValue->IsObject()) {
                for (const auto& [key, entryValue] : componentsValue->AsObject()) {
                    PrefabComponentEntry entry;
                    entry.type = key;
                    entry.enabled = true;
                    entry.data = entryValue;
                    node.components.push_back(std::move(entry));
                }
            }
        }

        // Legacy: promote top-level "script" block to a LuaScript component
        if (const JsonValue* scriptValue = nodeValue.Find("script")) {
            if (scriptValue->IsObject()) {
                PrefabComponentEntry entry;
                entry.type = "LuaScript";
                entry.enabled = true;
                entry.data.type = JsonValue::Type::Object;
                if (const JsonValue* langValue = scriptValue->Find("language")) {
                    entry.data.objectValue["language"] = *langValue;
                }
                if (const JsonValue* sourceValue = scriptValue->Find("source")) {
                    entry.data.objectValue["source"] = *sourceValue;
                }
                if (const JsonValue* varsValue = scriptValue->Find("defaultVars")) {
                    entry.data.objectValue["variables"] = *varsValue;
                }
                node.components.push_back(std::move(entry));
            }
        }

        if (const JsonValue* exposedValue = nodeValue.Find("exposed")) {
            if (exposedValue->IsArray()) {
                for (const auto& entry : exposedValue->AsArray()) {
                    if (entry.IsString()) {
                        node.exposed.push_back(entry.AsString());
                    } else if (entry.IsObject()) {
                        const JsonValue* typeValue = entry.Find("type");
                        const JsonValue* keyValue = entry.Find("key");
                        if (typeValue && keyValue && typeValue->IsString() && keyValue->IsString()) {
                            node.exposed.push_back(typeValue->AsString() + std::string(":") + keyValue->AsString());
                        }
                    }
                }
            }
        }

        asset.nodes.push_back(std::move(node));
    }

    if (asset.rootNodeId.empty()) {
        for (const auto& node : asset.nodes) {
            if (node.parentId.empty()) {
                asset.rootNodeId = node.id;
                break;
            }
        }
    }

    if (asset.rootNodeId.empty()) {
        outError = "Prefab missing root node.";
        return false;
    }

    return true;
}

std::string PrefabSerializer::SerializeToString(const PrefabAsset& asset) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"schema\": \"HephaestusPrefab\",\n";
    out << "  \"assetVersion\": " << asset.assetVersion << ",\n";
    out << "  \"guid\": \"" << EscapeString(asset.guid) << "\",\n";
    out << "  \"name\": \"" << EscapeString(asset.name) << "\",\n";
    out << "  \"rootNodeId\": \"" << EscapeString(asset.rootNodeId) << "\",\n";
    out << "  \"nodes\": [\n";

    for (size_t i = 0; i < asset.nodes.size(); ++i) {
        const PrefabNode& node = asset.nodes[i];
        out << "    {\n";
        out << "      \"id\": \"" << EscapeString(node.id) << "\",\n";
        out << "      \"name\": \"" << EscapeString(node.name) << "\",\n";
        if (node.parentId.empty()) {
            out << "      \"parentId\": null,\n";
        } else {
            out << "      \"parentId\": \"" << EscapeString(node.parentId) << "\",\n";
        }
        if (!node.prefabPath.empty()) {
            out << "      \"prefabPath\": \"" << EscapeString(node.prefabPath) << "\",\n";
            out << "      \"overrides\": ";
            WriteJsonValue(out, PrefabComponentIO::NodeOverridesToJson(node.nestedOverrides), 6);
            out << ",\n";
        }
        if (!node.nestedFrom.empty()) {
            out << "      \"nestedFrom\": \"" << EscapeString(node.nestedFrom) << "\",\n";
            out << "      \"nestedNodeId\": \"" << EscapeString(node.nestedNodeId) << "\",\n";
        }
        out << "      \"transform\": {\n";
        out << "        \"position\": " << FormatVec3(node.localTransform.position) << ",\n";
        out << "        \"rotation\": " << FormatVec3(node.localTransform.rotation) << ",\n";
        out << "        \"scale\": " << FormatVec3(node.localTransform.scale) << "\n";
        out << "      },\n";

        out << "      \"components\": [\n";
        for (size_t c = 0; c < node.components.size(); ++c) {
            const PrefabComponentEntry& comp = node.components[c];
            out << "        {\n";
            out << "          \"type\": \"" << EscapeString(comp.type) << "\",\n";
            out << "          \"enabled\": " << (comp.enabled ? "true" : "false") << ",\n";
            out << "          \"data\": ";
            WriteJsonValue(out, comp.data, 10);
            out << "\n";
            out << "        }";
            if (c + 1 < node.components.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ],\n";

        out << "      \"exposed\": [";
        for (size_t ex = 0; ex < node.exposed.size(); ++ex) {
            if (ex) out << ", ";
            out << "\"" << EscapeString(node.exposed[ex]) << "\"";
        }
        out << "]\n";
        out << "    }";
        if (i + 1 < asset.nodes.size()) {
            out << ",\n";
        } else {
            out << "\n";
        }
    }

    out << "  ]\n";
    out << "}\n";
    return out.str();
}

bool PrefabSerializer::Serialize(const PrefabAsset& asset, const std::filesystem::path& filepath, std::string& outError) {
    namespace fs = std::filesystem;
    const std::string content = SerializeToString(asset);
    if (content.empty()) {
        outError = "Unable to serialize prefab.";
        return false;
    }

    std::error_code dirErr;
    fs::create_directories(filepath.parent_path(), dirErr);
    if (dirErr) {
        outError = "Unable to create prefab directory: " + dirErr.message();
        return false;
    }

    fs::path tempPath = filepath;
    tempPath += ".tmp";

    std::ofstream out(tempPath, std::ios::trunc);
    if (!out.is_open()) {
        outError = "Unable to open prefab file for writing.";
        return false;
    }
    out << content;
    out.close();

    std::error_code replaceErr;
    fs::remove(filepath, replaceErr);
    replaceErr.clear();
    fs::rename(tempPath, filepath, replaceErr);
    if (replaceErr) {
        outError = "Failed to replace prefab file: " + replaceErr.message();
        return false;
    }

    outError.clear();
    return true;
}
