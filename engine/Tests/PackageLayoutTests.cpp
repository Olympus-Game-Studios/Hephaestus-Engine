#include "Build/PackageStager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

int main(int argc, char** argv) {
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
    Write(project / "assets" / "characters" / ".source" / "Cultist" / "Cultist.blend", "blender scene");
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
    Write(engine / "assets" / "fonts" / "Roboto-Regular.ttf", "font");
    Write(engine / "THIRD_PARTY_NOTICES.md", "Third-party notices");
    Write(runtime / kPlayerExecutable, "fake executable");
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
    request.physicsEnabled = false;
    std::vector<std::string> stages;
    request.onProgress = [&](float, const std::string& stage) {
        stages.push_back(stage);
    };

    RuntimeArtifacts artifacts{
        runtime / kPlayerExecutable,
        runtime / "shaders",
        runtime / "lua.dll"
    };
    PackageStager stager;
    auto result = stager.stage(request, artifacts);
    const fs::path package = outputRoot / "Tiny_Game";
    const fs::path content = PackageStager::contentDirectory(package, "Tiny_Game");
    bool ok = true;
    ok &= Check(result.success, "initial package succeeds");
    ok &= Check(fs::exists(PackageStager::launchPath(package, "Tiny_Game")), "runtime is renamed");
    ok &= Check(!kRuntimeShipsLua || fs::exists(package / "lua54.dll"), "Lua is normalized to lua54.dll");
    ok &= Check(fs::exists(content / "assets" / "scenes" / "Main.hpscene"),
                "assets are packaged");
    ok &= Check(fs::exists(content / "assets" / "prefabs" / "Ship.hpprefab"),
                "prefabs are packaged");
    ok &= Check(fs::exists(content / "assets" / "characters" / "Cultist.hpcharacter"),
                "character assets are packaged");
    ok &= Check(!fs::exists(content / "assets" / "characters" / ".source" / "Cultist" / "Cultist.blend"),
                "Blender scenes are not packaged");
    ok &= Check(fs::exists(content / "assets" / "characters" / ".source" / "Cultist" / "mesh.glb"),
                "owned character sources are packaged");
    ok &= Check(fs::exists(content / "assets" / "characters" / ".source" / "Cultist" /
                           "CultistCharacter.lua"),
                "owned character controller scripts are packaged");
    ok &= Check(fs::exists(content / "assets" / "ui" / "Hud.hui"),
                "HUI documents are packaged");
    ok &= Check(fs::exists(content / "assets" / "audio" / "engine.wav"),
                "audio is packaged");
    ok &= Check(fs::exists(content / "assets" / "input" / "Default.hpinput"),
                "input map is packaged");
    ok &= Check(fs::exists(content / "scripts" / "boot.lua"), "scripts are packaged");
    ok &= Check(fs::exists(content / "Plugins" / "com.engine.content" / "Content" / "Scripts" /
                           "Boot.lua"),
                "enabled engine plugin content is staged");
    ok &= Check(fs::exists(content / "Plugins" / "com.project.content" / "Content" / "UI" / "Hud.hui"),
                "enabled project plugin content is staged");
    ok &= Check(!fs::exists(content / "Plugins" / "com.disabled.content"),
                "disabled plugin content is omitted");
    ok &= Check(!fs::exists(content / "Plugins" / "com.engine.content" / "bin" / "HephaestusFPS.dll") &&
                    !fs::exists(content / "Plugins" / "com.engine.content" / "Content" / "bin"),
                "plugin entry DLLs are omitted");
    ok &= Check(!fs::exists(content / "Plugins" / "com.engine.content" / "bin" / "Qt6Core.dll") &&
                    !fs::exists(package / "Qt6Core.dll"),
                "Qt is not packaged from plugin content");
    {
        std::ifstream mountsStream(content / "Plugins" / "content-mounts.json");
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
    ok &= Check(fs::exists(content / ".engine" / "meshes" / "builtin.mesh"),
                "engine meshes are packaged");
    ok &= Check(fs::exists(content / ".engine" / "meshes" / "project.mesh"),
                "project meshes are packaged");
    ok &= Check(fs::exists(content / ".engine" / "hdris" / "project.hdr"),
                "project HDRIs are packaged");
    ok &= Check(fs::exists(content / ".engine" / "fonts" / "Roboto-Regular.ttf"),
                "the engine's bundled fonts are packaged for the game's UI");
    ok &= Check(fs::exists(content / ".engine" / "hdris" / "builtin.hdr"),
                "builtin HDRIs are packaged");
    ok &= Check(fs::exists(content / ".hephaestus" / "physics_layers.json"),
                "physics_layers.json is packaged");
    ok &= Check(!fs::exists(content / ".hephaestus" / "private.json"),
                "other .hephaestus editor state is excluded");
    ok &= Check(fs::exists(package / "build_manifest.json"), "JSON manifest exists");
    ok &= Check(fs::exists(package / "export_manifest.txt"), "export manifest exists");
    {
        std::ifstream noticesStream(content / "ThirdPartyNotices.txt", std::ios::binary);
        const std::string notices((std::istreambuf_iterator<char>(noticesStream)),
                                  std::istreambuf_iterator<char>());
        ok &= Check(notices == "Third-party notices",
                    "THIRD_PARTY_NOTICES.md is packaged as ThirdPartyNotices.txt");
    }
    {
        std::ifstream manifestStream(package / "build_manifest.json");
        nlohmann::json manifest;
        manifestStream >> manifest;
        ok &= Check(manifest.value("engineVersion", "") == "2026.9.0",
                    "manifest records engine version");
        ok &= Check(manifest.value("platform", "") == BuildManifestData{}.platform,
                    "manifest records platform");
    }
    {
        std::ifstream iniStream(content / "game.ini");
        const std::string ini((std::istreambuf_iterator<char>(iniStream)),
                              std::istreambuf_iterator<char>());
        ok &= Check(ini.find("scene = assets/scenes/Main.hpscene") != std::string::npos,
                    "game.ini records the startup scene");
        ok &= Check(ini.find("input_profile = assets/input/Default.hpinput") != std::string::npos,
                    "game.ini records the input profile");
        ok &= Check(ini.find("physics_enabled = false") != std::string::npos,
                    "game.ini records the project physics switch");
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
        std::ifstream iniStream(PackageStager::contentDirectory(
            root / "automation-output" / "Tiny_Game", "Tiny_Game") / "game.ini");
        const std::string ini((std::istreambuf_iterator<char>(iniStream)),
                              std::istreambuf_iterator<char>());
        ok &= Check(automationResult.success, "Development automation package succeeds");
        ok &= Check(ini.find("[Automation]") != std::string::npos &&
                    ini.find("enabled = true") != std::string::npos,
                    "Development automation package writes the [Automation] section");
    }
    request.enableRuntimeAutomation = false;
    request.outputRoot = outputRoot;

    Write(project / ".hephaestus" / "plugins.json",
          R"({"version":1,"enabled":{"com.project.content":true,"com.missing.content":true}})");
    result = stager.stage(request, artifacts);
    ok &= Check(!result.success && HasProblem(result.problems, "PACKAGE_PLUGIN_NOT_FOUND"),
                "an enabled plugin that is not installed fails the package");
    ok &= Check(fs::exists(package / "preserve.marker"),
                "a missing enabled plugin preserves previous output");
    fs::remove(project / ".hephaestus" / "plugins.json");

    if (argc > 2) {
        const fs::path testModule = argv[2];
        const fs::path languagePlugin = engine / "Plugins" / "com.test.language";
        Write(languagePlugin / "com.test.language.hpplugin",
              R"({"version":1,"apiVersion":1,"id":"com.test.language","name":"Language","author":"Tests",)"
              R"("pluginVersion":"1.0.0","engineVersion":">=2026.9.0","type":"runtime",)"
              R"("runtimeEntry":"bin/ScriptLanguageTestPlugin.dll","content":"Content"})");
        Write(languagePlugin / "Content" / "Scripts" / "plugin.tscript", "ok");
        // Built beside the player, as CMake does for first-party plugins, not in the plugin's folder.
        const fs::path builtPlugin = runtime / "Plugins" / "com.test.language";
        fs::create_directories(builtPlugin / "bin");
        fs::copy_file(testModule, builtPlugin / "bin" / testModule.filename(),
                      fs::copy_options::overwrite_existing);
        Write(project / "scripts" / "mover.tscript", "ok");
        result = stager.stage(request, artifacts);
        ok &= Check(result.success && fs::exists(content / "mover.tscript.packaged") &&
                        fs::exists(content / "plugin.tscript.packaged") &&
                        fs::exists(content / "Plugins" / "com.test.language" / "bin" / testModule.filename()) &&
                        HasStage(stages, "Preparing Test Language script mover.tscript"),
                    "a script language prepares project and plugin scripts while packaging");
        Write(project / "scripts" / "broken.tscript", "fail");
        result = stager.stage(request, artifacts);
        ok &= Check(!result.success && HasProblem(result.problems, "PACKAGE_SCRIPT_PREPARE_FAILED"),
                    "a script the language cannot prepare fails the package");
        fs::remove(project / "scripts" / "broken.tscript");
        fs::remove(project / "scripts" / "mover.tscript");
        fs::remove_all(languagePlugin);
        fs::remove_all(runtime / "Plugins");
    }

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
