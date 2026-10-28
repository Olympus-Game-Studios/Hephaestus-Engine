#include "Gfx/SkyIrradiance.h"

#include <cmath>
#include <iostream>
#include <vector>

using namespace Hephaestus;

namespace {

constexpr float kPi = 3.14159265358979f;

bool Near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "[FAIL] " << message << ": expected " << expected << ", got " << actual << '\n';
        return false;
    }
    return true;
}

// Rows are bottom-up, so the top half of the rows is the upper hemisphere.
std::vector<float> Equirect(uint32_t width, uint32_t height, bool upperOnly, float r, float g, float b) {
    std::vector<float> pixels(static_cast<size_t>(width) * height * 4, 0.0f);
    for (uint32_t row = 0; row < height; ++row) {
        if (upperOnly && row < height / 2) continue;
        for (uint32_t col = 0; col < width; ++col) {
            float* p = &pixels[(static_cast<size_t>(row) * width + col) * 4];
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 1.0f;
        }
    }
    return pixels;
}

} // namespace

int main() {
    bool ok = true;

    {
        const auto pixels = Equirect(256, 128, false, 1.0f, 0.5f, 2.0f);
        const auto sh = ProjectEquirectIrradianceSH(pixels.data(), 256, 128);
        const float dirs[][3] = {{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {0, 0, -1}, {0.577f, 0.577f, 0.577f}};
        // A uniform sky has no sun, but one 5 degree cap still goes; that is under 1% of the sphere.
        for (const auto& d : dirs) {
            const auto e = EvaluateIrradianceSH(sh, d[0], d[1], d[2]);
            ok &= Near(e[0], kPi * 1.0f, 0.03f, "uniform sky red irradiance is pi * radiance");
            ok &= Near(e[1], kPi * 0.5f, 0.015f, "uniform sky green irradiance is pi * radiance");
            ok &= Near(e[2], kPi * 2.0f, 0.06f, "uniform sky blue irradiance is pi * radiance");
        }
    }
    {
        // A sun texel 1e5 times brighter than the sky is left to the scene's directional light.
        auto pixels = Equirect(256, 128, false, 1.0f, 1.0f, 1.0f);
        float* sun = &pixels[(static_cast<size_t>(100) * 256 + 40) * 4];
        sun[0] = sun[1] = sun[2] = 1.0e5f;
        const auto sh = ProjectEquirectIrradianceSH(pixels.data(), 256, 128);
        const float dirs[][3] = {{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const auto& d : dirs) {
            ok &= Near(EvaluateIrradianceSH(sh, d[0], d[1], d[2])[0], kPi, 0.03f,
                       "the sun is excluded and the remaining sky stays uniform");
        }
    }
    {
        // Exact values for a lit upper hemisphere: pi facing up, pi/2 sideways, 0 facing down.
        // Order-2 SH approximates the clamped cosine, so the tolerance allows its known error.
        const auto pixels = Equirect(256, 128, true, 1.0f, 1.0f, 1.0f);
        const auto sh = ProjectEquirectIrradianceSH(pixels.data(), 256, 128);
        ok &= Near(EvaluateIrradianceSH(sh, 0, 1, 0)[0], kPi, 0.1f * kPi, "upper sky lights an up-facing normal");
        ok &= Near(EvaluateIrradianceSH(sh, 1, 0, 0)[0], kPi * 0.5f, 0.05f * kPi, "upper sky half-lights a side normal");
        ok &= Near(EvaluateIrradianceSH(sh, 0, -1, 0)[0], 0.0f, 0.1f * kPi, "upper sky barely lights a down-facing normal");
    }
    {
        const auto sh = ProjectEquirectIrradianceSH(nullptr, 0, 0);
        ok &= Near(EvaluateIrradianceSH(sh, 0, 1, 0)[0], 0.0f, 0.0f, "no pixels give zero irradiance");
    }

    if (ok) {
        std::cout << "SkyIrradianceTests passed\n";
    }
    return ok ? 0 : 1;
}
