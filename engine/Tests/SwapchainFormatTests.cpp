#include "Gfx/VulkanSwapchain.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

VkSurfaceFormatKHR Fmt(VkFormat format, VkColorSpaceKHR space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
    return {format, space};
}

} // namespace

int main() {
    bool ok = true;

    {
        const auto chosen = ChooseSurfaceFormat({Fmt(VK_FORMAT_B8G8R8A8_SRGB), Fmt(VK_FORMAT_B8G8R8A8_UNORM)});
        ok &= Check(chosen.format == VK_FORMAT_B8G8R8A8_UNORM,
                    "UNORM is preferred even when the driver lists SRGB first");
    }
    {
        const auto chosen = ChooseSurfaceFormat({Fmt(VK_FORMAT_A2B10G10R10_UNORM_PACK32),
                                                 Fmt(VK_FORMAT_R8G8B8A8_UNORM)});
        ok &= Check(chosen.format == VK_FORMAT_R8G8B8A8_UNORM, "falls back to R8G8B8A8_UNORM");
    }
    {
        const auto chosen = ChooseSurfaceFormat({Fmt(VK_FORMAT_A2B10G10R10_UNORM_PACK32),
                                                 Fmt(VK_FORMAT_B8G8R8A8_SRGB)});
        ok &= Check(chosen.format == VK_FORMAT_B8G8R8A8_SRGB && IsSrgbFormat(chosen.format),
                    "uses an SRGB format when no UNORM format is offered");
    }
    {
        const auto chosen = ChooseSurfaceFormat({Fmt(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT),
                                                 Fmt(VK_FORMAT_A2B10G10R10_UNORM_PACK32)});
        ok &= Check(chosen.format == VK_FORMAT_B8G8R8A8_UNORM &&
                        chosen.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
                    "keeps the first entry when nothing uses the sRGB nonlinear color space");
    }
    {
        const auto chosen = ChooseSurfaceFormat({Fmt(VK_FORMAT_UNDEFINED)});
        ok &= Check(chosen.format == VK_FORMAT_B8G8R8A8_UNORM &&
                        chosen.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                    "a single UNDEFINED entry means any format is allowed");
    }
    {
        bool threw = false;
        try {
            ChooseSurfaceFormat({});
        } catch (const std::exception&) {
            threw = true;
        }
        ok &= Check(threw, "an empty format list is an error");
    }
    ok &= Check(!IsSrgbFormat(VK_FORMAT_B8G8R8A8_UNORM), "UNORM is not reported as SRGB");

    if (ok) {
        std::cout << "SwapchainFormatTests passed\n";
    }
    return ok ? 0 : 1;
}
