#include "MaterialGraph.h"

namespace MaterialEditor {
namespace {

constexpr std::string_view NodeLabel(NodeType type) {
    switch (type) {
    case NodeType::MaterialOutput: return "Material Output";
    case NodeType::ConstantFloat: return "Scalar";
    case NodeType::ConstantVec3: return "Vector3";
    case NodeType::ColorParameter: return "Color";
    case NodeType::FloatParameter: return "Float";
    case NodeType::AlbedoTextureSample: return "Albedo Texture";
    case NodeType::NormalTextureSample: return "Normal Texture";
    case NodeType::RoughnessTextureSample: return "Roughness Texture";
    case NodeType::AoTextureSample: return "AO Texture";
    case NodeType::WorldAlignedTexture: return "World Aligned Texture";
    case NodeType::Add: return "Add";
    case NodeType::Multiply: return "Multiply";
    case NodeType::Unknown: return "Unknown Node";
    }
    return "Node";
}

constexpr std::string_view PinLabel(NodeType ownerType, PinKind kind, int index) {
    if (ownerType == NodeType::MaterialOutput && kind == PinKind::Input) {
        switch (index) {
        case 0: return "Base Color";
        case 1: return "Metallic";
        case 2: return "Roughness";
        case 3: return "Emissive";
        case 4: return "Normal Map";
        case 5: return "Roughness Map";
        case 6: return "AO Map";
        default: break;
        }
    }
    if ((ownerType == NodeType::ConstantFloat || ownerType == NodeType::FloatParameter) && kind == PinKind::Output) {
        return "Value";
    }
    if ((ownerType == NodeType::ConstantVec3 || ownerType == NodeType::ColorParameter) && kind == PinKind::Output) {
        return "Value";
    }
    if ((ownerType == NodeType::AlbedoTextureSample || ownerType == NodeType::NormalTextureSample || 
         ownerType == NodeType::RoughnessTextureSample || ownerType == NodeType::AoTextureSample) && kind == PinKind::Output) {
        return "Color";
    }
    if (ownerType == NodeType::WorldAlignedTexture && kind == PinKind::Output) {
        return index == 0 ? "Color" : "Value";
    }
    if ((ownerType == NodeType::Add || ownerType == NodeType::Multiply) && kind == PinKind::Input) {
        return index == 0 ? "A" : "B";
    }
    if ((ownerType == NodeType::Add || ownerType == NodeType::Multiply) && kind == PinKind::Output) {
        return "Result";
    }
    return kind == PinKind::Input ? "In" : "Out";
}

constexpr ValueType DefaultPinType(NodeType ownerType, PinKind kind, int index) {
    if (ownerType == NodeType::MaterialOutput) {
        if (index == 0 || index == 3 || index == 4) {
            return ValueType::Float3;
        }
        return ValueType::Float1;
    }
    if (ownerType == NodeType::ConstantFloat || ownerType == NodeType::FloatParameter) {
        return ValueType::Float1;
    }
    if (ownerType == NodeType::ConstantVec3 || ownerType == NodeType::ColorParameter) {
        return ValueType::Float3;
    }
    if (ownerType == NodeType::AlbedoTextureSample || ownerType == NodeType::NormalTextureSample || 
        ownerType == NodeType::RoughnessTextureSample || ownerType == NodeType::AoTextureSample) {
        if (kind == PinKind::Output) {
            return (ownerType == NodeType::RoughnessTextureSample || ownerType == NodeType::AoTextureSample)
                   ? ValueType::Float1 : ValueType::Float3;
        }
        return ValueType::Float1;
    }
    if (ownerType == NodeType::WorldAlignedTexture) {
        if (kind == PinKind::Output) {
            // First output is Color (Float3), second is Value (Float1)
            return index == 0 ? ValueType::Float3 : ValueType::Float1;
        }
        return ValueType::Float1;
    }
    if (ownerType == NodeType::Add || ownerType == NodeType::Multiply) {
        return ValueType::Float1;
    }
    return ValueType::Float1;
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
    switch (type) {
    case NodeType::ConstantFloat:
    case NodeType::FloatParameter:
        node.scalar = 0.5f;
        break;
    case NodeType::ConstantVec3:
    case NodeType::ColorParameter:
        node.color = Float3{1.0f, 1.0f, 1.0f};
        break;
    case NodeType::AlbedoTextureSample:
    case NodeType::NormalTextureSample:
    case NodeType::RoughnessTextureSample:
    case NodeType::AoTextureSample:
        node.resourcePath.clear();
        break;
    case NodeType::WorldAlignedTexture:
        node.resourcePath.clear();
        node.worldAlignedScale = 1.0f;
        break;
    case NodeType::Unknown:
        break;
    default:
        break;
    }
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
    if (startPin->valueType != endPin->valueType) {
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
    return std::string(NodeLabel(type));
}

void Graph::AttachDefaultPins(Node& node) {
    node.inputs.clear();
    node.outputs.clear();

    auto addInputs = [&](int count) {
        for (int i = 0; i < count; ++i) {
            AddPin(node, PinKind::Input, DefaultPinType(node.type, PinKind::Input, i), std::string(PinLabel(node.type, PinKind::Input, i)));
        }
    };
    auto addOutputs = [&](int count) {
        for (int i = 0; i < count; ++i) {
            AddPin(node, PinKind::Output, DefaultPinType(node.type, PinKind::Output, i), std::string(PinLabel(node.type, PinKind::Output, i)));
        }
    };

    switch (node.type) {
    case NodeType::MaterialOutput:
        addInputs(7);  // Base Color, Metallic, Roughness, Emissive, Normal, RoughnessMap, AO
        break;
    case NodeType::ConstantFloat:
    case NodeType::FloatParameter:
    case NodeType::ConstantVec3:
    case NodeType::ColorParameter:
    case NodeType::AlbedoTextureSample:
    case NodeType::NormalTextureSample:
    case NodeType::RoughnessTextureSample:
    case NodeType::AoTextureSample:
        addOutputs(1);
        break;
    case NodeType::WorldAlignedTexture:
        addOutputs(2);  // Color (Float3) and Value (Float1)
        break;
    case NodeType::Add:
    case NodeType::Multiply:
        addInputs(2);
        addOutputs(1);
        break;
    case NodeType::Unknown:
        break;
    default:
        break;
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
    switch (type) {
    case NodeType::ConstantFloat:
    case NodeType::FloatParameter:
        node.scalar = 0.5f;
        break;
    case NodeType::ConstantVec3:
    case NodeType::ColorParameter:
        node.color = Float3{1.0f, 1.0f, 1.0f};
        break;
    case NodeType::AlbedoTextureSample:
    case NodeType::NormalTextureSample:
    case NodeType::RoughnessTextureSample:
    case NodeType::AoTextureSample:
        node.resourcePath.clear();
        break;
    case NodeType::WorldAlignedTexture:
        node.resourcePath.clear();
        node.worldAlignedScale = 1.0f;
        break;
    case NodeType::Unknown:
        break;
    default:
        break;
    }
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
    
    // Update output pin IDs
    for (size_t i = 0; i < outputPinIds.size() && i < node->outputs.size(); ++i) {
        PinID oldId = node->outputs[i];
        PinID newId = outputPinIds[i];
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
        } else if (startPin->valueType != endPin->valueType) {
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
