#include "MaterialGraph.h"
#include "MaterialNodeDefinitions.h"

namespace MaterialEditor {
namespace {

void ApplyNodeDefaults(Node& node) {
    switch (node.type) {
    case NodeType::ConstantFloat:
    case NodeType::FloatParameter:
    case NodeType::BoolParameter:
    case NodeType::StaticSwitchParameter:
        node.scalar = 0.5f;
        break;
    case NodeType::ConstantVec3:
    case NodeType::ColorParameter:
        node.color = Float3{1.0f, 1.0f, 1.0f};
        break;
    case NodeType::Clamp:
        node.scalar = 1.0f;
        break;
    case NodeType::Compare:
        node.operation = ">";
        break;
    case NodeType::Gradient:
        node.operation = "Linear";
        break;
    case NodeType::Transform:
        node.sourceSpace = "World";
        node.targetSpace = "Local";
        break;
    case NodeType::TextureParameter:
        node.parameterName = "Texture";
        break;
    case NodeType::Vector2Parameter:
        node.parameterName = "Vector2";
        break;
    default:
        break;
    }
}

} // namespace

Graph::Graph()
    : m_NextNodeID(1)
    , m_NextPinID(1)
    , m_NextLinkID(1) {}

Node& Graph::CreateNode(NodeType type, std::string displayName) {
    Node node;
    node.id = m_NextNodeID++;
    node.type = type;
    node.serializedType = (type == NodeType::Unknown) ? "Unknown" : "";
    node.displayName = displayName.empty() ? MakeNodeDisplayName(type) : std::move(displayName);
    ApplyNodeDefaults(node);
    m_Nodes.push_back(std::move(node));
    Node& stored = m_Nodes.back();
    AttachDefaultPins(stored);
    return stored;
}

bool Graph::RemoveNode(NodeID id) {
    auto nodeIt = std::find_if(m_Nodes.begin(), m_Nodes.end(), [id](const Node& n) { return n.id == id; });
    if (nodeIt == m_Nodes.end()) {
        return false;
    }

    for (PinID pinID : nodeIt->inputs) {
        RemoveLinksForPin(pinID);
    }
    for (PinID pinID : nodeIt->outputs) {
        RemoveLinksForPin(pinID);
    }

    m_Pins.erase(std::remove_if(m_Pins.begin(), m_Pins.end(), [id](const Pin& p) { return p.node == id; }), m_Pins.end());
    m_Nodes.erase(nodeIt);
    return true;
}

Pin* Graph::FindPin(PinID id) {
    auto it = std::find_if(m_Pins.begin(), m_Pins.end(), [id](const Pin& p) { return p.id == id; });
    return it != m_Pins.end() ? &(*it) : nullptr;
}

const Pin* Graph::FindPin(PinID id) const {
    auto it = std::find_if(m_Pins.begin(), m_Pins.end(), [id](const Pin& p) { return p.id == id; });
    return it != m_Pins.end() ? &(*it) : nullptr;
}

Node* Graph::FindNode(NodeID id) {
    auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(), [id](const Node& n) { return n.id == id; });
    return it != m_Nodes.end() ? &(*it) : nullptr;
}

const Node* Graph::FindNode(NodeID id) const {
    auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(), [id](const Node& n) { return n.id == id; });
    return it != m_Nodes.end() ? &(*it) : nullptr;
}

Link* Graph::FindLink(LinkID id) {
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [id](const Link& l) { return l.id == id; });
    return it != m_Links.end() ? &(*it) : nullptr;
}

const Link* Graph::FindLink(LinkID id) const {
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [id](const Link& l) { return l.id == id; });
    return it != m_Links.end() ? &(*it) : nullptr;
}

Link* Graph::CreateLink(PinID from, PinID to) {
    Pin* startPin = FindPin(from);
    Pin* endPin = FindPin(to);
    if (!startPin || !endPin) {
        return nullptr;
    }
    if (startPin->kind != PinKind::Output || endPin->kind != PinKind::Input) {
        return nullptr;
    }
    if (!ArePinTypesCompatible(startPin->valueType, endPin->valueType)) {
        return nullptr;
    }

    for (const Link& link : m_Links) {
        if (link.startPin == from && link.endPin == to) {
            return nullptr;
        }
    }

    m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(), [to](const Link& link) { return link.endPin == to; }), m_Links.end());

    Link link;
    link.id = m_NextLinkID++;
    link.startPin = from;
    link.endPin = to;
    m_Links.push_back(link);
    return &m_Links.back();
}

bool Graph::CanConnect(PinID from, PinID to) const {
    const Pin* startPin = FindPin(from);
    const Pin* endPin = FindPin(to);
    if (!startPin || !endPin) {
        return false;
    }
    if (startPin->kind != PinKind::Output || endPin->kind != PinKind::Input) {
        return false;
    }
    return ArePinTypesCompatible(startPin->valueType, endPin->valueType);
}

bool Graph::RemoveLink(LinkID id) {
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [id](const Link& l) { return l.id == id; });
    if (it == m_Links.end()) {
        return false;
    }
    m_Links.erase(it);
    return true;
}

PinID Graph::FindInputSource(PinID inputPin) const {
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [inputPin](const Link& link) {
        return link.endPin == inputPin;
    });
    return it != m_Links.end() ? it->startPin : 0;
}

void Graph::Clear() {
    m_Nodes.clear();
    m_Pins.clear();
    m_Links.clear();
    m_NextNodeID = 1;
    m_NextPinID = 1;
    m_NextLinkID = 1;
    m_LegacyMode = false;
    m_GraphEditingEnabled = true;
    m_HasSourceGraph = false;
    m_ApplyGraphRepairsOnSave = false;
    m_CleanOrphansOnSave = false;
    m_LoadWarnings.clear();
    m_GraphUnknownFields = nlohmann::json::object();
    ClearOriginalRootJson();
}

std::string Graph::MakeNodeDisplayName(NodeType type) const {
    return std::string(GetNodeDefinition(type).displayName);
}

void Graph::AttachDefaultPins(Node& node) {
    node.inputs.clear();
    node.outputs.clear();

    const NodeDefinition& definition = GetNodeDefinition(node.type);
    for (const PinDefinition& pin : definition.inputs) {
        AddPin(node, PinKind::Input, pin.valueType, std::string(pin.name));
    }
    for (const PinDefinition& pin : definition.outputs) {
        AddPin(node, PinKind::Output, pin.valueType, std::string(pin.name));
    }
}

Pin& Graph::AddPin(Node& owner, PinKind kind, ValueType valueType, std::string name) {
    Pin pin;
    pin.id = m_NextPinID++;
    pin.node = owner.id;
    pin.name = std::move(name);
    pin.kind = kind;
    pin.valueType = valueType;
    m_Pins.push_back(std::move(pin));
    Pin& stored = m_Pins.back();
    if (kind == PinKind::Input) {
        owner.inputs.push_back(stored.id);
    } else {
        owner.outputs.push_back(stored.id);
    }
    return stored;
}

void Graph::RemoveLinksForPin(PinID id) {
    m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(), [id](const Link& link) {
        return link.startPin == id || link.endPin == id;
    }), m_Links.end());
}

Node& Graph::CreateNodeWithID(NodeID id, NodeType type, std::string displayName) {
    Node node;
    node.id = id;
    node.type = type;
    node.serializedType = (type == NodeType::Unknown) ? "Unknown" : "";
    node.displayName = displayName.empty() ? MakeNodeDisplayName(type) : std::move(displayName);
    ApplyNodeDefaults(node);
    m_Nodes.push_back(std::move(node));
    Node& stored = m_Nodes.back();
    AttachDefaultPins(stored);
    return stored;
}

void Graph::SetPinIDsForNode(NodeID nodeId, const std::vector<PinID>& inputPinIds, const std::vector<PinID>& outputPinIds) {
    Node* node = FindNode(nodeId);
    if (!node) return;

    if (node->type == NodeType::Unknown) {
        for (PinID pinId : node->inputs) {
            m_Pins.erase(std::remove_if(m_Pins.begin(), m_Pins.end(), [pinId](const Pin& pin) {
                return pin.id == pinId;
            }), m_Pins.end());
        }
        for (PinID pinId : node->outputs) {
            m_Pins.erase(std::remove_if(m_Pins.begin(), m_Pins.end(), [pinId](const Pin& pin) {
                return pin.id == pinId;
            }), m_Pins.end());
        }
        node->inputs.clear();
        node->outputs.clear();

        for (size_t i = 0; i < inputPinIds.size(); ++i) {
            Pin pin;
            pin.id = inputPinIds[i];
            pin.node = nodeId;
            pin.kind = PinKind::Input;
            pin.valueType = ValueType::Float1;
            pin.name = "In " + std::to_string(static_cast<int>(i) + 1);
            m_Pins.push_back(std::move(pin));
            node->inputs.push_back(inputPinIds[i]);
        }
        for (size_t i = 0; i < outputPinIds.size(); ++i) {
            Pin pin;
            pin.id = outputPinIds[i];
            pin.node = nodeId;
            pin.kind = PinKind::Output;
            pin.valueType = ValueType::Float1;
            pin.name = "Out " + std::to_string(static_cast<int>(i) + 1);
            m_Pins.push_back(std::move(pin));
            node->outputs.push_back(outputPinIds[i]);
        }
        return;
    }
    
    // Update input pin IDs
    for (size_t i = 0; i < inputPinIds.size() && i < node->inputs.size(); ++i) {
        PinID oldId = node->inputs[i];
        PinID newId = inputPinIds[i];
        if (Pin* pin = FindPin(oldId)) {
            pin->id = newId;
        }
        node->inputs[i] = newId;
    }
    for (size_t i = inputPinIds.size(); i < node->inputs.size(); ++i) {
        PinID oldId = node->inputs[i];
        PinID newId = m_NextPinID++;
        if (Pin* pin = FindPin(oldId)) {
            pin->id = newId;
        }
        node->inputs[i] = newId;
    }
    
    // Update output pin IDs
    for (size_t i = 0; i < outputPinIds.size() && i < node->outputs.size(); ++i) {
        PinID oldId = node->outputs[i];
        PinID newId = outputPinIds[i];
        if (Pin* pin = FindPin(oldId)) {
            pin->id = newId;
        }
        node->outputs[i] = newId;
    }
    for (size_t i = outputPinIds.size(); i < node->outputs.size(); ++i) {
        PinID oldId = node->outputs[i];
        PinID newId = m_NextPinID++;
        if (Pin* pin = FindPin(oldId)) {
            pin->id = newId;
        }
        node->outputs[i] = newId;
    }
}

Link* Graph::CreateLinkWithID(LinkID id, PinID from, PinID to) {
    Pin* startPin = FindPin(from);
    Pin* endPin = FindPin(to);
    const PinID originalFrom = from;
    const PinID originalTo = to;

    bool orphan = (startPin == nullptr || endPin == nullptr);
    bool repaired = false;

    if (!orphan && startPin->kind == PinKind::Input && endPin->kind == PinKind::Output) {
        std::swap(from, to);
        std::swap(startPin, endPin);
        repaired = true;
    }

    if (!orphan) {
        if (startPin->kind != PinKind::Output || endPin->kind != PinKind::Input) {
            orphan = true;
        } else if (!ArePinTypesCompatible(startPin->valueType, endPin->valueType)) {
            orphan = true;
        }
    }

    if (!orphan) {
        // Remove any existing link to this input
        m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(), [to](const Link& link) { return link.endPin == to; }), m_Links.end());
    }

    Link link;
    link.id = id;
    link.startPin = from;
    link.endPin = to;
    link.orphan = orphan;
    link.directionRepairedInMemory = repaired;
    link.originalStartPin = originalFrom;
    link.originalEndPin = originalTo;
    m_Links.push_back(link);
    return &m_Links.back();
}

void Graph::UpdateNextIDs() {
    m_NextNodeID = 1;
    m_NextPinID = 1;
    m_NextLinkID = 1;
    
    for (const Node& node : m_Nodes) {
        if (node.id >= m_NextNodeID) {
            m_NextNodeID = node.id + 1;
        }
    }
    
    for (const Pin& pin : m_Pins) {
        if (pin.id >= m_NextPinID) {
            m_NextPinID = pin.id + 1;
        }
    }
    
    for (const Link& link : m_Links) {
        if (link.id >= m_NextLinkID) {
            m_NextLinkID = link.id + 1;
        }
    }
}

NodeID Graph::AllocateNodeID() {
    return m_NextNodeID++;
}

PinID Graph::AllocatePinID() {
    return m_NextPinID++;
}

LinkID Graph::AllocateLinkID() {
    return m_NextLinkID++;
}

} // namespace MaterialEditor
