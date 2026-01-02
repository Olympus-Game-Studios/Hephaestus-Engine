#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MaterialEditor {

using NodeID = int32_t;
using PinID = int32_t;
using LinkID = int32_t;

enum class ValueType {
    Float1,
    Float2,
    Float3,
    Float4,
    Sampler2D
};

enum class PinKind {
    Input,
    Output
};

enum class NodeType {
    MaterialOutput,
    ConstantFloat,
    ConstantVec3,
    ColorParameter,
    FloatParameter,
    AlbedoTextureSample,
    NormalTextureSample,
    RoughnessTextureSample,
    AoTextureSample,
    WorldAlignedTexture,
    Add,
    Multiply
};

struct Float3 {
    float x = 1.0f;
    float y = 1.0f;
    float z = 1.0f;
};

struct Pin {
    PinID id = 0;
    NodeID node = 0;
    std::string name;
    PinKind kind = PinKind::Input;
    ValueType valueType = ValueType::Float1;
};

struct Node {
    NodeID id = 0;
    NodeType type = NodeType::MaterialOutput;
    std::string displayName;
    float scalar = 0.0f;
    Float3 color;
    std::string resourcePath;
    float worldAlignedScale = 1.0f;  // Scale for world-aligned textures (units per texture repeat)
    std::vector<PinID> inputs;
    std::vector<PinID> outputs;
    
    // Position in editor (for serialization)
    float posX = 0.0f;
    float posY = 0.0f;
};

struct Link {
    LinkID id = 0;
    PinID startPin = 0;
    PinID endPin = 0;
};

class Graph {
public:
    Graph();

    Node& CreateNode(NodeType type, std::string displayName = {});
    bool RemoveNode(NodeID id);

    Pin* FindPin(PinID id);
    const Pin* FindPin(PinID id) const;
    Node* FindNode(NodeID id);
    const Node* FindNode(NodeID id) const;
    Link* FindLink(LinkID id);
    const Link* FindLink(LinkID id) const;

    Link* CreateLink(PinID from, PinID to);
    bool RemoveLink(LinkID id);
    PinID FindInputSource(PinID inputPin) const;

    void Clear();

    const std::vector<Node>& GetNodes() const { return m_Nodes; }
    const std::vector<Link>& GetLinks() const { return m_Links; }
    const std::vector<Pin>& GetPins() const { return m_Pins; }

    // Serialization support - creates nodes/links with specific IDs for loading saved graphs
    Node& CreateNodeWithID(NodeID id, NodeType type, std::string displayName = {});
    void SetPinIDsForNode(NodeID nodeId, const std::vector<PinID>& inputPinIds, const std::vector<PinID>& outputPinIds);
    Link* CreateLinkWithID(LinkID id, PinID from, PinID to);
    void UpdateNextIDs(); // Call after loading to ensure new IDs don't conflict

private:
    std::string MakeNodeDisplayName(NodeType type) const;
    void AttachDefaultPins(Node& node);
    Pin& AddPin(Node& owner, PinKind kind, ValueType valueType, std::string name);
    void RemoveLinksForPin(PinID id);

    NodeID m_NextNodeID;
    PinID m_NextPinID;
    LinkID m_NextLinkID;
    std::vector<Node> m_Nodes;
    std::vector<Pin> m_Pins;
    std::vector<Link> m_Links;
};

} // namespace MaterialEditor
