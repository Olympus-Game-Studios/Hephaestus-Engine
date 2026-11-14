#include "MaterialAssetSerializer.h"
#include "MaterialGraphEvaluator.h"
#include "MaterialNodeDefinitions.h"
#include "MaterialShaderCompiler.h"
#include "../../Assets/AssetPathPolicy.h"

#include <algorithm>
#include <fstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace MaterialEditor {
namespace {

using json = nlohmann::json;

std::string NodeTypeToString(NodeType type) {
    return std::string(NodeTypeName(type));
}

bool TryNodeTypeFromString(const std::string& str, NodeType& outType) {
    return TryNodeTypeName(str, outType);
}

void ParseVec3(const json& value, Vec3& out) {
    if (!value.is_array() || value.size() < 3) {
        return;
    }
    if (value[0].is_number()) out.x = value[0].get<float>();
    if (value[1].is_number()) out.y = value[1].get<float>();
    if (value[2].is_number()) out.z = value[2].get<float>();
}

void ParseFloat3(const json& value, Float3& out) {
    if (!value.is_array() || value.size() < 3) {
        return;
    }
    if (value[0].is_number()) out.x = value[0].get<float>();
    if (value[1].is_number()) out.y = value[1].get<float>();
    if (value[2].is_number()) out.z = value[2].get<float>();
}

json ExtractUnknownObjectKeys(const json& objectValue, const std::unordered_set<std::string>& knownKeys) {
    json unknown = json::object();
    if (!objectValue.is_object()) {
        return unknown;
    }
    for (auto it = objectValue.begin(); it != objectValue.end(); ++it) {
        if (knownKeys.find(it.key()) == knownKeys.end()) {
            unknown[it.key()] = it.value();
        }
    }
    return unknown;
}

void MergeUnknownInto(json& destination, const json& unknown) {
    if (!unknown.is_object()) {
        return;
    }
    for (auto it = unknown.begin(); it != unknown.end(); ++it) {
        destination[it.key()] = it.value();
    }
}

} // namespace

bool LoadMaterialAssetCompat(const std::filesystem::path& path,
                             MaterialComponent& outMaterial,
                             std::string& outDisplayName,
                             Graph& outGraph,
                             std::string& outError) {
    outMaterial = MaterialComponent();
    outDisplayName = path.stem().string();
    outGraph.Clear();

    std::ifstream in(path);
    if (!in) {
        outError = "Unable to open material file for reading.";
        return false;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& ex) {
        outError = std::string("Failed to parse material JSON: ") + ex.what();
        return false;
    }

    if (!root.is_object()) {
        outError = "Material file root must be an object.";
        return false;
    }

    outGraph.SetOriginalRootJson(root);

    if (root.contains("name") && root["name"].is_string()) {
        outDisplayName = root["name"].get<std::string>();
    }

    if (root.contains("albedo")) ParseVec3(root["albedo"], outMaterial.albedo);
    if (root.contains("metallic") && root["metallic"].is_number()) outMaterial.metallic = root["metallic"].get<float>();
    if (root.contains("roughness") && root["roughness"].is_number()) outMaterial.roughness = root["roughness"].get<float>();
    if (root.contains("ao") && root["ao"].is_number()) outMaterial.ao = root["ao"].get<float>();
    if (root.contains("emissive")) ParseVec3(root["emissive"], outMaterial.emissive);
    if (root.contains("emissiveStrength") && root["emissiveStrength"].is_number()) outMaterial.emissiveStrength = root["emissiveStrength"].get<float>();
    if (root.contains("reflectionStrength") && root["reflectionStrength"].is_number()) outMaterial.reflectionStrength = root["reflectionStrength"].get<float>();
    if (root.contains("specular") && root["specular"].is_number()) outMaterial.specular = root["specular"].get<float>();
    if (root.contains("opacity") && root["opacity"].is_number()) outMaterial.opacity = root["opacity"].get<float>();
    if (root.contains("opacityMask") && root["opacityMask"].is_number()) outMaterial.opacityMask = root["opacityMask"].get<float>();
    if (root.contains("refraction") && root["refraction"].is_number()) outMaterial.refraction = root["refraction"].get<float>();
    if (root.contains("blendMode") && root["blendMode"].is_string()) {
        outMaterial.blendMode = MaterialBlendModeFromName(root["blendMode"].get<std::string>());
    }

    if (root.contains("useAlbedoTexture") && root["useAlbedoTexture"].is_boolean()) outMaterial.useAlbedoTexture = root["useAlbedoTexture"].get<bool>();
    if (root.contains("albedoTexturePath") && root["albedoTexturePath"].is_string()) outMaterial.albedoTexturePath = root["albedoTexturePath"].get<std::string>();
    if (root.contains("useNormalTexture") && root["useNormalTexture"].is_boolean()) outMaterial.useNormalTexture = root["useNormalTexture"].get<bool>();
    if (root.contains("normalTexturePath") && root["normalTexturePath"].is_string()) outMaterial.normalTexturePath = root["normalTexturePath"].get<std::string>();
    if (root.contains("useRoughnessTexture") && root["useRoughnessTexture"].is_boolean()) outMaterial.useRoughnessTexture = root["useRoughnessTexture"].get<bool>();
    if (root.contains("roughnessTexturePath") && root["roughnessTexturePath"].is_string()) outMaterial.roughnessTexturePath = root["roughnessTexturePath"].get<std::string>();
    if (root.contains("useMetallicTexture") && root["useMetallicTexture"].is_boolean()) outMaterial.useMetallicTexture = root["useMetallicTexture"].get<bool>();
    if (root.contains("metallicTexturePath") && root["metallicTexturePath"].is_string()) outMaterial.metallicTexturePath = root["metallicTexturePath"].get<std::string>();
    if (root.contains("useAoTexture") && root["useAoTexture"].is_boolean()) outMaterial.useAoTexture = root["useAoTexture"].get<bool>();
    if (root.contains("aoTexturePath") && root["aoTexturePath"].is_string()) outMaterial.aoTexturePath = root["aoTexturePath"].get<std::string>();
    if (root.contains("useWorldAlignedTexture") && root["useWorldAlignedTexture"].is_boolean()) outMaterial.useWorldAlignedTexture = root["useWorldAlignedTexture"].get<bool>();
    for (auto [key, channel] : {std::pair{"roughnessChannel", &outMaterial.roughnessChannel},
                                std::pair{"metallicChannel", &outMaterial.metallicChannel},
                                std::pair{"aoChannel", &outMaterial.aoChannel}}) {
        if (root.contains(key) && root[key].is_number_unsigned()) *channel = root[key].get<uint32_t>() & 3u;
    }
    if (root.contains("worldTextureScale") && root["worldTextureScale"].is_number()) outMaterial.worldTextureScale = root["worldTextureScale"].get<float>();

    std::vector<std::string> warnings;

    if (!root.contains("graph") || !root["graph"].is_object()) {
        outGraph.SetHasSourceGraph(false);
        outGraph.SetLegacyMode(true);
        outGraph.SetGraphEditingEnabled(false);
        outGraph.SetLoadWarnings(std::move(warnings));
        outError.clear();
        return true;
    }

    outGraph.SetHasSourceGraph(true);
    const json& graphObj = root["graph"];

    static const std::unordered_set<std::string> graphKeys = {"nodes", "links"};
    outGraph.SetGraphUnknownFields(ExtractUnknownObjectKeys(graphObj, graphKeys));

    bool graphValid = true;

    int nextNodeId = 1;
    int nextLinkId = 1;

    if (graphObj.contains("nodes") && graphObj["nodes"].is_array()) {
        for (const json& nodeVal : graphObj["nodes"]) {
            if (!nodeVal.is_object()) {
                graphValid = false;
                warnings.emplace_back("Skipped non-object node entry.");
                continue;
            }

            NodeID nodeId = nextNodeId++;
            if (nodeVal.contains("id") && nodeVal["id"].is_number_integer()) {
                nodeId = nodeVal["id"].get<NodeID>();
                nextNodeId = std::max(nextNodeId, static_cast<int>(nodeId) + 1);
            }

            std::string typeString = "MaterialOutput";
            if (nodeVal.contains("type") && nodeVal["type"].is_string()) {
                typeString = nodeVal["type"].get<std::string>();
            }

            NodeType nodeType = NodeType::MaterialOutput;
            const bool recognized = TryNodeTypeFromString(typeString, nodeType);
            if (!recognized) {
                nodeType = NodeType::Unknown;
                warnings.emplace_back("Encountered unknown node type: " + typeString);
            }

            std::string displayName = recognized ? "" : typeString;
            if (nodeVal.contains("displayName") && nodeVal["displayName"].is_string()) {
                displayName = nodeVal["displayName"].get<std::string>();
            }

            Node& node = outGraph.CreateNodeWithID(nodeId, nodeType, displayName);
            node.serializedType = typeString;

            if (nodeVal.contains("scalar") && nodeVal["scalar"].is_number()) node.scalar = nodeVal["scalar"].get<float>();
            if (nodeVal.contains("color")) ParseFloat3(nodeVal["color"], node.color);
            if (nodeVal.contains("resourcePath") && nodeVal["resourcePath"].is_string()) node.resourcePath = nodeVal["resourcePath"].get<std::string>();
            if (nodeVal.contains("worldAlignedScale") && nodeVal["worldAlignedScale"].is_number()) node.worldAlignedScale = nodeVal["worldAlignedScale"].get<float>();
            if (nodeVal.contains("parameterName") && nodeVal["parameterName"].is_string()) node.parameterName = nodeVal["parameterName"].get<std::string>();
            if (nodeVal.contains("operation") && nodeVal["operation"].is_string()) node.operation = nodeVal["operation"].get<std::string>();
            if (nodeVal.contains("sourceSpace") && nodeVal["sourceSpace"].is_string()) node.sourceSpace = nodeVal["sourceSpace"].get<std::string>();
            if (nodeVal.contains("targetSpace") && nodeVal["targetSpace"].is_string()) node.targetSpace = nodeVal["targetSpace"].get<std::string>();
            if (nodeVal.contains("boolValue") && nodeVal["boolValue"].is_boolean()) node.boolValue = nodeVal["boolValue"].get<bool>();
            if (nodeVal.contains("maskR") && nodeVal["maskR"].is_boolean()) node.maskR = nodeVal["maskR"].get<bool>();
            if (nodeVal.contains("maskG") && nodeVal["maskG"].is_boolean()) node.maskG = nodeVal["maskG"].get<bool>();
            if (nodeVal.contains("maskB") && nodeVal["maskB"].is_boolean()) node.maskB = nodeVal["maskB"].get<bool>();
            if (nodeVal.contains("maskA") && nodeVal["maskA"].is_boolean()) node.maskA = nodeVal["maskA"].get<bool>();
            if (nodeVal.contains("posX") && nodeVal["posX"].is_number()) node.posX = nodeVal["posX"].get<float>();
            if (nodeVal.contains("posY") && nodeVal["posY"].is_number()) node.posY = nodeVal["posY"].get<float>();

            std::vector<PinID> inputPins;
            std::vector<PinID> outputPins;
            if (nodeVal.contains("inputPins") && nodeVal["inputPins"].is_array()) {
                for (const auto& pin : nodeVal["inputPins"]) {
                    if (pin.is_number_integer()) {
                        inputPins.push_back(pin.get<PinID>());
                    }
                }
            }
            if (nodeVal.contains("outputPins") && nodeVal["outputPins"].is_array()) {
                for (const auto& pin : nodeVal["outputPins"]) {
                    if (pin.is_number_integer()) {
                        outputPins.push_back(pin.get<PinID>());
                    }
                }
            }
            if (!inputPins.empty() || !outputPins.empty()) {
                outGraph.SetPinIDsForNode(node.id, inputPins, outputPins);
            }

            static const std::unordered_set<std::string> nodeKeys = {
                "id", "type", "displayName", "parameterName", "operation", "sourceSpace", "targetSpace",
                "scalar", "color", "resourcePath", "worldAlignedScale", "boolValue",
                "maskR", "maskG", "maskB", "maskA", "posX", "posY", "inputPins", "outputPins"
            };
            node.unknownFields = ExtractUnknownObjectKeys(nodeVal, nodeKeys);
        }
    } else {
        graphValid = false;
        warnings.emplace_back("Graph has no valid nodes array.");
    }

    outGraph.UpdateNextIDs();

    if (graphObj.contains("links") && graphObj["links"].is_array()) {
        for (const json& linkVal : graphObj["links"]) {
            if (!linkVal.is_object()) {
                graphValid = false;
                warnings.emplace_back("Skipped non-object link entry.");
                continue;
            }

            LinkID linkId = nextLinkId++;
            if (linkVal.contains("id") && linkVal["id"].is_number_integer()) {
                linkId = linkVal["id"].get<LinkID>();
                nextLinkId = std::max(nextLinkId, static_cast<int>(linkId) + 1);
            }

            if (!linkVal.contains("startPin") || !linkVal["startPin"].is_number_integer() ||
                !linkVal.contains("endPin") || !linkVal["endPin"].is_number_integer()) {
                graphValid = false;
                warnings.emplace_back("Skipped link with invalid startPin/endPin.");
                continue;
            }

            PinID startPin = linkVal["startPin"].get<PinID>();
            PinID endPin = linkVal["endPin"].get<PinID>();

            Link* link = outGraph.CreateLinkWithID(linkId, startPin, endPin);
            if (!link) {
                warnings.emplace_back("Skipped duplicate link id=" + std::to_string(static_cast<int>(linkId)));
                continue;
            }

            static const std::unordered_set<std::string> linkKeys = {"id", "startPin", "endPin"};
            link->unknownFields = ExtractUnknownObjectKeys(linkVal, linkKeys);

            if (link->orphan) {
                warnings.emplace_back("Orphan link id=" + std::to_string(static_cast<int>(link->id)));
            }
            if (link->directionRepairedInMemory) {
                warnings.emplace_back("Repaired swapped link direction in-memory for id=" + std::to_string(static_cast<int>(link->id)));
            }
        }
    } else {
        graphValid = false;
        warnings.emplace_back("Graph has no valid links array.");
    }

    outGraph.UpdateNextIDs();

    bool hasOutput = false;
    for (const Node& node : outGraph.GetNodes()) {
        if (node.type == NodeType::MaterialOutput) {
            hasOutput = true;
            break;
        }
    }

    if (!hasOutput) {
        graphValid = false;
        warnings.emplace_back("Graph is missing a MaterialOutput node.");
    }

    outGraph.SetLegacyMode(!graphValid);
    outGraph.SetGraphEditingEnabled(graphValid);
    outGraph.SetLoadWarnings(std::move(warnings));

    outError.clear();
    return true;
}

bool SaveMaterialAssetCompat(const std::filesystem::path& path,
                             const MaterialComponent& material,
                             const std::string& displayName,
                             const Graph& graph,
                             const NodePositionProvider& nodePositionProvider,
                             std::string& outError,
                             std::vector<std::string>* shaderDiagnostics) {
    json root = graph.HasOriginalRootJson() ? graph.GetOriginalRootJson() : json::object();
    if (!root.is_object()) {
        root = json::object();
    }

    root["name"] = displayName;
    root["albedo"] = { material.albedo.x, material.albedo.y, material.albedo.z };
    root["metallic"] = material.metallic;
    root["roughness"] = material.roughness;
    root["ao"] = material.ao;
    root["emissive"] = { material.emissive.x, material.emissive.y, material.emissive.z };
    root["emissiveStrength"] = material.emissiveStrength;
    root["specular"] = material.specular;
    root["opacity"] = material.opacity;
    root["opacityMask"] = material.opacityMask;
    root["refraction"] = material.refraction;
    if (material.blendMode != MaterialBlendMode::Opaque) {
        root["blendMode"] = MaterialBlendModeName(material.blendMode);
    } else {
        root.erase("blendMode");
    }
    if (root.contains("reflectionStrength") || material.reflectionStrength != 1.0f) {
        root["reflectionStrength"] = material.reflectionStrength;
    }
    root["useAlbedoTexture"] = material.useAlbedoTexture;
    root["albedoTexturePath"] = Hephaestus::AssetPathPolicy::ToPortableKey({}, material.albedoTexturePath);
    root["useNormalTexture"] = material.useNormalTexture;
    root["normalTexturePath"] = Hephaestus::AssetPathPolicy::ToPortableKey({}, material.normalTexturePath);
    root["useRoughnessTexture"] = material.useRoughnessTexture;
    root["roughnessTexturePath"] = Hephaestus::AssetPathPolicy::ToPortableKey({}, material.roughnessTexturePath);
    root["useMetallicTexture"] = material.useMetallicTexture;
    root["metallicTexturePath"] = Hephaestus::AssetPathPolicy::ToPortableKey({}, material.metallicTexturePath);
    root["useAoTexture"] = material.useAoTexture;
    root["aoTexturePath"] = Hephaestus::AssetPathPolicy::ToPortableKey({}, material.aoTexturePath);
    for (const auto& [key, channel] : {std::pair{"roughnessChannel", material.roughnessChannel},
                                       std::pair{"metallicChannel", material.metallicChannel},
                                       std::pair{"aoChannel", material.aoChannel}}) {
        if (channel != 0) {
            root[key] = channel;
        } else {
            root.erase(key);
        }
    }
    root["useWorldAlignedTexture"] = material.useWorldAlignedTexture;
    root["worldTextureScale"] = material.worldTextureScale;

    if (!(graph.IsLegacyMode() && !graph.IsGraphEditingEnabled())) {
        json graphJson = (root.contains("graph") && root["graph"].is_object()) ? root["graph"] : json::object();
        MergeUnknownInto(graphJson, graph.GetGraphUnknownFields());

        json nodes = json::array();
        for (const Node& node : graph.GetNodes()) {
            json nodeJson = json::object();
            MergeUnknownInto(nodeJson, node.unknownFields);

            const std::string typeString =
                (node.type == NodeType::Unknown && !node.serializedType.empty()) ? node.serializedType : NodeTypeToString(node.type);

            nodeJson["id"] = node.id;
            nodeJson["type"] = typeString;
            nodeJson["displayName"] = node.displayName;
            nodeJson["scalar"] = node.scalar;
            nodeJson["color"] = { node.color.x, node.color.y, node.color.z };
            nodeJson["resourcePath"] = Hephaestus::AssetPathPolicy::ToPortableKey({}, node.resourcePath);
            nodeJson["worldAlignedScale"] = node.worldAlignedScale;
            if (!node.parameterName.empty()) nodeJson["parameterName"] = node.parameterName;
            if (!node.operation.empty()) nodeJson["operation"] = node.operation;
            if (!node.sourceSpace.empty()) nodeJson["sourceSpace"] = node.sourceSpace;
            if (!node.targetSpace.empty()) nodeJson["targetSpace"] = node.targetSpace;
            nodeJson["boolValue"] = node.boolValue;
            nodeJson["maskR"] = node.maskR;
            nodeJson["maskG"] = node.maskG;
            nodeJson["maskB"] = node.maskB;
            nodeJson["maskA"] = node.maskA;

            float posX = node.posX;
            float posY = node.posY;
            if (nodePositionProvider) {
                float editorX = 0.0f;
                float editorY = 0.0f;
                if (nodePositionProvider(node.id, editorX, editorY)) {
                    posX = editorX;
                    posY = editorY;
                }
            }
            nodeJson["posX"] = posX;
            nodeJson["posY"] = posY;

            nodeJson["inputPins"] = node.inputs;
            nodeJson["outputPins"] = node.outputs;

            nodes.push_back(std::move(nodeJson));
        }

        json links = json::array();
        for (const Link& link : graph.GetLinks()) {
            if (link.orphan && graph.ShouldCleanOrphansOnSave()) {
                continue;
            }

            json linkJson = json::object();
            MergeUnknownInto(linkJson, link.unknownFields);

            linkJson["id"] = link.id;
            if (link.directionRepairedInMemory && !graph.ShouldApplyGraphRepairsOnSave()) {
                linkJson["startPin"] = link.originalStartPin;
                linkJson["endPin"] = link.originalEndPin;
            } else {
                linkJson["startPin"] = link.startPin;
                linkJson["endPin"] = link.endPin;
            }

            links.push_back(std::move(linkJson));
        }

        graphJson["nodes"] = std::move(nodes);
        graphJson["links"] = std::move(links);
        root["graph"] = std::move(graphJson);
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        outError = "Unable to open material file for writing.";
        return false;
    }

    out << root.dump(2) << "\n";
    if (!out.good()) {
        outError = "Failed to write material data.";
        return false;
    }
    out.close();

    std::vector<std::string> diagnostics;
    const std::filesystem::path shaderPath = Hephaestus::CompiledMaterialShaderPath(path);
    bool keepShader = false;
    if (graph.HasSourceGraph() && !graph.IsLegacyMode() && GraphNeedsCompiledShader(graph)) {
        MaterialShaderCompileResult compiled = CompileMaterialGraph(graph, FindMaterialShaderSourceDirectory());
        diagnostics = std::move(compiled.diagnostics);
        std::string writeError;
        if (compiled.ok && Hephaestus::WriteCompiledMaterialShader(shaderPath, compiled.shader, writeError)) {
            keepShader = true;
        } else if (!writeError.empty()) {
            diagnostics.push_back(writeError);
        }
    }
    if (!keepShader) {
        std::error_code ec;
        std::filesystem::remove(shaderPath, ec);
    }
    if (shaderDiagnostics) {
        *shaderDiagnostics = std::move(diagnostics);
    }

    outError.clear();
    return true;
}

void BuildMinimalGraphFromLegacyPbr(const MaterialComponent& material, Graph& graph) {
    const bool hadOriginalRoot = graph.HasOriginalRootJson();
    json originalRoot = graph.GetOriginalRootJson();
    graph.Clear();
    if (hadOriginalRoot) {
        graph.SetOriginalRootJson(std::move(originalRoot));
    }
    graph.SetLegacyMode(false);
    graph.SetGraphEditingEnabled(true);
    graph.SetHasSourceGraph(true);

    constexpr float kRowHeight = 120.0f;
    constexpr float kInputX = 160.0f;
    constexpr float kSampleX = -80.0f;
    const MaterialComponent defaults;

    // CreateNode may reallocate the node list, so keep copies of pin ids rather than node references.
    Node& outputNode = graph.CreateNode(NodeType::MaterialOutput);
    const NodeID outputId = outputNode.id;
    const std::vector<PinID> outputInputs = outputNode.inputs;
    float rowY = 0.0f;

    auto place = [](Node& node, float x, float y) {
        node.posX = x;
        node.posY = y;
    };
    auto link = [&](PinID from, size_t outputSlot) {
        if (from != 0 && outputSlot < outputInputs.size()) {
            graph.CreateLink(from, outputInputs[outputSlot]);
        }
    };
    auto scalarNode = [&](float value, float x, float y) {
        Node& node = graph.CreateNode(NodeType::ConstantFloat);
        node.scalar = value;
        place(node, x, y);
        return node.outputs.empty() ? PinID{0} : node.outputs[0];
    };
    auto colorNode = [&](const Vec3& value, float x, float y) {
        Node& node = graph.CreateNode(NodeType::ConstantVec3);
        node.color = Float3{value.x, value.y, value.z};
        place(node, x, y);
        return node.outputs.empty() ? PinID{0} : node.outputs[0];
    };
    auto multiply = [&](PinID a, PinID b, float x, float y) {
        Node& node = graph.CreateNode(NodeType::Multiply);
        place(node, x, y);
        const std::vector<PinID> inputs = node.inputs;
        const PinID result = node.outputs.empty() ? PinID{0} : node.outputs[0];
        if (inputs.size() >= 2) {
            graph.CreateLink(a, inputs[0]);
            graph.CreateLink(b, inputs[1]);
        }
        return result;
    };
    auto sample = [&](NodeType type, const std::string& path, bool scalar, uint32_t channel, float y) {
        const bool worldAligned = material.useWorldAlignedTexture;
        const bool split = scalar && channel != 0;
        Node& node = graph.CreateNode(worldAligned ? NodeType::WorldAlignedTexture
                                      : split      ? NodeType::TextureSample
                                                   : type);
        node.resourcePath = path;
        node.worldAlignedScale = material.worldTextureScale;
        place(node, split ? kSampleX - 220.0f : kSampleX, y);
        const std::vector<PinID> outputs = node.outputs;
        if (outputs.empty()) {
            return PinID{0};
        }
        if (!split) {
            return worldAligned && scalar && outputs.size() > 1 ? outputs[1] : outputs[0];
        }
        Node& breakNode = graph.CreateNode(NodeType::BreakVector);
        place(breakNode, kSampleX, y);
        const std::vector<PinID> breakInputs = breakNode.inputs;
        const std::vector<PinID> breakOutputs = breakNode.outputs;
        if (breakInputs.empty() || channel >= breakOutputs.size()) {
            return PinID{0};
        }
        graph.CreateLink(outputs[0], breakInputs[0]);
        return breakOutputs[channel];
    };
    auto mapTimes = [&](NodeType type, const std::string& path, uint32_t channel, float factor, size_t outputSlot) {
        PinID map = sample(type, path, true, channel, rowY);
        if (factor != 1.0f) {
            map = multiply(map, scalarNode(factor, kSampleX, rowY + kRowHeight * 0.5f), kInputX, rowY);
            rowY += kRowHeight * 0.5f;
        }
        link(map, outputSlot);
        rowY += kRowHeight;
    };

    if (material.useAlbedoTexture && !material.albedoTexturePath.empty()) {
        PinID color = sample(NodeType::AlbedoTextureSample, material.albedoTexturePath, false, 0, rowY);
        const Vec3& tint = material.albedo;
        if (tint.x != 1.0f || tint.y != 1.0f || tint.z != 1.0f) {
            color = multiply(color, colorNode(tint, kSampleX, rowY + kRowHeight * 0.5f), kInputX, rowY);
            rowY += kRowHeight * 0.5f;
        }
        link(color, 0);
    } else {
        link(colorNode(material.albedo, kInputX, rowY), 0);
    }
    rowY += kRowHeight;

    if (material.useMetallicTexture && !material.metallicTexturePath.empty()) {
        mapTimes(NodeType::MetallicTextureSample, material.metallicTexturePath, material.metallicChannel,
                 material.metallic, 1);
    } else {
        link(scalarNode(material.metallic, kInputX, rowY), 1);
        rowY += kRowHeight;
    }

    link(scalarNode(material.roughness, kInputX, rowY), 2);
    rowY += kRowHeight;
    if (material.useRoughnessTexture && !material.roughnessTexturePath.empty()) {
        link(sample(NodeType::RoughnessTextureSample, material.roughnessTexturePath, true, material.roughnessChannel,
                    rowY), 5);
        rowY += kRowHeight;
    }

    if (material.emissive.x != 0.0f || material.emissive.y != 0.0f || material.emissive.z != 0.0f) {
        link(colorNode(material.emissive, kInputX, rowY), 3);
        rowY += kRowHeight;
    }

    if (material.useNormalTexture && !material.normalTexturePath.empty()) {
        link(sample(NodeType::NormalTextureSample, material.normalTexturePath, false, 0, rowY), 4);
        rowY += kRowHeight;
    }

    if (material.useAoTexture && !material.aoTexturePath.empty()) {
        mapTimes(NodeType::AoTextureSample, material.aoTexturePath, material.aoChannel, material.ao, 6);
    } else if (material.ao != defaults.ao) {
        link(scalarNode(material.ao, kInputX, rowY), 6);
        rowY += kRowHeight;
    }

    link(scalarNode(material.specular, kInputX, rowY), 7);
    rowY += kRowHeight;
    link(scalarNode(material.opacity, kInputX, rowY), 8);
    rowY += kRowHeight;
    if (material.opacityMask != defaults.opacityMask || material.blendMode == MaterialBlendMode::Masked) {
        link(scalarNode(material.opacityMask, kInputX, rowY), 9);
        rowY += kRowHeight;
    }
    if (material.refraction != defaults.refraction) {
        link(scalarNode(material.refraction, kInputX, rowY), 10);
        rowY += kRowHeight;
    }

    if (Node* output = graph.FindNode(outputId)) {
        place(*output, 480.0f, std::max(0.0f, rowY * 0.5f - 200.0f));
    }
    graph.UpdateNextIDs();
}

} // namespace MaterialEditor
