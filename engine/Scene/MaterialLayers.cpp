#include "MaterialLayersJson.h"

#include <array>
#include <utility>

namespace {

using Json = nlohmann::json;

template <typename Enum, std::size_t N>
bool EnumFrom(const Json& value, const std::array<const char*, N>& names, Enum& out) {
    if (!value.is_string()) {
        return false;
    }
    const std::string text = value.get<std::string>();
    for (std::size_t i = 0; i < N; ++i) {
        if (text == names[i]) {
            out = static_cast<Enum>(i);
            return true;
        }
    }
    return false;
}

template <typename Enum, std::size_t N>
std::string EnumName(Enum value, const std::array<const char*, N>& names) {
    const auto index = static_cast<std::size_t>(value);
    return index < N ? names[index] : names[0];
}

constexpr std::array<const char*, 3> kSources = {"texture", "lightmap", "white"};
constexpr std::array<const char*, 10> kFactors = {"zero", "one", "srcColor", "oneMinusSrcColor", "srcAlpha",
                                                  "oneMinusSrcAlpha", "dstColor", "oneMinusDstColor", "dstAlpha",
                                                  "oneMinusDstAlpha"};
constexpr std::array<const char*, 4> kAlphaTests = {"none", "greater0", "less128", "greaterEqual128"};
constexpr std::array<const char*, 4> kColors = {"white", "vertex", "wave", "constant"};
constexpr std::array<const char*, 6> kWaves = {"none", "sin", "triangle", "square", "sawtooth", "inverseSawtooth"};

bool Floats(const Json& value, float* out, std::size_t count) {
    if (!value.is_array() || value.size() != count) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!value[i].is_number()) {
            return false;
        }
        out[i] = value[i].get<float>();
    }
    return true;
}

bool ParseWave(const Json& json, MaterialWave& wave) {
    if (!json.is_object() || !EnumFrom(json.value("function", Json("none")), kWaves, wave.function)) {
        return false;
    }
    wave.base = json.value("base", 1.0f);
    wave.amplitude = json.value("amplitude", 0.0f);
    wave.phase = json.value("phase", 0.0f);
    wave.frequency = json.value("frequency", 0.0f);
    return true;
}

Json WaveToJson(const MaterialWave& wave) {
    return {{"function", EnumName(wave.function, kWaves)},
            {"base", wave.base},
            {"amplitude", wave.amplitude},
            {"phase", wave.phase},
            {"frequency", wave.frequency}};
}

bool ParseLayer(const Json& json, MaterialLayer& layer, std::string& error) {
    if (!json.is_object()) {
        error = "a layer is not an object";
        return false;
    }
    const auto bad = [&error](const char* what) {
        error = std::string("a layer has a bad \"") + what + "\"";
        return false;
    };
    if (json.contains("source") && !EnumFrom(json["source"], kSources, layer.source)) return bad("source");
    if (json.contains("frames")) {
        const Json& frames = json["frames"];
        if (!frames.is_array() || frames.empty() || frames.size() > MaterialLayer::kMaxFrames) return bad("frames");
        for (const Json& frame : frames) {
            if (!frame.is_string()) return bad("frames");
            layer.frames.push_back(frame.get<std::string>());
        }
    }
    if (layer.source == MaterialLayer::Source::Texture && layer.frames.empty()) {
        error = "a texture layer has no frames";
        return false;
    }
    layer.frameRate = json.value("frameRate", 0.0f);
    layer.clamp = json.value("clamp", false);
    if (json.contains("src") && !EnumFrom(json["src"], kFactors, layer.src)) return bad("src");
    if (json.contains("dst") && !EnumFrom(json["dst"], kFactors, layer.dst)) return bad("dst");
    if (json.contains("alphaTest") && !EnumFrom(json["alphaTest"], kAlphaTests, layer.alphaTest)) return bad("alphaTest");
    if (json.contains("color") && !EnumFrom(json["color"], kColors, layer.color)) return bad("color");
    if (json.contains("colorWave") && !ParseWave(json["colorWave"], layer.colorWave)) return bad("colorWave");
    if (json.contains("constantColor") && !Floats(json["constantColor"], layer.constantColor, 3)) return bad("constantColor");
    if (json.contains("alphaWave") && !ParseWave(json["alphaWave"], layer.alphaWave)) return bad("alphaWave");
    if (json.contains("turbulence")) {
        float turbulence[3];
        if (!Floats(json["turbulence"], turbulence, 3)) return bad("turbulence");
        layer.turbulenceAmplitude = turbulence[0];
        layer.turbulenceFrequency = turbulence[1];
        layer.turbulencePhase = turbulence[2];
    }
    if (json.contains("scale") && !Floats(json["scale"], layer.scale, 2)) return bad("scale");
    layer.rotation = json.value("rotation", 0.0f);
    if (json.contains("scroll") && !Floats(json["scroll"], layer.scroll, 2)) return bad("scroll");
    return true;
}

} // namespace

bool ParseMaterialLayers(const Json& layersJson, std::vector<MaterialLayer>& layers, std::string& error) {
    layers.clear();
    if (!layersJson.is_array()) {
        error = "\"layers\" is not an array";
        return false;
    }
    if (layersJson.size() > kMaxMaterialLayers) {
        error = "a material has at most " + std::to_string(kMaxMaterialLayers) + " layers";
        return false;
    }
    for (const Json& json : layersJson) {
        MaterialLayer layer;
        if (!ParseLayer(json, layer, error)) {
            return false;
        }
        layers.push_back(std::move(layer));
    }
    return true;
}

Json MaterialLayersToJson(const std::vector<MaterialLayer>& layers) {
    Json out = Json::array();
    for (const MaterialLayer& layer : layers) {
        Json json = {{"source", EnumName(layer.source, kSources)},
                     {"src", EnumName(layer.src, kFactors)},
                     {"dst", EnumName(layer.dst, kFactors)},
                     {"alphaTest", EnumName(layer.alphaTest, kAlphaTests)},
                     {"color", EnumName(layer.color, kColors)},
                     {"colorWave", WaveToJson(layer.colorWave)},
                     {"constantColor", {layer.constantColor[0], layer.constantColor[1], layer.constantColor[2]}},
                     {"alphaWave", WaveToJson(layer.alphaWave)},
                     {"turbulence", {layer.turbulenceAmplitude, layer.turbulenceFrequency, layer.turbulencePhase}},
                     {"scale", {layer.scale[0], layer.scale[1]}},
                     {"rotation", layer.rotation},
                     {"scroll", {layer.scroll[0], layer.scroll[1]}},
                     {"clamp", layer.clamp}};
        if (!layer.frames.empty()) {
            json["frames"] = layer.frames;
            json["frameRate"] = layer.frameRate;
        }
        out.push_back(std::move(json));
    }
    return out;
}
