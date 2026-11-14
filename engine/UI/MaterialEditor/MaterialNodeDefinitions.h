#pragma once

#include "MaterialGraph.h"

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
const std::vector<NodeDefinition>& GetAllNodeDefinitions();

// False for nodes EvaluateGraph ignores: they do not change the rendered material yet.
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
