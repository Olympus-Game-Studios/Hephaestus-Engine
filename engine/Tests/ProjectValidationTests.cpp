#include "Build/ProjectValidator.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace Hephaestus::Build;

namespace {

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

bool Has(const std::vector<EditorProblem>& problems, const std::string& code,
         ProblemSeverity severity) {
    for (const auto& problem : problems) {
        if (problem.code == code && problem.severity == severity) {
            return true;
        }
    }
    return false;
}

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "hephaestus_project_validation_tests";
    fs::remove_all(root);
    Write(root / "project.heproject",
          R"({"name":"Tiny","defaultScene":"assets/scenes/Main.hpscene"})");
    Write(root / ".hephaestus" / "project_settings.json",
          R"({"projectInfo":{"defaultScene":"assets/scenes/Main.hpscene"}})");
    Write(root / "assets" / "scenes" / "Main.hpscene",
          R"({"entities":[{"meshSource":"assets/meshes/missing.glb","scriptPath":"assets/scripts/missing.lua","clipPath":"assets/audio/music.ogg","albedoTexturePath":"C:/outside/missing.png"}]})");
    Write(root / "assets" / "audio" / "music.ogg", "not real audio");
    Write(root / "assets" / "ui" / "Hud.hui",
          R"({"controller":"assets/scripts/missing-controller.lua","imageId":"assets/textures/missing-ui.png"})");
    Write(root / "assets" / "prefabs" / "Broken.hpprefab", "{ malformed");
    Write(root / "assets" / "characters" / "Ghost.hpcharacter",
          R"({"schema":"HephaestusCharacter","assetVersion":1,"skeleton":{"source":"assets/characters/.source/Ghost/mesh.glb"},"controller":{"scriptPath":"assets/characters/.source/Ghost/GhostCharacter.lua"}})");

    ProjectValidator validator;
    auto problems = validator.validate(root);
    bool ok = true;
    ok &= Check(Has(problems, "ASSET_REFERENCE_MISSING", ProblemSeverity::Error),
                "missing referenced file is an error");
    ok &= Check(Has(problems, "AUDIO_OGG_UNSUPPORTED", ProblemSeverity::Error),
                "referenced ogg is an error");
    ok &= Check(Has(problems, "ASSET_JSON_MALFORMED", ProblemSeverity::Error),
                "malformed indexed document is an error");
    ok &= Check(Has(problems, "ASSET_PATH_EXTERNAL", ProblemSeverity::Error),
                "external absolute path is an error");
    ok &= Check(!Has(problems, "STARTUP_SCENE_MISSING", ProblemSeverity::Error),
                "existing startup scene is accepted");

    Write(root / ".hephaestus" / "project_settings.json",
          R"({"projectInfo":{"defaultScene":"assets/scenes/Absent.hpscene"}})");
    problems = validator.validate(root);
    ok &= Check(Has(problems, "STARTUP_SCENE_MISSING", ProblemSeverity::Error),
                "settings defaultScene overrides project file");

    fs::remove_all(root);
    if (ok) {
        std::cout << "Project validation tests passed.\n";
    }
    return ok ? 0 : 1;
}
