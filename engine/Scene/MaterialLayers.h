#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// A periodic value: base + amplitude * f(frequency * time + phase).
struct MaterialWave {
    enum class Function : uint32_t { None = 0, Sin, Triangle, Square, Sawtooth, InverseSawtooth };
    Function function = Function::None;
    float base = 1.0f;
    float amplitude = 0.0f;
    float phase = 0.0f;
    float frequency = 0.0f;
};

/// One texture layer of a layered material. Layers are drawn in order on the
/// same surface; each blends onto the layers before it.
struct MaterialLayer {
    enum class Source : uint32_t { Texture = 0, Lightmap, White };
    /// Blend factors, as in OpenGL glBlendFunc.
    enum class Factor : uint32_t {
        Zero = 0, One, SrcColor, OneMinusSrcColor, SrcAlpha, OneMinusSrcAlpha, DstColor, OneMinusDstColor,
        DstAlpha, OneMinusDstAlpha
    };
    enum class AlphaTest : uint32_t { None = 0, Greater0, Less128, GreaterEqual128 };
    enum class ColorSource : uint32_t { White = 0, Vertex, Wave, Constant };

    Source source = Source::Texture;
    /// The texture, or several frames cycled at frameRate frames a second (up to kMaxFrames).
    std::vector<std::string> frames;
    float frameRate = 0.0f;
    bool clamp = false;
    /// Ignored on the first layer, which writes its colour as is.
    Factor src = Factor::One;
    Factor dst = Factor::Zero;
    AlphaTest alphaTest = AlphaTest::None;
    ColorSource color = ColorSource::White;
    MaterialWave colorWave;
    float constantColor[3] = {1.0f, 1.0f, 1.0f};
    MaterialWave alphaWave; // function None keeps the texture's alpha

    // UV changes, applied in this order: turbulence, scale, rotation (degrees a second about 0.5, 0.5), scroll.
    float turbulenceAmplitude = 0.0f;
    float turbulenceFrequency = 0.0f;
    float turbulencePhase = 0.0f;
    float scale[2] = {1.0f, 1.0f};
    float rotation = 0.0f;
    float scroll[2] = {0.0f, 0.0f};

    static constexpr std::size_t kMaxFrames = 8;
};

inline constexpr std::size_t kMaxMaterialLayers = 4;
