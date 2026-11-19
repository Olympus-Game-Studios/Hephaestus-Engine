#include "Scene/MaterialLayersJson.h"

#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string& message)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << "\n";
    return condition;
}

bool Rejects(const char* text, const std::string& expected)
{
    std::vector<MaterialLayer> layers;
    std::string error;
    const bool rejected = !ParseMaterialLayers(nlohmann::json::parse(text), layers, error) &&
                          error.find(expected) != std::string::npos;
    if (!rejected) std::cerr << "  error was: " << error << "\n";
    return rejected;
}

} // namespace

int main()
{
    bool pass = true;
    const auto json = nlohmann::json::parse(R"([
        {"frames": ["textures/panel.tga"], "scroll": [0.5, 0], "rotation": 30, "turbulence": [0.1, 0.5, 0.25],
         "scale": [2, 2], "clamp": true, "alphaTest": "greaterEqual128"},
        {"source": "lightmap", "src": "dstColor", "dst": "zero"},
        {"frames": ["a.tga", "b.tga", "c.tga"], "frameRate": 4, "src": "one", "dst": "one",
         "color": "wave", "colorWave": {"function": "sin", "base": 0.5, "amplitude": 0.5, "phase": 0, "frequency": 1},
         "alphaWave": {"function": "square", "base": 0, "amplitude": 1, "phase": 0.25, "frequency": 2}},
        {"source": "white", "color": "constant", "constantColor": [0.25, 0.5, 1]}
    ])");
    std::vector<MaterialLayer> layers;
    std::string error;
    pass &= Check(ParseMaterialLayers(json, layers, error) && layers.size() == 4, "four layers parse: " + error);
    if (layers.size() == 4) {
        pass &= Check(layers[0].frames.size() == 1 && layers[0].scroll[0] == 0.5f && layers[0].rotation == 30.0f &&
                          layers[0].turbulenceFrequency == 0.5f && layers[0].scale[1] == 2.0f && layers[0].clamp &&
                          layers[0].alphaTest == MaterialLayer::AlphaTest::GreaterEqual128 &&
                          layers[0].src == MaterialLayer::Factor::One && layers[0].dst == MaterialLayer::Factor::Zero,
                      "a texture layer keeps its UV changes, alpha test and default blend");
        pass &= Check(layers[1].source == MaterialLayer::Source::Lightmap && layers[1].frames.empty() &&
                          layers[1].src == MaterialLayer::Factor::DstColor,
                      "a lightmap layer needs no frames and keeps its blend");
        pass &= Check(layers[2].frames.size() == 3 && layers[2].frameRate == 4.0f &&
                          layers[2].colorWave.function == MaterialWave::Function::Sin &&
                          layers[2].alphaWave.function == MaterialWave::Function::Square && layers[2].alphaWave.phase == 0.25f,
                      "animated frames and colour and alpha waves are read");
        pass &= Check(layers[3].color == MaterialLayer::ColorSource::Constant && layers[3].constantColor[2] == 1.0f,
                      "a constant colour is read");

        std::vector<MaterialLayer> again;
        pass &= Check(ParseMaterialLayers(MaterialLayersToJson(layers), again, error) && again.size() == 4 &&
                          MaterialLayersToJson(again) == MaterialLayersToJson(layers),
                      "layers survive a write and read");
    }

    pass &= Check(Rejects(R"([{"scroll": [1, 0]}])", "no frames"), "a texture layer without frames is rejected");
    pass &= Check(Rejects(R"([{"frames": ["a.tga"], "src": "glOne"}])", "\"src\""), "an unknown blend factor is rejected");
    pass &= Check(Rejects(R"([{"source":"white"},{"source":"white"},{"source":"white"},{"source":"white"},{"source":"white"}])",
                          "at most 4"),
                  "more than four layers are rejected");
    pass &= Check(Rejects(R"({"frames": ["a.tga"]})", "not an array"), "layers must be an array");

    std::cout << (pass ? "[PASS] MaterialLayersTests\n" : "[FAIL] MaterialLayersTests\n");
    return pass ? 0 : 1;
}
