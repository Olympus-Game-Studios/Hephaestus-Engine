#include "Build/PackageStager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

bool HasProblem(const std::vector<EditorProblem>& problems, const std::string& code) {
    for (const auto& problem : problems) {
        if (problem.code == code) {
            return true;
        }
    }
    return false;
}

bool HasStage(const std::vector<std::string>& stages, const std::string& prefix) {
    return std::any_of(stages.begin(), stages.end(), [&](const std::string& stage) {
        return stage.rfind(prefix, 0) == 0;
    });
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
    Write(project / "assets" / "characters" / "Cultist.hpcharacter", "{}");
    Write(project / "assets" / "characters" / ".source" / "Cultist" / "mesh.glb", "mesh");
    Write(project / "assets" / "characters" / ".source" / "Cultist" / "CultistCharacter.lua",
          "function onUpdate(dt) end");
    Write(project / "assets" / "ui" / "Hud.hui", "{}");
    Write(project / "assets" / "audio" / "engine.wav", "audio");
    Write(project / "assets" / "input" / "Default.hpinput",
          R"({"version":1,"actions":[],"axes":[]})");
    Write(project / "scripts" / "boot.lua", "return true");
    Write(project / ".engine" / "meshes" / "project.mesh", "mesh");
    Write(project / ".engine" / "hdris" / "project.hdr", "project hdri");
    Write(project / ".hephaestus" / "private.json", "must not ship");
    Write(project / ".hephaestus" / "physics_layers.json", "{\"version\":1}");
    Write(engine / ".engine" / "meshes" / "builtin.mesh", "mesh");
    Write(engine / ".engine" / "hdris" / "builtin.hdr", "builtin hdri");
    Write(engine / "LICENSE", "Engine license");
    Write(runtime / "HephaestusGame.exe", "fake executable");
    Write(runtime / "lua.dll", "fake lua");
    Write(runtime / "shaders" / "basic.vert.spv", "fake shader");
    const std::string pluginManifest =
        R"({"version":1,"apiVersion":1,"id":"ID","name":"Test","author":"Tests",)"
        R"("pluginVersion":"1.0.0","engineVersion":">=2026.9.0","type":"editor",)"
        R"("entry":"bin/HephaestusFPS.dll","content":"Content"})";
    auto writePlugin = [&](const fs::path& root, const std::string& id, bool enabledDefault,
                           bool withDll) {
        std::string text = pluginManifest;
        const auto pos = text.find("\"id\":\"ID\"");
        text.replace(pos, 9, "\"id\":\"" + id + "\"");
        if (!enabledDefault) {
            const auto typePos = text.find("\"type\":\"editor\"");
            text.insert(typePos, "\"defaultEnabled\":false,");
        }
        Write(root / "Plugins" / id / (id + ".hpplugin"), text);
        Write(root / "Plugins" / id / "Content" / "Scripts" / "Boot.lua", "return true");
        Write(root / "Plugins" / id / "Content" / "UI" / "Hud.hui", "{}");
        if (withDll) {
            Write(root / "Plugins" / id / "bin" / "HephaestusFPS.dll", "dll");
            Write(root / "Plugins" / id / "bin" / "Qt6Core.dll", "qt");
        }
    };
    writePlugin(engine, "com.engine.content", true, true);
    writePlugin(project, "com.project.content", true, true);
    writePlugin(engine, "com.disabled.content", false, true);

    BuildRequest request;
    request.projectRoot = project;
    request.engineRoot = engine;
    request.outputRoot = outputRoot;
    request.productName = "Tiny Game!";
    request.version = "1.2.3";
    request.engineVersion = "2026.9.0";
    request.startupScene = "assets/scenes/Main.hpscene";
    std::vector<std::string> stages;
    request.onProgress = [&](float, const std::string& stage) {
        stages.push_back(stage);
    };

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
    ok &= Check(fs::exists(package / "assets" / "characters" / "Cultist.hpcharacter"),
                "character assets are packaged");
    ok &= Check(fs::exists(package / "assets" / "characters" / ".source" / "Cultist" / "mesh.glb"),
                "owned character sources are packaged");
    ok &= Check(fs::exists(package / "assets" / "characters" / ".source" / "Cultist" /
                           "CultistCharacter.lua"),
                "owned character controller scripts are packaged");
    ok &= Check(fs::exists(package / "assets" / "ui" / "Hud.hui"),
                "HUI documents are packaged");
    ok &= Check(fs::exists(package / "assets" / "audio" / "engine.wav"),
                "audio is packaged");
    ok &= Check(fs::exists(package / "assets" / "input" / "Default.hpinput"),
                "input map is packaged");
    ok &= Check(fs::exists(package / "scripts" / "boot.lua"), "scripts are packaged");
    ok &= Check(fs::exists(package / "Plugins" / "com.engine.content" / "Content" / "Scripts" /
                           "Boot.lua"),
                "enabled engine plugin content is staged");
    ok &= Check(fs::exists(package / "Plugins" / "com.project.content" / "Content" / "UI" / "Hud.hui"),
                "enabled project plugin content is staged");
    ok &= Check(!fs::exists(package / "Plugins" / "com.disabled.content"),
                "disabled plugin content is omitted");
    ok &= Check(!fs::exists(package / "Plugins" / "com.engine.content" / "bin" / "HephaestusFPS.dll") &&
                    !fs::exists(package / "Plugins" / "com.engine.content" / "Content" / "bin"),
                "plugin entry DLLs are omitted");
    ok &= Check(!fs::exists(package / "Plugins" / "com.engine.content" / "bin" / "Qt6Core.dll") &&
                    !fs::exists(package / "Qt6Core.dll"),
                "Qt is not packaged from plugin content");
    {
        std::ifstream mountsStream(package / "Plugins" / "content-mounts.json");
        nlohmann::json mounts;
        mountsStream >> mounts;
        bool engineMount = false;
        bool projectMount = false;
        bool disabledMount = false;
        for (const auto& plugin : mounts.value("plugins", nlohmann::json::array())) {
            const std::string id = plugin.value("id", "");
            const std::string rootPath = plugin.value("root", "");
            if (id == "com.engine.content") {
                engineMount = rootPath == "Plugins/com.engine.content/Content";
            }
            if (id == "com.project.content") {
                projectMount = rootPath == "Plugins/com.project.content/Content";
            }
            if (id == "com.disabled.content") {
                disabledMount = true;
            }
        }
        ok &= Check(engineMount && projectMount && !disabledMount,
                    "each enabled plugin id mounts its own content root");
    }
    ok &= Check(fs::exists(package / ".engine" / "meshes" / "builtin.mesh"),
                "engine meshes are packaged");
    ok &= Check(fs::exists(package / ".engine" / "meshes" / "project.mesh"),
                "project meshes are packaged");
    ok &= Check(fs::exists(package / ".engine" / "hdris" / "project.hdr"),
                "project HDRIs are packaged");
    ok &= Check(fs::exists(package / ".engine" / "hdris" / "builtin.hdr"),
                "builtin HDRIs are packaged");
    ok &= Check(fs::exists(package / ".hephaestus" / "physics_layers.json"),
                "physics_layers.json is packaged");
    ok &= Check(!fs::exists(package / ".hephaestus" / "private.json"),
                "other .hephaestus editor state is excluded");
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
    {
        std::ifstream iniStream(package / "game.ini");
        const std::string ini((std::istreambuf_iterator<char>(iniStream)),
                              std::istreambuf_iterator<char>());
        ok &= Check(ini.find("scene = assets/scenes/Main.hpscene") != std::string::npos,
                    "game.ini records the startup scene");
        ok &= Check(ini.find("input_profile = assets/input/Default.hpinput") != std::string::npos,
                    "game.ini records the input profile");
        ok &= Check(ini.find("version =") == std::string::npos &&
                    ini.find("company =") == std::string::npos &&
                    ini.find("resizable=") == std::string::npos,
                    "game.ini contains only runtime-supported metadata");
        ok &= Check(ini.find("[Automation]") == std::string::npos,
                    "game.ini omits [Automation] when runtime automation is disabled");
    }
    ok &= Check(HasProblem(result.problems, "PACKAGE_PRODUCT_NAME_SANITIZED"),
                "sanitized product names produce a warning");
    ok &= Check(HasStage(stages, "Copying assets ") &&
                HasStage(stages, "Copying scripts ") &&
                HasStage(stages, "Copying shaders ") &&
                HasStage(stages, "Writing configuration") &&
                HasStage(stages, "Finalising package"),
                "named progress stages include known file counts");

    Write(package / "preserve.marker", "previous output");
    RuntimeArtifacts broken = artifacts;
    broken.executable = runtime / "missing.exe";
    result = stager.stage(request, broken);
    ok &= Check(!result.success, "failed staging reports failure");
    ok &= Check(fs::exists(package / "preserve.marker"),
                "failed staging preserves previous output");

    request.isCancelled = []() { return true; };
    result = stager.stage(request, artifacts);
    ok &= Check(result.cancelled, "cancelled staging reports cancellation");
    ok &= Check(fs::exists(package / "preserve.marker"),
                "cancelled staging preserves previous output");
    request.isCancelled = {};

    request.enableRuntimeAutomation = true;
    request.outputRoot = root / "automation-output";
    auto automationResult = stager.stage(request, artifacts);
    {
        std::ifstream iniStream(root / "automation-output" / "Tiny_Game" / "game.ini");
        const std::string ini((std::istreambuf_iterator<char>(iniStream)),
                              std::istreambuf_iterator<char>());
        ok &= Check(automationResult.success, "Development automation package succeeds");
        ok &= Check(ini.find("[Automation]") != std::string::npos &&
                    ini.find("enabled = true") != std::string::npos,
                    "Development automation package writes the [Automation] section");
    }
    request.enableRuntimeAutomation = false;
    request.outputRoot = outputRoot;

#ifdef _WIN32
    HANDLE lockedExecutable = CreateFileW(
        (package / "Tiny_Game.exe").c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ok &= Check(lockedExecutable != INVALID_HANDLE_VALUE,
                "test can lock the previous executable");
    if (lockedExecutable != INVALID_HANDLE_VALUE) {
        result = stager.stage(request, artifacts);
        ok &= Check(!result.success &&
                    HasProblem(result.problems, "PACKAGE_OUTPUT_LOCKED"),
                    "locked runtime is detected before staging");
        ok &= Check(!fs::exists(outputRoot / ".Tiny_Game_building"),
                    "lock preflight does not create a staging directory");
        CloseHandle(lockedExecutable);
    }
#endif

    fs::remove_all(root);
    if (ok) {
        std::cout << "Package layout tests passed.\n";
    }
    return ok ? 0 : 1;
}
