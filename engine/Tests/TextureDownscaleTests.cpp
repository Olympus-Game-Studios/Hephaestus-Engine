#include "Gfx/VulkanTexture.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

bool TestExtent()
{
    bool ok = true;
    uint32_t w = 0;
    uint32_t h = 0;
    CappedTextureExtent(4096, 4096, 2048, w, h);
    ok &= Check(w == 2048 && h == 2048, "4096 square capped at 2048");
    CappedTextureExtent(4096, 4096, 1024, w, h);
    ok &= Check(w == 1024 && h == 1024, "4096 square capped at 1024");
    CappedTextureExtent(4096, 1024, 2048, w, h);
    ok &= Check(w == 2048 && h == 512, "the longer side decides and the aspect stays");
    CappedTextureExtent(2048, 2048, 2048, w, h);
    ok &= Check(w == 2048 && h == 2048, "a texture at the cap keeps its size");
    CappedTextureExtent(4096, 4096, 0, w, h);
    ok &= Check(w == 4096 && h == 4096, "0 means no cap");
    CappedTextureExtent(3000, 3000, 2048, w, h);
    ok &= Check(w == 1500 && h == 1500, "non power of two halves once");
    return ok;
}

bool TestPixels()
{
    bool ok = true;
    // 4x2 RGBA: left 2x2 block averages to (10, 20, 30, 255), right block to (101, 0, 255, 128).
    const std::vector<unsigned char> pixels = {
        0, 10, 20, 255,   20, 30, 40, 255,   100, 0, 255, 128,   102, 0, 255, 128,
        10, 20, 30, 255,  10, 20, 30, 255,   101, 0, 255, 128,   101, 0, 255, 128,
    };
    std::vector<unsigned char> out;
    uint32_t w = 0;
    uint32_t h = 0;
    ok &= Check(DownscaleRgba8ToFit(pixels.data(), 4, 2, 2, out, w, h), "4x2 above a cap of 2 is downscaled");
    ok &= Check(w == 2 && h == 1 && out.size() == 8, "4x2 halves to 2x1");
    ok &= Check(out[0] == 10 && out[1] == 20 && out[2] == 30 && out[3] == 255, "left block is box filtered");
    ok &= Check(out[4] == 101 && out[5] == 0 && out[6] == 255 && out[7] == 128, "right block is box filtered");
    out.clear();
    ok &= Check(!DownscaleRgba8ToFit(pixels.data(), 4, 2, 4, out, w, h) && out.empty(),
                "an image within the cap is left alone");
    std::vector<unsigned char> big(16 * 16 * 4, 200);
    ok &= Check(DownscaleRgba8ToFit(big.data(), 16, 16, 4, out, w, h) && w == 4 && h == 4 && out.size() == 64,
                "16x16 capped at 4 halves twice");
    ok &= Check(out[0] == 200 && out[63] == 200, "a flat image stays flat");
    return ok;
}

} // namespace

int main()
{
    bool ok = TestExtent();
    ok &= TestPixels();
    if (!ok) {
        return 1;
    }
    std::cout << "TextureDownscaleTests passed\n";
    return 0;
}
