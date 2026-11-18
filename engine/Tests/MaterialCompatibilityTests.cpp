#include "../UI/MaterialEditor/MaterialAssetSerializer.h"
#include "../UI/MaterialEditor/MaterialGraphEvaluator.h"
#include "../UI/MaterialEditor/MaterialNodeDefinitions.h"
#include "../UI/MaterialEditor/MaterialShaderCompiler.h"
#include "../UI/MaterialEditor/MaterialShaderGenerator.h"

#include <cmath>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

using namespace MaterialEditor;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

json LoadJson(const fs::path& path) {
    std::ifstream in(path);
    json value;
    in >> value;
    return value;
}

bool TestLegacyNoGraph(const fs::path& root, const fs::path& outDir) {
    const fs::path input = root / "assets/tests/material_compat/legacy_no_graph.hematerial";
    const fs::path output = outDir / "legacy_no_graph.saved.hematerial";

    MaterialComponent material;
    Graph graph;
    std::string displayName;
    std::string error;
    if (!LoadMaterialAssetCompat(input, material, displayName, graph, error)) {
        std::cerr << error << "\n";
        return false;
    }

    bool ok = true;
    ok &= Check(graph.IsLegacyMode(), "legacy_no_graph should open in legacy mode");
    ok &= Check(!graph.IsGraphEditingEnabled(), "legacy_no_graph should start with graph editing disabled");
    ok &= Check(material.albedo.x == 1.0f && material.albedo.y == 1.0f && material.albedo.z == 1.0f, "albedo defaults to [1,1,1]");
    ok &= Check(material.roughness == 0.5f, "roughness default is 0.5");
    ok &= Check(material.ao == 1.0f, "ao default is 1.0");
    ok &= Check(material.emissive.x == 0.0f && material.emissive.y == 0.0f && material.emissive.z == 0.0f, "emissive default is [0,0,0]");
    ok &= Check(material.useAlbedoTexture == false, "useAlbedoTexture default false");

    if (!SaveMaterialAssetCompat(output, material, displayName, graph, {}, error)) {
        std::cerr << error << "\n";
        return false;
    }

    const json original = LoadJson(input);
    const json saved = LoadJson(output);
    ok &= Check(saved.contains("legacyCustom"), "unknown top-level key preserved");
    ok &= Check(original["legacyCustom"] == saved["legacyCustom"], "unknown top-level value preserved");
    ok &= Check(!saved.contains("graph"), "save does not force graph for legacy no-graph material");

    return ok;
}

bool TestUnknownGraphRoundtrip(const fs::path& root, const fs::path& outDir) {
    const fs::path input = root / "assets/tests/material_compat/unknown_graph_payload.hematerial";
    const fs::path output = outDir / "unknown_graph_payload.saved.hematerial";

    MaterialComponent material;
    Graph graph;
    std::string displayName;
    std::string error;
    if (!LoadMaterialAssetCompat(input, material, displayName, graph, error)) {
        std::cerr << error << "\n";
        return false;
    }

    bool ok = true;
    ok &= Check(!graph.GetNodes().empty(), "unknown graph should load nodes");
    ok &= Check(graph.GetLinks().size() == 2, "link count preserved on load");

    bool hasUnknownNode = false;
    for (const Node& node : graph.GetNodes()) {
        if (node.type == NodeType::Unknown) {
            hasUnknownNode = true;
            ok &= Check(node.serializedType == "LegacyCustomNode", "unknown node type captured");
            ok &= Check(node.unknownFields.contains("unknownNodePayload"), "unknown node payload captured");
        }
    }
    ok &= Check(hasUnknownNode, "unknown node rendered as opaque node");

    int orphanCount = 0;
    for (const Link& link : graph.GetLinks()) {
        if (link.orphan) {
            ++orphanCount;
        }
    }
    ok &= Check(orphanCount >= 1, "orphan link retained in model");

    if (!SaveMaterialAssetCompat(output, material, displayName, graph, {}, error)) {
        std::cerr << error << "\n";
        return false;
    }

    const json original = LoadJson(input);
    const json saved = LoadJson(output);
    ok &= Check(saved.contains("customTop"), "unknown top-level metadata preserved");
    ok &= Check(saved["customTop"] == original["customTop"], "unknown top-level value unchanged");
    ok &= Check(saved["graph"].contains("customGraph"), "unknown graph key preserved");

    const auto& savedNodes = saved["graph"]["nodes"];
    const auto& savedLinks = saved["graph"]["links"];
    ok &= Check(savedNodes.size() == original["graph"]["nodes"].size(), "node count preserved");
    ok &= Check(savedLinks.size() == original["graph"]["links"].size(), "link count preserved");

    bool foundUnknownPayload = false;
    for (const auto& node : savedNodes) {
        if (node.contains("type") && node["type"].is_string() && node["type"].get<std::string>() == "LegacyCustomNode") {
            foundUnknownPayload = node.contains("unknownNodePayload");
        }
    }
    ok &= Check(foundUnknownPayload, "unknown per-node payload preserved");
    ok &= Check(savedLinks[0].contains("customLink"), "unknown per-link payload preserved");

    return ok;
}

bool TestSwappedDirectionRepairControl(const fs::path& root, const fs::path& outDir) {
    const fs::path input = root / "assets/tests/material_compat/swapped_link_direction.hematerial";
    const fs::path outputUnfixed = outDir / "swapped_link_direction.unfixed.saved.hematerial";
    const fs::path outputFixed = outDir / "swapped_link_direction.fixed.saved.hematerial";

    MaterialComponent material;
    Graph graph;
    std::string displayName;
    std::string error;
    if (!LoadMaterialAssetCompat(input, material, displayName, graph, error)) {
        std::cerr << error << "\n";
        return false;
    }

    bool ok = true;
    ok &= Check(graph.GetLinks().size() == 1, "swapped test should have one link");
    ok &= Check(graph.GetLinks()[0].directionRepairedInMemory, "swapped link repaired in-memory");

    if (!SaveMaterialAssetCompat(outputUnfixed, material, displayName, graph, {}, error)) {
        std::cerr << error << "\n";
        return false;
    }

    json savedUnfixed = LoadJson(outputUnfixed);
    ok &= Check(savedUnfixed["graph"]["links"][0]["startPin"].get<int>() == 1, "without fix graph, original startPin preserved");
    ok &= Check(savedUnfixed["graph"]["links"][0]["endPin"].get<int>() == 8, "without fix graph, original endPin preserved");

    graph.SetApplyGraphRepairsOnSave(true);
    if (!SaveMaterialAssetCompat(outputFixed, material, displayName, graph, {}, error)) {
        std::cerr << error << "\n";
        return false;
    }

    json savedFixed = LoadJson(outputFixed);
    ok &= Check(savedFixed["graph"]["links"][0]["startPin"].get<int>() == 8, "with fix graph, repaired startPin persisted");
    ok &= Check(savedFixed["graph"]["links"][0]["endPin"].get<int>() == 1, "with fix graph, repaired endPin persisted");

    return ok;
}

bool TestGoldenRoundtrip(const fs::path& root, const fs::path& outDir, const std::string& name) {
    const fs::path input = root / ("assets/tests/material_compat/" + name);
    const fs::path output = outDir / (name + ".golden.saved");

    MaterialComponent material;
    Graph graph;
    std::string displayName;
    std::string error;
    if (!LoadMaterialAssetCompat(input, material, displayName, graph, error)) {
        std::cerr << error << "\n";
        return false;
    }
    if (!SaveMaterialAssetCompat(output, material, displayName, graph, {}, error)) {
        std::cerr << error << "\n";
        return false;
    }

    const json original = LoadJson(input);
    const json saved = LoadJson(output);

    bool ok = true;
    ok &= Check(saved.contains("graph"), "golden roundtrip keeps graph for " + name);
    ok &= Check(saved["graph"].contains("nodes"), "golden roundtrip keeps nodes for " + name);
    ok &= Check(saved["graph"].contains("links"), "golden roundtrip keeps links for " + name);
    ok &= Check(saved["graph"]["nodes"].size() == original["graph"]["nodes"].size(), "golden node count preserved for " + name);
    ok &= Check(saved["graph"]["links"].size() == original["graph"]["links"].size(), "golden link count preserved for " + name);
    return ok;
}

bool TestPinIdCollision(const fs::path& root, const fs::path& outDir) {
    const fs::path input = root / "assets/tests/material_compat/pin_id_collision.hematerial";
    const fs::path output = outDir / "pin_id_collision.saved.hematerial";

    MaterialComponent material;
    Graph graph;
    std::string displayName;
    std::string error;
    if (!LoadMaterialAssetCompat(input, material, displayName, graph, error)) {
        std::cerr << error << "\n";
        return false;
    }

    bool ok = true;
    ok &= Check(!graph.IsLegacyMode(), "pin_id_collision should not be in legacy mode");
    ok &= Check(graph.IsGraphEditingEnabled(), "pin_id_collision should have graph editing enabled");
    ok &= Check(graph.GetNodes().size() == 4, "pin_id_collision should have 4 nodes");
    ok &= Check(graph.GetLinks().size() == 3, "pin_id_collision should have 3 links");

    int orphanCount = 0;
    for (const Link& link : graph.GetLinks()) {
        if (link.orphan) {
            ++orphanCount;
        }
    }
    ok &= Check(orphanCount == 0, "pin_id_collision should have no orphan links (regression test for SetPinIDsForNode bug)");

    for (const Node& node : graph.GetNodes()) {
        if (node.type == NodeType::ConstantVec3 || node.type == NodeType::ConstantFloat) {
            ok &= Check(!node.outputs.empty(), "constant node should have output pin");
            if (!node.outputs.empty()) {
                const Pin* pin = graph.FindPin(node.outputs[0]);
                ok &= Check(pin != nullptr, "output pin should exist");
                if (pin) {
                    ok &= Check(pin->name == "Value", "constant node output pin should be named 'Value', not '" + pin->name + "'");
                }
            }
        }
    }

    if (!SaveMaterialAssetCompat(output, material, displayName, graph, {}, error)) {
        std::cerr << error << "\n";
        return false;
    }

    MaterialComponent material2;
    Graph graph2;
    std::string displayName2;
    if (!LoadMaterialAssetCompat(output, material2, displayName2, graph2, error)) {
        std::cerr << error << "\n";
        return false;
    }

    ok &= Check(graph2.GetLinks().size() == 3, "roundtrip should preserve 3 links");
    orphanCount = 0;
    for (const Link& link : graph2.GetLinks()) {
        if (link.orphan) {
            ++orphanCount;
        }
    }
    ok &= Check(orphanCount == 0, "roundtrip should have no orphan links");

    return ok;
}

bool TestExpandedNodesAndShaderGeneration() {
    Graph graph;
    NodeID outputId = graph.CreateNode(NodeType::MaterialOutput).id;
    NodeID colorId = graph.CreateNode(NodeType::ConstantVec3).id;
    NodeID roughnessId = graph.CreateNode(NodeType::Saturate).id;
    NodeID scalarId = graph.CreateNode(NodeType::ConstantFloat).id;
    if (Node* color = graph.FindNode(colorId)) color->color = Float3{0.2f, 0.4f, 0.8f};
    if (Node* scalar = graph.FindNode(scalarId)) scalar->scalar = 0.75f;
    const Node* output = graph.FindNode(outputId);
    const Node* color = graph.FindNode(colorId);
    const Node* roughness = graph.FindNode(roughnessId);
    const Node* scalar = graph.FindNode(scalarId);

    bool ok = true;
    ok &= Check(output && output->inputs.size() >= 11, "MaterialOutput exposes expanded PBR pins");
    ok &= Check(color && !color->outputs.empty(), "color node has output pin");
    ok &= Check(roughness && !roughness->inputs.empty() && !roughness->outputs.empty(), "saturate node has pins");
    ok &= Check(scalar && !scalar->outputs.empty(), "scalar node has output pin");
    if (!output || !color || !roughness || !scalar) {
        return false;
    }
    ok &= Check(graph.CreateLink(color->outputs[0], output->inputs[0]) != nullptr, "vec3 base color link succeeds");
    ok &= Check(graph.CreateLink(scalar->outputs[0], roughness->inputs[0]) != nullptr, "scalar math link succeeds");
    ok &= Check(graph.CreateLink(roughness->outputs[0], output->inputs[2]) != nullptr, "math roughness link succeeds");

    NodeID scalarToColorId = graph.CreateNode(NodeType::ConstantFloat).id;
    if (Node* scalarToColor = graph.FindNode(scalarToColorId)) scalarToColor->scalar = 0.25f;
    const Node* scalarToColor = graph.FindNode(scalarToColorId);
    output = graph.FindNode(outputId);
    ok &= Check(output && scalarToColor && graph.CanConnect(scalarToColor->outputs[0], output->inputs[3]), "scalar can auto-expand to vector input");

    MaterialShaderGenerationResult generated = GenerateMaterialShader(graph);
    ok &= Check(generated.ok, "expanded graph shader generation succeeds");
    ok &= Check(generated.body.find("surface.albedo") != std::string::npos, "shader body writes albedo");
    ok &= Check(generated.body.find("clamp") != std::string::npos, "shader body contains Saturate clamp");
    return ok;
}

bool CheckDefaultGraphLossless(const MaterialComponent& material, const std::string& label) {
    Graph graph;
    BuildMinimalGraphFromLegacyPbr(material, graph);
    MaterialComponent evaluated = material;
    std::string error;
    if (!Check(EvaluateGraph(graph, evaluated, error), label + ": default graph evaluates: " + error)) {
        return false;
    }
    const std::string loss = DescribeRenderedDifferences(material, evaluated);
    return Check(loss.empty(), label + ": default graph loses" + loss);
}

bool TestDefaultGraphIsLossless() {
    bool ok = true;
    MaterialComponent plain;
    plain.albedo = Vec3(0.2f, 0.4f, 0.6f);
    plain.metallic = 0.3f;
    plain.roughness = 0.7f;
    plain.specular = 0.25f;
    plain.emissive = Vec3(1.0f, 0.5f, 0.0f);
    plain.emissiveStrength = 3.0f;
    plain.reflectionStrength = 0.4f;
    ok &= CheckDefaultGraphLossless(plain, "constant PBR");

    MaterialComponent tinted;
    tinted.albedo = Vec3(0.8f, 0.6f, 0.4f);
    tinted.useAlbedoTexture = true;
    tinted.albedoTexturePath = "assets/textures/bark.png";
    tinted.useMetallicTexture = true;
    tinted.metallicTexturePath = "assets/textures/orm.png";
    tinted.metallicChannel = 2;
    tinted.metallic = 0.5f;
    tinted.useRoughnessTexture = true;
    tinted.roughnessTexturePath = "assets/textures/orm.png";
    tinted.roughnessChannel = 1;
    tinted.roughness = 0.9f;
    tinted.useAoTexture = true;
    tinted.aoTexturePath = "assets/textures/orm.png";
    tinted.ao = 0.8f;
    tinted.useNormalTexture = true;
    tinted.normalTexturePath = "assets/textures/bark_n.png";
    ok &= CheckDefaultGraphLossless(tinted, "tinted textures with packed maps");

    MaterialComponent worldAligned = tinted;
    worldAligned.useWorldAlignedTexture = true;
    worldAligned.worldTextureScale = 0.25f;
    worldAligned.metallicChannel = 0;
    worldAligned.roughnessChannel = 0;
    ok &= CheckDefaultGraphLossless(worldAligned, "world-aligned textures");

    MaterialComponent worldAlignedPacked = worldAligned;
    worldAlignedPacked.roughnessChannel = 1;
    Graph packedGraph;
    BuildMinimalGraphFromLegacyPbr(worldAlignedPacked, packedGraph);
    MaterialComponent packedEvaluated = worldAlignedPacked;
    std::string error;
    ok &= Check(EvaluateGraph(packedGraph, packedEvaluated, error) &&
                    !DescribeRenderedDifferences(worldAlignedPacked, packedEvaluated).empty(),
                "a world-aligned packed map the graph cannot express is reported, not silently changed");

    MaterialComponent masked;
    masked.blendMode = MaterialBlendMode::Masked;
    masked.useAlbedoTexture = true;
    masked.albedoTexturePath = "assets/textures/leaves.png";
    masked.opacityMask = 0.35f;
    ok &= CheckDefaultGraphLossless(masked, "masked foliage");

    MaterialComponent glass;
    glass.blendMode = MaterialBlendMode::AlphaBlend;
    glass.opacity = 0.25f;
    glass.opacityMask = 0.0f;
    glass.refraction = 1.2f;
    ok &= CheckDefaultGraphLossless(glass, "translucent glass");

    MaterialComponent flash;
    flash.blendMode = MaterialBlendMode::Additive;
    flash.ao = 0.5f;
    ok &= CheckDefaultGraphLossless(flash, "additive");
    return ok;
}

bool TestMaskedBlendModeRoundtrip(const fs::path& outDir) {
    MaterialComponent material;
    material.blendMode = MaterialBlendMode::Masked;
    material.opacityMask = 0.4f;
    Graph graph;
    BuildMinimalGraphFromLegacyPbr(material, graph);
    const fs::path output = outDir / "masked.saved.hematerial";
    std::string error;
    bool ok = Check(SaveMaterialAssetCompat(output, material, "Masked", graph, {}, error), "save masked: " + error);
    ok &= Check(LoadJson(output).value("blendMode", std::string()) == "masked", "masked writes blendMode masked");
    MaterialComponent loaded;
    std::string displayName;
    Graph loadedGraph;
    ok &= Check(LoadMaterialAssetCompat(output, loaded, displayName, loadedGraph, error) &&
                    loaded.blendMode == MaterialBlendMode::Masked && std::abs(loaded.opacityMask - 0.4f) < 1e-6f,
                "masked loads back with its clip threshold");
    return ok;
}

bool TestProjectMaterialsMigrate(const fs::path& folder) {
    bool ok = true;
    int checked = 0;
    int graphs = 0;
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (entry.path().extension() != ".hematerial") continue;
        MaterialComponent material;
        std::string displayName;
        std::string error;
        Graph graph;
        if (!Check(LoadMaterialAssetCompat(entry.path(), material, displayName, graph, error),
                   entry.path().string() + ": " + error)) {
            ok = false;
            continue;
        }
        if (graph.HasSourceGraph()) {
            MaterialComponent evaluated = material;
            if (!graph.IsLegacyMode() && EvaluateGraph(graph, evaluated, error)) {
                ok &= Check(evaluated.blendMode == material.blendMode,
                            entry.path().string() + ": graph keeps its blend mode");
            }
            ++graphs;
            continue;
        }
        ok &= CheckDefaultGraphLossless(material, entry.path().string());
        ++checked;
    }
    std::cout << "Migrated " << checked << " materials without a graph, evaluated " << graphs << " graphs, under "
              << folder.string() << "\n";
    return ok;
}

bool TestCompiledMaterialShaders(const fs::path& root, const fs::path& outDir) {
    bool ok = true;
    MaterialComponent tinted;
    tinted.albedo = Vec3(0.5f, 0.6f, 0.7f);
    tinted.useAlbedoTexture = true;
    tinted.albedoTexturePath = "assets/textures/bark.png";
    tinted.useRoughnessTexture = true;
    tinted.roughnessTexturePath = "assets/textures/orm.png";
    tinted.roughnessChannel = 1;
    Graph fixedGraph;
    BuildMinimalGraphFromLegacyPbr(tinted, fixedGraph);
    ok &= Check(!GraphNeedsCompiledShader(fixedGraph), "a default graph draws with the standard shader");

    std::string registerError;
    CustomNodeDefinition timeNode;
    timeNode.owner = "test.plugin";
    timeNode.typeName = "TestTime";
    timeNode.outputs = {{"Seconds", ValueType::Float1, "{time}"}};
    CustomNodeDefinition noiseNode;
    noiseNode.owner = "test.plugin";
    noiseNode.typeName = "TestNoise";
    noiseNode.category = "Procedural";
    noiseNode.functions = "float testHash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }";
    noiseNode.inputs = {{"UV", ValueType::Float2, "{uv}"}, {"Scale", ValueType::Float1, "1.0"}};
    noiseNode.outputs = {{"Value", ValueType::Float1, "testHash({0} * {1})"}};
    ok &= Check(RegisterCustomNode(timeNode, registerError) && RegisterCustomNode(noiseNode, registerError),
                "plugin nodes register: " + registerError);
    ok &= Check(!RegisterCustomNode(timeNode, registerError), "a plugin node name can only be registered once");
    CustomNodeDefinition clash = timeNode;
    clash.typeName = "Multiply";
    ok &= Check(!RegisterCustomNode(clash, registerError), "a plugin node cannot take a core node's name");

    Graph graph;
    Node& output = graph.CreateNode(NodeType::MaterialOutput);
    const std::vector<PinID> outputs = output.inputs;
    Node& uv = graph.CreateNode(NodeType::TexCoord);
    const PinID uvOut = uv.outputs[0];
    Node& tiling = graph.CreateNode(NodeType::TextureTilingOffset);
    const std::vector<PinID> tilingIn = tiling.inputs;
    const PinID tilingOut = tiling.outputs[0];
    Node& scale = graph.CreateNode(NodeType::Vector2Parameter);
    scale.color = Float3{4.0f, 2.0f, 0.0f};
    const PinID scaleOut = scale.outputs[0];
    Node& sample = graph.CreateNode(NodeType::TextureSample);
    sample.resourcePath = "assets/textures/bark.png";
    const std::vector<PinID> sampleIn = sample.inputs;
    const PinID sampleRgb = sample.outputs[1];
    Node* time = graph.CreateNodeOfType("TestTime");
    const PinID timeOut = time ? time->outputs[0] : 0;
    Node* noise = graph.CreateNodeOfType("TestNoise");
    const std::vector<PinID> noiseIn = noise ? noise->inputs : std::vector<PinID>{0, 0};
    const PinID noiseOut = noise ? noise->outputs[0] : 0;
    ok &= Check(time && noise && noise->type == NodeType::Custom && noiseIn.size() == 2, "plugin nodes get their pins");
    Node& tint = graph.CreateNode(NodeType::ConstantVec3);
    tint.color = Float3{1.0f, 0.5f, 0.25f};
    const PinID tintOut = tint.outputs[0];
    Node& multiply = graph.CreateNode(NodeType::Multiply);
    const std::vector<PinID> multiplyIn = multiply.inputs;
    const PinID multiplyOut = multiply.outputs[0];
    ok &= Check(graph.CreateLink(uvOut, tilingIn[0]) && graph.CreateLink(scaleOut, tilingIn[1]) &&
                    graph.CreateLink(tilingOut, sampleIn[0]) && graph.CreateLink(sampleRgb, multiplyIn[0]) &&
                    graph.CreateLink(tintOut, multiplyIn[1]) && graph.CreateLink(multiplyOut, outputs[0]) &&
                    graph.CreateLink(timeOut, noiseIn[1]) && graph.CreateLink(noiseOut, outputs[3]),
                "per-pixel test graph links");
    ok &= Check(GraphNeedsCompiledShader(graph), "tiled UVs and animated noise need a compiled shader");

    const MaterialShaderGenerationResult generated = GenerateMaterialShader(graph);
    const std::string source = BuildMaterialGraphSource(generated);
    ok &= Check(source.find("ubo.time") != std::string::npos && source.find("graphTextureSlots[0][0]") != std::string::npos &&
                    source.find("testHash(((fragTexCoord)) * (matNode_") != std::string::npos &&
                    source.find("float testHash(vec2 p)") != std::string::npos,
                "generated surface reads time, the graph's texture slot and the plugin node's GLSL:\n" + source);

    const MaterialShaderCompileResult compiled = CompileMaterialGraph(graph, root / "shaders");
    std::string diagnostics;
    for (const std::string& line : compiled.diagnostics) diagnostics += line + "\n";
    ok &= Check(compiled.ok && compiled.shader.spirv.size() > 5 && compiled.shader.spirv[0] == 0x07230203u &&
                    compiled.shader.spirvRayTraced.size() > 5,
                "graph compiles to raster and ray-traced SPIR-V:\n" + diagnostics + source);
    ok &= Check(compiled.shader.textures.size() == 1 && compiled.shader.textures[0].path == "assets/textures/bark.png" && !compiled.shader.textures[0].linear,
                "compiled shader lists its texture");

    const fs::path shaderPath = Hephaestus::CompiledMaterialShaderPath(outDir / "animated.hematerial");
    std::string error;
    ok &= Check(Hephaestus::WriteCompiledMaterialShader(shaderPath, compiled.shader, error), "write .hemshader: " + error);
    Hephaestus::CompiledMaterialShader loaded;
    ok &= Check(Hephaestus::ReadCompiledMaterialShader(shaderPath, loaded, error) && loaded.spirv == compiled.shader.spirv &&
                    loaded.spirvRayTraced == compiled.shader.spirvRayTraced && loaded.textures == compiled.shader.textures &&
                    loaded.sourceHash == compiled.shader.sourceHash,
                ".hemshader round trips: " + error);

    ok &= Check(CompileMaterialGraph(graph, root / "missing-folder").diagnostics.size() > 0,
                "a missing shader source folder is reported, not ignored");

    graph.SetHasSourceGraph(true);
    const fs::path pluginMaterial = outDir / "plugin_nodes.hematerial";
    MaterialComponent material;
    ok &= Check(SaveMaterialAssetCompat(pluginMaterial, material, "PluginNodes", graph, {}, error),
                "save a material using plugin nodes: " + error);
    UnregisterCustomNodes("test.plugin");
    MaterialComponent reloaded;
    std::string displayName;
    Graph withoutPlugin;
    ok &= Check(LoadMaterialAssetCompat(pluginMaterial, reloaded, displayName, withoutPlugin, error),
                "load it with the plugin off: " + error);
    ok &= Check(fs::exists(Hephaestus::CompiledMaterialShaderPath(pluginMaterial)),
                "saving with plugin nodes compiles the material's shader");
    std::vector<std::string> shaderDiagnostics;
    ok &= Check(SaveMaterialAssetCompat(pluginMaterial, reloaded, displayName, withoutPlugin, {}, error,
                                        &shaderDiagnostics),
                "resave: " + error);
    ok &= Check(fs::exists(Hephaestus::CompiledMaterialShaderPath(pluginMaterial)) && !shaderDiagnostics.empty(),
                "saving while the plugin is off keeps the last compiled shader and says why");
    const json resavedJson = LoadJson(pluginMaterial);
    int customNodes = 0;
    for (const json& node : resavedJson["graph"]["nodes"]) {
        if (node.value("type", "") == "TestNoise" || node.value("type", "") == "TestTime") ++customNodes;
    }
    ok &= Check(customNodes == 2, "plugin nodes survive a save while their plugin is off");
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: MaterialCompatibilityTests <repo_root> [project_assets_folder]\n";
        return 2;
    }

    const fs::path root = fs::path(argv[1]);
    const fs::path outDir = root / "build/material_compat_test_out";
    fs::create_directories(outDir);

    bool ok = true;
    ok &= TestLegacyNoGraph(root, outDir);
    ok &= TestUnknownGraphRoundtrip(root, outDir);
    ok &= TestSwappedDirectionRepairControl(root, outDir);
    ok &= TestGoldenRoundtrip(root, outDir, "unknown_graph_payload.hematerial");
    ok &= TestPinIdCollision(root, outDir);
    ok &= TestExpandedNodesAndShaderGeneration();
    ok &= TestDefaultGraphIsLossless();
    ok &= TestMaskedBlendModeRoundtrip(outDir);
    ok &= TestCompiledMaterialShaders(root, outDir);
    if (argc > 2) {
        ok &= TestProjectMaterialsMigrate(fs::path(argv[2]));
    }

    if (!ok) {
        return 1;
    }

    std::cout << "Material compatibility tests passed.\n";
    return 0;
}
