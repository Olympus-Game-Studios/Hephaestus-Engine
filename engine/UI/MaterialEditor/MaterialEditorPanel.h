#pragma once

#include "MaterialGraph.h"
#include "MaterialGraphEvaluator.h"
#include "TextureDiscovery.h"

#include "imgui.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct ImNodesEditorContext;

namespace MaterialEditor {

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
