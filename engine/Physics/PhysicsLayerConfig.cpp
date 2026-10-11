#include "PhysicsLayerConfig.h"

#include "../Core/EngineLog.h"
#include "../Scene/Json.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace Physics {
namespace {

void WarnUnknownLayer(std::uint16_t id)
{
    static std::unordered_set<std::uint16_t> warned;
    if (!warned.insert(id).second) {
        return;
    }
    Hephaestus::EngineLog::Write(
        Hephaestus::EngineLogLevel::Warning,
        "Unknown physics layer id " + std::to_string(id) + " resolved to Default (0)",
        "Physics");
}

bool ParseBool(const JsonValue& value, bool fallback)
{
    if (value.IsBool()) {
        return value.boolValue;
    }
    return fallback;
}

} // namespace

PhysicsLayerConfig PhysicsLayerConfig::Compatibility()
{
    PhysicsLayerConfig config;
    config.m_Layers.push_back(PhysicsLayerDesc{kLayerDefault, "Default", true});
    config.SetCollides(kLayerDefault, kLayerDefault, true);
    return config;
}

PhysicsLayerConfig PhysicsLayerConfig::Seed()
{
    PhysicsLayerConfig config;
    const PhysicsLayerLoadResult loaded = LoadFromString(kPhysicsLayerSeedJson, config);
    if (!loaded.error.empty() || loaded.usedCompatibility) {
        return Compatibility();
    }
    return config;
}

std::filesystem::path PhysicsLayerConfig::ProjectFile(const std::filesystem::path& projectRoot)
{
    return projectRoot / ".hephaestus" / "physics_layers.json";
}

void PhysicsLayerConfig::ClearMatrix()
{
    for (std::uint16_t a = 0; a < kMaxGameplayLayers; ++a) {
        for (std::uint16_t b = 0; b < kMaxGameplayLayers; ++b) {
            m_Matrix[a][b] = false;
        }
    }
}

void PhysicsLayerConfig::SortLayers()
{
    std::sort(m_Layers.begin(), m_Layers.end(),
              [](const PhysicsLayerDesc& a, const PhysicsLayerDesc& b) { return a.id < b.id; });
}

bool PhysicsLayerConfig::HasLayer(std::uint16_t id) const
{
    return FindById(id) != nullptr;
}

bool PhysicsLayerConfig::IsTombstoned(std::uint16_t id) const
{
    return std::find(m_Tombstones.begin(), m_Tombstones.end(), id) != m_Tombstones.end();
}

const PhysicsLayerDesc* PhysicsLayerConfig::FindById(std::uint16_t id) const
{
    for (const PhysicsLayerDesc& layer : m_Layers) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

std::uint16_t PhysicsLayerConfig::FindIdByName(const std::string& name) const
{
    for (const PhysicsLayerDesc& layer : m_Layers) {
        if (layer.name == name) {
            return layer.id;
        }
    }
    return kInvalidLayer;
}

bool PhysicsLayerConfig::Collides(std::uint16_t a, std::uint16_t b) const
{
    if (a >= kMaxGameplayLayers || b >= kMaxGameplayLayers) {
        return false;
    }
    return m_Matrix[a][b];
}

void PhysicsLayerConfig::SetCollides(std::uint16_t a, std::uint16_t b, bool collides)
{
    if (a >= kMaxGameplayLayers || b >= kMaxGameplayLayers) {
        return;
    }
    m_Matrix[a][b] = collides;
    m_Matrix[b][a] = collides;
}

std::uint16_t PhysicsLayerConfig::Resolve(std::uint16_t id) const
{
    if (id >= kMaxGameplayLayers || IsTombstoned(id) || !HasLayer(id)) {
        WarnUnknownLayer(id);
        return kLayerDefault;
    }
    return id;
}

bool PhysicsLayerConfig::NameTaken(const std::string& name, std::uint16_t exceptId) const
{
    for (const PhysicsLayerDesc& layer : m_Layers) {
        if (layer.id != exceptId && layer.name == name) {
            return true;
        }
    }
    return false;
}

bool PhysicsLayerConfig::AddUserLayer(const std::string& name, std::string& outError)
{
    outError.clear();
    if (name.empty()) {
        outError = "Layer name is empty";
        return false;
    }
    if (m_Layers.size() >= kMaxGameplayLayers) {
        outError = "Physics layer cap is 16";
        return false;
    }
    if (NameTaken(name, kInvalidLayer)) {
        outError = "Layer name already exists";
        return false;
    }
    std::uint16_t id = kInvalidLayer;
    for (std::uint16_t candidate = 4; candidate < kMaxGameplayLayers; ++candidate) {
        if (!HasLayer(candidate) && !IsTombstoned(candidate)) {
            id = candidate;
            break;
        }
    }
    if (id == kInvalidLayer) {
        outError = "No free physics layer id";
        return false;
    }
    m_Layers.push_back(PhysicsLayerDesc{id, name, false});
    for (const PhysicsLayerDesc& layer : m_Layers) {
        SetCollides(id, layer.id, true);
    }
    SortLayers();
    return true;
}

bool PhysicsLayerConfig::RenameLayer(std::uint16_t id, const std::string& name, std::string& outError)
{
    outError.clear();
    if (name.empty()) {
        outError = "Layer name is empty";
        return false;
    }
    PhysicsLayerDesc* layer = nullptr;
    for (PhysicsLayerDesc& entry : m_Layers) {
        if (entry.id == id) {
            layer = &entry;
            break;
        }
    }
    if (!layer) {
        outError = "Layer id does not exist";
        return false;
    }
    if (NameTaken(name, id)) {
        outError = "Layer name already exists";
        return false;
    }
    layer->name = name;
    return true;
}

bool PhysicsLayerConfig::DeleteUserLayer(std::uint16_t id, std::string& outError)
{
    outError.clear();
    if (id < 4) {
        outError = "Built-in physics layers cannot be deleted";
        return false;
    }
    const auto it = std::find_if(m_Layers.begin(), m_Layers.end(),
                                 [id](const PhysicsLayerDesc& layer) { return layer.id == id; });
    if (it == m_Layers.end()) {
        outError = "Layer id does not exist";
        return false;
    }
    if (it->builtin) {
        outError = "Built-in physics layers cannot be deleted";
        return false;
    }
    m_Layers.erase(it);
    if (!IsTombstoned(id)) {
        m_Tombstones.push_back(id);
    }
    for (std::uint16_t other = 0; other < kMaxGameplayLayers; ++other) {
        m_Matrix[id][other] = false;
        m_Matrix[other][id] = false;
    }
    return true;
}

PhysicsLayerLoadResult PhysicsLayerConfig::LoadFromString(const std::string& json, PhysicsLayerConfig& outConfig)
{
    PhysicsLayerLoadResult result;
    JsonValue root;
    std::string error;
    JsonParser parser(json);
    if (!parser.Parse(root, error) || !root.IsObject()) {
        outConfig = Compatibility();
        result.usedCompatibility = true;
        result.error = error.empty() ? "Physics layer file is not an object" : error;
        Hephaestus::EngineLog::Write(Hephaestus::EngineLogLevel::Error, result.error, "Physics");
        return result;
    }
    const JsonValue* version = root.Find("version");
    if (!version || !version->IsNumber() || static_cast<int>(version->AsNumber()) != 1) {
        outConfig = Compatibility();
        result.usedCompatibility = true;
        result.error = "Physics layer file version is not 1";
        Hephaestus::EngineLog::Write(Hephaestus::EngineLogLevel::Error, result.error, "Physics");
        return result;
    }

    PhysicsLayerConfig config;
    const JsonValue* layers = root.Find("layers");
    if (!layers || !layers->IsArray()) {
        outConfig = Compatibility();
        result.usedCompatibility = true;
        result.error = "Physics layer file is missing layers";
        Hephaestus::EngineLog::Write(Hephaestus::EngineLogLevel::Error, result.error, "Physics");
        return result;
    }

    for (const JsonValue& entry : layers->AsArray()) {
        if (!entry.IsObject()) {
            continue;
        }
        const JsonValue* idValue = entry.Find("id");
        const JsonValue* nameValue = entry.Find("name");
        if (!idValue || !idValue->IsNumber() || !nameValue || !nameValue->IsString()) {
            continue;
        }
        const int rawId = static_cast<int>(idValue->AsNumber());
        if (rawId < 0 || rawId >= static_cast<int>(kMaxGameplayLayers)) {
            continue;
        }
        const auto id = static_cast<std::uint16_t>(rawId);
        if (config.HasLayer(id)) {
            continue;
        }
        PhysicsLayerDesc desc;
        desc.id = id;
        desc.name = nameValue->AsString();
        desc.builtin = id < 4;
        if (const JsonValue* builtin = entry.Find("builtin")) {
            if (id >= 4) {
                desc.builtin = ParseBool(*builtin, false);
            }
        }
        if (desc.name.empty()) {
            desc.name = "Layer" + std::to_string(id);
        }
        config.m_Layers.push_back(std::move(desc));
    }

    if (!config.HasLayer(kLayerDefault)) {
        config.m_Layers.push_back(PhysicsLayerDesc{kLayerDefault, "Default", true});
    }
    config.SortLayers();

    if (const JsonValue* tombstones = root.Find("tombstones"); tombstones && tombstones->IsArray()) {
        for (const JsonValue& entry : tombstones->AsArray()) {
            if (!entry.IsNumber()) {
                continue;
            }
            const int rawId = static_cast<int>(entry.AsNumber());
            if (rawId < 4 || rawId >= static_cast<int>(kMaxGameplayLayers)) {
                continue;
            }
            const auto id = static_cast<std::uint16_t>(rawId);
            config.m_Layers.erase(
                std::remove_if(config.m_Layers.begin(), config.m_Layers.end(),
                               [id](const PhysicsLayerDesc& layer) { return layer.id == id; }),
                config.m_Layers.end());
            if (!config.IsTombstoned(id)) {
                config.m_Tombstones.push_back(id);
            }
        }
    }

    for (const PhysicsLayerDesc& a : config.m_Layers) {
        for (const PhysicsLayerDesc& b : config.m_Layers) {
            config.SetCollides(a.id, b.id, true);
        }
    }
    if (const JsonValue* matrix = root.Find("matrix"); matrix && matrix->IsArray()) {
        for (const JsonValue& entry : matrix->AsArray()) {
            if (!entry.IsArray() || entry.AsArray().size() < 3) {
                continue;
            }
            const JsonValue& aValue = entry.AsArray()[0];
            const JsonValue& bValue = entry.AsArray()[1];
            const JsonValue& collideValue = entry.AsArray()[2];
            if (!aValue.IsNumber() || !bValue.IsNumber()) {
                continue;
            }
            const int rawA = static_cast<int>(aValue.AsNumber());
            const int rawB = static_cast<int>(bValue.AsNumber());
            if (rawA < 0 || rawB < 0 || rawA >= static_cast<int>(kMaxGameplayLayers) ||
                rawB >= static_cast<int>(kMaxGameplayLayers)) {
                continue;
            }
            config.SetCollides(static_cast<std::uint16_t>(rawA), static_cast<std::uint16_t>(rawB),
                               ParseBool(collideValue, true));
        }
    }

    outConfig = std::move(config);
    return result;
}

PhysicsLayerLoadResult PhysicsLayerConfig::LoadFromFile(const std::filesystem::path& path, PhysicsLayerConfig& outConfig)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        outConfig = Compatibility();
        PhysicsLayerLoadResult result;
        result.missingFile = true;
        result.usedCompatibility = true;
        return result;
    }
    std::ifstream input(path);
    if (!input) {
        outConfig = Compatibility();
        PhysicsLayerLoadResult result;
        result.usedCompatibility = true;
        result.error = "Failed to open physics layer file";
        Hephaestus::EngineLog::Write(Hephaestus::EngineLogLevel::Error, result.error, "Physics");
        return result;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return LoadFromString(buffer.str(), outConfig);
}

namespace {

std::string EscapeJson(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

} // namespace

std::string PhysicsLayerConfig::ToJson() const
{
    std::ostringstream out;
    out << "{\n  \"version\": 1,\n  \"layers\": [\n";
    for (size_t i = 0; i < m_Layers.size(); ++i) {
        const PhysicsLayerDesc& layer = m_Layers[i];
        out << "    { \"id\": " << layer.id << ", \"name\": \"" << EscapeJson(layer.name)
            << "\", \"builtin\": " << (layer.builtin ? "true" : "false") << " }";
        if (i + 1 < m_Layers.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n  \"tombstones\": [";
    for (size_t i = 0; i < m_Tombstones.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << m_Tombstones[i];
    }
    out << "],\n  \"matrix\": [\n";
    bool first = true;
    for (const PhysicsLayerDesc& a : m_Layers) {
        for (const PhysicsLayerDesc& b : m_Layers) {
            if (a.id > b.id) {
                continue;
            }
            if (!first) {
                out << ",\n";
            }
            first = false;
            out << "    [" << a.id << ", " << b.id << ", " << (Collides(a.id, b.id) ? "true" : "false") << "]";
        }
    }
    if (!first) {
        out << "\n";
    }
    out << "  ]\n}\n";
    return out.str();
}

bool PhysicsLayerConfig::SaveToFile(const std::filesystem::path& path, std::string& outError) const
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        outError = "Failed to write physics layer file";
        return false;
    }
    output << ToJson();
    if (!output) {
        outError = "Failed to write physics layer file";
        return false;
    }
    return true;
}

} // namespace Physics
