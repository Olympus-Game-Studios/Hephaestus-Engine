#include "../UI/MaterialEditor/MaterialAssetSerializer.h"

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
    return Check(original == saved, "golden roundtrip mismatch for " + name);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: MaterialCompatibilityTests <repo_root>\n";
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

    if (!ok) {
        return 1;
    }

    std::cout << "Material compatibility tests passed.\n";
    return 0;
}
