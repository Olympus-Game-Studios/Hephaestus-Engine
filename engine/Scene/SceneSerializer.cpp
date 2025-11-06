#include "SceneSerializer.h"

#include "Scene.h"
#include "Components.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <unordered_map>
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

struct JsonValue {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::unordered_map<std::string, JsonValue> objectValue;

    bool IsNull() const { return type == Type::Null; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    bool AsBool(bool fallback = false) const { return IsBool() ? boolValue : fallback; }
    double AsNumber(double fallback = 0.0) const { return IsNumber() ? numberValue : fallback; }
    const std::string& AsString() const { return stringValue; }
    const std::vector<JsonValue>& AsArray() const { return arrayValue; }
    const std::unordered_map<std::string, JsonValue>& AsObject() const { return objectValue; }

    const JsonValue* Find(const std::string& key) const {
        if (!IsObject()) {
            return nullptr;
        }
        auto it = objectValue.find(key);
        return it != objectValue.end() ? &it->second : nullptr;
    }
};

static void AppendUtf8(std::string& out, unsigned int codepoint)
{
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

class JsonParser {
public:
    explicit JsonParser(const std::string& source)
        : m_Begin(source.data()), m_Cursor(source.data()), m_End(source.data() + source.size())
    {
    }

    bool Parse(JsonValue& outValue, std::string& outError)
    {
        outError.clear();
        SkipWhitespace();
        if (!ParseValue(outValue)) {
            outError = m_Error.empty() ? std::string("Malformed scene file.") : m_Error;
            return false;
        }
        SkipWhitespace();
        if (m_Cursor != m_End) {
            outError = "Unexpected trailing characters in scene file.";
            return false;
        }
        return true;
    }

private:
    bool ParseValue(JsonValue& value)
    {
        SkipWhitespace();
        if (m_Cursor == m_End) {
            return SetError("Unexpected end of input.");
        }

        char ch = *m_Cursor;
        switch (ch) {
        case '"':
            return ParseString(value);
        case '{':
            return ParseObject(value);
        case '[':
            return ParseArray(value);
        case 't':
            return ParseLiteral("true", JsonValue::Type::Bool, value, true);
        case 'f':
            return ParseLiteral("false", JsonValue::Type::Bool, value, false);
        case 'n':
            return ParseLiteral("null", JsonValue::Type::Null, value, false);
        default:
            if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
                return ParseNumber(value);
            }
            break;
        }

        return SetError("Unexpected token while parsing scene file.");
    }

    bool ParseString(JsonValue& value)
    {
        std::string result;
        if (!ParseStringInternal(result)) {
            return false;
        }
        value.type = JsonValue::Type::String;
        value.stringValue = std::move(result);
        return true;
    }

    bool ParseStringInternal(std::string& out)
    {
        if (*m_Cursor != '"') {
            return SetError("Expected '\"' at beginning of string.");
        }
        ++m_Cursor;

        while (m_Cursor != m_End) {
            char ch = *m_Cursor++;
            if (ch == '"') {
                return true;
            }
            if (ch == '\\') {
                if (m_Cursor == m_End) {
                    return SetError("Incomplete escape sequence in string.");
                }
                char esc = *m_Cursor++;
                switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (std::distance(m_Cursor, m_End) < 4) {
                        return SetError("Incomplete unicode escape.");
                    }
                    unsigned int codepoint = 0;
                    for (int i = 0; i < 4; ++i) {
                        char hex = *m_Cursor++;
                        codepoint <<= 4;
                        if (hex >= '0' && hex <= '9') {
                            codepoint += static_cast<unsigned int>(hex - '0');
                        } else if (hex >= 'a' && hex <= 'f') {
                            codepoint += static_cast<unsigned int>(10 + hex - 'a');
                        } else if (hex >= 'A' && hex <= 'F') {
                            codepoint += static_cast<unsigned int>(10 + hex - 'A');
                        } else {
                            return SetError("Invalid unicode escape sequence.");
                        }
                    }
                    AppendUtf8(out, codepoint);
                    break;
                }
                default:
                    return SetError("Unknown escape sequence in string.");
                }
            } else {
                if (static_cast<unsigned char>(ch) < 0x20) {
                    return SetError("Control character in string literal.");
                }
                out.push_back(ch);
            }
        }

        return SetError("Unterminated string literal.");
    }

    bool ParseNumber(JsonValue& value)
    {
        const char* start = m_Cursor;
        if (*m_Cursor == '-') {
            ++m_Cursor;
        }
        while (m_Cursor != m_End && std::isdigit(static_cast<unsigned char>(*m_Cursor))) {
            ++m_Cursor;
        }
        if (m_Cursor != m_End && *m_Cursor == '.') {
            ++m_Cursor;
            while (m_Cursor != m_End && std::isdigit(static_cast<unsigned char>(*m_Cursor))) {
                ++m_Cursor;
            }
        }
        if (m_Cursor != m_End && (*m_Cursor == 'e' || *m_Cursor == 'E')) {
            ++m_Cursor;
            if (m_Cursor != m_End && (*m_Cursor == '+' || *m_Cursor == '-')) {
                ++m_Cursor;
            }
            while (m_Cursor != m_End && std::isdigit(static_cast<unsigned char>(*m_Cursor))) {
                ++m_Cursor;
            }
        }

        char* parseEnd = nullptr;
        double parsed = std::strtod(start, &parseEnd);
        if (parseEnd != m_Cursor) {
            return SetError("Failed to parse number literal.");
        }

        value.type = JsonValue::Type::Number;
        value.numberValue = parsed;
        return true;
    }

    bool ParseLiteral(const char* literal, JsonValue::Type type, JsonValue& value, bool boolValue)
    {
        size_t len = std::strlen(literal);
        if (static_cast<size_t>(std::distance(m_Cursor, m_End)) < len) {
            return SetError("Unexpected end while parsing literal.");
        }
        if (std::strncmp(m_Cursor, literal, len) != 0) {
            return SetError("Invalid literal in scene file.");
        }
        m_Cursor += len;
        value.type = type;
        value.boolValue = boolValue;
        value.numberValue = boolValue ? 1.0 : 0.0;
        return true;
    }

    bool ParseArray(JsonValue& value)
    {
        value.type = JsonValue::Type::Array;
        value.arrayValue.clear();
        ++m_Cursor; // skip [
        SkipWhitespace();
        if (m_Cursor == m_End) {
            return SetError("Unexpected end while parsing array.");
        }
        if (*m_Cursor == ']') {
            ++m_Cursor;
            return true;
        }

        while (true) {
            JsonValue element;
            if (!ParseValue(element)) {
                return false;
            }
            value.arrayValue.push_back(std::move(element));
            SkipWhitespace();
            if (m_Cursor == m_End) {
                return SetError("Unexpected end while parsing array.");
            }
            char ch = *m_Cursor;
            if (ch == ',') {
                ++m_Cursor;
                SkipWhitespace();
                continue;
            }
            if (ch == ']') {
                ++m_Cursor;
                break;
            }
            return SetError("Expected ',' or ']' in array.");
        }
        return true;
    }

    bool ParseObject(JsonValue& value)
    {
        value.type = JsonValue::Type::Object;
        value.objectValue.clear();
        ++m_Cursor; // skip {
        SkipWhitespace();
        if (m_Cursor == m_End) {
            return SetError("Unexpected end while parsing object.");
        }
        if (*m_Cursor == '}') {
            ++m_Cursor;
            return true;
        }

        while (true) {
            std::string key;
            if (!ParseStringInternal(key)) {
                return false;
            }
            SkipWhitespace();
            if (m_Cursor == m_End || *m_Cursor != ':') {
                return SetError("Expected ':' after object key.");
            }
            ++m_Cursor;
            SkipWhitespace();

            JsonValue memberValue;
            if (!ParseValue(memberValue)) {
                return false;
            }
            value.objectValue.emplace(std::move(key), std::move(memberValue));
            SkipWhitespace();
            if (m_Cursor == m_End) {
                return SetError("Unexpected end while parsing object.");
            }
            char ch = *m_Cursor;
            if (ch == ',') {
                ++m_Cursor;
                SkipWhitespace();
                continue;
            }
            if (ch == '}') {
                ++m_Cursor;
                break;
            }
            return SetError("Expected ',' or '}' in object.");
        }
        return true;
    }

    void SkipWhitespace()
    {
        while (m_Cursor != m_End && std::isspace(static_cast<unsigned char>(*m_Cursor))) {
            ++m_Cursor;
        }
    }

    bool SetError(const char* message)
    {
        if (m_Error.empty()) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            size_t position = static_cast<size_t>(std::distance(m_Begin, m_Cursor));
            oss << message << " (at byte " << position << ")";
            m_Error = oss.str();
        }
        return false;
    }

    const char* m_Begin = nullptr;
    const char* m_Cursor = nullptr;
    const char* m_End = nullptr;
    std::string m_Error;
};

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
} // namespace

SceneSerializer::SceneSerializer(const Scene& scene)
    : m_Scene(scene)
{
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
    stream << "  \"entities\": [\n";

    for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
        entt::entity handle = entities[entityIndex];

        const TagComponent* tag = registry.try_get<TagComponent>(handle);
    const TransformComponent* transform = registry.try_get<TransformComponent>(handle);
    const MeshRendererComponent* mesh = registry.try_get<MeshRendererComponent>(handle);
    const MeshColliderComponent* collider = registry.try_get<MeshColliderComponent>(handle);
        const MaterialComponent* material = registry.try_get<MaterialComponent>(handle);
        const LightComponent* light = registry.try_get<LightComponent>(handle);
        const CameraComponent* camera = registry.try_get<CameraComponent>(handle);
        const SelectionComponent* selection = registry.try_get<SelectionComponent>(handle);

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

        if (mesh) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"MeshRenderer\": {\n";
            oss << "          \"visible\": " << (mesh->visible ? "true" : "false") << ",\n";
            oss << "          \"mesh\": ";
            if (mesh->HasValidMesh()) {
                oss << static_cast<uint64_t>(mesh->meshHandle) << ",\n";
            } else {
                oss << "null,\n";
            }
            oss << "          \"meshSource\": ";
            if (!mesh->meshAssetPath.empty()) {
                oss << "\"" << EscapeString(mesh->meshAssetPath) << "\"";
            } else {
                oss << "null";
            }
            oss << ",\n";
            oss << "          \"meshImported\": " << (mesh->meshImported ? "true" : "false") << ",\n";
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
            oss << "          \"debugDraw\": " << (collider->debugDraw ? "true" : "false") << "\n";
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
            oss << "          \"useAlbedoTexture\": " << (material->useAlbedoTexture ? "true" : "false") << ",\n";
            oss << "          \"albedoTexturePath\": \"" << EscapeString(material->albedoTexturePath) << "\",\n";
            oss << "          \"useNormalTexture\": " << (material->useNormalTexture ? "true" : "false") << ",\n";
            oss << "          \"normalTexturePath\": \"" << EscapeString(material->normalTexturePath) << "\",\n";
            oss << "          \"useRoughnessTexture\": " << (material->useRoughnessTexture ? "true" : "false") << ",\n";
            oss << "          \"roughnessTexturePath\": \"" << EscapeString(material->roughnessTexturePath) << "\",\n";
            oss << "          \"useAoTexture\": " << (material->useAoTexture ? "true" : "false") << ",\n";
            oss << "          \"aoTexturePath\": \"" << EscapeString(material->aoTexturePath) << "\"\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (light) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Light\": {\n";
            oss << "          \"type\": \"" << LightTypeToString(light->type) << "\",\n";
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
            oss << "          \"direction\": " << FormatVec3(light->direction) << "\n";
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
            oss << "          \"isPrimary\": " << (camera->isPrimary ? "true" : "false") << "\n";
            oss << "        }";
            componentBlocks.push_back(oss.str());
        }

        if (selection) {
            std::ostringstream oss;
            oss.imbue(std::locale::classic());
            oss << "        \"Selection\": {\n";
            oss << "          \"selected\": " << (selection->selected ? "true" : "false") << "\n";
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
        Entity entity = scene.CreateEntityWithID(static_cast<entt::entity>(entityId), entityName);
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
            const JsonValue* meshHandleValue = meshObj->Find("mesh");
            if (meshHandleValue) {
                if (meshHandleValue->IsNumber()) {
                    double rawHandle = meshHandleValue->AsNumber();
                    if (rawHandle >= 0.0) {
                        mesh.meshHandle = static_cast<size_t>(rawHandle);
                    }
                } else {
                    mesh.meshHandle = MeshRendererComponent::InvalidMesh;
                }
            }
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
            if (const JsonValue* useAoValue = materialObj->Find("useAoTexture")) {
                material.useAoTexture = ParseBoolLike(*useAoValue, material.useAoTexture);
            }
            if (const JsonValue* aoPathValue = materialObj->Find("aoTexturePath")) {
                if (aoPathValue->IsString()) {
                    material.aoTexturePath = aoPathValue->AsString();
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
            entity.AddComponent<LightComponent>(light);
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
            if (const JsonValue* primaryValue = cameraObj->Find("isPrimary")) {
                if (primaryValue->IsBool()) {
                    camera.isPrimary = primaryValue->AsBool();
                }
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

    return true;
}
