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
    TextureParameter,
    TextureSample,
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
    std::vector<PinID> inputs;
    std::vector<PinID> outputs;
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
