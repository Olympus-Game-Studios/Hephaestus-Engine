#include "MaterialEditorPanel.h"

#include "imgui.h"
#include "imnodes.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
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

bool SaveMaterialAssetFile(const std::filesystem::path& path,
                           const MaterialComponent& material,
                           const std::string& displayName,
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
    out << "  \"albedoTexturePath\": \"" << EscapeJson(material.albedoTexturePath) << "\"\n";
    out << "}\n";

    if (!out.good()) {
        outError = "Failed to write material data.";
        return false;
    }

    outError.clear();
    return true;
}

} // namespace

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

    EnsureContext();
    if (!m_EditorContext) {
        return applyResult;
    }

    ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Material Editor", &m_IsOpen)) {
        ImGui::End();
        return applyResult;
    }

    DrawToolbar(graph);

    ImGui::Separator();

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
            if (ImGui::MenuItem("Texture Parameter")) {
                spawnNode(NodeType::TextureParameter);
            }
            if (ImGui::MenuItem("Texture Sample")) {
                spawnNode(NodeType::TextureSample);
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

    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !io.WantCaptureKeyboard) {
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
                if (node->type == NodeType::MaterialOutput) {
                    continue;
                }
                graph.RemoveNode(static_cast<NodeID>(nodeId));
            }
            ImNodes::ClearNodeSelection();
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
        case NodeType::TextureParameter: {
            InputText("Texture Path", node->resourcePath);
            ImGui::TextDisabled("Drag & drop texture paths once asset browser integration lands.");
            break;
        }
        case NodeType::TextureSample:
            ImGui::TextWrapped("Feeds color from an upstream texture parameter. UV input is reserved for future workflows.");
            break;
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

    if (ImGui::Button("Apply To Selected Entity")) {
        MaterialComponent material;
        std::string error;
        if (EvaluateGraph(graph, material, error)) {
            std::string saveError;
            bool saved = SaveMaterialAssetFile(m_CurrentAssetPath, material, m_CurrentMaterialName, saveError);
            if (!saved) {
                m_StatusMessage = "Material applied, but save failed: " + saveError;
            } else {
                m_StatusMessage = "Material saved.";
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
    m_StatusMessage = "Opened material '" + m_CurrentMaterialName + "'.";
    m_LastSelectedNode.reset();
    m_PendingPlacements.clear();
    m_IsOpen = true;

    if (m_EditorContext) {
        ImNodes::EditorContextSet(m_EditorContext);
        ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
        ImNodes::EditorContextSet(nullptr);
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
