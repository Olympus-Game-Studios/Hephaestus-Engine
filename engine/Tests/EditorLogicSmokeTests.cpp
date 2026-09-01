/**
 * @file EditorLogicSmokeTests.cpp
 * @brief Narrow headless editor-logic checks (no Qt Widgets / no Vulkan).
 */

#include "Assets/AssetPathPolicy.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

bool TestPortableKeySmoke() {
    bool ok = true;
    ok &= Check(Hephaestus::AssetPathPolicy::ToPortableKey(
                    {}, "F:/proj/.engine/meshes/Capsule.glb") ==
                    ".engine/meshes/Capsule.glb",
                "capsule remap");
    ok &= Check(Hephaestus::AssetPathPolicy::IsPortableKey("assets/textures/a.png"),
                "assets portable");
    ok &= Check(Hephaestus::AssetPathPolicy::KeysEqual("Assets/A.PNG", "assets/a.png"),
                "keys equal");
    return ok;
}

bool TestRecentListLogic(const fs::path& scratch) {
    // Mirrors ProjectContext recent-list prune/remove behaviour without linking the editor.
    bool ok = true;
    const fs::path a = scratch / "A" / "project.heproject";
    const fs::path b = scratch / "B" / "project.heproject";
    const fs::path missing = scratch / "Missing" / "project.heproject";
    fs::create_directories(a.parent_path());
    fs::create_directories(b.parent_path());
    {
        std::ofstream(a) << "{}";
        std::ofstream(b) << "{}";
    }

    std::vector<std::string> recent = {a.string(), b.string(), missing.string()};
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [](const std::string& e) {
                                    std::error_code ec;
                                    return !fs::exists(e, ec);
                                }),
                 recent.end());
    ok &= Check(recent.size() == 2, "missing pruned");

    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [&](const std::string& e) {
                                    std::error_code ec;
                                    return fs::exists(e, ec) && fs::equivalent(e, a, ec);
                                }),
                 recent.end());
    ok &= Check(recent.size() == 1, "remove one");
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: EditorLogicSmokeTests <repo_root>\n";
        return 2;
    }

    const fs::path root = fs::path(argv[1]);
    const fs::path scratch = root / "build" / "editor_logic_smoke_out";
    fs::remove_all(scratch);
    fs::create_directories(scratch);

    bool ok = true;
    ok &= TestPortableKeySmoke();
    ok &= TestRecentListLogic(scratch);

    if (!ok) {
        return 1;
    }
    std::cout << "EditorLogicSmokeTests passed.\n";
    return 0;
}
