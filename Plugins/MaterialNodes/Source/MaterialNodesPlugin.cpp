#include "PluginAPI/PluginAPI.h"

#include <iterator>

using namespace Hephaestus::Plugins;

namespace {

constexpr MaterialValueType Float = MaterialValueType::Float;
constexpr MaterialValueType Float2 = MaterialValueType::Float2;
constexpr MaterialValueType Float3 = MaterialValueType::Float3;
constexpr MaterialValueType Float4 = MaterialValueType::Float4;

constexpr const char* kNoiseFunctions =
    "float hpHash21(vec2 p) {\n"
    "    p = fract(p * vec2(123.34, 456.21));\n"
    "    p += dot(p, p + 45.32);\n"
    "    return fract(p.x * p.y);\n"
    "}\n"
    "float hpValueNoise(vec2 p) {\n"
    "    vec2 i = floor(p);\n"
    "    vec2 f = fract(p);\n"
    "    vec2 u = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(hpHash21(i), hpHash21(i + vec2(1.0, 0.0)), u.x),\n"
    "               mix(hpHash21(i + vec2(0.0, 1.0)), hpHash21(i + vec2(1.0, 1.0)), u.x), u.y);\n"
    "}\n";

const MaterialNodePin kUvScaleInputs[] = {{"UV", Float2, "{uv}"}, {"Scale", Float, "1.0"}};
const MaterialNodePin kNoiseOutputs[] = {{"Value", Float, "hpValueNoise({0} * {1})"}};

const MaterialNodePin kUvInput[] = {{"UV", Float2, "{uv}"}};
const MaterialNodePin kGradientOutputs[] = {{"Linear", Float, "{0}.x"},
                                            {"Radial", Float, "length({0} - vec2(0.5)) * 2.0"}};

const MaterialNodePin kFresnelInputs[] = {{"Normal", Float3, "{normal}"}, {"Power", Float, "5.0"}};
const MaterialNodePin kFresnelOutputs[] = {
    {"Value", Float, "pow(1.0 - max(dot(normalize({0}), {viewDir}), 0.0), max({1}, 0.001))"}};

const MaterialNodePin kDistortionInputs[] = {
    {"UV", Float2, "{uv}"}, {"Offset", Float2, "vec2(0.0)"}, {"Strength", Float, "1.0"}};
const MaterialNodePin kUvOutput[] = {{"UV", Float2, "{0} + {1} * {2}"}};

const MaterialNodePin kPannerInputs[] = {
    {"UV", Float2, "{uv}"}, {"Speed", Float2, "vec2(0.1, 0.0)"}, {"Time", Float, "{time}"}};

const MaterialNodePin kTimeOutputs[] = {{"Seconds", Float, "{time}"}};

const MaterialNodePin kCameraVectorOutputs[] = {{"Vector", Float3, "{cameraPos} - {worldPos}"}};
const MaterialNodePin kViewDirectionOutputs[] = {{"Direction", Float3, "{viewDir}"}};
const MaterialNodePin kNormalInput[] = {{"Normal", Float3, "{normal}"}};
const MaterialNodePin kReflectionOutputs[] = {{"Vector", Float3, "reflect(-{viewDir}, normalize({0}))"}};
const MaterialNodePin kWorldPositionOutputs[] = {{"Position", Float3, "{worldPos}"}};
const MaterialNodePin kObjectPositionOutputs[] = {{"Position", Float3, "{objectPos}"}};
const MaterialNodePin kDistanceOutputs[] = {{"Distance", Float, "length({cameraPos} - {worldPos})"}};
const MaterialNodePin kScreenOutputs[] = {{"Position", Float4, "{screenPos}"}};

template <size_t InputCount, size_t OutputCount>
MaterialNodeDescriptor Node(const char* typeName, const char* displayName, const char* category, const char* tooltip,
                            const MaterialNodePin (&inputs)[InputCount], const MaterialNodePin (&outputs)[OutputCount],
                            const char* functions = nullptr) {
    MaterialNodeDescriptor node;
    node.typeName = typeName;
    node.displayName = displayName;
    node.category = category;
    node.tooltip = tooltip;
    node.inputs = inputs;
    node.inputCount = static_cast<std::uint32_t>(InputCount);
    node.outputs = outputs;
    node.outputCount = static_cast<std::uint32_t>(OutputCount);
    node.glslFunctions = functions;
    return node;
}

template <size_t OutputCount>
MaterialNodeDescriptor Node(const char* typeName, const char* displayName, const char* category, const char* tooltip,
                            const MaterialNodePin (&outputs)[OutputCount]) {
    MaterialNodeDescriptor node;
    node.typeName = typeName;
    node.displayName = displayName;
    node.category = category;
    node.tooltip = tooltip;
    node.outputs = outputs;
    node.outputCount = static_cast<std::uint32_t>(OutputCount);
    return node;
}

class MaterialNodesPlugin final : public IHephaestusPlugin {
public:
    bool OnLoad(IPluginContext& context) override {
        IMaterialNodeService* service = context.MaterialNodes();
        if (!service) {
            return false;
        }
        const MaterialNodeDescriptor nodes[] = {
            Node("Noise", "Noise", "Procedural", "Smooth value noise of UV times Scale, from 0 to 1.", kUvScaleInputs,
                 kNoiseOutputs, kNoiseFunctions),
            Node("Gradient", "Gradient", "Procedural", "A left-to-right and a radial gradient across the UVs.", kUvInput,
                 kGradientOutputs),
            Node("Fresnel", "Fresnel", "Procedural", "Bright where the surface turns away from the camera; for rims.",
                 kFresnelInputs, kFresnelOutputs),
            Node("UVDistortion", "UV Distortion", "Procedural", "Offsets UVs by Offset times Strength.", kDistortionInputs,
                 kUvOutput),
            Node("Panner", "Panner", "Procedural", "Scrolls UVs by Speed per second; for flowing water, lava and screens.",
                 kPannerInputs, kUvOutput),
            Node("Time", "Time", "Procedural", "Seconds since the editor or game started.", kTimeOutputs),
            Node("CameraVector", "Camera Vector", "Inputs", "From the surface to the camera, not normalized.",
                 kCameraVectorOutputs),
            Node("ViewDirection", "View Direction", "Inputs", "Normalized direction from the surface to the camera.",
                 kViewDirectionOutputs),
            Node("ReflectionVector", "Reflection Vector", "Inputs", "The view direction reflected about Normal.",
                 kNormalInput, kReflectionOutputs),
            Node("WorldPosition", "World Position", "Inputs", "The surface's position in the world, in metres.",
                 kWorldPositionOutputs),
            Node("ObjectPosition", "Object Position", "Inputs", "The world position of the object's origin.",
                 kObjectPositionOutputs),
            Node("DistanceToCamera", "Distance To Camera", "Inputs", "Metres from the surface to the camera.",
                 kDistanceOutputs),
            Node("ScreenPosition", "Screen Position", "Utility", "The pixel's position on screen (x, y in pixels).",
                 kScreenOutputs),
        };
        bool ok = true;
        for (const MaterialNodeDescriptor& node : nodes) {
            ok &= service->RegisterNode(node);
        }
        return ok;
    }
};

} // namespace

HP_DECLARE_PLUGIN(MaterialNodesPlugin)
