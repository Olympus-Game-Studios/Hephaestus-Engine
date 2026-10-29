#include "SkyIrradiance.h"

#include <cmath>

namespace Hephaestus {

namespace {

constexpr float kPi = 3.14159265358979f;

// The sun disc is ~0.5 degrees wide; HDRIs smear it into a glare halo a few degrees across.
constexpr float kSunExclusionRadians = 5.0f * kPi / 180.0f;

std::array<float, 9> Basis(float x, float y, float z)
{
    return {0.282095f,
            0.488603f * y,
            0.488603f * z,
            0.488603f * x,
            1.092548f * x * y,
            1.092548f * y * z,
            0.315392f * (3.0f * z * z - 1.0f),
            1.092548f * x * z,
            0.546274f * (x * x - y * y)};
}

// Ramamoorthi & Hanrahan clamped-cosine convolution per band.
constexpr float kBandScale[9] = {kPi,
                                 2.0f * kPi / 3.0f, 2.0f * kPi / 3.0f, 2.0f * kPi / 3.0f,
                                 kPi / 4.0f, kPi / 4.0f, kPi / 4.0f, kPi / 4.0f, kPi / 4.0f};

struct Texel {
    float x, y, z;
    float solidAngle;
};

Texel TexelAt(uint32_t col, uint32_t row, uint32_t width, uint32_t height)
{
    const float lat = ((static_cast<float>(row) + 0.5f) / static_cast<float>(height) - 0.5f) * kPi;
    const float phi = ((static_cast<float>(col) + 0.5f) / static_cast<float>(width) - 0.5f) * 2.0f * kPi;
    const float cosLat = std::cos(lat);
    const float solidAngle = (2.0f * kPi / static_cast<float>(width)) * (kPi / static_cast<float>(height)) * cosLat;
    return {cosLat * std::cos(phi), std::sin(lat), cosLat * std::sin(phi), solidAngle};
}

float Luminance(const float* texel)
{
    return 0.2126f * texel[0] + 0.7152f * texel[1] + 0.0722f * texel[2];
}

} // namespace

IrradianceSH ProjectEquirectIrradianceSH(const float* rgba, uint32_t width, uint32_t height)
{
    IrradianceSH sh{};
    if (!rgba || width == 0 || height == 0) {
        return sh;
    }

    size_t brightest = 0;
    for (size_t i = 1; i < static_cast<size_t>(width) * height; ++i) {
        if (Luminance(rgba + i * 4) > Luminance(rgba + brightest * 4)) {
            brightest = i;
        }
    }
    const Texel sun = TexelAt(static_cast<uint32_t>(brightest % width),
                              static_cast<uint32_t>(brightest / width), width, height);
    const float cosSunExclusion = std::cos(kSunExclusionRadians);

    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const Texel t = TexelAt(col, row, width, height);
            if (t.x * sun.x + t.y * sun.y + t.z * sun.z > cosSunExclusion) {
                continue;
            }
            const auto basis = Basis(t.x, t.y, t.z);
            const float* texel = rgba + (static_cast<size_t>(row) * width + col) * 4;
            for (int i = 0; i < 9; ++i) {
                const float w = basis[i] * t.solidAngle;
                sh[i][0] += texel[0] * w;
                sh[i][1] += texel[1] * w;
                sh[i][2] += texel[2] * w;
            }
        }
    }
    for (int i = 0; i < 9; ++i) {
        for (float& c : sh[i]) {
            c *= kBandScale[i];
        }
    }
    return sh;
}

std::array<float, 3> EvaluateIrradianceSH(const IrradianceSH& sh, float x, float y, float z)
{
    const auto basis = Basis(x, y, z);
    std::array<float, 3> e{};
    for (int i = 0; i < 9; ++i) {
        for (int c = 0; c < 3; ++c) {
            e[c] += sh[i][c] * basis[i];
        }
    }
    return e;
}

} // namespace Hephaestus
