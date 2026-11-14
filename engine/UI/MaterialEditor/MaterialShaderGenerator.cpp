#include "MaterialShaderGenerator.h"

#include "MaterialNodeDefinitions.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace MaterialEditor {
namespace {

struct Expression {
    ValueType type = ValueType::Float1;
    std::string code = "0.0";
};

struct GeneratorContext {
    const Graph& graph;
    MaterialShaderGenerationResult result;
    std::unordered_map<PinID, Expression> cache;
    std::unordered_set<PinID> active;
    int tempIndex = 0;
    int samplerIndex = 0;
};

std::string GlslType(ValueType type) {
    switch (type) {
    case ValueType::Float1: return "float";
    case ValueType::Float2: return "vec2";
    case ValueType::Float3: return "vec3";
    case ValueType::Float4: return "vec4";
    case ValueType::Sampler2D: return "sampler2D";
    }
    return "float";
}

std::string DefaultExpression(ValueType type) {
    switch (type) {
    case ValueType::Float1: return "0.0";
    case ValueType::Float2: return "vec2(0.0)";
    case ValueType::Float3: return "vec3(0.0)";
    case ValueType::Float4: return "vec4(0.0)";
    case ValueType::Sampler2D: return "";
    }
    return "0.0";
}

std::string SanitizeIdentifier(std::string value, const char* fallback) {
    std::string out;
    for (char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_') {
            out.push_back(ch);
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9')) {
        out = fallback + out;
    }
    return out;
}

std::string CoerceExpression(const Expression& expression, ValueType targetType) {
    if (expression.type == targetType) {
        return expression.code;
    }
    if (expression.type == ValueType::Float1 && IsNumericValueType(targetType)) {
        return GlslType(targetType) + "(" + expression.code + ")";
    }
    if (targetType == ValueType::Float1 && expression.type != ValueType::Sampler2D) {
        return expression.code + ".x";
    }
    return expression.code;
}

Expression GenerateOutput(GeneratorContext& ctx, PinID outputPin);

Expression GenerateInput(GeneratorContext& ctx, const Node& node, int inputIndex, ValueType fallbackType = ValueType::Float1) {
    if (inputIndex < 0 || inputIndex >= static_cast<int>(node.inputs.size())) {
        return {fallbackType, DefaultExpression(fallbackType)};
    }

    const Pin* input = ctx.graph.FindPin(node.inputs[inputIndex]);
    const ValueType inputType = input ? input->valueType : fallbackType;
    const PinID source = ctx.graph.FindInputSource(node.inputs[inputIndex]);
    if (source == 0) {
        return {inputType, DefaultExpression(inputType)};
    }
    return GenerateOutput(ctx, source);
}

Expression MakeTemp(GeneratorContext& ctx, ValueType type, const std::string& expression) {
    const std::string name = "matNode_" + std::to_string(ctx.tempIndex++);
    ctx.result.body += "    " + GlslType(type) + " " + name + " = " + expression + ";\n";
    return {type, name};
}

std::string RegisterSampler(GeneratorContext& ctx, const Node& node) {
    std::string name = node.parameterName.empty() ? ("materialTexture_" + std::to_string(ctx.samplerIndex)) : SanitizeIdentifier(node.parameterName, "texture_");
    if (node.parameterName.empty()) {
        ++ctx.samplerIndex;
    }
    ctx.result.samplers.push_back({name, node.resourcePath});
    ctx.result.declarations += "layout(set = 1, binding = " + std::to_string(ctx.result.samplers.size() - 1) + ") uniform sampler2D " + name + ";\n";
    return name;
}

Expression TextureSampleExpression(GeneratorContext& ctx, const Node& node, PinID outputPin, const std::string& samplerName) {
    int outputIndex = 0;
    for (size_t i = 0; i < node.outputs.size(); ++i) {
        if (node.outputs[i] == outputPin) {
            outputIndex = static_cast<int>(i);
            break;
        }
    }

    const Expression uv = GenerateInput(ctx, node, node.type == NodeType::TextureSampleFromObject ? 1 : 0, ValueType::Float2);
    const std::string sample = "texture(" + samplerName + ", " + CoerceExpression(uv, ValueType::Float2) + ")";
    if (outputIndex == 0) return MakeTemp(ctx, ValueType::Float4, sample);
    if (outputIndex == 1) return MakeTemp(ctx, ValueType::Float3, sample + ".rgb");
    return MakeTemp(ctx, ValueType::Float1, sample + ".r");
}

Expression GenerateOutput(GeneratorContext& ctx, PinID outputPin) {
    if (auto it = ctx.cache.find(outputPin); it != ctx.cache.end()) {
        return it->second;
    }
    if (!ctx.active.insert(outputPin).second) {
        ctx.result.ok = false;
        ctx.result.diagnostics.push_back("Cycle detected while generating shader.");
        return {};
    }

    const Pin* pin = ctx.graph.FindPin(outputPin);
    if (!pin) {
        return {};
    }
    const Node* node = ctx.graph.FindNode(pin->node);
    if (!node) {
        return {};
    }

    auto input = [&](int index, ValueType type = ValueType::Float1) { return GenerateInput(ctx, *node, index, type); };
    auto binary = [&](const char* op) {
        const Expression a = input(0, pin->valueType);
        const Expression b = input(1, pin->valueType);
        ValueType type = ValueTypeComponentCount(a.type) >= ValueTypeComponentCount(b.type) ? a.type : b.type;
        if (!IsNumericValueType(type)) {
            type = ValueType::Float1;
        }
        return MakeTemp(ctx, type, "(" + CoerceExpression(a, type) + " " + op + " " + CoerceExpression(b, type) + ")");
    };

    Expression expression{pin->valueType, DefaultExpression(pin->valueType)};
    switch (node->type) {
    case NodeType::ConstantFloat:
    case NodeType::FloatParameter:
        expression = {ValueType::Float1, std::to_string(node->scalar)};
        break;
    case NodeType::BoolParameter:
        expression = {ValueType::Float1, node->boolValue ? "1.0" : "0.0"};
        break;
    case NodeType::ConstantVec3:
    case NodeType::ColorParameter:
        expression = {ValueType::Float3, "vec3(" + std::to_string(node->color.x) + ", " + std::to_string(node->color.y) + ", " + std::to_string(node->color.z) + ")"};
        break;
    case NodeType::Vector2Parameter:
        expression = {ValueType::Float2, "vec2(" + std::to_string(node->color.x) + ", " + std::to_string(node->color.y) + ")"};
        break;
    case NodeType::TextureObject:
    case NodeType::TextureParameter:
        expression = {ValueType::Sampler2D, RegisterSampler(ctx, *node)};
        break;
    case NodeType::AlbedoTextureSample:
    case NodeType::NormalTextureSample:
    case NodeType::RoughnessTextureSample:
    case NodeType::MetallicTextureSample:
    case NodeType::AoTextureSample:
    case NodeType::TextureSample:
    case NodeType::TriplanarTexture:
        expression = TextureSampleExpression(ctx, *node, outputPin, RegisterSampler(ctx, *node));
        break;
    case NodeType::TextureSampleFromObject: {
        Expression sampler = input(0, ValueType::Sampler2D);
        expression = TextureSampleExpression(ctx, *node, outputPin, sampler.code);
        break;
    }
    case NodeType::Add: expression = binary("+"); break;
    case NodeType::Subtract: expression = binary("-"); break;
    case NodeType::Multiply: expression = binary("*"); break;
    case NodeType::Divide: expression = binary("/"); break;
    case NodeType::Clamp:
        expression = MakeTemp(ctx, ValueType::Float1, "clamp(" + input(0).code + ", " + input(1).code + ", " + input(2).code + ")");
        break;
    case NodeType::Saturate:
        expression = MakeTemp(ctx, ValueType::Float1, "clamp(" + input(0).code + ", 0.0, 1.0)");
        break;
    case NodeType::Power: expression = MakeTemp(ctx, ValueType::Float1, "pow(max(" + input(0).code + ", 0.0), " + input(1).code + ")"); break;
    case NodeType::Abs: expression = MakeTemp(ctx, ValueType::Float1, "abs(" + input(0).code + ")"); break;
    case NodeType::Min: expression = MakeTemp(ctx, ValueType::Float1, "min(" + input(0).code + ", " + input(1).code + ")"); break;
    case NodeType::Max: expression = MakeTemp(ctx, ValueType::Float1, "max(" + input(0).code + ", " + input(1).code + ")"); break;
    case NodeType::OneMinus: expression = MakeTemp(ctx, ValueType::Float1, "(1.0 - " + input(0).code + ")"); break;
    case NodeType::DotProduct: expression = MakeTemp(ctx, ValueType::Float1, "dot(" + CoerceExpression(input(0, ValueType::Float3), ValueType::Float3) + ", " + CoerceExpression(input(1, ValueType::Float3), ValueType::Float3) + ")"); break;
    case NodeType::CrossProduct: expression = MakeTemp(ctx, ValueType::Float3, "cross(" + CoerceExpression(input(0, ValueType::Float3), ValueType::Float3) + ", " + CoerceExpression(input(1, ValueType::Float3), ValueType::Float3) + ")"); break;
    case NodeType::Normalize: expression = MakeTemp(ctx, ValueType::Float3, "normalize(" + CoerceExpression(input(0, ValueType::Float3), ValueType::Float3) + ")"); break;
    case NodeType::Length: expression = MakeTemp(ctx, ValueType::Float1, "length(" + CoerceExpression(input(0, ValueType::Float3), ValueType::Float3) + ")"); break;
    case NodeType::Lerp: expression = MakeTemp(ctx, pin->valueType, "mix(" + CoerceExpression(input(0, pin->valueType), pin->valueType) + ", " + CoerceExpression(input(1, pin->valueType), pin->valueType) + ", " + input(2).code + ")"); break;
    case NodeType::ComponentMask: {
        std::string swizzle;
        if (node->maskR) swizzle += "r";
        if (node->maskG) swizzle += "g";
        if (node->maskB) swizzle += "b";
        if (node->maskA) swizzle += "a";
        if (swizzle.empty()) swizzle = "r";
        expression = MakeTemp(ctx, swizzle.size() == 1 ? ValueType::Float1 : (swizzle.size() == 2 ? ValueType::Float2 : (swizzle.size() == 3 ? ValueType::Float3 : ValueType::Float4)), CoerceExpression(input(0, ValueType::Float4), ValueType::Float4) + "." + swizzle);
        break;
    }
    case NodeType::AppendVector: expression = MakeTemp(ctx, ValueType::Float2, "vec2(" + input(0).code + ", " + input(1).code + ")"); break;
    case NodeType::BreakVector: {
        int outputIndex = 0;
        for (size_t i = 0; i < node->outputs.size(); ++i) {
            if (node->outputs[i] == outputPin) {
                outputIndex = static_cast<int>(i);
                break;
            }
        }
        static const char* components[] = {"r", "g", "b", "a"};
        expression = MakeTemp(ctx, ValueType::Float1, CoerceExpression(input(0, ValueType::Float4), ValueType::Float4) + "." + components[outputIndex]);
        break;
    }
    case NodeType::Transform: expression = input(0, ValueType::Float3); break;
    case NodeType::TexCoord: expression = {ValueType::Float2, "fragTexCoord"}; break;
    case NodeType::ScreenPosition: expression = {ValueType::Float4, "gl_FragCoord"}; break;
    case NodeType::TextureTilingOffset: expression = MakeTemp(ctx, ValueType::Float2, CoerceExpression(input(0, ValueType::Float2), ValueType::Float2) + " * " + CoerceExpression(input(1, ValueType::Float2), ValueType::Float2) + " + " + CoerceExpression(input(2, ValueType::Float2), ValueType::Float2)); break;
    case NodeType::NormalUnpack: expression = MakeTemp(ctx, ValueType::Float3, "normalize(" + CoerceExpression(input(0, ValueType::Float3), ValueType::Float3) + " * 2.0 - 1.0)"); break;
    case NodeType::Noise: expression = MakeTemp(ctx, ValueType::Float1, "fract(sin(dot(" + CoerceExpression(input(0, ValueType::Float2), ValueType::Float2) + " * " + input(1).code + ", vec2(12.9898, 78.233))) * 43758.5453)"); break;
    case NodeType::Gradient: expression = MakeTemp(ctx, ValueType::Float1, node->operation == "Radial" ? "length(" + CoerceExpression(input(0, ValueType::Float2), ValueType::Float2) + " - vec2(0.5)) * 2.0" : CoerceExpression(input(0, ValueType::Float2), ValueType::Float2) + ".x"); break;
    case NodeType::Fresnel: expression = MakeTemp(ctx, ValueType::Float1, "pow(1.0 - max(dot(normalize(" + CoerceExpression(input(0, ValueType::Float3), ValueType::Float3) + "), normalize(ubo.viewPos - fragWorldPos)), 0.0), max(" + input(1).code + ", 0.001))"); break;
    case NodeType::UVDistortion: expression = MakeTemp(ctx, ValueType::Float2, CoerceExpression(input(0, ValueType::Float2), ValueType::Float2) + " + " + CoerceExpression(input(1, ValueType::Float2), ValueType::Float2) + " * " + input(2).code); break;
    case NodeType::Time: expression = {ValueType::Float1, "materialTime"}; ctx.result.uniforms.push_back({"materialTime", ValueType::Float1}); break;
    case NodeType::CameraVector:
    case NodeType::ViewDirection: expression = {ValueType::Float3, "normalize(ubo.viewPos - fragWorldPos)"}; break;
    case NodeType::ReflectionVector: expression = MakeTemp(ctx, ValueType::Float3, "reflect(-normalize(ubo.viewPos - fragWorldPos), normalize(" + CoerceExpression(input(0, ValueType::Float3), ValueType::Float3) + "))"); break;
    case NodeType::WorldPosition: expression = {ValueType::Float3, "fragWorldPos"}; break;
    case NodeType::ObjectPosition: expression = {ValueType::Float3, "vec3(model[3])"}; break;
    case NodeType::DistanceToCamera: expression = MakeTemp(ctx, ValueType::Float1, "length(ubo.viewPos - fragWorldPos)"); break;
    case NodeType::If: expression = MakeTemp(ctx, ValueType::Float1, "(" + input(0).code + " > " + input(1).code + " ? " + input(2).code + " : " + input(3).code + ")"); break;
    case NodeType::Step: expression = MakeTemp(ctx, ValueType::Float1, "step(" + input(0).code + ", " + input(1).code + ")"); break;
    case NodeType::SmoothStep: expression = MakeTemp(ctx, ValueType::Float1, "smoothstep(" + input(0).code + ", " + input(1).code + ", " + input(2).code + ")"); break;
    case NodeType::Compare: expression = MakeTemp(ctx, ValueType::Float1, "(" + input(0).code + " " + (node->operation.empty() ? ">" : node->operation) + " " + input(1).code + " ? 1.0 : 0.0)"); break;
    case NodeType::Overlay: expression = MakeTemp(ctx, ValueType::Float3, "mix(2.0 * " + CoerceExpression(input(0, ValueType::Float3), ValueType::Float3) + " * " + CoerceExpression(input(1, ValueType::Float3), ValueType::Float3) + ", 1.0 - 2.0 * (1.0 - " + CoerceExpression(input(0, ValueType::Float3), ValueType::Float3) + ") * (1.0 - " + CoerceExpression(input(1, ValueType::Float3), ValueType::Float3) + "), step(vec3(0.5), " + CoerceExpression(input(0, ValueType::Float3), ValueType::Float3) + "))"); break;
    case NodeType::Screen: expression = MakeTemp(ctx, ValueType::Float3, "1.0 - (1.0 - " + CoerceExpression(input(0, ValueType::Float3), ValueType::Float3) + ") * (1.0 - " + CoerceExpression(input(1, ValueType::Float3), ValueType::Float3) + ")"); break;
    case NodeType::HeightBlend: expression = MakeTemp(ctx, ValueType::Float3, "mix(" + CoerceExpression(input(0, ValueType::Float3), ValueType::Float3) + ", " + CoerceExpression(input(1, ValueType::Float3), ValueType::Float3) + ", clamp((" + input(2).code + " - 0.5) * max(" + input(3).code + ", 1.0) + 0.5, 0.0, 1.0))"); break;
    case NodeType::StaticSwitchParameter: expression = node->boolValue ? input(1) : input(0); break;
    case NodeType::MaterialOutput:
    case NodeType::Unknown:
        ctx.result.ok = false;
        ctx.result.diagnostics.push_back("Unsupported shader node: " + std::string(NodeTypeName(node->type)));
        break;
    }

    ctx.cache[outputPin] = expression;
    ctx.active.erase(outputPin);
    return expression;
}

} // namespace

MaterialShaderGenerationResult GenerateMaterialShader(const Graph& graph) {
    GeneratorContext ctx{graph};

    const Node* outputNode = nullptr;
    for (const Node& node : graph.GetNodes()) {
        if (node.type == NodeType::MaterialOutput) {
            outputNode = &node;
            break;
        }
    }
    if (!outputNode) {
        ctx.result.ok = false;
        ctx.result.diagnostics.push_back("MaterialOutput node is required.");
        return ctx.result;
    }

    static const char* outputNames[] = {
        "albedo", "metallic", "roughness", "emissive", "normal", "roughnessMap",
        "ao", "specular", "opacity", "opacityMask", "refraction"
    };
    const int outputCount = std::min<int>(static_cast<int>(outputNode->inputs.size()), static_cast<int>(sizeof(outputNames) / sizeof(outputNames[0])));
    for (int i = 0; i < outputCount; ++i) {
        const Pin* pin = graph.FindPin(outputNode->inputs[i]);
        if (!pin) {
            continue;
        }
        const Expression expression = GenerateInput(ctx, *outputNode, i, pin->valueType);
        ctx.result.body += "    material." + std::string(outputNames[i]) + " = " + CoerceExpression(expression, pin->valueType) + ";\n";
    }

    return ctx.result;
}

} // namespace MaterialEditor
