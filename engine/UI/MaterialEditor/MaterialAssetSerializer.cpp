#include "MaterialAssetSerializer.h"

#include <fstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace MaterialEditor {
namespace {

using json = nlohmann::json;

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
    case NodeType::Lerp: return "Lerp";
    case NodeType::Unknown: return "Unknown";
    }
    return "Unknown";
}

bool TryNodeTypeFromString(const std::string& str, NodeType& outType) {
    if (str == "MaterialOutput") { outType = NodeType::MaterialOutput; return true; }
    if (str == "ConstantFloat") { outType = NodeType::ConstantFloat; return true; }
    if (str == "ConstantVec3") { outType = NodeType::ConstantVec3; return true; }
    if (str == "ColorParameter") { outType = NodeType::ColorParameter; return true; }
    if (str == "FloatParameter") { outType = NodeType::FloatParameter; return true; }
    if (str == "AlbedoTextureSample") { outType = NodeType::AlbedoTextureSample; return true; }
    if (str == "NormalTextureSample") { outType = NodeType::NormalTextureSample; return true; }
    if (str == "RoughnessTextureSample") { outType = NodeType::RoughnessTextureSample; return true; }
    if (str == "AoTextureSample") { outType = NodeType::AoTextureSample; return true; }
    if (str == "AOTextureSample") { outType = NodeType::AoTextureSample; return true; }
    if (str == "WorldAlignedTexture") { outType = NodeType::WorldAlignedTexture; return true; }
    if (str == "Add") { outType = NodeType::Add; return true; }
    if (str == "Multiply") { outType = NodeType::Multiply; return true; }
    if (str == "Lerp") { outType = NodeType::Lerp; return true; }
    if (str == "ConstantScalar") { outType = NodeType::ConstantFloat; return true; }
    if (str == "ConstantColor") { outType = NodeType::ConstantVec3; return true; }
    return false;
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

    if (root.contains("useAlbedoTexture") && root["useAlbedoTexture"].is_boolean()) outMaterial.useAlbedoTexture = root["useAlbedoTexture"].get<bool>();
    if (root.contains("albedoTexturePath") && root["albedoTexturePath"].is_string()) outMaterial.albedoTexturePath = root["albedoTexturePath"].get<std::string>();
    if (root.contains("useNormalTexture") && root["useNormalTexture"].is_boolean()) outMaterial.useNormalTexture = root["useNormalTexture"].get<bool>();
    if (root.contains("normalTexturePath") && root["normalTexturePath"].is_string()) outMaterial.normalTexturePath = root["normalTexturePath"].get<std::string>();
    if (root.contains("useRoughnessTexture") && root["useRoughnessTexture"].is_boolean()) outMaterial.useRoughnessTexture = root["useRoughnessTexture"].get<bool>();
    if (root.contains("roughnessTexturePath") && root["roughnessTexturePath"].is_string()) outMaterial.roughnessTexturePath = root["roughnessTexturePath"].get<std::string>();
    if (root.contains("useAoTexture") && root["useAoTexture"].is_boolean()) outMaterial.useAoTexture = root["useAoTexture"].get<bool>();
    if (root.contains("aoTexturePath") && root["aoTexturePath"].is_string()) outMaterial.aoTexturePath = root["aoTexturePath"].get<std::string>();
    if (root.contains("useWorldAlignedTexture") && root["useWorldAlignedTexture"].is_boolean()) outMaterial.useWorldAlignedTexture = root["useWorldAlignedTexture"].get<bool>();
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
                "id", "type", "displayName", "scalar", "color", "resourcePath", "worldAlignedScale", "posX", "posY", "inputPins", "outputPins"
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
                             std::string& outError) {
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
    root["useAlbedoTexture"] = material.useAlbedoTexture;
    root["albedoTexturePath"] = material.albedoTexturePath;
    root["useNormalTexture"] = material.useNormalTexture;
    root["normalTexturePath"] = material.normalTexturePath;
    root["useRoughnessTexture"] = material.useRoughnessTexture;
    root["roughnessTexturePath"] = material.roughnessTexturePath;
    root["useAoTexture"] = material.useAoTexture;
    root["aoTexturePath"] = material.aoTexturePath;
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
            nodeJson["resourcePath"] = node.resourcePath;
            nodeJson["worldAlignedScale"] = node.worldAlignedScale;

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

    outError.clear();
    return true;
}

void BuildMinimalGraphFromLegacyPbr(const MaterialComponent& material, Graph& graph) {
    graph.Clear();
    graph.SetLegacyMode(false);
    graph.SetGraphEditingEnabled(true);
    graph.SetHasSourceGraph(true);

    Node& output = graph.CreateNode(NodeType::MaterialOutput);
    output.posX = 450.0f;
    output.posY = 200.0f;

    auto connectScalar = [&](float value, PinID outputPin) {
        Node& scalarNode = graph.CreateNode(NodeType::ConstantFloat);
        scalarNode.scalar = value;
        scalarNode.posX = 160.0f;
        scalarNode.posY = output.posY;
        if (!scalarNode.outputs.empty()) {
            graph.CreateLink(scalarNode.outputs[0], outputPin);
        }
    };

    auto connectColor = [&](const Vec3& color, PinID outputPin) {
        Node& colorNode = graph.CreateNode(NodeType::ConstantVec3);
        colorNode.color = Float3{color.x, color.y, color.z};
        colorNode.posX = 160.0f;
        colorNode.posY = output.posY;
        if (!colorNode.outputs.empty()) {
            graph.CreateLink(colorNode.outputs[0], outputPin);
        }
    };

    if (!output.inputs.empty()) {
        if (material.useAlbedoTexture && !material.albedoTexturePath.empty()) {
            Node& tex = graph.CreateNode(NodeType::AlbedoTextureSample);
            tex.resourcePath = material.albedoTexturePath;
            tex.posX = 140.0f;
            tex.posY = 80.0f;
            if (!tex.outputs.empty()) {
                graph.CreateLink(tex.outputs[0], output.inputs[0]);
            }
        } else {
            connectColor(material.albedo, output.inputs[0]);
        }
    }
    if (output.inputs.size() > 1) connectScalar(material.metallic, output.inputs[1]);
    if (output.inputs.size() > 2) connectScalar(material.roughness, output.inputs[2]);
    if (output.inputs.size() > 3) connectColor(material.emissive, output.inputs[3]);

    if (output.inputs.size() > 4 && material.useNormalTexture && !material.normalTexturePath.empty()) {
        Node& tex = graph.CreateNode(NodeType::NormalTextureSample);
        tex.resourcePath = material.normalTexturePath;
        tex.posX = 140.0f;
        tex.posY = 260.0f;
        if (!tex.outputs.empty()) {
            graph.CreateLink(tex.outputs[0], output.inputs[4]);
        }
    }

    if (output.inputs.size() > 5) {
        if (material.useRoughnessTexture && !material.roughnessTexturePath.empty()) {
            Node& tex = graph.CreateNode(NodeType::RoughnessTextureSample);
            tex.resourcePath = material.roughnessTexturePath;
            tex.posX = 140.0f;
            tex.posY = 320.0f;
            if (!tex.outputs.empty()) {
                graph.CreateLink(tex.outputs[0], output.inputs[5]);
            }
        } else {
            connectScalar(material.roughness, output.inputs[5]);
        }
    }

    if (output.inputs.size() > 6) {
        if (material.useAoTexture && !material.aoTexturePath.empty()) {
            Node& tex = graph.CreateNode(NodeType::AoTextureSample);
            tex.resourcePath = material.aoTexturePath;
            tex.posX = 140.0f;
            tex.posY = 380.0f;
            if (!tex.outputs.empty()) {
                graph.CreateLink(tex.outputs[0], output.inputs[6]);
            }
        } else {
            connectScalar(material.ao, output.inputs[6]);
        }
    }

    graph.UpdateNextIDs();
}

} // namespace MaterialEditor
