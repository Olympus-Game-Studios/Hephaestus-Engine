#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Hephaestus {

// One visual quality tier chooses the whole lighting path.
enum class RenderQuality { Low, Medium, High, Ultra };

// What a tier turns on once the GPU's ray tracing support is known.
struct RenderQualityProfile {
    RenderQuality requested = RenderQuality::High;
    RenderQuality lighting = RenderQuality::High;  // what GI, reflections and traced shadows actually run as
    bool rayTracing = false;           // ray-traced GI, reflections and per-light ray-traced shadows
    bool softSunShadows = true;        // PCSS from the sun's angular size
    bool localShadows = true;          // shadow maps for point and spot lights
};

const char* RenderQualityName(RenderQuality quality);
// Case-insensitive "Low", "Medium", "High" or "Ultra".
std::optional<RenderQuality> ParseRenderQuality(std::string_view text);
// High on GPUs with hardware ray tracing, Medium otherwise.
RenderQuality DefaultRenderQuality(bool rayTracingAvailable);
// High and Ultra run as Medium for lighting on GPUs without hardware ray tracing.
RenderQualityProfile ResolveRenderQuality(RenderQuality requested, bool rayTracingAvailable);

// A player's saved tier: an ini file with "quality = <name>" (the packaged game's graphics.ini).
std::optional<RenderQuality> ReadRenderQualityFile(const std::filesystem::path& file);
bool WriteRenderQualityFile(const std::filesystem::path& file, RenderQuality quality);

} // namespace Hephaestus
