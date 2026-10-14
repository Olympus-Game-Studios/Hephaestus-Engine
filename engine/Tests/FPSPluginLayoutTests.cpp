#include "Plugins/PluginManifest.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using Hephaestus::Plugins::ParsePluginManifest;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

std::string ReadAll(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: FPSPluginLayoutTests <repo_root>\n";
        return 2;
    }
    const fs::path root = argv[1];
    const fs::path plugin = root / "Plugins" / "HephaestusFPS";
    bool ok = true;
    ok &= Check(fs::exists(plugin / "HephaestusFPS.hpplugin"), "manifest exists");
    ok &= Check(fs::exists(plugin / "Content" / "Scripts" / "FPS" / "Player.lua"), "player script exists");
    ok &= Check(fs::exists(plugin / "Content" / "Prefabs" / "FPSPlayer.hpprefab"), "player prefab exists");
    ok &= Check(fs::exists(plugin / "Content" / "Maps" / "FPSVerification.hpscene"), "verification scene exists");
    ok &= Check(fs::exists(plugin / "Content" / "UI" / "FPSHud.hui"), "HUD document exists");
    ok &= Check(fs::exists(plugin / "Documentation" / "FPS-Plugin.md"), "docs exist");

    const auto parsed = ParsePluginManifest(plugin / "HephaestusFPS.hpplugin");
    ok &= Check(parsed.success, "FPS manifest parses");
    ok &= Check(parsed.manifest.id == "com.hephaestus.fps", "id is com.hephaestus.fps");
    ok &= Check(parsed.manifest.type == Hephaestus::Plugins::PluginType::Editor, "type is editor");
    ok &= Check(!parsed.manifest.defaultEnabled, "defaultEnabled is false");
    ok &= Check(fs::exists(plugin / parsed.manifest.content), "content directory exists");

    const std::string prefabIo = ReadAll(root / "engine" / "Scene" / "PrefabComponentIO.cpp");
    const std::string sceneIo = ReadAll(root / "engine" / "Scene" / "SceneSerializer.cpp");
    ok &= Check(prefabIo.find("FPSPlayer") == std::string::npos &&
                    prefabIo.find("FPSWeapon") == std::string::npos,
                "PrefabComponentIO has no FPS type names");
    ok &= Check(sceneIo.find("FPSPlayer") == std::string::npos &&
                    sceneIo.find("FPSWeapon") == std::string::npos,
                "SceneSerializer has no FPS type names");

    if (!ok) {
        return 1;
    }
    std::cout << "FPSPluginLayoutTests passed\n";
    return 0;
}
