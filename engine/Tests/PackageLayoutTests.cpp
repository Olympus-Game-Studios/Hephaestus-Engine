#include "Build/PackageStager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using namespace Hephaestus::Build;

namespace {

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "hephaestus_package_layout_tests";
    fs::remove_all(root);
    const fs::path project = root / "project";
    const fs::path engine = root / "engine";
    const fs::path runtime = root / "runtime";
    const fs::path outputRoot = root / "output";

    Write(project / "assets" / "scenes" / "Main.hpscene", "{}");
    Write(project / "assets" / "meshes" / "ship.glb", "mesh");
    Write(project / "assets" / "materials" / "ship.hematerial", "{}");
    Write(project / "assets" / "textures" / "ship.png", "texture");
    Write(project / "assets" / "prefabs" / "Ship.hpprefab", "{}");
    Write(project / "assets" / "ui" / "Hud.hui", "{}");
    Write(project / "assets" / "audio" / "engine.wav", "audio");
    Write(project / "scripts" / "boot.lua", "return true");
    Write(project / ".engine" / "meshes" / "project.mesh", "mesh");
    Write(project / ".hephaestus" / "private.json", "must not ship");
    Write(engine / ".engine" / "meshes" / "builtin.mesh", "mesh");
    Write(engine / "LICENSE", "Engine license");
    Write(runtime / "HephaestusGame.exe", "fake executable");
    Write(runtime / "lua.dll", "fake lua");
    Write(runtime / "shaders" / "basic.vert.spv", "fake shader");

    BuildRequest request;
    request.projectRoot = project;
    request.engineRoot = engine;
    request.outputRoot = outputRoot;
    request.productName = "Tiny Game!";
    request.version = "1.2.3";
    request.engineVersion = "2026.9.0";
    request.startupScene = "assets/scenes/Main.hpscene";

    RuntimeArtifacts artifacts{
        runtime / "HephaestusGame.exe",
        runtime / "shaders",
        runtime / "lua.dll"
    };
    PackageStager stager;
    auto result = stager.stage(request, artifacts);
    const fs::path package = outputRoot / "Tiny_Game";
    bool ok = true;
    ok &= Check(result.success, "initial package succeeds");
    ok &= Check(fs::exists(package / "Tiny_Game.exe"), "runtime is renamed");
    ok &= Check(fs::exists(package / "lua54.dll"), "Lua is normalized to lua54.dll");
    ok &= Check(fs::exists(package / "assets" / "scenes" / "Main.hpscene"),
                "assets are packaged");
    ok &= Check(fs::exists(package / "assets" / "prefabs" / "Ship.hpprefab"),
                "prefabs are packaged");
    ok &= Check(fs::exists(package / "assets" / "ui" / "Hud.hui"),
                "HUI documents are packaged");
    ok &= Check(fs::exists(package / "assets" / "audio" / "engine.wav"),
                "audio is packaged");
    ok &= Check(fs::exists(package / "scripts" / "boot.lua"), "scripts are packaged");
    ok &= Check(fs::exists(package / ".engine" / "meshes" / "builtin.mesh"),
                "engine meshes are packaged");
    ok &= Check(fs::exists(package / ".engine" / "meshes" / "project.mesh"),
                "project meshes are packaged");
    ok &= Check(!fs::exists(package / ".hephaestus"), ".hephaestus is excluded");
    ok &= Check(fs::exists(package / "build_manifest.json"), "JSON manifest exists");
    ok &= Check(fs::exists(package / "export_manifest.txt"), "export manifest exists");
    ok &= Check(fs::exists(package / "ThirdPartyNotices.txt"), "license notices exist");
    {
        std::ifstream manifestStream(package / "build_manifest.json");
        nlohmann::json manifest;
        manifestStream >> manifest;
        ok &= Check(manifest.value("engineVersion", "") == "2026.9.0",
                    "manifest records engine version");
        ok &= Check(manifest.value("platform", "") == "windows-x64",
                    "manifest records platform");
    }

    Write(package / "preserve.marker", "previous output");
    RuntimeArtifacts broken = artifacts;
    broken.executable = runtime / "missing.exe";
    result = stager.stage(request, broken);
    ok &= Check(!result.success, "failed staging reports failure");
    ok &= Check(fs::exists(package / "preserve.marker"),
                "failed staging preserves previous output");

    fs::remove_all(root);
    if (ok) {
        std::cout << "Package layout tests passed.\n";
    }
    return ok ? 0 : 1;
}
