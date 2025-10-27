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

std::string NodeTypeToString(NodeType type) {
    switch (type) {
    case NodeType::MaterialOutput: return "MaterialOutput";
    case NodeType::ConstantFloat: return "ConstantFloat";
    case NodeType::ConstantVec3: return "ConstantVec3";
    case NodeType::ColorParameter: return "ColorParameter";
    case NodeType::FloatParameter: return "FloatParameter";
    case NodeType::TextureParameter: return "TextureParameter";
    case NodeType::TextureSample: return "TextureSample";
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
    if (str == "TextureParameter") return NodeType::TextureParameter;
    if (str == "TextureSample") return NodeType::TextureSample;
    if (str == "Add") return NodeType::Add;
    if (str == "Multiply") return NodeType::Multiply;
    return NodeType::MaterialOutput;
}

bool SaveMaterialAssetFile(const std::filesystem::path& path,
                           const MaterialComponent& material,
                           const std::string& displayName,
                           const Graph& graph,
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
    
    // Save node graph
    out << "  \"graph\": {\n";
    out << "    \"nodes\": [\n";
    
    const auto& nodes = graph.GetNodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node& node = nodes[i];
        out << "      {\n";
        out << "        \"id\": " << node.id << ",\n";
        out << "        \"type\": \"" << NodeTypeToString(node.type) << "\",\n";
        out << "        \"displayName\": \"" << EscapeJson(node.displayName) << "\",\n";
        out << "        \"scalar\": " << node.scalar << ",\n";
        out << "        \"color\": [" << node.color.x << ", " << node.color.y << ", " << node.color.z << "],\n";
        out << "        \"resourcePath\": \"" << EscapeJson(node.resourcePath) << "\"\n";
        out << "      }";
        if (i < nodes.size() - 1) out << ",";
        out << "\n";
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

    // Mode tabs
    ImGui::BeginGroup();
    if (ImGui::RadioButton("Simple Properties", m_UseSimpleMode)) {
        m_UseSimpleMode = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Advanced (Graph)", !m_UseSimpleMode)) {
        m_UseSimpleMode = false;
    }
    ImGui::EndGroup();

    ImGui::Separator();

    if (m_UseSimpleMode) {
        // Simple mode: direct material property editing
        if (auto result = DrawSimpleProperties(graph)) {
            applyResult = *result;
        }
    } else {
        // Advanced mode: node graph editing
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
    }

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
            bool saved = SaveMaterialAssetFile(m_CurrentAssetPath, material, m_CurrentMaterialName, graph, saveError);
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

std::optional<PanelResult> Panel::DrawSimpleProperties(Graph& graph) {
    std::optional<PanelResult> applyResult;

    ImGui::TextUnformatted("Material Properties");
    ImGui::Separator();

    // Albedo color
    ImGui::ColorEdit3("Albedo", &m_SimpleMaterialProps.albedo.x);

    // Metallic
    ImGui::SliderFloat("Metallic", &m_SimpleMaterialProps.metallic, 0.0f, 1.0f);

    // Roughness
    ImGui::SliderFloat("Roughness", &m_SimpleMaterialProps.roughness, 0.0f, 1.0f);

    // AO
    ImGui::SliderFloat("Ambient Occlusion", &m_SimpleMaterialProps.ao, 0.0f, 1.0f);

    // Emissive
    ImGui::ColorEdit3("Emissive", &m_SimpleMaterialProps.emissive.x);

    // Emissive Strength
    ImGui::SliderFloat("Emissive Strength", &m_SimpleMaterialProps.emissiveStrength, 0.0f, 10.0f);

    ImGui::Separator();
    ImGui::TextUnformatted("Texture Settings");

    // Texture checkbox
    ImGui::Checkbox("Use Texture##SimpleMode", &m_SimpleMaterialProps.useAlbedoTexture);

    if (m_SimpleMaterialProps.useAlbedoTexture) {
        // Texture dropdown
        int currentSelection = -1;
        for (size_t i = 0; i < m_DiscoveredTextures.size(); ++i) {
            if (m_DiscoveredTextures[i] == m_SimpleMaterialProps.albedoTexturePath) {
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
        if (ImGui::Combo("Albedo Texture##Simple", &currentSelection, textureNames.data(), static_cast<int>(textureNames.size()))) {
            if (currentSelection > 0 && currentSelection - 1 < static_cast<int>(m_DiscoveredTextures.size())) {
                m_SimpleMaterialProps.albedoTexturePath = m_DiscoveredTextures[currentSelection - 1];
            } else {
                m_SimpleMaterialProps.albedoTexturePath.clear();
            }
        }

        // Manual input fallback
        static char textureBuffer[256] = { 0 };
        if (preview != nullptr) {
            std::strncpy(textureBuffer, preview, sizeof(textureBuffer) - 1);
            textureBuffer[sizeof(textureBuffer) - 1] = '\0';
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##TextureManualSimple", textureBuffer, sizeof(textureBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_SimpleMaterialProps.albedoTexturePath = textureBuffer;
        }
        ImGui::TextDisabled("Found %zu textures", m_DiscoveredTextures.size());
    }

    ImGui::Separator();

    if (ImGui::Button("Save & Apply Material", ImVec2(-1.0f, 0.0f))) {
        std::string saveError;
        bool saved = SaveMaterialAssetFile(m_CurrentAssetPath, m_SimpleMaterialProps, m_CurrentMaterialName, graph, saveError);
        if (!saved) {
            m_StatusMessage = "Save failed: " + saveError;
        } else {
            m_StatusMessage = "Material saved successfully!";
            applyResult = PanelResult{m_SimpleMaterialProps, m_CurrentMaterialName, m_CurrentAssetPath};
        }
    }

    if (!m_StatusMessage.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_StatusMessage.c_str());
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
