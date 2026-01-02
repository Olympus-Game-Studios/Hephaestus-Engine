#include "MaterialGraphEvaluator.h"

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
    case NodeType::ConstantVec3:
    case NodeType::ColorParameter:
        result.vec = node->color;
        break;
    case NodeType::AlbedoTextureSample:
    case NodeType::NormalTextureSample:
    case NodeType::RoughnessTextureSample:
    case NodeType::AoTextureSample: {
        // Texture sample nodes combine parameter + sampling
        result.hasTexture = !node->resourcePath.empty();
        result.texturePath = node->resourcePath;
        
        // Output type depends on the texture type
        if (node->type == NodeType::RoughnessTextureSample || node->type == NodeType::AoTextureSample) {
            result.type = ValueType::Float1;
            result.scalar = 1.0f;
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
        Value a = fetchInput(0);
        Value b = fetchInput(1);
        if (pin->valueType == ValueType::Float1) {
            result.scalar = a.scalar + b.scalar;
        } else {
            result.vec = Add(a.vec, b.vec);
        }
        break;
    }
    case NodeType::Multiply: {
        Value a = fetchInput(0);
        Value b = fetchInput(1);
        if (pin->valueType == ValueType::Float1) {
            result.scalar = a.scalar * b.scalar;
        } else {
            result.vec = Multiply(a.vec, b.vec);
        }
        break;
    }
    case NodeType::MaterialOutput:
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
        return connected ? value.scalar : fallback;
    };

    outMaterial = MaterialComponent();

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
    }
    if (outputNode->inputs.size() > 2) {
        outMaterial.roughness = resolveScalar(outputNode->inputs[2], outMaterial.roughness);
        if (!outErrorMessage.empty()) {
            return false;
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
        resolveColor(outputNode->inputs[4], Float3{0.5f, 0.5f, 1.0f}, true,
                     outMaterial.emissive, outMaterial.useNormalTexture, outMaterial.normalTexturePath,
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
        outMaterial.roughness = resolveScalar(outputNode->inputs[5], outMaterial.roughness);
        if (!outErrorMessage.empty()) {
            return false;
        }
        // Check if it's a texture instead
        bool connected = false;
        Value texValue = EvaluateInputPin(graph, outputNode->inputs[5], cache, active, outErrorMessage, &connected);
        if (connected && (texValue.hasTexture || !texValue.texturePath.empty())) {
            outMaterial.useRoughnessTexture = true;
            outMaterial.roughnessTexturePath = texValue.texturePath;
            if (texValue.isWorldAligned) {
                anyWorldAligned = true;
                worldScale = texValue.worldAlignedScale;
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
            if (texValue.isWorldAligned) {
                anyWorldAligned = true;
                worldScale = texValue.worldAlignedScale;
            }
        }
    }

    // Apply world-aligned settings if any texture uses it
    if (anyWorldAligned) {
        outMaterial.useWorldAlignedTexture = true;
        outMaterial.worldTextureScale = worldScale;
    }

    return true;
}

} // namespace MaterialEditor
