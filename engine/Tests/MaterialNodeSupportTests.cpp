#include "UI/MaterialEditor/MaterialGraph.h"
#include "UI/MaterialEditor/MaterialGraphEvaluator.h"
#include "UI/MaterialEditor/MaterialNodeDefinitions.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace MaterialEditor;

namespace {

constexpr float kProbe = 0.73f;
constexpr int kRoughnessInput = 2;

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    std::cout << "[PASS] " << message << "\n";
    return true;
}

bool EvaluateRoughnessThrough(NodeType type, float& outRoughness)
{
    Graph graph;
    Node& output = graph.CreateNode(NodeType::MaterialOutput);
    const PinID roughnessPin = output.inputs[kRoughnessInput];

    Node& probe = graph.CreateNode(type);
    probe.scalar = kProbe;
    probe.color = Float3{kProbe, kProbe, kProbe};
    probe.boolValue = true;
    const NodeID probeId = probe.id;
    const std::vector<PinID> probeInputs = probe.inputs;
    for (PinID input : probeInputs) {
        Node& constant = graph.CreateNode(NodeType::ConstantFloat);
        constant.scalar = kProbe;
        graph.CreateLink(constant.outputs[0], input);
    }

    const Node* probeNode = graph.FindNode(probeId);
    if (!probeNode || probeNode->outputs.empty() || !graph.CreateLink(probeNode->outputs[0], roughnessPin)) {
        return false;
    }

    MaterialComponent material;
    std::string error;
    if (!EvaluateGraph(graph, material, error)) {
        return false;
    }
    outRoughness = material.roughness;
    return true;
}

// Roughness 0.4 on the Roughness pin and a texture on the Roughness Map pin (the layout imported PBR materials use).
bool RoughnessValueSurvivesMap()
{
    Graph graph;
    Node& output = graph.CreateNode(NodeType::MaterialOutput);
    const PinID roughnessPin = output.inputs[kRoughnessInput];
    const PinID roughnessMapPin = output.inputs[5];
    Node& constant = graph.CreateNode(NodeType::ConstantFloat);
    constant.scalar = 0.4f;
    const PinID constantOut = constant.outputs[0];
    Node& map = graph.CreateNode(NodeType::RoughnessTextureSample);
    map.resourcePath = "assets/textures/rough.png";
    const PinID mapOut = map.outputs[0];
    if (!graph.CreateLink(constantOut, roughnessPin) || !graph.CreateLink(mapOut, roughnessMapPin)) {
        return Check(false, "roughness graph links");
    }
    MaterialComponent material;
    std::string error;
    const bool evaluated = EvaluateGraph(graph, material, error);
    return Check(evaluated && material.useRoughnessTexture && material.roughnessTexturePath == "assets/textures/rough.png"
                     && std::fabs(material.roughness - 0.4f) < 1e-6f,
                 "a Roughness Map keeps the Roughness pin's value as its multiplier");
}

} // namespace

int main()
{
    bool ok = true;
    ok &= RoughnessValueSurvivesMap();

    float roughness = -1.0f;
    ok &= Check(EvaluateRoughnessThrough(NodeType::ConstantFloat, roughness) &&
                    std::fabs(roughness - kProbe) < 1e-4f,
                "a supported node drives the rendered roughness");

    for (const NodeDefinition& definition : GetAllNodeDefinitions()) {
        if (NodeAffectsRenderedMaterial(definition.type)) {
            continue;
        }
        roughness = -1.0f;
        const bool linked = EvaluateRoughnessThrough(definition.type, roughness);
        ok &= Check(linked && std::fabs(roughness) < 1e-6f,
                    std::string(definition.typeName) + " is marked as no effect and the evaluator ignores it");
    }

    return ok ? 0 : 1;
}
