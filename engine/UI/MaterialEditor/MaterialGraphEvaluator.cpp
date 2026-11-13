#include "MaterialGraphEvaluator.h"
#include "MaterialNodeDefinitions.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace MaterialEditor {
namespace {

struct Value {
    ValueType type = ValueType::Float1;
    float scalar = 0.0f;
    Float3 vec{0.0f, 0.0f, 0.0f};
    bool hasTexture = false;
    std::string texturePath;
    bool isWorldAligned = false;
    float worldAlignedScale = 1.0f;
    uint32_t channel = 0; ///< channel of a texture a scalar map reads, set by Break Vector and Component Mask
};

Value MakeDefault(ValueType type) {
    Value value;
    value.type = type;
    switch (type) {
    case ValueType::Float1:
        value.scalar = 0.0f;
        break;
    case ValueType::Float2:
    case ValueType::Float3:
    case ValueType::Float4:
        value.vec = Float3{0.0f, 0.0f, 0.0f};
        break;
    case ValueType::Sampler2D:
        value.hasTexture = false;
        break;
    }
    return value;
}

Float3 Add(const Float3& a, const Float3& b) {
    return Float3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Float3 Multiply(const Float3& a, const Float3& b) {
    return Float3{a.x * b.x, a.y * b.y, a.z * b.z};
}

Float3 Lerp(const Float3& a, const Float3& b, float alpha) {
    return Float3{
        a.x + (b.x - a.x) * alpha,
        a.y + (b.y - a.y) * alpha,
        a.z + (b.z - a.z) * alpha
    };
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float Length(const Float3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

Float3 Cross(const Float3& a, const Float3& b) {
    return Float3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

Float3 Normalize(const Float3& value) {
    const float length = Length(value);
    if (length <= 0.00001f) {
        return Float3{0.0f, 0.0f, 1.0f};
    }
    return Float3{value.x / length, value.y / length, value.z / length};
}

float Dot(const Float3& a, const Float3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Float3 ScalarToVec(float value) {
    return Float3{value, value, value};
}

Value Coerce(Value value, ValueType targetType) {
    if (value.type == targetType) {
        return value;
    }
    if (value.type == ValueType::Float1 && IsNumericValueType(targetType)) {
        value.vec = ScalarToVec(value.scalar);
        value.type = targetType;
    }
    return value;
}

bool EvaluateOutputPin(const Graph& graph,
                       PinID outputPin,
                       std::unordered_map<PinID, Value>& cache,
                       std::unordered_set<PinID>& active,
                       std::string& errorMessage,
                       Value& outValue);

Value EvaluateInputPin(const Graph& graph,
                       PinID inputPin,
                       std::unordered_map<PinID, Value>& cache,
                       std::unordered_set<PinID>& active,
                       std::string& errorMessage,
                       bool* outConnected = nullptr) {
    const Pin* input = graph.FindPin(inputPin);
    if (!input) {
        return Value{};
    }

    PinID source = graph.FindInputSource(inputPin);
    if (outConnected) {
        *outConnected = source != 0;
    }
    if (source == 0) {
        return MakeDefault(input->valueType);
    }

    Value value;
    if (!EvaluateOutputPin(graph, source, cache, active, errorMessage, value)) {
        return MakeDefault(input->valueType);
    }
    return value;
}

bool EvaluateOutputPin(const Graph& graph,
                       PinID outputPin,
                       std::unordered_map<PinID, Value>& cache,
                       std::unordered_set<PinID>& active,
                       std::string& errorMessage,
                       Value& outValue) {
    auto cached = cache.find(outputPin);
    if (cached != cache.end()) {
        outValue = cached->second;
        return true;
    }

    if (!active.insert(outputPin).second) {
        errorMessage = "Detected a cyclic connection in the material graph.";
        return false;
    }

    const Pin* pin = graph.FindPin(outputPin);
    if (!pin) {
        outValue = Value{};
        active.erase(outputPin);
        return true;
    }

    const Node* node = graph.FindNode(pin->node);
    if (!node) {
        outValue = Value{};
        active.erase(outputPin);
        return true;
    }

    Value result;
    result.type = pin->valueType;

    auto fetchInput = [&](int idx) {
        if (idx < 0 || idx >= static_cast<int>(node->inputs.size())) {
            return Value{};
        }
        return EvaluateInputPin(graph, node->inputs[idx], cache, active, errorMessage, nullptr);
    };

    switch (node->type) {
    case NodeType::ConstantFloat:
    case NodeType::FloatParameter:
        result.scalar = node->scalar;
        break;
    case NodeType::BoolParameter:
        result.scalar = node->boolValue ? 1.0f : 0.0f;
        break;
    case NodeType::Vector2Parameter:
        result.vec = Float3{node->color.x, node->color.y, 0.0f};
        break;
    case NodeType::ConstantVec3:
    case NodeType::ColorParameter:
        result.vec = node->color;
        break;
    case NodeType::TextureParameter:
    case NodeType::TextureObject:
        result.hasTexture = !node->resourcePath.empty();
        result.texturePath = node->resourcePath;
        break;
    case NodeType::AlbedoTextureSample:
    case NodeType::NormalTextureSample:
    case NodeType::RoughnessTextureSample:
    case NodeType::MetallicTextureSample:
    case NodeType::AoTextureSample:
    case NodeType::TextureSample:
    case NodeType::TextureSampleFromObject:
    case NodeType::TriplanarTexture: {
        // Texture sample nodes combine parameter + sampling
        result.hasTexture = !node->resourcePath.empty();
        result.texturePath = node->resourcePath;
        
        // Output type depends on the texture type
        if (node->type == NodeType::RoughnessTextureSample || node->type == NodeType::MetallicTextureSample || node->type == NodeType::AoTextureSample ||
            pin->valueType == ValueType::Float1) {
            result.type = ValueType::Float1;
            result.scalar = 1.0f;
        } else if (pin->valueType == ValueType::Float4) {
            result.type = ValueType::Float4;
            result.vec = Float3{1.0f, 1.0f, 1.0f};
        } else {
            result.type = ValueType::Float3;
            result.vec = Float3{1.0f, 1.0f, 1.0f};
        }
        break;
    }
    case NodeType::WorldAlignedTexture: {
        // World-aligned texture node - uses world position for UVs
        // Has two outputs: Color (Float3) at index 0, Value (Float1) at index 1
        result.hasTexture = !node->resourcePath.empty();
        result.texturePath = node->resourcePath;
        result.isWorldAligned = true;
        result.worldAlignedScale = node->worldAlignedScale;
        
        // Determine which output pin this is
        int outputIndex = 0;
        for (size_t i = 0; i < node->outputs.size(); ++i) {
            if (node->outputs[i] == outputPin) {
                outputIndex = static_cast<int>(i);
                break;
            }
        }
        
        if (outputIndex == 0) {
            // Color output (Float3)
            result.type = ValueType::Float3;
            result.vec = Float3{1.0f, 1.0f, 1.0f};
        } else {
            // Value output (Float1) - typically grayscale/single channel
            result.type = ValueType::Float1;
            result.scalar = 1.0f;
        }
        break;
    }
    case NodeType::Add: {
        Value a = Coerce(fetchInput(0), pin->valueType);
        Value b = Coerce(fetchInput(1), pin->valueType);
        if (pin->valueType == ValueType::Float1) {
            result.scalar = a.scalar + b.scalar;
        } else {
            result.vec = Add(a.vec, b.vec);
        }
        break;
    }
    case NodeType::Subtract: {
        Value a = Coerce(fetchInput(0), pin->valueType);
        Value b = Coerce(fetchInput(1), pin->valueType);
        if (pin->valueType == ValueType::Float1) {
            result.scalar = a.scalar - b.scalar;
        } else {
            result.vec = Float3{a.vec.x - b.vec.x, a.vec.y - b.vec.y, a.vec.z - b.vec.z};
        }
        break;
    }
    case NodeType::Multiply: {
        Value a = Coerce(fetchInput(0), pin->valueType);
        Value b = Coerce(fetchInput(1), pin->valueType);
        if (pin->valueType == ValueType::Float1) {
            result.scalar = a.scalar * b.scalar;
        } else {
            result.vec = Multiply(a.vec, b.vec);
        }
        break;
    }
    case NodeType::Divide: {
        Value a = Coerce(fetchInput(0), pin->valueType);
        Value b = Coerce(fetchInput(1), pin->valueType);
        if (pin->valueType == ValueType::Float1) {
            result.scalar = std::abs(b.scalar) > 0.00001f ? a.scalar / b.scalar : 0.0f;
        } else {
            result.vec = Float3{
                std::abs(b.vec.x) > 0.00001f ? a.vec.x / b.vec.x : 0.0f,
                std::abs(b.vec.y) > 0.00001f ? a.vec.y / b.vec.y : 0.0f,
                std::abs(b.vec.z) > 0.00001f ? a.vec.z / b.vec.z : 0.0f
            };
        }
        break;
    }
    case NodeType::Clamp: {
        Value value = fetchInput(0);
        Value minValue = fetchInput(1);
        Value maxValue = fetchInput(2);
        const float minScalar = minValue.scalar;
        const float maxScalar = maxValue.scalar == 0.0f ? node->scalar : maxValue.scalar;
        result.scalar = std::clamp(value.scalar, minScalar, maxScalar);
        break;
    }
    case NodeType::Saturate: {
        result.scalar = Clamp01(fetchInput(0).scalar);
        break;
    }
    case NodeType::Power: {
        Value base = fetchInput(0);
        Value exponent = fetchInput(1);
        result.scalar = std::pow(std::max(base.scalar, 0.0f), exponent.scalar == 0.0f ? 1.0f : exponent.scalar);
        break;
    }
    case NodeType::Abs:
        result.scalar = std::abs(fetchInput(0).scalar);
        break;
    case NodeType::Min:
        result.scalar = std::min(fetchInput(0).scalar, fetchInput(1).scalar);
        break;
    case NodeType::Max:
        result.scalar = std::max(fetchInput(0).scalar, fetchInput(1).scalar);
        break;
    case NodeType::OneMinus:
        result.scalar = 1.0f - fetchInput(0).scalar;
        break;
    case NodeType::DotProduct: {
        Value a = Coerce(fetchInput(0), ValueType::Float3);
        Value b = Coerce(fetchInput(1), ValueType::Float3);
        result.scalar = Dot(a.vec, b.vec);
        break;
    }
    case NodeType::CrossProduct: {
        Value a = Coerce(fetchInput(0), ValueType::Float3);
        Value b = Coerce(fetchInput(1), ValueType::Float3);
        result.vec = Cross(a.vec, b.vec);
        break;
    }
    case NodeType::Normalize:
        result.vec = Normalize(Coerce(fetchInput(0), ValueType::Float3).vec);
        break;
    case NodeType::Length:
        result.scalar = Length(Coerce(fetchInput(0), ValueType::Float3).vec);
        break;
    case NodeType::Lerp: {
        Value a = Coerce(fetchInput(0), pin->valueType);
        Value b = Coerce(fetchInput(1), pin->valueType);
        Value alpha = fetchInput(2);
        if (pin->valueType == ValueType::Float1) {
            result.scalar = a.scalar + (b.scalar - a.scalar) * alpha.scalar;
        } else {
            result.vec = Lerp(a.vec, b.vec, alpha.scalar);
        }
        break;
    }
    case NodeType::ComponentMask: {
        Value value = Coerce(fetchInput(0), ValueType::Float4);
        const int count = (node->maskR ? 1 : 0) + (node->maskG ? 1 : 0) + (node->maskB ? 1 : 0) + (node->maskA ? 1 : 0);
        result.type = count <= 1 ? ValueType::Float1 : (count == 2 ? ValueType::Float2 : (count == 3 ? ValueType::Float3 : ValueType::Float4));
        result.scalar = node->maskR ? value.vec.x : (node->maskG ? value.vec.y : (node->maskB ? value.vec.z : 1.0f));
        if (value.hasTexture && count == 1) {
            result.hasTexture = true;
            result.texturePath = value.texturePath;
            result.isWorldAligned = value.isWorldAligned;
            result.worldAlignedScale = value.worldAlignedScale;
            result.channel = node->maskR ? 0u : (node->maskG ? 1u : (node->maskB ? 2u : 3u));
        }
        result.vec = Float3{
            node->maskR ? value.vec.x : result.scalar,
            node->maskG ? value.vec.y : result.scalar,
            node->maskB ? value.vec.z : result.scalar
        };
        break;
    }
    case NodeType::AppendVector:
        result.vec = Float3{fetchInput(0).scalar, fetchInput(1).scalar, 0.0f};
        break;
    case NodeType::BreakVector: {
        Value value = Coerce(fetchInput(0), ValueType::Float4);
        int outputIndex = 0;
        for (size_t i = 0; i < node->outputs.size(); ++i) {
            if (node->outputs[i] == outputPin) {
                outputIndex = static_cast<int>(i);
                break;
            }
        }
        result.scalar = outputIndex == 0 ? value.vec.x : (outputIndex == 1 ? value.vec.y : (outputIndex == 2 ? value.vec.z : 1.0f));
        if (value.hasTexture) {
            result.hasTexture = true;
            result.texturePath = value.texturePath;
            result.isWorldAligned = value.isWorldAligned;
            result.worldAlignedScale = value.worldAlignedScale;
            result.channel = static_cast<uint32_t>(std::clamp(outputIndex, 0, 3));
        }
        break;
    }
    case NodeType::NormalUnpack: {
        Value packed = Coerce(fetchInput(0), ValueType::Float3);
        result.vec = Normalize(Float3{packed.vec.x * 2.0f - 1.0f, packed.vec.y * 2.0f - 1.0f, packed.vec.z * 2.0f - 1.0f});
        break;
    }
    case NodeType::Step:
        result.scalar = fetchInput(1).scalar < fetchInput(0).scalar ? 0.0f : 1.0f;
        break;
    case NodeType::SmoothStep: {
        const float edge0 = fetchInput(0).scalar;
        const float edge1 = fetchInput(1).scalar;
        const float value = fetchInput(2).scalar;
        const float t = Clamp01((value - edge0) / std::max(edge1 - edge0, 0.00001f));
        result.scalar = t * t * (3.0f - 2.0f * t);
        break;
    }
    case NodeType::Compare: {
        const float a = fetchInput(0).scalar;
        const float b = fetchInput(1).scalar;
        bool pass = a > b;
        if (node->operation == "<") pass = a < b;
        else if (node->operation == "==") pass = std::abs(a - b) <= 0.0001f;
        result.scalar = pass ? 1.0f : 0.0f;
        break;
    }
    case NodeType::If:
        result.scalar = fetchInput(0).scalar > fetchInput(1).scalar ? fetchInput(2).scalar : fetchInput(3).scalar;
        break;
    case NodeType::Overlay: {
        Value base = Coerce(fetchInput(0), ValueType::Float3);
        Value blend = Coerce(fetchInput(1), ValueType::Float3);
        auto overlay = [](float a, float b) { return a < 0.5f ? 2.0f * a * b : 1.0f - 2.0f * (1.0f - a) * (1.0f - b); };
        result.vec = Float3{overlay(base.vec.x, blend.vec.x), overlay(base.vec.y, blend.vec.y), overlay(base.vec.z, blend.vec.z)};
        break;
    }
    case NodeType::Screen: {
        Value base = Coerce(fetchInput(0), ValueType::Float3);
        Value blend = Coerce(fetchInput(1), ValueType::Float3);
        result.vec = Float3{1.0f - (1.0f - base.vec.x) * (1.0f - blend.vec.x),
                            1.0f - (1.0f - base.vec.y) * (1.0f - blend.vec.y),
                            1.0f - (1.0f - base.vec.z) * (1.0f - blend.vec.z)};
        break;
    }
    case NodeType::HeightBlend: {
        Value a = Coerce(fetchInput(0), ValueType::Float3);
        Value b = Coerce(fetchInput(1), ValueType::Float3);
        const float alpha = Clamp01((fetchInput(2).scalar - 0.5f) * std::max(fetchInput(3).scalar, 1.0f) + 0.5f);
        result.vec = Lerp(a.vec, b.vec, alpha);
        break;
    }
    case NodeType::TexCoord:
    case NodeType::ScreenPosition:
    case NodeType::Transform:
    case NodeType::TextureTilingOffset:
    case NodeType::Noise:
    case NodeType::Gradient:
    case NodeType::Fresnel:
    case NodeType::UVDistortion:
    case NodeType::Time:
    case NodeType::CameraVector:
    case NodeType::ViewDirection:
    case NodeType::ReflectionVector:
    case NodeType::WorldPosition:
    case NodeType::ObjectPosition:
    case NodeType::DistanceToCamera:
    case NodeType::StaticSwitchParameter:
        // Per-fragment/runtime-only nodes are represented by the shader generator.
        break;
    case NodeType::MaterialOutput:
    case NodeType::Unknown:
        break;
    }

    cache.emplace(outputPin, result);
    outValue = result;
    active.erase(outputPin);
    return true;
}

} // namespace

bool EvaluateGraph(const Graph& graph, MaterialComponent& outMaterial, std::string& outErrorMessage) {
    const Node* outputNode = nullptr;
    for (const Node& node : graph.GetNodes()) {
        if (node.type == NodeType::MaterialOutput) {
            outputNode = &node;
            break;
        }
    }

    if (!outputNode) {
        outErrorMessage = "Material graph is missing an output node.";
        return false;
    }

    std::unordered_map<PinID, Value> cache;
    std::unordered_set<PinID> active;

    auto resolveColor = [&](PinID inputPin, const Float3& fallback, bool affectTexture, Vec3& outColor, bool& outUseTexture, std::string& outTexturePath, bool* outWorldAligned = nullptr, float* outWorldScale = nullptr) {
        bool connected = false;
        Value value = EvaluateInputPin(graph, inputPin, cache, active, outErrorMessage, &connected);
        if (!outErrorMessage.empty()) {
            return;
        }
        if (!connected) {
            outColor = Vec3(fallback.x, fallback.y, fallback.z);
            if (affectTexture) {
                outUseTexture = false;
                outTexturePath.clear();
            }
            if (outWorldAligned) *outWorldAligned = false;
            if (outWorldScale) *outWorldScale = 1.0f;
            return;
        }
        if ((value.hasTexture || !value.texturePath.empty()) && affectTexture) {
            outUseTexture = true;
            outTexturePath = value.texturePath;
            // Handle world-aligned texture
            if (outWorldAligned) *outWorldAligned = value.isWorldAligned;
            if (outWorldScale) *outWorldScale = value.worldAlignedScale;
        } else {
            outColor = Vec3(value.vec.x, value.vec.y, value.vec.z);
            if (affectTexture) {
                outUseTexture = false;
                outTexturePath.clear();
            }
            if (outWorldAligned) *outWorldAligned = false;
            if (outWorldScale) *outWorldScale = 1.0f;
        }
    };

    auto resolveScalar = [&](PinID inputPin, float fallback) {
        bool connected = false;
        Value value = EvaluateInputPin(graph, inputPin, cache, active, outErrorMessage, &connected);
        if (!outErrorMessage.empty()) {
            return fallback;
        }
        if (!connected) {
            return fallback;
        }
        // Handle vector-to-scalar conversion by extracting first component
        if (value.type == ValueType::Float2 || value.type == ValueType::Float3 || value.type == ValueType::Float4) {
            return value.vec.x;
        }
        return value.scalar;
    };

    const float emissiveStrength = outMaterial.emissiveStrength;
    const float reflectionStrength = outMaterial.reflectionStrength;
    outMaterial = MaterialComponent();
    outMaterial.emissiveStrength = emissiveStrength;
    outMaterial.reflectionStrength = reflectionStrength;

    // Track if any texture uses world-aligned mode
    bool anyWorldAligned = false;
    float worldScale = 1.0f;

    if (!outputNode->inputs.empty()) {
        bool texWorldAligned = false;
        float texWorldScale = 1.0f;
        resolveColor(outputNode->inputs[0], Float3{1.0f, 1.0f, 1.0f}, true,
                     outMaterial.albedo, outMaterial.useAlbedoTexture, outMaterial.albedoTexturePath,
                     &texWorldAligned, &texWorldScale);
        // If albedo uses a world-aligned texture, enable world-aligned mode for the whole material
        if (texWorldAligned && outMaterial.useAlbedoTexture) {
            anyWorldAligned = true;
            worldScale = texWorldScale;
        }
        if (!outErrorMessage.empty()) {
            return false;
        }
    }
    if (outputNode->inputs.size() > 1) {
        outMaterial.metallic = resolveScalar(outputNode->inputs[1], outMaterial.metallic);
        if (!outErrorMessage.empty()) {
            return false;
        }
        // Check if it's a texture instead
        bool connected = false;
        Value texValue = EvaluateInputPin(graph, outputNode->inputs[1], cache, active, outErrorMessage, &connected);
        if (connected && (texValue.hasTexture || !texValue.texturePath.empty())) {
            outMaterial.useMetallicTexture = true;
            outMaterial.metallicTexturePath = texValue.texturePath;
            outMaterial.metallicChannel = texValue.channel;
            if (texValue.isWorldAligned) {
                anyWorldAligned = true;
                worldScale = texValue.worldAlignedScale;
            }
        }
    }
    if (outputNode->inputs.size() > 2) {
        outMaterial.roughness = resolveScalar(outputNode->inputs[2], outMaterial.roughness);
        if (!outErrorMessage.empty()) {
            return false;
        }
        // Check if it's a texture instead (allows texture on scalar Roughness pin)
        bool connected = false;
        Value texValue = EvaluateInputPin(graph, outputNode->inputs[2], cache, active, outErrorMessage, &connected);
        if (connected && (texValue.hasTexture || !texValue.texturePath.empty())) {
            outMaterial.useRoughnessTexture = true;
            outMaterial.roughnessTexturePath = texValue.texturePath;
            outMaterial.roughnessChannel = texValue.channel;
            if (texValue.isWorldAligned) {
                anyWorldAligned = true;
                worldScale = texValue.worldAlignedScale;
            }
        }
    }
    if (outputNode->inputs.size() > 3) {
        bool dummyTexture = false;
        std::string dummyPath;
        resolveColor(outputNode->inputs[3], Float3{0.0f, 0.0f, 0.0f}, false,
                     outMaterial.emissive, dummyTexture, dummyPath);
        if (!outErrorMessage.empty()) {
            return false;
        }
    }
    if (outputNode->inputs.size() > 4) {
        bool texWorldAligned = false;
        float texWorldScale = 1.0f;
        Vec3 normalFallback(0.5f, 0.5f, 1.0f);
        resolveColor(outputNode->inputs[4], Float3{0.5f, 0.5f, 1.0f}, true,
                     normalFallback, outMaterial.useNormalTexture, outMaterial.normalTexturePath,
                     &texWorldAligned, &texWorldScale);
        if (texWorldAligned && outMaterial.useNormalTexture) {
            anyWorldAligned = true;
            worldScale = texWorldScale;
        }
        if (!outErrorMessage.empty()) {
            return false;
        }
    }
    if (outputNode->inputs.size() > 5) {
        bool connected = false;
        Value texValue = EvaluateInputPin(graph, outputNode->inputs[5], cache, active, outErrorMessage, &connected);
        if (!outErrorMessage.empty()) {
            return false;
        }
        if (connected && (texValue.hasTexture || !texValue.texturePath.empty())) {
            // A map keeps the Roughness pin's value, which the shader multiplies it by.
            outMaterial.useRoughnessTexture = true;
            outMaterial.roughnessTexturePath = texValue.texturePath;
            outMaterial.roughnessChannel = texValue.channel;
            if (texValue.isWorldAligned) {
                anyWorldAligned = true;
                worldScale = texValue.worldAlignedScale;
            }
        } else {
            outMaterial.roughness = resolveScalar(outputNode->inputs[5], outMaterial.roughness);
            if (!outErrorMessage.empty()) {
                return false;
            }
        }
    }
    if (outputNode->inputs.size() > 6) {
        outMaterial.ao = resolveScalar(outputNode->inputs[6], outMaterial.ao);
        if (!outErrorMessage.empty()) {
            return false;
        }
        // Check if it's a texture instead
        bool connected = false;
        Value texValue = EvaluateInputPin(graph, outputNode->inputs[6], cache, active, outErrorMessage, &connected);
        if (connected && (texValue.hasTexture || !texValue.texturePath.empty())) {
            outMaterial.useAoTexture = true;
            outMaterial.aoTexturePath = texValue.texturePath;
            outMaterial.aoChannel = texValue.channel;
            if (texValue.isWorldAligned) {
                anyWorldAligned = true;
                worldScale = texValue.worldAlignedScale;
            }
        }
    }
    if (outputNode->inputs.size() > 7) {
        outMaterial.specular = resolveScalar(outputNode->inputs[7], outMaterial.specular);
    }
    if (outputNode->inputs.size() > 8) {
        outMaterial.opacity = resolveScalar(outputNode->inputs[8], outMaterial.opacity);
    }
    if (outputNode->inputs.size() > 9) {
        outMaterial.opacityMask = resolveScalar(outputNode->inputs[9], outMaterial.opacityMask);
    }
    if (outputNode->inputs.size() > 10) {
        outMaterial.refraction = resolveScalar(outputNode->inputs[10], outMaterial.refraction);
    }

    // Apply world-aligned settings if any texture uses it
    if (anyWorldAligned) {
        outMaterial.useWorldAlignedTexture = true;
        outMaterial.worldTextureScale = worldScale;
    }

    return true;
}

} // namespace MaterialEditor
