#include "MaterialEditorPanel.h"

#include "imgui.h"
#include "imnodes.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <unordered_map>
#include <vector>

namespace MaterialEditor {
namespace {

bool InputText(const char* label, std::string& value) {
    char buffer[128];
    std::strncpy(buffer, value.c_str(), sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';
    bool edited = ImGui::InputText(label, buffer, sizeof(buffer));
    if (edited) {
        value = buffer;
    }
    return edited;
}

std::string EscapeJson(const std::string& value) {
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

std::string NodeTypeToString(NodeType type) {
    switch (type) {
    case NodeType::MaterialOutput: return "MaterialOutput";
    case NodeType::ConstantFloat: return "ConstantFloat";
    case NodeType::ConstantVec3: return "ConstantVec3";
    case NodeType::ColorParameter: return "ColorParameter";
    case NodeType::FloatParameter: return "FloatParameter";
    case NodeType::AlbedoTextureSample: return "AlbedoTextureSample";
    case NodeType::NormalTextureSample: return "NormalTextureSample";
    case NodeType::RoughnessTextureSample: return "RoughnessTextureSample";
    case NodeType::AoTextureSample: return "AoTextureSample";
    case NodeType::WorldAlignedTexture: return "WorldAlignedTexture";
    case NodeType::Add: return "Add";
    case NodeType::Multiply: return "Multiply";
    default: return "Unknown";
    }
}

NodeType StringToNodeType(const std::string& str) {
    if (str == "MaterialOutput") return NodeType::MaterialOutput;
    if (str == "ConstantFloat") return NodeType::ConstantFloat;
    if (str == "ConstantVec3") return NodeType::ConstantVec3;
    if (str == "ColorParameter") return NodeType::ColorParameter;
    if (str == "FloatParameter") return NodeType::FloatParameter;
    if (str == "AlbedoTextureSample") return NodeType::AlbedoTextureSample;
    if (str == "NormalTextureSample") return NodeType::NormalTextureSample;
    if (str == "RoughnessTextureSample") return NodeType::RoughnessTextureSample;
    if (str == "AoTextureSample") return NodeType::AoTextureSample;
    if (str == "WorldAlignedTexture") return NodeType::WorldAlignedTexture;
    if (str == "Add") return NodeType::Add;
    if (str == "Multiply") return NodeType::Multiply;
    return NodeType::MaterialOutput;
}

bool SaveMaterialAssetFile(const std::filesystem::path& path,
                           const MaterialComponent& material,
                           const std::string& displayName,
                           const Graph& graph,
                           ImNodesEditorContext* editorContext,
                           std::string& outError) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        outError = "Unable to open material file for writing.";
        return false;
    }

    out << std::fixed << std::setprecision(4);
    out << "{\n";
    out << "  \"name\": \"" << EscapeJson(displayName) << "\",\n";
    out << "  \"albedo\": [" << material.albedo.x << ", " << material.albedo.y << ", " << material.albedo.z << "],\n";
    out << "  \"metallic\": " << material.metallic << ",\n";
    out << "  \"roughness\": " << material.roughness << ",\n";
    out << "  \"ao\": " << material.ao << ",\n";
    out << "  \"emissive\": [" << material.emissive.x << ", " << material.emissive.y << ", " << material.emissive.z << "],\n";
    out << "  \"emissiveStrength\": " << material.emissiveStrength << ",\n";
    out << "  \"useAlbedoTexture\": " << (material.useAlbedoTexture ? "true" : "false") << ",\n";
    out << "  \"albedoTexturePath\": \"" << EscapeJson(material.albedoTexturePath) << "\",\n";
    out << "  \"useNormalTexture\": " << (material.useNormalTexture ? "true" : "false") << ",\n";
    out << "  \"normalTexturePath\": \"" << EscapeJson(material.normalTexturePath) << "\",\n";
    out << "  \"useRoughnessTexture\": " << (material.useRoughnessTexture ? "true" : "false") << ",\n";
    out << "  \"roughnessTexturePath\": \"" << EscapeJson(material.roughnessTexturePath) << "\",\n";
    out << "  \"useAoTexture\": " << (material.useAoTexture ? "true" : "false") << ",\n";
    out << "  \"aoTexturePath\": \"" << EscapeJson(material.aoTexturePath) << "\",\n";
    
    // Save node graph
    out << "  \"graph\": {\n";
    out << "    \"nodes\": [\n";
    
    const auto& nodes = graph.GetNodes();
    
    // If we have an editor context, get node positions
    bool hasPositions = (editorContext != nullptr);
    if (hasPositions) {
        ImNodes::EditorContextSet(editorContext);
    }
    
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node& node = nodes[i];
        out << "      {\n";
        out << "        \"id\": " << node.id << ",\n";
        out << "        \"type\": \"" << NodeTypeToString(node.type) << "\",\n";
        out << "        \"displayName\": \"" << EscapeJson(node.displayName) << "\",\n";
        out << "        \"scalar\": " << node.scalar << ",\n";
        out << "        \"color\": [" << node.color.x << ", " << node.color.y << ", " << node.color.z << "],\n";
        out << "        \"resourcePath\": \"" << EscapeJson(node.resourcePath) << "\",\n";
        out << "        \"worldAlignedScale\": " << node.worldAlignedScale << ",\n";
        
        // Save node position from ImNodes
        if (hasPositions) {
            ImVec2 pos = ImNodes::GetNodeEditorSpacePos(node.id);
            out << "        \"posX\": " << pos.x << ",\n";
            out << "        \"posY\": " << pos.y << ",\n";
        } else {
            out << "        \"posX\": " << node.posX << ",\n";
            out << "        \"posY\": " << node.posY << ",\n";
        }
        
        // Save input pin IDs
        out << "        \"inputPins\": [";
        for (size_t j = 0; j < node.inputs.size(); ++j) {
            out << node.inputs[j];
            if (j < node.inputs.size() - 1) out << ", ";
        }
        out << "],\n";
        
        // Save output pin IDs
        out << "        \"outputPins\": [";
        for (size_t j = 0; j < node.outputs.size(); ++j) {
            out << node.outputs[j];
            if (j < node.outputs.size() - 1) out << ", ";
        }
        out << "]\n";
        
        out << "      }";
        if (i < nodes.size() - 1) out << ",";
        out << "\n";
    }
    
    if (hasPositions) {
        ImNodes::EditorContextSet(nullptr);
    }
    
    out << "    ],\n";
    out << "    \"links\": [\n";
    
    const auto& links = graph.GetLinks();
    for (size_t i = 0; i < links.size(); ++i) {
        const Link& link = links[i];
        out << "      {\n";
        out << "        \"id\": " << link.id << ",\n";
        out << "        \"startPin\": " << link.startPin << ",\n";
        out << "        \"endPin\": " << link.endPin << "\n";
        out << "      }";
        if (i < links.size() - 1) out << ",";
        out << "\n";
    }
    
    out << "    ]\n";
    out << "  }\n";
    out << "}\n";

    if (!out.good()) {
        outError = "Failed to write material data.";
        return false;
    }

    outError.clear();
    return true;
}

bool LoadMaterialAssetFile(const std::filesystem::path& path,
                           MaterialComponent& outMaterial,
                           std::string& outDisplayName,
                           Graph& outGraph,
                           std::string& outError) {
    std::ifstream in(path);
    if (!in) {
        outError = "Unable to open material file for reading.";
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    // Simple JSON parser (reusing pattern from SceneSerializer)
    struct JsonValue {
        enum class Type { Null, Bool, Number, String, Array, Object };
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

        const JsonValue* Find(const std::string& key) const {
            if (!IsObject()) return nullptr;
            auto it = objectValue.find(key);
            return it != objectValue.end() ? &it->second : nullptr;
        }
    };

    // Minimal JSON parser
    auto skipWhitespace = [](const char*& p, const char* end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    };

    std::function<bool(const char*&, const char*, JsonValue&, std::string&)> parseValue;
    auto parseString = [&](const char*& p, const char* end, std::string& out) -> bool {
        if (*p != '"') return false;
        ++p;
        out.clear();
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                switch (*p) {
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case '\\': out += '\\'; break;
                    case '"': out += '"'; break;
                    default: out += *p; break;
                }
            } else {
                out += *p;
            }
            ++p;
        }
        if (p >= end) return false;
        ++p; // skip closing quote
        return true;
    };

    auto parseNumber = [](const char*& p, const char* end, double& out) -> bool {
        char* parseEnd;
        out = std::strtod(p, &parseEnd);
        if (parseEnd == p) return false;
        p = parseEnd;
        return true;
    };

    auto parseArray = [&](const char*& p, const char* end, JsonValue& value, std::string& err) -> bool {
        if (*p != '[') return false;
        ++p;
        value.type = JsonValue::Type::Array;
        value.arrayValue.clear();
        skipWhitespace(p, end);
        if (p < end && *p == ']') { ++p; return true; }
        while (p < end) {
            JsonValue elem;
            if (!parseValue(p, end, elem, err)) return false;
            value.arrayValue.push_back(elem);
            skipWhitespace(p, end);
            if (p < end && *p == ',') { ++p; skipWhitespace(p, end); }
            else if (p < end && *p == ']') { ++p; return true; }
            else return false;
        }
        return false;
    };

    auto parseObject = [&](const char*& p, const char* end, JsonValue& value, std::string& err) -> bool {
        if (*p != '{') return false;
        ++p;
        value.type = JsonValue::Type::Object;
        value.objectValue.clear();
        skipWhitespace(p, end);
        if (p < end && *p == '}') { ++p; return true; }
        while (p < end) {
            skipWhitespace(p, end);
            std::string key;
            if (!parseString(p, end, key)) return false;
            skipWhitespace(p, end);
            if (p >= end || *p != ':') return false;
            ++p;
            skipWhitespace(p, end);
            JsonValue val;
            if (!parseValue(p, end, val, err)) return false;
            value.objectValue[key] = val;
            skipWhitespace(p, end);
            if (p < end && *p == ',') { ++p; }
            else if (p < end && *p == '}') { ++p; return true; }
            else return false;
        }
        return false;
    };

    parseValue = [&](const char*& p, const char* end, JsonValue& value, std::string& err) -> bool {
        skipWhitespace(p, end);
        if (p >= end) return false;
        if (*p == '"') {
            value.type = JsonValue::Type::String;
            return parseString(p, end, value.stringValue);
        } else if (*p == '{') {
            return parseObject(p, end, value, err);
        } else if (*p == '[') {
            return parseArray(p, end, value, err);
        } else if (*p == 't' && p + 3 < end && std::strncmp(p, "true", 4) == 0) {
            value.type = JsonValue::Type::Bool;
            value.boolValue = true;
            p += 4;
            return true;
        } else if (*p == 'f' && p + 4 < end && std::strncmp(p, "false", 5) == 0) {
            value.type = JsonValue::Type::Bool;
            value.boolValue = false;
            p += 5;
            return true;
        } else if (*p == 'n' && p + 3 < end && std::strncmp(p, "null", 4) == 0) {
            value.type = JsonValue::Type::Null;
            p += 4;
            return true;
        } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
            value.type = JsonValue::Type::Number;
            return parseNumber(p, end, value.numberValue);
        }
        return false;
    };

    JsonValue root;
    const char* p = content.c_str();
    const char* end = p + content.size();
    if (!parseValue(p, end, root, outError)) {
        outError = "Failed to parse material JSON.";
        return false;
    }

    if (!root.IsObject()) {
        outError = "Material file root must be an object.";
        return false;
    }

    // Parse material properties
    if (const JsonValue* name = root.Find("name")) {
        if (name->IsString()) {
            outDisplayName = name->AsString();
        }
    }

    auto parseVec3 = [](const JsonValue& val, Vec3& out) {
        if (val.IsArray() && val.arrayValue.size() >= 3) {
            if (val.arrayValue[0].IsNumber()) out.x = static_cast<float>(val.arrayValue[0].AsNumber());
            if (val.arrayValue[1].IsNumber()) out.y = static_cast<float>(val.arrayValue[1].AsNumber());
            if (val.arrayValue[2].IsNumber()) out.z = static_cast<float>(val.arrayValue[2].AsNumber());
        }
    };

    auto parseFloat3 = [](const JsonValue& val, Float3& out) {
        if (val.IsArray() && val.arrayValue.size() >= 3) {
            if (val.arrayValue[0].IsNumber()) out.x = static_cast<float>(val.arrayValue[0].AsNumber());
            if (val.arrayValue[1].IsNumber()) out.y = static_cast<float>(val.arrayValue[1].AsNumber());
            if (val.arrayValue[2].IsNumber()) out.z = static_cast<float>(val.arrayValue[2].AsNumber());
        }
    };

    if (const JsonValue* albedo = root.Find("albedo")) {
        parseVec3(*albedo, outMaterial.albedo);
    }
    if (const JsonValue* metallic = root.Find("metallic")) {
        if (metallic->IsNumber()) outMaterial.metallic = static_cast<float>(metallic->AsNumber());
    }
    if (const JsonValue* roughness = root.Find("roughness")) {
        if (roughness->IsNumber()) outMaterial.roughness = static_cast<float>(roughness->AsNumber());
    }
    if (const JsonValue* ao = root.Find("ao")) {
        if (ao->IsNumber()) outMaterial.ao = static_cast<float>(ao->AsNumber());
    }
    if (const JsonValue* emissive = root.Find("emissive")) {
        parseVec3(*emissive, outMaterial.emissive);
    }
    if (const JsonValue* emissiveStrength = root.Find("emissiveStrength")) {
        if (emissiveStrength->IsNumber()) outMaterial.emissiveStrength = static_cast<float>(emissiveStrength->AsNumber());
    }
    if (const JsonValue* useAlbedoTexture = root.Find("useAlbedoTexture")) {
        if (useAlbedoTexture->IsBool()) outMaterial.useAlbedoTexture = useAlbedoTexture->AsBool();
    }
    if (const JsonValue* albedoTexturePath = root.Find("albedoTexturePath")) {
        if (albedoTexturePath->IsString()) outMaterial.albedoTexturePath = albedoTexturePath->AsString();
    }
    if (const JsonValue* useNormalTexture = root.Find("useNormalTexture")) {
        if (useNormalTexture->IsBool()) outMaterial.useNormalTexture = useNormalTexture->AsBool();
    }
    if (const JsonValue* normalTexturePath = root.Find("normalTexturePath")) {
        if (normalTexturePath->IsString()) outMaterial.normalTexturePath = normalTexturePath->AsString();
    }
    if (const JsonValue* useRoughnessTexture = root.Find("useRoughnessTexture")) {
        if (useRoughnessTexture->IsBool()) outMaterial.useRoughnessTexture = useRoughnessTexture->AsBool();
    }
    if (const JsonValue* roughnessTexturePath = root.Find("roughnessTexturePath")) {
        if (roughnessTexturePath->IsString()) outMaterial.roughnessTexturePath = roughnessTexturePath->AsString();
    }
    if (const JsonValue* useAoTexture = root.Find("useAoTexture")) {
        if (useAoTexture->IsBool()) outMaterial.useAoTexture = useAoTexture->AsBool();
    }
    if (const JsonValue* aoTexturePath = root.Find("aoTexturePath")) {
        if (aoTexturePath->IsString()) outMaterial.aoTexturePath = aoTexturePath->AsString();
    }

    // Parse graph
    if (const JsonValue* graphObj = root.Find("graph")) {
        if (graphObj->IsObject()) {
            // First pass: Parse nodes with IDs and pin IDs
            struct NodeLoadData {
                NodeID id;
                NodeType type;
                std::string displayName;
                float scalar;
                Float3 color;
                std::string resourcePath;
                float worldAlignedScale;
                float posX;
                float posY;
                std::vector<PinID> inputPins;
                std::vector<PinID> outputPins;
            };
            std::vector<NodeLoadData> nodesToLoad;
            
            if (const JsonValue* nodesArray = graphObj->Find("nodes")) {
                if (nodesArray->IsArray()) {
                    for (const auto& nodeVal : nodesArray->arrayValue) {
                        if (!nodeVal.IsObject()) continue;
                        
                        NodeLoadData data;
                        data.id = 1;
                        data.type = NodeType::MaterialOutput;
                        data.scalar = 0.5f;
                        data.color = Float3{1.0f, 1.0f, 1.0f};
                        data.worldAlignedScale = 1.0f;
                        data.posX = 0.0f;
                        data.posY = 0.0f;
                        
                        if (const JsonValue* typeVal = nodeVal.Find("type")) {
                            if (typeVal->IsString()) {
                                data.type = StringToNodeType(typeVal->AsString());
                            }
                        }
                        if (const JsonValue* idVal = nodeVal.Find("id")) {
                            if (idVal->IsNumber()) {
                                data.id = static_cast<NodeID>(idVal->AsNumber());
                            }
                        }
                        if (const JsonValue* nameVal = nodeVal.Find("displayName")) {
                            if (nameVal->IsString()) {
                                data.displayName = nameVal->AsString();
                            }
                        }
                        if (const JsonValue* scalarVal = nodeVal.Find("scalar")) {
                            if (scalarVal->IsNumber()) {
                                data.scalar = static_cast<float>(scalarVal->AsNumber());
                            }
                        }
                        if (const JsonValue* colorVal = nodeVal.Find("color")) {
                            parseFloat3(*colorVal, data.color);
                        }
                        if (const JsonValue* resourceVal = nodeVal.Find("resourcePath")) {
                            if (resourceVal->IsString()) {
                                data.resourcePath = resourceVal->AsString();
                            }
                        }
                        if (const JsonValue* worldScaleVal = nodeVal.Find("worldAlignedScale")) {
                            if (worldScaleVal->IsNumber()) {
                                data.worldAlignedScale = static_cast<float>(worldScaleVal->AsNumber());
                            }
                        }
                        
                        // Load position
                        if (const JsonValue* posXVal = nodeVal.Find("posX")) {
                            if (posXVal->IsNumber()) {
                                data.posX = static_cast<float>(posXVal->AsNumber());
                            }
                        }
                        if (const JsonValue* posYVal = nodeVal.Find("posY")) {
                            if (posYVal->IsNumber()) {
                                data.posY = static_cast<float>(posYVal->AsNumber());
                            }
                        }
                        
                        // Parse input pin IDs
                        if (const JsonValue* inputPinsVal = nodeVal.Find("inputPins")) {
                            if (inputPinsVal->IsArray()) {
                                for (const auto& pinVal : inputPinsVal->arrayValue) {
                                    if (pinVal.IsNumber()) {
                                        data.inputPins.push_back(static_cast<PinID>(pinVal.AsNumber()));
                                    }
                                }
                            }
                        }
                        
                        // Parse output pin IDs
                        if (const JsonValue* outputPinsVal = nodeVal.Find("outputPins")) {
                            if (outputPinsVal->IsArray()) {
                                for (const auto& pinVal : outputPinsVal->arrayValue) {
                                    if (pinVal.IsNumber()) {
                                        data.outputPins.push_back(static_cast<PinID>(pinVal.AsNumber()));
                                    }
                                }
                            }
                        }
                        
                        nodesToLoad.push_back(std::move(data));
                    }
                }
            }
            
            // Create nodes with specific IDs
            for (const auto& data : nodesToLoad) {
                Node& node = outGraph.CreateNodeWithID(data.id, data.type, data.displayName);
                node.scalar = data.scalar;
                node.color = data.color;
                node.resourcePath = data.resourcePath;
                node.worldAlignedScale = data.worldAlignedScale;
                node.posX = data.posX;
                node.posY = data.posY;
                
                // Set pin IDs if provided
                if (!data.inputPins.empty() || !data.outputPins.empty()) {
                    outGraph.SetPinIDsForNode(node.id, data.inputPins, data.outputPins);
                }
            }
            
            // Update next IDs after loading all nodes
            outGraph.UpdateNextIDs();
            
            // Parse links
            if (const JsonValue* linksArray = graphObj->Find("links")) {
                if (linksArray->IsArray()) {
                    for (const auto& linkVal : linksArray->arrayValue) {
                        if (!linkVal.IsObject()) continue;
                        
                        LinkID linkId = 0;
                        PinID startPin = -1, endPin = -1;
                        
                        if (const JsonValue* idVal = linkVal.Find("id")) {
                            if (idVal->IsNumber()) linkId = static_cast<LinkID>(idVal->AsNumber());
                        }
                        if (const JsonValue* startVal = linkVal.Find("startPin")) {
                            if (startVal->IsNumber()) startPin = static_cast<PinID>(startVal->AsNumber());
                        }
                        if (const JsonValue* endVal = linkVal.Find("endPin")) {
                            if (endVal->IsNumber()) endPin = static_cast<PinID>(endVal->AsNumber());
                        }
                        
                        if (startPin >= 0 && endPin >= 0) {
                            if (linkId > 0) {
                                outGraph.CreateLinkWithID(linkId, startPin, endPin);
                            } else {
                                outGraph.CreateLink(startPin, endPin);
                            }
                        }
                    }
                }
            }
            
            // Final update of next IDs after links
            outGraph.UpdateNextIDs();
        }
    }

    outError.clear();
    return true;
}

} // namespace

void Panel::RefreshTextureCache() {
    if (m_ProjectRoot.empty()) {
        m_DiscoveredTextures.clear();
        return;
    }
    m_DiscoveredTextures = DiscoverProjectTextures(m_ProjectRoot);
}

Panel::Panel() {
    EnsureContext();
}

Panel::~Panel() {
    if (m_EditorContext) {
        ImNodes::EditorContextFree(m_EditorContext);
        m_EditorContext = nullptr;
    }
}

void Panel::EnsureContext() {
    if (m_EditorContext) {
        return;
    }
    if (ImNodes::GetCurrentContext() == nullptr) {
        return;
    }
    m_EditorContext = ImNodes::EditorContextCreate();
}

std::optional<PanelResult> Panel::Draw(Graph& graph) {
    std::optional<PanelResult> applyResult;

    if (!m_IsOpen) {
        return applyResult;
    }

    if (!HasActiveAsset()) {
        m_IsOpen = false;
        return applyResult;
    }

    ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Material Editor", &m_IsOpen)) {
        ImGui::End();
        return applyResult;
    }

    DrawToolbar(graph);

    ImGui::Separator();

    // Node graph editing
    EnsureContext();
    if (!m_EditorContext) {
        ImGui::End();
        return applyResult;
    }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float inspectorWidth = std::clamp(avail.x * 0.32f, 260.0f, 420.0f);
        float canvasWidth = std::max(100.0f, avail.x - inspectorWidth - ImGui::GetStyle().ItemSpacing.x);

        ImGui::BeginGroup();
        ImGui::BeginChild("MaterialEditorCanvas", ImVec2(canvasWidth, 0.0f), false, ImGuiWindowFlags_NoMove);
        DrawNodeCanvas(graph);
        ImGui::EndChild();
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginChild("MaterialEditorInspector", ImVec2(0.0f, 0.0f), false);
        if (auto inspectorResult = DrawInspector(graph)) {
            applyResult = *inspectorResult;
        }
        if (!m_StatusMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", m_StatusMessage.c_str());
        }
        ImGui::EndChild();

    ImGui::End();

    return applyResult;
}

void Panel::DrawToolbar(Graph& graph) {
    if (!HasActiveAsset()) {
        ImGui::TextDisabled("No material asset selected.");
        return;
    }

    std::string assetLabel = m_CurrentAssetPath.filename().string();
    if (assetLabel.empty()) {
        assetLabel = m_CurrentAssetPath.string();
    }

    ImGui::TextDisabled("Asset: %s", assetLabel.c_str());
    if (!m_CurrentAssetPath.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", m_CurrentAssetPath.string().c_str());
    }
    
    // Show keyboard shortcuts help
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("Shortcuts:");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Material Editor Keyboard Shortcuts:");
        ImGui::Separator();
        ImGui::BulletText("Ctrl+Shift+D - Delete selected nodes/links");
        ImGui::BulletText("Esc - Deselect all");
        ImGui::BulletText("F - Frame selected node");
        ImGui::BulletText("Right-click - Add node menu");
        ImGui::EndTooltip();
    }

    ImGui::Spacing();

    InputText("Display Name", m_CurrentMaterialName);

    if (ImGui::Button("Reset Graph")) {
        graph.Clear();
        Node& output = graph.CreateNode(NodeType::MaterialOutput);
        m_LastSelectedNode.reset();
        m_StatusMessage = "Reset material graph.";
        m_PendingPlacements.clear();
        m_PendingPlacements.push_back({output.id, ImVec2(0.0f, 0.0f)});
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        if (m_EditorContext) {
            ImNodes::EditorContextSet(m_EditorContext);
            ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
            ImNodes::EditorContextSet(nullptr);
        }
    }
}

void Panel::DrawNodeCanvas(Graph& graph) {
    if (!HasActiveAsset()) {
        ImGui::TextDisabled("Create or open a material asset to edit the graph.");
        return;
    }

    ImNodes::EditorContextSet(m_EditorContext);
    ImNodes::BeginNodeEditor();

    for (const Node& node : graph.GetNodes()) {
        ImNodes::BeginNode(node.id);

        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node.displayName.c_str());
        ImNodes::EndNodeTitleBar();

        for (PinID inputID : node.inputs) {
            if (const Pin* pin = graph.FindPin(inputID)) {
                ImNodes::BeginInputAttribute(pin->id);
                ImGui::TextUnformatted(pin->name.c_str());
                ImNodes::EndInputAttribute();
            }
        }

        for (PinID outputID : node.outputs) {
            if (const Pin* pin = graph.FindPin(outputID)) {
                ImNodes::BeginOutputAttribute(pin->id);
                ImGui::TextUnformatted(pin->name.c_str());
                ImNodes::EndOutputAttribute();
            }
        }

        ImNodes::EndNode();

        auto placementIt = std::find_if(m_PendingPlacements.begin(), m_PendingPlacements.end(), [&](const PendingPlacement& placement) {
            return placement.id == node.id;
        });
        if (placementIt != m_PendingPlacements.end()) {
            ImNodes::SetNodeScreenSpacePos(node.id, placementIt->screenPos);
            m_PendingPlacements.erase(placementIt);
        }
    }

    for (const Link& link : graph.GetLinks()) {
        ImNodes::Link(link.id, link.startPin, link.endPin);
    }

    if (ImGui::BeginPopupContextWindow("MaterialEditorContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        auto spawnNode = [&](NodeType type) {
            Node& node = graph.CreateNode(type);
            m_PendingPlacements.push_back({node.id, ImGui::GetMousePos()});
            m_LastSelectedNode = node.id;
        };

        if (ImGui::MenuItem("Material Output")) {
            spawnNode(NodeType::MaterialOutput);
        }
        if (ImGui::BeginMenu("Scalars")) {
            if (ImGui::MenuItem("Constant Float")) {
                spawnNode(NodeType::ConstantFloat);
            }
            if (ImGui::MenuItem("Float Parameter")) {
                spawnNode(NodeType::FloatParameter);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Colors")) {
            if (ImGui::MenuItem("Constant Color")) {
                spawnNode(NodeType::ConstantVec3);
            }
            if (ImGui::MenuItem("Color Parameter")) {
                spawnNode(NodeType::ColorParameter);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Textures")) {
            if (ImGui::MenuItem("Albedo Texture")) {
                spawnNode(NodeType::AlbedoTextureSample);
            }
            if (ImGui::MenuItem("Normal Texture")) {
                spawnNode(NodeType::NormalTextureSample);
            }
            if (ImGui::MenuItem("Roughness Texture")) {
                spawnNode(NodeType::RoughnessTextureSample);
            }
            if (ImGui::MenuItem("AO Texture")) {
                spawnNode(NodeType::AoTextureSample);
            }
            if (ImGui::MenuItem("World Aligned Texture")) {
                spawnNode(NodeType::WorldAlignedTexture);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Math")) {
            if (ImGui::MenuItem("Add")) {
                spawnNode(NodeType::Add);
            }
            if (ImGui::MenuItem("Multiply")) {
                spawnNode(NodeType::Multiply);
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImNodes::MiniMap(0.20f, ImNodesMiniMapLocation_BottomRight);

    ImNodes::EndNodeEditor();

    int startAttr = 0;
    int endAttr = 0;
    if (ImNodes::IsLinkCreated(&startAttr, &endAttr)) {
        if (!graph.CreateLink(startAttr, endAttr)) {
            graph.CreateLink(endAttr, startAttr);
        }
    }

    int removedLink = 0;
    if (ImNodes::IsLinkDestroyed(&removedLink)) {
        graph.RemoveLink(static_cast<LinkID>(removedLink));
    }

    // Keyboard shortcuts for material graph editor
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput) {
        const ImGuiInputFlags shortcutRoute = ImGuiInputFlags_RouteFocused;
        const bool deleteChord = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_D, shortcutRoute);
        const bool deleteKey = ImGui::Shortcut(ImGuiKey_Delete, shortcutRoute);

        // Ctrl+Shift+D or Delete - Delete selected nodes and links
        if (deleteChord || deleteKey || (ImGui::IsKeyPressed(ImGuiKey_D, false) && io.KeyCtrl && io.KeyShift)) {
            std::vector<int> selectedLinks(ImNodes::NumSelectedLinks());
            if (!selectedLinks.empty()) {
                ImNodes::GetSelectedLinks(selectedLinks.data());
                for (int linkId : selectedLinks) {
                    graph.RemoveLink(static_cast<LinkID>(linkId));
                }
                ImNodes::ClearLinkSelection();
            }

            std::vector<int> selectedNodes(ImNodes::NumSelectedNodes());
            if (!selectedNodes.empty()) {
                ImNodes::GetSelectedNodes(selectedNodes.data());
                for (int nodeId : selectedNodes) {
                    Node* node = graph.FindNode(static_cast<NodeID>(nodeId));
                    if (!node) {
                        continue;
                    }
                    // Prevent deletion of Material Output node
                    if (node->type == NodeType::MaterialOutput) {
                        continue;
                    }
                    graph.RemoveNode(static_cast<NodeID>(nodeId));
                }
                ImNodes::ClearNodeSelection();
            }
        }
        
        const bool escapeShortcut = ImGui::Shortcut(ImGuiKey_Escape, shortcutRoute);
        // Escape - Deselect all nodes and links
        if (escapeShortcut || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImNodes::ClearNodeSelection();
            ImNodes::ClearLinkSelection();
            m_LastSelectedNode.reset();
        }
        
        const bool frameShortcut = ImGui::Shortcut(ImGuiKey_F, shortcutRoute);
        // F key - Frame/focus selected nodes
        if (frameShortcut || ImGui::IsKeyPressed(ImGuiKey_F)) {
            int selectedCount = ImNodes::NumSelectedNodes();
            if (selectedCount > 0) {
                std::vector<int> selectedNodes(selectedCount);
                ImNodes::GetSelectedNodes(selectedNodes.data());
                if (!selectedNodes.empty()) {
                    // Get position of first selected node and navigate to it
                    ImVec2 nodePos = ImNodes::GetNodeEditorSpacePos(selectedNodes[0]);
                    ImNodes::EditorContextResetPanning(ImVec2(-nodePos.x, -nodePos.y));
                }
            }
        }
    }

    int selectedCount = ImNodes::NumSelectedNodes();
    if (selectedCount == 1) {
        int selected = 0;
        ImNodes::GetSelectedNodes(&selected);
        m_LastSelectedNode = selected;
    } else if (selectedCount == 0) {
        m_LastSelectedNode.reset();
    }

    ImNodes::EditorContextSet(nullptr);
}

std::optional<PanelResult> Panel::DrawInspector(Graph& graph) {
    std::optional<PanelResult> applyResult;

    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();

    if (!HasActiveAsset()) {
        ImGui::TextDisabled("No material asset selected.");
        return applyResult;
    }

    if (!m_LastSelectedNode) {
        ImGui::TextDisabled("Select a node to edit its properties.");
        ImGui::Spacing();
    }

    Node* node = m_LastSelectedNode ? graph.FindNode(*m_LastSelectedNode) : nullptr;
    if (node) {
        InputText("Label", node->displayName);

        switch (node->type) {
        case NodeType::ConstantFloat:
        case NodeType::FloatParameter:
            ImGui::SliderFloat("Value", &node->scalar, 0.0f, 1.0f, "%.3f");
            break;
        case NodeType::ConstantVec3:
        case NodeType::ColorParameter: {
            float color[3] = { node->color.x, node->color.y, node->color.z };
            if (ImGui::ColorEdit3("Color", color, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_DisplayRGB)) {
                node->color.x = color[0];
                node->color.y = color[1];
                node->color.z = color[2];
            }
            break;
        }
        case NodeType::AlbedoTextureSample:
        case NodeType::NormalTextureSample:
        case NodeType::RoughnessTextureSample:
        case NodeType::AoTextureSample: {
            // Create texture dropdown
            int currentSelection = -1;
            for (size_t i = 0; i < m_DiscoveredTextures.size(); ++i) {
                if (m_DiscoveredTextures[i] == node->resourcePath) {
                    currentSelection = static_cast<int>(i);
                    break;
                }
            }

            std::vector<const char*> textureNames;
            textureNames.reserve(m_DiscoveredTextures.size() + 1);
            textureNames.push_back("(None)");
            for (const auto& tex : m_DiscoveredTextures) {
                textureNames.push_back(tex.c_str());
            }

            const char* preview = (currentSelection >= 0) ? m_DiscoveredTextures[currentSelection].c_str() : "(None)";
            if (ImGui::Combo("##TextureSelection", &currentSelection, textureNames.data(), static_cast<int>(textureNames.size()))) {
                if (currentSelection > 0 && currentSelection - 1 < static_cast<int>(m_DiscoveredTextures.size())) {
                    node->resourcePath = m_DiscoveredTextures[currentSelection - 1];
                } else {
                    node->resourcePath.clear();
                }
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("Texture");
            
            // Also allow manual input as fallback
            static char textureBuffer[256];
            if (preview != nullptr) {
                std::strncpy(textureBuffer, preview, sizeof(textureBuffer) - 1);
                textureBuffer[sizeof(textureBuffer) - 1] = '\0';
            }
            
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##TextureManual", textureBuffer, sizeof(textureBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                node->resourcePath = textureBuffer;
            }
            ImGui::TextDisabled("Found %zu textures in project", m_DiscoveredTextures.size());
            
            if (!node->resourcePath.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "Selected: %s", node->resourcePath.c_str());
                ImGui::TextDisabled("Note: Texture rendering requires UV coordinates.");
                ImGui::TextDisabled("Ensure your models have UVs mapped.");
            }
            break;
        }
        case NodeType::WorldAlignedTexture: {
            // Create texture dropdown
            int currentSelection = -1;
            for (size_t i = 0; i < m_DiscoveredTextures.size(); ++i) {
                if (m_DiscoveredTextures[i] == node->resourcePath) {
                    currentSelection = static_cast<int>(i);
                    break;
                }
            }

            std::vector<const char*> textureNames;
            textureNames.reserve(m_DiscoveredTextures.size() + 1);
            textureNames.push_back("(None)");
            for (const auto& tex : m_DiscoveredTextures) {
                textureNames.push_back(tex.c_str());
            }

            const char* preview = (currentSelection >= 0) ? m_DiscoveredTextures[currentSelection].c_str() : "(None)";
            if (ImGui::Combo("##TextureSelection", &currentSelection, textureNames.data(), static_cast<int>(textureNames.size()))) {
                if (currentSelection > 0 && currentSelection - 1 < static_cast<int>(m_DiscoveredTextures.size())) {
                    node->resourcePath = m_DiscoveredTextures[currentSelection - 1];
                } else {
                    node->resourcePath.clear();
                }
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("Texture");
            
            // Also allow manual input as fallback
            static char textureBuffer[256];
            if (preview != nullptr) {
                std::strncpy(textureBuffer, preview, sizeof(textureBuffer) - 1);
                textureBuffer[sizeof(textureBuffer) - 1] = '\0';
            }
            
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##TextureManual", textureBuffer, sizeof(textureBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                node->resourcePath = textureBuffer;
            }
            ImGui::TextDisabled("Found %zu textures in project", m_DiscoveredTextures.size());
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // World-aligned scale setting
            ImGui::Text("World Scale");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::DragFloat("##WorldScale", &node->worldAlignedScale, 0.01f, 0.001f, 100.0f, "%.3f");
            ImGui::TextDisabled("Texture repeats per world unit.");
            ImGui::TextDisabled("Smaller = larger texture, Larger = more tiling");
            
            if (!node->resourcePath.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "Selected: %s", node->resourcePath.c_str());
            }
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Uses world position for UVs - no UV mapping needed!");
            break;
        }
        case NodeType::Add:
        case NodeType::Multiply:
            ImGui::TextDisabled("Combines scalar values. Vector math coming soon.");
            break;
        case NodeType::MaterialOutput:
            ImGui::TextDisabled("Connect material attributes to build the final surface.");
            break;
        }

        ImGui::Spacing();
    }

    // Save Material button - saves the graph without applying to entity
    if (ImGui::Button("Save Material")) {
        MaterialComponent material;
        std::string error;
        if (EvaluateGraph(graph, material, error)) {
            std::string saveError;
            bool saved = SaveMaterialAssetFile(m_CurrentAssetPath, material, m_CurrentMaterialName, graph, m_EditorContext, saveError);
            if (!saved) {
                m_StatusMessage = "Save failed: " + saveError;
            } else {
                m_StatusMessage = "Material saved successfully.";
            }
        } else {
            m_StatusMessage = "Cannot save: " + error;
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Apply To Selected Entity")) {
        MaterialComponent material;
        std::string error;
        if (EvaluateGraph(graph, material, error)) {
            std::string saveError;
            bool saved = SaveMaterialAssetFile(m_CurrentAssetPath, material, m_CurrentMaterialName, graph, m_EditorContext, saveError);
            if (!saved) {
                m_StatusMessage = "Material applied, but save failed: " + saveError;
            } else {
                m_StatusMessage = "Material saved and applied.";
            }
            applyResult = PanelResult{material, m_CurrentMaterialName, m_CurrentAssetPath};
        } else {
            m_StatusMessage = error;
        }
    }

    return applyResult;
}

void Panel::SetActiveAsset(const std::filesystem::path& path) {
    m_CurrentAssetPath = path.lexically_normal();
    m_CurrentMaterialName = m_CurrentAssetPath.stem().string();
    if (m_CurrentMaterialName.empty()) {
        m_CurrentMaterialName = "Material";
    }
    
    // Set project root if not already set (look for engine root)
    if (m_ProjectRoot.empty()) {
        std::filesystem::path current = m_CurrentAssetPath.parent_path();
        while (!current.empty()) {
            if (std::filesystem::exists(current / "assets")) {
                m_ProjectRoot = current;
                break;
            }
            current = current.parent_path();
        }
    }
    
    // Refresh texture cache
    RefreshTextureCache();
    
    // Load existing material data if file exists
    if (std::filesystem::exists(m_CurrentAssetPath)) {
        MaterialComponent loadedMaterial;
        std::string loadedName;
        Graph tempGraph; // Temporary graph, will be loaded via LoadMaterialIntoGraph later
        std::string loadError;
        
        if (LoadMaterialAssetFile(m_CurrentAssetPath, loadedMaterial, loadedName, tempGraph, loadError)) {
            if (!loadedName.empty()) {
                m_CurrentMaterialName = loadedName;
            }
            m_StatusMessage = "Loaded material '" + m_CurrentMaterialName + "'.";
        } else {
            m_StatusMessage = "Opened material '" + m_CurrentMaterialName + "', but failed to load data: " + loadError;
        }
    } else {
        // New material
        m_StatusMessage = "Created new material '" + m_CurrentMaterialName + "'.";
    }
    
    m_LastSelectedNode.reset();
    m_PendingPlacements.clear();
    m_IsOpen = true;

    if (m_EditorContext) {
        ImNodes::EditorContextSet(m_EditorContext);
        ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
        ImNodes::EditorContextSet(nullptr);
    }
}

void Panel::LoadMaterialIntoGraph(Graph& graph) {
    if (!std::filesystem::exists(m_CurrentAssetPath)) {
        return; // No file to load from
    }
    
    MaterialComponent tempMaterial;
    std::string tempName;
    std::string loadError;
    
    if (LoadMaterialAssetFile(m_CurrentAssetPath, tempMaterial, tempName, graph, loadError)) {
        // Graph has been populated
        // Queue node placements using loaded positions
        const auto& nodes = graph.GetNodes();
        for (const auto& node : nodes) {
            // Use the loaded position, or default position for MaterialOutput
            ImVec2 pos(node.posX, node.posY);
            if (node.type == NodeType::MaterialOutput && node.posX == 0.0f && node.posY == 0.0f) {
                pos = ImVec2(400.0f, 200.0f); // Default position for output node
            }
            QueueNodePlacement(node.id, pos);
        }
    }
}

void Panel::ClearActiveAsset() {
    m_CurrentAssetPath.clear();
    m_CurrentMaterialName = "NewMaterial";
    m_StatusMessage.clear();
    m_LastSelectedNode.reset();
    m_PendingPlacements.clear();
    m_IsOpen = false;
}

void Panel::QueueNodePlacement(NodeID id, ImVec2 screenPos) {
    m_PendingPlacements.push_back({id, screenPos});
}

} // namespace MaterialEditor
