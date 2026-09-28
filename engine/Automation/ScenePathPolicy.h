#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Hephaestus::Automation {

// Resolve a scene destination without allowing lexical traversal or symlink escape.
inline bool ResolveProjectScenePath(const std::filesystem::path& projectRoot,
                                    std::string_view destination,
                                    std::filesystem::path& absolute,
                                    std::string& error)
{
    namespace fs = std::filesystem;
    const fs::path relative = fs::u8path(destination);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.filename().empty()) {
        error = "Supply a project-relative scene file path";
        return false;
    }
    for (const auto& part : relative) {
        if (part == "..") {
            error = "Scene path must not traverse outside the project";
            return false;
        }
    }
    fs::path target = relative;
    if (target.extension() != ".hpscene") target += ".hpscene";
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(projectRoot, ec);
    if (ec) { error = "Cannot resolve project root"; return false; }
    absolute = fs::weakly_canonical(root / target, ec);
    if (ec || absolute == root) {
        error = "Scene destination must remain inside the project";
        return false;
    }
    const fs::path within = absolute.lexically_relative(root);
    if (within.empty() || *within.begin() == "..") {
        error = "Scene destination must remain inside the project";
        return false;
    }
    return true;
}
} // namespace Hephaestus::Automation
