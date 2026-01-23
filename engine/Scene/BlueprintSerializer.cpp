#include "BlueprintSerializer.h"

#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>

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
            bool first = true;
            for (const auto& [key, entry] : value.objectValue) {
                if (!first) out << ", ";
                out << "\"" << EscapeString(key) << "\": ";
                WriteJsonValue(out, entry, indent);
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
        bool first = true;
        for (const auto& [key, entry] : *value.AsObject()) {
            if (!first) out << ", ";
            out << "\"" << EscapeString(key) << "\": ";
            WritePropertyValue(out, entry, indent);
            first = false;
        }
        out << "}";
        return;
    }
}
}

bool BlueprintSerializer::Deserialize(BlueprintAsset& asset, const std::filesystem::path& filepath, std::string& outError) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        outError = "Unable to open blueprint file.";
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    JsonParser parser(content);
    JsonValue root;
    if (!parser.Parse(root, outError)) {
        return false;
    }
    if (!root.IsObject()) {
        outError = "Blueprint root must be an object.";
        return false;
    }

    asset = BlueprintAsset{};

    if (const JsonValue* versionValue = root.Find("version")) {
        if (versionValue->IsNumber()) {
            asset.version = static_cast<int>(versionValue->AsNumber());
        }
    }
    if (const JsonValue* idValue = root.Find("assetId")) {
        if (idValue->IsString()) {
            asset.assetId = idValue->AsString();
        }
    }
    if (const JsonValue* nameValue = root.Find("name")) {
        if (nameValue->IsString()) {
            asset.name = nameValue->AsString();
        } else {
            asset.name = filepath.stem().string();
        }
    } else {
        asset.name = filepath.stem().string();
    }

    const JsonValue* nodesValue = root.Find("nodes");
    if (!nodesValue || !nodesValue->IsArray()) {
        outError = "Blueprint missing nodes array.";
        return false;
    }

    for (const JsonValue& nodeValue : nodesValue->AsArray()) {
        if (!nodeValue.IsObject()) {
            outError = "Blueprint node entry must be an object.";
            return false;
        }

        BlueprintNode node;
        if (const JsonValue* idValue = nodeValue.Find("id")) {
            if (idValue->IsString()) {
                node.id = idValue->AsString();
            }
        }
        if (node.id.empty()) {
            outError = "Blueprint node missing id.";
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
            }
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
            if (componentsValue->IsObject()) {
                node.components = componentsValue->AsObject();
            }
        }

        if (const JsonValue* scriptValue = nodeValue.Find("script")) {
            if (scriptValue->IsObject()) {
                BlueprintScriptData script;
                if (const JsonValue* langValue = scriptValue->Find("language")) {
                    if (langValue->IsString()) {
                        script.language = langValue->AsString();
                    }
                }
                if (script.language.empty()) {
                    script.language = "lua";
                }
                if (const JsonValue* sourceValue = scriptValue->Find("source")) {
                    if (sourceValue->IsString()) {
                        script.source = sourceValue->AsString();
                    }
                }
                if (const JsonValue* varsValue = scriptValue->Find("defaultVars")) {
                    if (varsValue->IsObject()) {
                        for (const auto& [key, entry] : varsValue->AsObject()) {
                            script.defaultVars.emplace(key, ParsePropertyValue(entry));
                        }
                    }
                }
                node.script = std::move(script);
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

    outError.clear();
    return true;
}

bool BlueprintSerializer::Serialize(const BlueprintAsset& asset, const std::filesystem::path& filepath, std::string& outError) {
    std::ofstream out(filepath);
    if (!out.is_open()) {
        outError = "Unable to open blueprint file for writing.";
        return false;
    }

    out << "{\n";
    out << "  \"schema\": \"HephaestusBlueprint\",\n";
    out << "  \"version\": " << asset.version << ",\n";
    out << "  \"assetId\": \"" << EscapeString(asset.assetId) << "\",\n";
    out << "  \"name\": \"" << EscapeString(asset.name) << "\",\n";
    out << "  \"nodes\": [\n";

    for (size_t i = 0; i < asset.nodes.size(); ++i) {
        const BlueprintNode& node = asset.nodes[i];
        out << "    {\n";
        out << "      \"id\": \"" << EscapeString(node.id) << "\",\n";
        out << "      \"name\": \"" << EscapeString(node.name) << "\",\n";
        if (node.parentId.empty()) {
            out << "      \"parentId\": null,\n";
        } else {
            out << "      \"parentId\": \"" << EscapeString(node.parentId) << "\",\n";
        }
        out << "      \"transform\": {\n";
        out << "        \"position\": " << FormatVec3(node.localTransform.position) << ",\n";
        out << "        \"rotation\": " << FormatVec3(node.localTransform.rotation) << ",\n";
        out << "        \"scale\": " << FormatVec3(node.localTransform.scale) << "\n";
        out << "      },\n";

        out << "      \"components\": ";
        JsonValue componentsValue;
        componentsValue.type = JsonValue::Type::Object;
        componentsValue.objectValue = node.components;
        WriteJsonValue(out, componentsValue, 6);
        out << ",\n";

        if (node.script.has_value()) {
            out << "      \"script\": {\n";
            out << "        \"language\": \"" << EscapeString(node.script->language) << "\",\n";
            out << "        \"source\": \"" << EscapeString(node.script->source) << "\",\n";
            out << "        \"defaultVars\": {";
            bool first = true;
            for (const auto& [key, value] : node.script->defaultVars) {
                if (!first) out << ", ";
                out << "\"" << EscapeString(key) << "\": ";
                WritePropertyValue(out, value, 8);
                first = false;
            }
            out << "}\n";
            out << "      },\n";
        }

        if (!node.exposed.empty()) {
            out << "      \"exposed\": [";
            for (size_t e = 0; e < node.exposed.size(); ++e) {
                if (e) out << ", ";
                out << "\"" << EscapeString(node.exposed[e]) << "\"";
            }
            out << "]\n";
        } else {
            out << "      \"exposed\": []\n";
        }

        out << "    }";
        if (i + 1 < asset.nodes.size()) {
            out << ",";
        }
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";

    if (!out.good()) {
        outError = "Failed to write blueprint data.";
        return false;
    }

    outError.clear();
    return true;
}
