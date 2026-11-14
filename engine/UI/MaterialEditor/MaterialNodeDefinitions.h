#pragma once

#include "MaterialGraph.h"

#include <string>
#include <string_view>
#include <vector>

namespace MaterialEditor {

struct PinDefinition {
    std::string_view name;
    ValueType valueType = ValueType::Float1;
};

struct NodeDefinition {
    NodeType type = NodeType::Unknown;
    std::string_view typeName;
    std::string_view displayName;
    std::string_view category;
    std::string_view tooltip;
    std::vector<PinDefinition> inputs;
    std::vector<PinDefinition> outputs;
    bool textureNode = false;
    bool parameterNode = false;
};

const NodeDefinition& GetNodeDefinition(NodeType type);
/// A node's definition, including a plugin node's; the Unknown definition when its plugin is not loaded.
const NodeDefinition& GetNodeDefinition(const Node& node);
const std::vector<NodeDefinition>& GetAllNodeDefinitions();
/// Core or plugin node by type name; null when neither has it.
const NodeDefinition* FindNodeDefinition(std::string_view typeName);
/// What the node library offers: core nodes (without Material Output) followed by plugin nodes.
std::vector<const NodeDefinition*> GetPlaceableNodeDefinitions();

/// A node a plugin adds (PluginAPI.h:IMaterialNodeService). Output `glsl` is the value's expression and input `glsl`
/// the value used when nothing is connected; both may use {0}..{9} for the inputs and {uv}, {worldPos}, {normal},
/// {viewDir}, {cameraPos}, {time}, {objectPos} and {screenPos}. `functions` is GLSL placed before the surface
/// function once per material.
struct CustomPinDefinition {
    std::string name;
    ValueType valueType = ValueType::Float1;
    std::string glsl;
};

struct CustomNodeDefinition {
    std::string owner;
    std::string typeName;
    std::string displayName;
    std::string category;
    std::string tooltip;
    std::string functions;
    std::vector<CustomPinDefinition> inputs;
    std::vector<CustomPinDefinition> outputs;
};

bool RegisterCustomNode(CustomNodeDefinition definition, std::string& outError);
void UnregisterCustomNodes(std::string_view owner);
const CustomNodeDefinition* FindCustomNode(std::string_view typeName);

/// False for Transform, which passes its input through until space conversion exists.
bool NodeAffectsRenderedMaterial(NodeType type);

/// Normal, roughness, metallic and AO samples, and texture nodes ticked Linear (Node::boolValue), hold data rather
/// than colour, so they are read without sRGB decoding.
bool TextureNodeIsLinear(const Node& node);

std::string_view NodeTypeName(NodeType type);
bool TryNodeTypeName(std::string_view typeName, NodeType& outType);

std::string_view ValueTypeName(ValueType type);
int ValueTypeComponentCount(ValueType type);
bool IsNumericValueType(ValueType type);
bool IsScalarToVectorCompatible(ValueType from, ValueType to);
bool IsVectorToScalarCompatible(ValueType from, ValueType to);
bool IsVectorToVectorCompatible(ValueType from, ValueType to);
bool ArePinTypesCompatible(ValueType from, ValueType to);

} // namespace MaterialEditor
