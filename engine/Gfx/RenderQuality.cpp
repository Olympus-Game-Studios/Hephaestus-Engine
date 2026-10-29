#include "RenderQuality.h"

#include <cctype>
#include <fstream>
#include <string>

namespace Hephaestus {

const char* RenderQualityName(RenderQuality quality) {
    switch (quality) {
    case RenderQuality::Low: return "Low";
    case RenderQuality::Medium: return "Medium";
    case RenderQuality::High: return "High";
    case RenderQuality::Ultra: return "Ultra";
    }
    return "High";
}

std::optional<RenderQuality> ParseRenderQuality(std::string_view text) {
    std::string lower;
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (lower == "low") return RenderQuality::Low;
    if (lower == "medium") return RenderQuality::Medium;
    if (lower == "high") return RenderQuality::High;
    if (lower == "ultra") return RenderQuality::Ultra;
    return std::nullopt;
}

RenderQuality DefaultRenderQuality(bool rayTracingAvailable) {
    return rayTracingAvailable ? RenderQuality::High : RenderQuality::Medium;
}

RenderQualityProfile ResolveRenderQuality(RenderQuality requested, bool rayTracingAvailable) {
    RenderQualityProfile profile;
    profile.requested = requested;
    const bool wantsRayTracing = requested == RenderQuality::High || requested == RenderQuality::Ultra;
    profile.rayTracing = wantsRayTracing && rayTracingAvailable;
    profile.lighting = wantsRayTracing && !rayTracingAvailable ? RenderQuality::Medium : requested;
    profile.softSunShadows = requested != RenderQuality::Low;
    profile.localShadows = requested != RenderQuality::Low;
    return profile;
}

std::optional<RenderQuality> ReadRenderQualityFile(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        if (key == "quality") return ParseRenderQuality(line.substr(eq + 1));
    }
    return std::nullopt;
}

bool WriteRenderQualityFile(const std::filesystem::path& file, RenderQuality quality) {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    out << "[Graphics]\nquality = " << RenderQualityName(quality) << "\n";
    return static_cast<bool>(out);
}

} // namespace Hephaestus
