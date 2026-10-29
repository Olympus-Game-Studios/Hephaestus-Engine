#include "Gfx/RenderQuality.h"

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace Hephaestus;

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

} // namespace

int main() {
    bool pass = true;

    for (RenderQuality q : {RenderQuality::Low, RenderQuality::Medium, RenderQuality::High, RenderQuality::Ultra}) {
        pass &= Check(ParseRenderQuality(RenderQualityName(q)) == q, "every tier name parses back to its tier");
    }
    pass &= Check(ParseRenderQuality(" ultra ") == RenderQuality::Ultra && ParseRenderQuality("MEDIUM") == RenderQuality::Medium,
                  "parsing ignores case and spaces");
    pass &= Check(!ParseRenderQuality("Epic") && !ParseRenderQuality(""), "unknown names are rejected");

    pass &= Check(DefaultRenderQuality(true) == RenderQuality::High && DefaultRenderQuality(false) == RenderQuality::Medium,
                  "ray-tracing GPUs default to High, others to Medium");

    const RenderQualityProfile high = ResolveRenderQuality(RenderQuality::High, true);
    pass &= Check(high.rayTracing && high.lighting == RenderQuality::High && high.softSunShadows && high.localShadows,
                  "High on a ray-tracing GPU traces");
    const RenderQualityProfile ultraNoRt = ResolveRenderQuality(RenderQuality::Ultra, false);
    pass &= Check(!ultraNoRt.rayTracing && ultraNoRt.lighting == RenderQuality::Medium &&
                      ultraNoRt.requested == RenderQuality::Ultra,
                  "Ultra without ray tracing lights as Medium and remembers what was asked for");
    const RenderQualityProfile medium = ResolveRenderQuality(RenderQuality::Medium, true);
    pass &= Check(!medium.rayTracing && medium.softSunShadows && medium.localShadows,
                  "Medium uses baked GI and shadow maps even on a ray-tracing GPU");
    const RenderQualityProfile low = ResolveRenderQuality(RenderQuality::Low, true);
    pass &= Check(!low.rayTracing && !low.softSunShadows && !low.localShadows,
                  "Low drops soft sun shadows and point and spot shadows");

    {
        const auto file = std::filesystem::temp_directory_path() / "hephaestus_quality_test" / "graphics.ini";
        std::filesystem::remove_all(file.parent_path());
        pass &= Check(!ReadRenderQualityFile(file), "a missing settings file gives no saved tier");
        pass &= Check(WriteRenderQualityFile(file, RenderQuality::Ultra) &&
                          ReadRenderQualityFile(file) == RenderQuality::Ultra,
                      "a saved tier reads back, creating its folder");
        {
            std::ofstream edited(file, std::ios::trunc);
            edited << "[Graphics]\nqualityHint = Ultra\n quality=low \r\n";
        }
        pass &= Check(ReadRenderQualityFile(file) == RenderQuality::Low,
                      "only the quality key counts, whatever the spacing and case");
        std::filesystem::remove_all(file.parent_path());
    }

    if (pass) {
        std::cout << "RenderQualityTests passed\n";
    }
    return pass ? 0 : 1;
}
