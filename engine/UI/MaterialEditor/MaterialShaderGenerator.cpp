#include "MaterialShaderGenerator.h"

#include "MaterialNodeDefinitions.h"

#include <algorithm>
#include <charconv>
#include <unordered_map>
#include <unordered_set>

namespace MaterialEditor {
namespace {

struct Expression {
    ValueType type = ValueType::Float1;
    std::string code = "0.0";
    std::string alpha; ///< alpha of the texture a colour came from, for Base Color's coverage; empty means 1
};

struct GeneratorContext {
    const Graph& graph;
    MaterialShaderGenerationResult result;
    std::unordered_map<PinID, Expression> cache;
    std::unordered_set<PinID> active;
    std::unordered_set<std::string> customFunctions;
    int tempIndex = 0;
};

/// Replaces {0}..{9} with the inputs' code and the named builtins (MaterialNodeDefinitions.h:CustomPinDefinition).
std::string ExpandCustomGlsl(const std::string& text, const std::vector<std::string>& inputs) {
    static const std::pair<const char*, const char*> builtins[] = {
        {"uv", "fragTexCoord"},
        {"worldPos", "fragWorldPos"},
        {"normal", "N"},
        {"viewDir", "normalize(ubo.viewPos - fragWorldPos)"},
        {"cameraPos", "ubo.viewPos"},
        {"time", "ubo.time"},
        {"objectPos", "fragObjectPos"},
        {"screenPos", "gl_FragCoord"},
    };
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        const size_t close = text[i] == '{' ? text.find('}', i) : std::string::npos;
        if (close == std::string::npos) {
            out += text[i];
            continue;
        }
        const std::string name = text.substr(i + 1, close - i - 1);
        std::string replacement;
        bool known = false;
        if (name.size() == 1 && name[0] >= '0' && name[0] <= '9') {
            const size_t index = static_cast<size_t>(name[0] - '0');
            known = index < inputs.size();
            if (known) replacement = "(" + inputs[index] + ")";
        } else {
            for (const auto& [key, code] : builtins) {
                if (name == key) {
                    replacement = std::string("(") + code + ")";
                    known = true;
                }
            }
        }
        if (!known) {
            out += text[i];
            continue;
        }
        out += replacement;
        i = close;
    }
    return out;
}

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

std::string FloatLiteral(float value) {
    char buffer[32];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general);
    std::string text = error == std::errc() ? std::string(buffer, end) : std::string("0.0");
    if (text.find_first_of(".eEn") == std::string::npos) {
        text += ".0";
    }
    if (text == "inf" || text == "-inf" || text.find("nan") != std::string::npos) {
        return "0.0";
    }
    return text;
}

std::string CoerceExpression(const Expression& expression, ValueType targetType) {
    if (expression.type == targetType || expression.type == ValueType::Sampler2D || targetType == ValueType::Sampler2D) {
        return expression.code;
    }
    const int from = ValueTypeComponentCount(expression.type);
    const int to = ValueTypeComponentCount(targetType);
    if (from == 1) {
        return GlslType(targetType) + "(" + expression.code + ")";
    }
    if (to < from) {
        static const char* swizzles[] = {"", ".x", ".xy", ".xyz"};
        return "(" + expression.code + ")" + swizzles[to];
    }
    std::string padded = GlslType(targetType) + "(" + expression.code;
    for (int i = from; i < to; ++i) {
        padded += i == 3 ? ", 1.0" : ", 0.0";
    }
    return padded + ")";
}

ValueType Widest(ValueType a, ValueType b) {
    const ValueType type = ValueTypeComponentCount(a) >= ValueTypeComponentCount(b) ? a : b;
    return IsNumericValueType(type) ? type : ValueType::Float1;
}

Expression GenerateOutput(GeneratorContext& ctx, PinID outputPin);

bool IsInputConnected(const GeneratorContext& ctx, const Node& node, int inputIndex) {
    return inputIndex >= 0 && inputIndex < static_cast<int>(node.inputs.size()) &&
           ctx.graph.FindInputSource(node.inputs[inputIndex]) != 0;
}

Expression GenerateInput(GeneratorContext& ctx, const Node& node, int inputIndex, ValueType fallbackType,
                         const std::string& unconnected = {}) {
    if (!IsInputConnected(ctx, node, inputIndex)) {
        const Pin* input = inputIndex >= 0 && inputIndex < static_cast<int>(node.inputs.size())
            ? ctx.graph.FindPin(node.inputs[inputIndex])
            : nullptr;
        const ValueType inputType = input ? input->valueType : fallbackType;
        return {inputType, unconnected.empty() ? DefaultExpression(inputType) : unconnected};
    }
    return GenerateOutput(ctx, ctx.graph.FindInputSource(node.inputs[inputIndex]));
}

Expression MakeTemp(GeneratorContext& ctx, ValueType type, const std::string& expression, std::string alpha = {}) {
    const std::string name = "matNode_" + std::to_string(ctx.tempIndex++);
    ctx.result.body += "    " + GlslType(type) + " " + name + " = " + expression + ";\n";
    return {type, name, std::move(alpha)};
}

std::string RegisterSampler(GeneratorContext& ctx, const Node& node) {
    const bool linear = TextureNodeIsLinear(node);
    const auto existing = std::find_if(ctx.result.samplers.begin(), ctx.result.samplers.end(),
                                       [&](const GeneratedSamplerBinding& binding) {
                                           return binding.resourcePath == node.resourcePath && binding.linear == linear;
                                       });
    size_t index = static_cast<size_t>(existing - ctx.result.samplers.begin());
    if (existing == ctx.result.samplers.end()) {
        if (ctx.result.samplers.size() >= kMaxGraphTextures) {
            ctx.result.ok = false;
            ctx.result.diagnostics.push_back("A material graph can use at most " + std::to_string(kMaxGraphTextures) +
                                             " different textures.");
            return "materialTextures[0]";
        }
        ctx.result.samplers.push_back({"texture" + std::to_string(ctx.result.samplers.size()), node.resourcePath, linear});
        index = ctx.result.samplers.size() - 1;
    }
    return "materialTextures[material.graphTextureSlots[" + std::to_string(index / 4) + "][" +
           std::to_string(index % 4) + "] % MAX_TEXTURE_SLOTS]";
}

int OutputIndex(const Node& node, PinID outputPin) {
    for (size_t i = 0; i < node.outputs.size(); ++i) {
        if (node.outputs[i] == outputPin) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

Expression TextureSampleExpression(GeneratorContext& ctx, const Node& node, const Pin& pin, const std::string& sampler) {
    std::string uv = "surface.texCoord";
    if (node.type == NodeType::WorldAlignedTexture || node.type == NodeType::TriplanarTexture) {
        uv = "getWorldAlignedUV(fragWorldPos, N, " + FloatLiteral(node.worldAlignedScale) + ")";
    } else if (node.type == NodeType::TextureSample || node.type == NodeType::TextureSampleFromObject) {
        const int uvInput = node.type == NodeType::TextureSampleFromObject ? 1 : 0;
        uv = CoerceExpression(GenerateInput(ctx, node, uvInput, ValueType::Float2, "surface.texCoord"), ValueType::Float2);
    }
    const Expression sample = MakeTemp(ctx, ValueType::Float4, "texture(" + sampler + ", " + uv + ")");
    const std::string alpha = sample.code + ".a";
    switch (pin.valueType) {
    case ValueType::Float4: return {ValueType::Float4, sample.code, alpha};
    case ValueType::Float3: return {ValueType::Float3, sample.code + ".rgb", alpha};
    case ValueType::Float2: return {ValueType::Float2, sample.code + ".rg", alpha};
    default: return {ValueType::Float1, sample.code + ".r"};
    }
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
    const Node* node = pin ? ctx.graph.FindNode(pin->node) : nullptr;
    if (!pin || !node) {
        ctx.active.erase(outputPin);
        return {};
    }

    auto input = [&](int index, ValueType type = ValueType::Float1, const std::string& unconnected = {}) {
        return GenerateInput(ctx, *node, index, type, unconnected);
    };
    auto scalar = [&](int index, const std::string& unconnected = {}) {
        return CoerceExpression(input(index, ValueType::Float1, unconnected), ValueType::Float1);
    };
    auto vector = [&](int index, ValueType type, const std::string& unconnected = {}) {
        return CoerceExpression(input(index, type, unconnected), type);
    };
    auto binary = [&](const char* op) {
        const Expression a = input(0, pin->valueType);
        const Expression b = input(1, pin->valueType);
        const ValueType type = Widest(a.type, b.type);
        return MakeTemp(ctx, type, "(" + CoerceExpression(a, type) + " " + op + " " + CoerceExpression(b, type) + ")",
                        a.alpha.empty() ? b.alpha : a.alpha);
    };
    auto scalarCall = [&](const std::string& call) { return MakeTemp(ctx, ValueType::Float1, call); };

    Expression expression{pin->valueType, DefaultExpression(pin->valueType)};
    switch (node->type) {
    case NodeType::ConstantFloat:
    case NodeType::FloatParameter:
        expression = {ValueType::Float1, FloatLiteral(node->scalar)};
        break;
    case NodeType::BoolParameter:
        expression = {ValueType::Float1, node->boolValue ? "1.0" : "0.0"};
        break;
    case NodeType::ConstantVec3:
    case NodeType::ColorParameter:
        expression = {ValueType::Float3, "vec3(" + FloatLiteral(node->color.x) + ", " + FloatLiteral(node->color.y) +
                                             ", " + FloatLiteral(node->color.z) + ")"};
        break;
    case NodeType::Vector2Parameter:
        expression = {ValueType::Float2, "vec2(" + FloatLiteral(node->color.x) + ", " + FloatLiteral(node->color.y) + ")"};
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
    case NodeType::WorldAlignedTexture:
    case NodeType::TriplanarTexture:
        expression = TextureSampleExpression(ctx, *node, *pin, RegisterSampler(ctx, *node));
        break;
    case NodeType::TextureSampleFromObject: {
        const Expression sampler = input(0, ValueType::Sampler2D);
        if (sampler.type != ValueType::Sampler2D || sampler.code.empty()) {
            ctx.result.ok = false;
            ctx.result.diagnostics.push_back("Texture Sample From Object needs a Texture Object or Texture Parameter.");
            break;
        }
        expression = TextureSampleExpression(ctx, *node, *pin, sampler.code);
        break;
    }
    case NodeType::Add: expression = binary("+"); break;
    case NodeType::Subtract: expression = binary("-"); break;
    case NodeType::Multiply: expression = binary("*"); break;
    case NodeType::Divide: expression = binary("/"); break;
    case NodeType::Clamp:
        expression = scalarCall("clamp(" + scalar(0) + ", " + scalar(1) + ", " +
                                (IsInputConnected(ctx, *node, 2) ? scalar(2) : FloatLiteral(node->scalar)) + ")");
        break;
    case NodeType::Saturate: expression = scalarCall("clamp(" + scalar(0) + ", 0.0, 1.0)"); break;
    case NodeType::Power: expression = scalarCall("pow(max(" + scalar(0) + ", 0.0), " + scalar(1, "1.0") + ")"); break;
    case NodeType::Abs: expression = scalarCall("abs(" + scalar(0) + ")"); break;
    case NodeType::Min: expression = scalarCall("min(" + scalar(0) + ", " + scalar(1) + ")"); break;
    case NodeType::Max: expression = scalarCall("max(" + scalar(0) + ", " + scalar(1) + ")"); break;
    case NodeType::OneMinus: expression = scalarCall("(1.0 - " + scalar(0) + ")"); break;
    case NodeType::DotProduct:
        expression = scalarCall("dot(" + vector(0, ValueType::Float3) + ", " + vector(1, ValueType::Float3) + ")");
        break;
    case NodeType::CrossProduct:
        expression = MakeTemp(ctx, ValueType::Float3, "cross(" + vector(0, ValueType::Float3) + ", " + vector(1, ValueType::Float3) + ")");
        break;
    case NodeType::Normalize:
        expression = MakeTemp(ctx, ValueType::Float3, "normalize(" + vector(0, ValueType::Float3) + ")");
        break;
    case NodeType::Length: expression = scalarCall("length(" + vector(0, ValueType::Float3) + ")"); break;
    case NodeType::Lerp: {
        const Expression a = input(0);
        const Expression b = input(1);
        const ValueType type = Widest(a.type, b.type);
        expression = MakeTemp(ctx, type, "mix(" + CoerceExpression(a, type) + ", " + CoerceExpression(b, type) + ", " +
                                              scalar(2) + ")",
                              a.alpha.empty() ? b.alpha : a.alpha);
        break;
    }
    case NodeType::ComponentMask: {
        std::string swizzle;
        if (node->maskR) swizzle += "r";
        if (node->maskG) swizzle += "g";
        if (node->maskB) swizzle += "b";
        if (node->maskA) swizzle += "a";
        if (swizzle.empty()) swizzle = "r";
        const ValueType type = swizzle.size() == 1 ? ValueType::Float1
            : swizzle.size() == 2                  ? ValueType::Float2
            : swizzle.size() == 3                  ? ValueType::Float3
                                                   : ValueType::Float4;
        expression = MakeTemp(ctx, type, vector(0, ValueType::Float4) + "." + swizzle);
        break;
    }
    case NodeType::AppendVector:
        expression = MakeTemp(ctx, ValueType::Float2, "vec2(" + scalar(0) + ", " + scalar(1) + ")");
        break;
    case NodeType::BreakVector: {
        static const char* components[] = {"r", "g", "b", "a"};
        expression = MakeTemp(ctx, ValueType::Float1,
                              vector(0, ValueType::Float4) + "." + components[std::clamp(OutputIndex(*node, outputPin), 0, 3)]);
        break;
    }
    case NodeType::Transform: expression = input(0, ValueType::Float3); break;
    case NodeType::TexCoord: expression = {ValueType::Float2, "fragTexCoord"}; break;
    case NodeType::TextureTilingOffset:
        expression = MakeTemp(ctx, ValueType::Float2, vector(0, ValueType::Float2, "fragTexCoord") + " * " +
                                                          vector(1, ValueType::Float2, "vec2(1.0)") + " + " +
                                                          vector(2, ValueType::Float2));
        break;
    case NodeType::NormalUnpack:
        expression = MakeTemp(ctx, ValueType::Float3, "normalize(" + vector(0, ValueType::Float3) + " * 2.0 - 1.0)");
        break;
    case NodeType::If:
        expression = scalarCall("(" + scalar(0) + " > " + scalar(1) + " ? " + scalar(2) + " : " + scalar(3) + ")");
        break;
    case NodeType::Step: expression = scalarCall("step(" + scalar(0) + ", " + scalar(1) + ")"); break;
    case NodeType::SmoothStep:
        expression = scalarCall("smoothstep(" + scalar(0) + ", " + scalar(1, "1.0") + ", " + scalar(2) + ")");
        break;
    case NodeType::Compare: {
        const std::string op = node->operation == "<" || node->operation == "==" ? node->operation : ">";
        expression = op == "=="
            ? scalarCall("(abs(" + scalar(0) + " - " + scalar(1) + ") <= 0.0001 ? 1.0 : 0.0)")
            : scalarCall("(" + scalar(0) + " " + op + " " + scalar(1) + " ? 1.0 : 0.0)");
        break;
    }
    case NodeType::Overlay: {
        const std::string base = vector(0, ValueType::Float3);
        const std::string blend = vector(1, ValueType::Float3);
        expression = MakeTemp(ctx, ValueType::Float3, "mix(2.0 * " + base + " * " + blend + ", 1.0 - 2.0 * (1.0 - " + base +
                                                          ") * (1.0 - " + blend + "), step(vec3(0.5), " + base + "))");
        break;
    }
    case NodeType::Screen:
        expression = MakeTemp(ctx, ValueType::Float3, "1.0 - (1.0 - " + vector(0, ValueType::Float3) + ") * (1.0 - " +
                                                          vector(1, ValueType::Float3) + ")");
        break;
    case NodeType::HeightBlend:
        expression = MakeTemp(ctx, ValueType::Float3, "mix(" + vector(0, ValueType::Float3) + ", " + vector(1, ValueType::Float3) +
                                                          ", clamp((" + scalar(2) + " - 0.5) * max(" + scalar(3, "1.0") +
                                                          ", 1.0) + 0.5, 0.0, 1.0))");
        break;
    case NodeType::StaticSwitchParameter: expression = node->boolValue ? input(1) : input(0); break;
    case NodeType::Custom: {
        const CustomNodeDefinition* custom = FindCustomNode(node->serializedType);
        const int outputIndex = OutputIndex(*node, outputPin);
        if (!custom || outputIndex >= static_cast<int>(custom->outputs.size())) {
            ctx.result.ok = false;
            ctx.result.diagnostics.push_back("The plugin providing the " + node->serializedType +
                                             " node is not loaded.");
            break;
        }
        std::vector<std::string> inputs;
        for (size_t i = 0; i < custom->inputs.size(); ++i) {
            const CustomPinDefinition& pin = custom->inputs[i];
            const std::string unconnected = pin.glsl.empty() ? std::string() : ExpandCustomGlsl(pin.glsl, {});
            inputs.push_back(CoerceExpression(input(static_cast<int>(i), pin.valueType, unconnected), pin.valueType));
        }
        if (!custom->functions.empty() && ctx.customFunctions.insert(custom->typeName).second) {
            ctx.result.declarations += custom->functions + "\n";
        }
        const CustomPinDefinition& output = custom->outputs[static_cast<size_t>(outputIndex)];
        expression = MakeTemp(ctx, output.valueType, ExpandCustomGlsl(output.glsl, inputs));
        break;
    }
    case NodeType::MaterialOutput:
    case NodeType::Unknown:
        ctx.result.ok = false;
        ctx.result.diagnostics.push_back("Unsupported shader node: " +
                                         (node->serializedType.empty() ? std::string(NodeTypeName(node->type))
                                                                       : node->serializedType));
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

    enum Pin { BaseColor, Metallic, Roughness, Emissive, NormalMap, RoughnessMap, AoMap, Specular, Opacity, OpacityClip };
    auto connected = [&](int pin) { return IsInputConnected(ctx, *outputNode, pin); };
    auto value = [&](int pin, ValueType type) {
        return CoerceExpression(GenerateInput(ctx, *outputNode, pin, type), type);
    };
    std::string& body = ctx.result.body;

    if (connected(BaseColor)) {
        const Expression color = GenerateInput(ctx, *outputNode, BaseColor, ValueType::Float3);
        body += "    surface.albedo = " + CoerceExpression(color, ValueType::Float3) + " * fragColor;\n";
        if (!color.alpha.empty()) {
            body += "    surface.textureAlpha = " + color.alpha + ";\n";
        }
    }
    if (connected(Metallic)) body += "    surface.metallic = " + value(Metallic, ValueType::Float1) + ";\n";
    if (connected(Roughness)) body += "    surface.roughness = " + value(Roughness, ValueType::Float1) + ";\n";
    if (connected(RoughnessMap)) body += "    surface.roughness *= " + value(RoughnessMap, ValueType::Float1) + ";\n";
    if (connected(Emissive)) {
        body += "    surface.emissive = " + value(Emissive, ValueType::Float3) + " * material.emissiveStrength;\n";
    }
    if (connected(NormalMap)) {
        body += "    surface.normal = perturbNormal(N, fragWorldPos, fragTexCoord, " + value(NormalMap, ValueType::Float3) +
                " * 2.0 - 1.0);\n";
    }
    if (connected(AoMap)) body += "    surface.ao = " + value(AoMap, ValueType::Float1) + ";\n";
    if (connected(Specular)) body += "    surface.specular = " + value(Specular, ValueType::Float1) + ";\n";
    if (connected(Opacity)) body += "    surface.opacity = " + value(Opacity, ValueType::Float1) + ";\n";
    if (connected(OpacityClip)) body += "    surface.clipThreshold = " + value(OpacityClip, ValueType::Float1) + ";\n";

    return ctx.result;
}

std::string BuildMaterialGraphSource(const MaterialShaderGenerationResult& generated) {
    return generated.declarations +
           "Surface evaluateSurfaceCoverage(vec3 N) {\n"
           "    Surface surface;\n"
           "    surface.texCoord = fragTexCoord;\n"
           "    surface.albedo = material.albedo * fragColor;\n"
           "    surface.textureAlpha = 1.0;\n"
           "    surface.opacity = material.opacity;\n"
           "    surface.clipThreshold = material.opacityMask;\n"
           "    surface.emissive = material.emissive * material.emissiveStrength;\n"
           "    surface.normal = N;\n"
           "    surface.roughness = material.roughness;\n"
           "    surface.metallic = material.metallic;\n"
           "    surface.ao = material.ao;\n"
           "    surface.specular = material.specular;\n" +
           generated.body +
           "    return surface;\n"
           "}\n"
           "\n"
           "void evaluateSurfaceShading(inout Surface surface) {}\n";
}

} // namespace MaterialEditor
