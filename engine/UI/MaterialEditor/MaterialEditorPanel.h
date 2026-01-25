#pragma once

#include "MaterialGraph.h"
#include "MaterialGraphEvaluator.h"
#include "TextureDiscovery.h"

#if HEPHAESTUS_EDITOR_IMGUI
#include "imgui.h"
#else
#ifndef HEPHAESTUS_IMGUI_TYPES_DEFINED
#define HEPHAESTUS_IMGUI_TYPES_DEFINED
struct ImVec2 {
    float x = 0.0f;
    float y = 0.0f;
    ImVec2() = default;
    ImVec2(float _x, float _y) : x(_x), y(_y) {}
};
using ImTextureID = void*;
#endif
#endif

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct ImNodesEditorContext;

namespace MaterialEditor {

bool SaveMaterialAssetFile(const std::filesystem::path& path,
                           const MaterialComponent& material,
                           const std::string& displayName,
                           const Graph& graph,
                           ImNodesEditorContext* editorContext,
                           std::string& outError);

bool LoadMaterialAssetFile(const std::filesystem::path& path,
                           MaterialComponent& outMaterial,
                           std::string& outDisplayName,
                           Graph& outGraph,
                           std::string& outError);

struct PanelResult {
    MaterialComponent material;
    std::string name;
    std::filesystem::path assetPath;
};

class Panel {
public:
    Panel();
    ~Panel();

    std::optional<PanelResult> Draw(Graph& graph);

    bool IsOpen() const { return m_IsOpen; }
    void SetOpen(bool open) { m_IsOpen = open; }
    void SetActiveAsset(const std::filesystem::path& path);
    void LoadMaterialIntoGraph(Graph& graph);
    void ClearActiveAsset();
    bool HasActiveAsset() const { return !m_CurrentAssetPath.empty(); }
    const std::filesystem::path& GetActiveAssetPath() const { return m_CurrentAssetPath; }
    void QueueNodePlacement(NodeID id, ImVec2 screenPos);

private:
    void EnsureContext();
    void DrawToolbar(Graph& graph);
    void DrawNodeCanvas(Graph& graph);
    std::optional<PanelResult> DrawInspector(Graph& graph);
    void RefreshTextureCache();

    bool m_IsOpen = false;
    ImNodesEditorContext* m_EditorContext = nullptr;
    std::string m_CurrentMaterialName = "NewMaterial";
    std::string m_StatusMessage;
    std::optional<NodeID> m_LastSelectedNode;
    struct PendingPlacement {
        NodeID id;
        ImVec2 screenPos;
    };
    std::vector<PendingPlacement> m_PendingPlacements;
    std::filesystem::path m_CurrentAssetPath;
    std::vector<std::string> m_DiscoveredTextures;
    std::filesystem::path m_ProjectRoot;
};

} // namespace MaterialEditor
