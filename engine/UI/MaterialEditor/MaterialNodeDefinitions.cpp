#include "MaterialNodeDefinitions.h"

#include <array>

namespace MaterialEditor {
namespace {

using Pins = std::vector<PinDefinition>;

const Pins& NoPins() {
    static const Pins pins;
    return pins;
}

const std::vector<NodeDefinition>& Definitions() {
    static const std::vector<NodeDefinition> definitions = {
        {NodeType::MaterialOutput, "MaterialOutput", "Material Output", "PBR", "Final PBR material attributes.", {
            {"Base Color", ValueType::Float3}, {"Metallic", ValueType::Float1}, {"Roughness", ValueType::Float1},
            {"Emissive", ValueType::Float3}, {"Normal Map", ValueType::Float3}, {"Roughness Map", ValueType::Float1},
            {"AO Map", ValueType::Float1}, {"Specular", ValueType::Float1}, {"Opacity", ValueType::Float1},
            {"Opacity Mask", ValueType::Float1}, {"Refraction", ValueType::Float1}}, NoPins()},

        {NodeType::ConstantFloat, "ConstantFloat", "Scalar", "Inputs", "Constant float value.", NoPins(), {{"Value", ValueType::Float1}}},
        {NodeType::ConstantVec3, "ConstantVec3", "Vector3", "Inputs", "Constant RGB/vector value.", NoPins(), {{"Value", ValueType::Float3}}},
        {NodeType::ColorParameter, "ColorParameter", "Color Parameter", "Parameters", "Named vector parameter.", NoPins(), {{"Value", ValueType::Float3}}, false, true},
        {NodeType::FloatParameter, "FloatParameter", "Float Parameter", "Parameters", "Named scalar parameter.", NoPins(), {{"Value", ValueType::Float1}}, false, true},
        {NodeType::TextureParameter, "TextureParameter", "Texture Parameter", "Parameters", "Named texture parameter for future material instances.", NoPins(), {{"Texture", ValueType::Sampler2D}}, true, true},
        {NodeType::BoolParameter, "BoolParameter", "Bool Parameter", "Parameters", "Named boolean parameter represented as 0 or 1.", NoPins(), {{"Value", ValueType::Float1}}, false, true},
        {NodeType::Vector2Parameter, "Vector2Parameter", "Vector2 Parameter", "Parameters", "Named vec2 parameter.", NoPins(), {{"Value", ValueType::Float2}}, false, true},
        {NodeType::StaticSwitchParameter, "StaticSwitchParameter", "Static Switch Parameter", "Parameters", "Chooses one input using a named boolean parameter.", {{"False", ValueType::Float1}, {"True", ValueType::Float1}}, {{"Result", ValueType::Float1}}, false, true},

        {NodeType::AlbedoTextureSample, "AlbedoTextureSample", "Albedo Texture", "Textures", "Legacy albedo texture sample.", NoPins(), {{"Color", ValueType::Float3}}, true},
        {NodeType::NormalTextureSample, "NormalTextureSample", "Normal Texture", "Textures", "Legacy normal texture sample.", NoPins(), {{"Color", ValueType::Float3}}, true},
        {NodeType::RoughnessTextureSample, "RoughnessTextureSample", "Roughness Texture", "Textures", "Legacy roughness texture sample.", NoPins(), {{"Value", ValueType::Float1}}, true},
        {NodeType::MetallicTextureSample, "MetallicTextureSample", "Metallic Texture", "Textures", "Metallic texture sample.", NoPins(), {{"Value", ValueType::Float1}}, true},
        {NodeType::AoTextureSample, "AoTextureSample", "AO Texture", "Textures", "Legacy ambient occlusion texture sample.", NoPins(), {{"Value", ValueType::Float1}}, true},
        {NodeType::WorldAlignedTexture, "WorldAlignedTexture", "World Aligned Texture", "Textures", "Legacy world-aligned texture sample.", NoPins(), {{"Color", ValueType::Float3}, {"Value", ValueType::Float1}}, true},
        {NodeType::TextureSample, "TextureSample", "Texture Sample", "Textures", "Samples the node's assigned texture using optional UV input.", {{"UV", ValueType::Float2}}, {{"RGBA", ValueType::Float4}, {"RGB", ValueType::Float3}, {"R", ValueType::Float1}}, true},
        {NodeType::TextureObject, "TextureObject", "Texture Object", "Textures", "Texture reference without sampling.", NoPins(), {{"Texture", ValueType::Sampler2D}}, true},
        {NodeType::TextureSampleFromObject, "TextureSampleFromObject", "Texture Sample From Object", "Textures", "Samples a texture object using optional UV input.", {{"Texture", ValueType::Sampler2D}, {"UV", ValueType::Float2}}, {{"RGBA", ValueType::Float4}, {"RGB", ValueType::Float3}, {"R", ValueType::Float1}}, true},
        {NodeType::TextureTilingOffset, "TextureTilingOffset", "Texture Tiling Offset", "Textures", "Applies UV scale and offset.", {{"UV", ValueType::Float2}, {"Tiling", ValueType::Float2}, {"Offset", ValueType::Float2}}, {{"UV", ValueType::Float2}}},
        {NodeType::TriplanarTexture, "TriplanarTexture", "Triplanar Texture", "Textures", "Samples texture projected along world axes.", {{"World Position", ValueType::Float3}, {"World Normal", ValueType::Float3}}, {{"Color", ValueType::Float3}, {"Value", ValueType::Float1}}, true},
        {NodeType::NormalUnpack, "NormalUnpack", "Normal Unpack", "Textures", "Converts packed normal-map color to a signed normal.", {{"Color", ValueType::Float3}}, {{"Normal", ValueType::Float3}}},

        {NodeType::Add, "Add", "Add", "Math", "Adds A and B.", {{"A", ValueType::Float1}, {"B", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::Subtract, "Subtract", "Subtract", "Math", "Subtracts B from A.", {{"A", ValueType::Float1}, {"B", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::Multiply, "Multiply", "Multiply", "Math", "Multiplies A and B.", {{"A", ValueType::Float1}, {"B", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::Divide, "Divide", "Divide", "Math", "Divides A by B.", {{"A", ValueType::Float1}, {"B", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::Clamp, "Clamp", "Clamp", "Math", "Clamps Value between Min and Max.", {{"Value", ValueType::Float1}, {"Min", ValueType::Float1}, {"Max", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::Saturate, "Saturate", "Saturate", "Math", "Clamps Value to 0..1.", {{"Value", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::Power, "Power", "Power", "Math", "Raises Base to Exp.", {{"Base", ValueType::Float1}, {"Exp", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::Abs, "Abs", "Abs", "Math", "Absolute value.", {{"Value", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::Min, "Min", "Min", "Math", "Minimum of A and B.", {{"A", ValueType::Float1}, {"B", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::Max, "Max", "Max", "Math", "Maximum of A and B.", {{"A", ValueType::Float1}, {"B", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::OneMinus, "OneMinus", "One Minus", "Math", "Computes 1 - Value.", {{"Value", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::DotProduct, "DotProduct", "Dot Product", "Math", "Dot product of two vectors.", {{"A", ValueType::Float3}, {"B", ValueType::Float3}}, {{"Result", ValueType::Float1}}},
        {NodeType::CrossProduct, "CrossProduct", "Cross Product", "Math", "Cross product of two vec3 values.", {{"A", ValueType::Float3}, {"B", ValueType::Float3}}, {{"Result", ValueType::Float3}}},
        {NodeType::Normalize, "Normalize", "Normalize", "Math", "Normalizes a vector.", {{"Value", ValueType::Float3}}, {{"Result", ValueType::Float3}}},
        {NodeType::Length, "Length", "Length", "Math", "Vector length.", {{"Value", ValueType::Float3}}, {{"Result", ValueType::Float1}}},
        {NodeType::Lerp, "Lerp", "Lerp", "Blend", "Linear interpolation between A and B.", {{"A", ValueType::Float1}, {"B", ValueType::Float1}, {"Alpha", ValueType::Float1}}, {{"Result", ValueType::Float1}}},

        {NodeType::ComponentMask, "ComponentMask", "Component Mask", "Utility", "Masks vector components using R/G/B/A toggles.", {{"Value", ValueType::Float4}}, {{"Result", ValueType::Float4}}},
        {NodeType::AppendVector, "AppendVector", "Append Vector", "Utility", "Combines scalar/vector values.", {{"A", ValueType::Float1}, {"B", ValueType::Float1}}, {{"Result", ValueType::Float2}}},
        {NodeType::BreakVector, "BreakVector", "Break Vector", "Utility", "Splits a vector into scalar components.", {{"Value", ValueType::Float4}}, {{"R", ValueType::Float1}, {"G", ValueType::Float1}, {"B", ValueType::Float1}, {"A", ValueType::Float1}}},
        {NodeType::Transform, "Transform", "Transform", "Utility", "Transforms a vector between spaces.", {{"Value", ValueType::Float3}}, {{"Result", ValueType::Float3}}},
        {NodeType::TexCoord, "TexCoord", "TexCoord", "Utility", "Mesh UV coordinates.", NoPins(), {{"UV", ValueType::Float2}}},
        {NodeType::ScreenPosition, "ScreenPosition", "Screen Position", "Utility", "Current fragment screen position.", NoPins(), {{"Position", ValueType::Float4}}},

        {NodeType::Noise, "Noise", "Noise", "Procedural", "Procedural value noise.", {{"UV", ValueType::Float2}, {"Scale", ValueType::Float1}}, {{"Value", ValueType::Float1}}},
        {NodeType::Gradient, "Gradient", "Gradient", "Procedural", "Linear or radial gradient.", {{"UV", ValueType::Float2}}, {{"Value", ValueType::Float1}}},
        {NodeType::Fresnel, "Fresnel", "Fresnel", "Procedural", "View-dependent edge falloff.", {{"Normal", ValueType::Float3}, {"Power", ValueType::Float1}}, {{"Value", ValueType::Float1}}},
        {NodeType::UVDistortion, "UVDistortion", "UV Distortion", "Procedural", "Offsets UVs by a distortion vector and strength.", {{"UV", ValueType::Float2}, {"Offset", ValueType::Float2}, {"Strength", ValueType::Float1}}, {{"UV", ValueType::Float2}}},
        {NodeType::Time, "Time", "Time", "Procedural", "Engine time input.", NoPins(), {{"Seconds", ValueType::Float1}}},

        {NodeType::CameraVector, "CameraVector", "Camera Vector", "Inputs", "Vector from surface to camera.", NoPins(), {{"Vector", ValueType::Float3}}},
        {NodeType::ViewDirection, "ViewDirection", "View Direction", "Inputs", "Normalized view direction.", NoPins(), {{"Direction", ValueType::Float3}}},
        {NodeType::ReflectionVector, "ReflectionVector", "Reflection Vector", "Inputs", "Reflection vector from view and normal.", {{"Normal", ValueType::Float3}}, {{"Vector", ValueType::Float3}}},
        {NodeType::WorldPosition, "WorldPosition", "World Position", "Inputs", "Fragment world position.", NoPins(), {{"Position", ValueType::Float3}}},
        {NodeType::ObjectPosition, "ObjectPosition", "Object Position", "Inputs", "Object origin position.", NoPins(), {{"Position", ValueType::Float3}}},
        {NodeType::DistanceToCamera, "DistanceToCamera", "Distance To Camera", "Inputs", "Distance from fragment to camera.", NoPins(), {{"Distance", ValueType::Float1}}},

        {NodeType::If, "If", "If", "Logic", "Selects A or B depending on comparison.", {{"A", ValueType::Float1}, {"B", ValueType::Float1}, {"AGreater", ValueType::Float1}, {"ALessEqual", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::Step, "Step", "Step", "Logic", "Returns 0 below edge and 1 above.", {{"Edge", ValueType::Float1}, {"Value", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::SmoothStep, "SmoothStep", "Smooth Step", "Logic", "Smooth Hermite threshold.", {{"Min", ValueType::Float1}, {"Max", ValueType::Float1}, {"Value", ValueType::Float1}}, {{"Result", ValueType::Float1}}},
        {NodeType::Compare, "Compare", "Compare", "Logic", "Compares A and B using the node operation.", {{"A", ValueType::Float1}, {"B", ValueType::Float1}}, {{"Result", ValueType::Float1}}},

        {NodeType::Overlay, "Overlay", "Overlay", "Blend", "Overlay blend mode.", {{"Base", ValueType::Float3}, {"Blend", ValueType::Float3}}, {{"Result", ValueType::Float3}}},
        {NodeType::Screen, "Screen", "Screen", "Blend", "Screen blend mode.", {{"Base", ValueType::Float3}, {"Blend", ValueType::Float3}}, {{"Result", ValueType::Float3}}},
        {NodeType::HeightBlend, "HeightBlend", "Height Blend", "Blend", "Blends layers using height and contrast.", {{"A", ValueType::Float3}, {"B", ValueType::Float3}, {"Height", ValueType::Float1}, {"Contrast", ValueType::Float1}}, {{"Result", ValueType::Float3}}},
    };
    return definitions;
}

} // namespace

const NodeDefinition& GetNodeDefinition(NodeType type) {
    for (const NodeDefinition& definition : Definitions()) {
        if (definition.type == type) {
            return definition;
        }
    }

    static const NodeDefinition unknown{NodeType::Unknown, "Unknown", "Unknown Node", "Utility", "Unsupported or forward-compatible node.", NoPins(), NoPins()};
    return unknown;
}

const std::vector<NodeDefinition>& GetAllNodeDefinitions() {
    return Definitions();
}

std::string_view NodeTypeName(NodeType type) {
    return GetNodeDefinition(type).typeName;
}

bool TryNodeTypeName(std::string_view typeName, NodeType& outType) {
    for (const NodeDefinition& definition : Definitions()) {
        if (definition.typeName == typeName) {
            outType = definition.type;
            return true;
        }
    }
    if (typeName == "AOTextureSample") {
        outType = NodeType::AoTextureSample;
        return true;
    }
    if (typeName == "ConstantScalar") {
        outType = NodeType::ConstantFloat;
        return true;
    }
    if (typeName == "ConstantColor") {
        outType = NodeType::ConstantVec3;
        return true;
    }
    return false;
}

std::string_view ValueTypeName(ValueType type) {
    switch (type) {
    case ValueType::Float1: return "Float";
    case ValueType::Float2: return "Float2";
    case ValueType::Float3: return "Float3";
    case ValueType::Float4: return "Float4";
    case ValueType::Sampler2D: return "Texture";
    }
    return "Value";
}

int ValueTypeComponentCount(ValueType type) {
    switch (type) {
    case ValueType::Float1: return 1;
    case ValueType::Float2: return 2;
    case ValueType::Float3: return 3;
    case ValueType::Float4: return 4;
    case ValueType::Sampler2D: return 0;
    }
    return 0;
}

bool IsNumericValueType(ValueType type) {
    return type != ValueType::Sampler2D;
}

bool IsScalarToVectorCompatible(ValueType from, ValueType to) {
    return from == ValueType::Float1 && IsNumericValueType(to);
}

bool IsVectorToScalarCompatible(ValueType from, ValueType to) {
    // Allow connecting higher-dimension vectors to scalar inputs (extracts first component)
    // e.g., Float3 -> Float1 takes the R/X channel, Float4 -> Float1 takes R/X
    return to == ValueType::Float1 && IsNumericValueType(from);
}

bool IsVectorToVectorCompatible(ValueType from, ValueType to) {
    // Allow connecting higher-dimension vectors to lower-dimension vectors
    // e.g., Float4 -> Float3 takes RGB, Float4 -> Float2 takes RG
    if (!IsNumericValueType(from) || !IsNumericValueType(to)) {
        return false;
    }
    // Get dimensions (Float1=1, Float2=2, Float3=3, Float4=4)
    int fromDim = (from == ValueType::Float1) ? 1 : (from == ValueType::Float2) ? 2 : (from == ValueType::Float3) ? 3 : 4;
    int toDim = (to == ValueType::Float1) ? 1 : (to == ValueType::Float2) ? 2 : (to == ValueType::Float3) ? 3 : 4;
    return fromDim >= toDim;
}

bool ArePinTypesCompatible(ValueType from, ValueType to) {
    if (from == to) {
        return true;
    }
    // Allow scalar to vector (broadcasts scalar to all components)
    if (IsScalarToVectorCompatible(from, to)) {
        return true;
    }
    // Allow vector to scalar/smaller vector (extracts first component(s))
    if (IsVectorToVectorCompatible(from, to)) {
        return true;
    }
    return false;
}

} // namespace MaterialEditor
