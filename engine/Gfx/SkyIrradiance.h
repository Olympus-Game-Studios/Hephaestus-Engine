#pragma once

#include <array>
#include <cstdint>

namespace Hephaestus {

// Order-2 spherical harmonics of the irradiance (cosine-convolved radiance), RGB per coefficient.
using IrradianceSH = std::array<std::array<float, 3>, 9>;

// rgba: bottom-up rows of an equirect map, sampled as in triangle.frag:sampleEnvironmentReflection.
IrradianceSH ProjectEquirectIrradianceSH(const float* rgba, uint32_t width, uint32_t height);

// Same formula as triangle.frag:evaluateSkyIrradiance; (x, y, z) must be unit length.
std::array<float, 3> EvaluateIrradianceSH(const IrradianceSH& sh, float x, float y, float z);

} // namespace Hephaestus
