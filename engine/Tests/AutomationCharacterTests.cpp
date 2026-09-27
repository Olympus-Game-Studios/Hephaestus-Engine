// Automation character-tool tests: inspect a .hpcharacter and import a GLB/FBX
// source through the real CharacterImporter/CharacterSerializer.

#include "Automation/AutomationService.h"
#include "Automation/CharacterAutomation.h"
#include "Assets/AssetManager.h"
#include "Assets/CharacterAsset.h"
#include "Assets/CharacterImport.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

class CharacterBackend final : public Hephaestus::Automation::AutomationBackend {
public:
    Hephaestus::Automation::HostMask Host() const noexcept override
    {
        return Hephaestus::Automation::HostMask::Editor;
    }
    Hephaestus::Automation::SceneAccess ActiveScene() noexcept override { return {}; }
    Hephaestus::PlayState GetPlayState() const noexcept override
    {
        return Hephaestus::PlayState::Editor;
    }
    Hephaestus::AssetManager* Assets() noexcept override
    {
        return &Hephaestus::AssetManager::GetInstance();
    }
    void NotifyAssetChanged() override { ++m_notifyCount; }
    int NotifyCount() const { return m_notifyCount; }

private:
    int m_notifyCount = 0;
};

Hephaestus::Automation::ToolResult Call(Hephaestus::Automation::AutomationService& service,
                                        const std::string& name,
                                        Hephaestus::Automation::Json arguments)
{
    auto posted = service.Post({name, std::move(arguments)});
    service.Tick();
    return posted.result.get();
}

} // namespace

int main(int argc, char** argv)
{
    bool ok = true;
    const std::string repoRoot = argc > 1 ? argv[1] : ".";
    const fs::path project = fs::temp_directory_path() / "heph_automation_character_test";
    std::error_code ec;
    fs::remove_all(project, ec);
    fs::create_directories(project / "assets" / "characters");

    auto& assetManager = Hephaestus::AssetManager::GetInstance();
    assetManager.Clear();
    assetManager.SetProjectRoot(project.string());
    assetManager.SetAssetRoot((project / "assets").string());
    assetManager.Refresh();

    CharacterBackend backend;
    Hephaestus::Automation::AutomationService service(backend);
    Hephaestus::Automation::RegisterCharacterAutomation(service);

    // ── inspect an empty character created by the engine serializer ────────
    std::string error;
    const fs::path emptyCharacter = project / "assets" / "characters" / "Hero.hpcharacter";
    ok &= Check(Hephaestus::CharacterSerializer::CreateNew(emptyCharacter, project, error),
                "fixture character is created through CharacterSerializer");

    auto emptyInfo = Call(service, "character.get_info", {{"path", "assets/characters/Hero.hpcharacter"}});
    ok &= Check(emptyInfo.Ok() &&
                    emptyInfo.value["character"]["path"] == "assets/characters/Hero.hpcharacter" &&
                    emptyInfo.value["data"]["name"] == "Hero",
                "character.get_info reads a .hpcharacter asset");

    auto missing = Call(service, "character.get_info", {{"path", "assets/characters/Nope.hpcharacter"}});
    ok &= Check(!missing.Ok() &&
                    missing.error->code == Hephaestus::Automation::ErrorCode::InvalidArguments,
                "character.get_info rejects unknown assets");

    {
        std::ofstream bogus(project / "assets" / "characters" / "Bogus.hpcharacter",
                            std::ios::binary | std::ios::trunc);
        bogus << "{ this is not a character asset }\n";
    }
    auto wrongType = Call(service, "character.get_info", {{"path", "assets/characters/Bogus.hpcharacter"}});
    ok &= Check(!wrongType.Ok(),
                "character.get_info rejects malformed character assets");

    // ── import a real skinned FBX fixture ──────────────────────────────────
    const fs::path source = fs::path(repoRoot) / "engine" / "Tests" / "Fixtures" / "Fbx" /
                            "skinned.fbx";
    ok &= Check(fs::exists(source), "skinned FBX fixture exists");

    auto imported = Call(service, "character.import",
                         {{"source", source.string()},
                          {"name", "ImportedHero"},
                          {"destination", "characters"}});
    ok &= Check(imported.Ok() &&
                    imported.value["character"]["path"] ==
                        "assets/characters/ImportedHero.hpcharacter" &&
                    fs::exists(project / "assets" / "characters" / "ImportedHero.hpcharacter"),
                "character.import generates a .hpcharacter through CharacterImporter");
    ok &= Check(imported.Ok() && imported.value["skeleton"]["boneCount"] >= 2,
                "character.import reports skeleton bone count");
    ok &= Check(imported.Ok() &&
                    imported.value["mesh"]["skinnedMeshCount"] >= 1,
                "character.import reports mesh parts");

    auto importedInfo = Call(service, "character.get_info",
                             {{"path", "assets/characters/ImportedHero.hpcharacter"}});
    ok &= Check(importedInfo.Ok() && importedInfo.value.contains("skeletonSummary") &&
                    importedInfo.value["skeletonSummary"]["boneCount"] >= 2,
                "character.get_info derives skeleton info from the owned source");
    ok &= Check(importedInfo.Ok() && importedInfo.value["data"]["mesh"]["source"].is_string(),
                "character.get_info reports the owned mesh source");

    auto duplicate = Call(service, "character.import",
                          {{"source", source.string()}, {"name", "ImportedHero"}});
    ok &= Check(!duplicate.Ok(),
                "character.import rejects duplicate character names");

    auto badSource = Call(service, "character.import",
                          {{"source", (project / "assets" / "characters" / "missing.glb").string()}});
    ok &= Check(!badSource.Ok() &&
                    badSource.error->code == Hephaestus::Automation::ErrorCode::InvalidArguments,
                "character.import rejects a missing source");

    ok &= Check(backend.NotifyCount() > 0, "character.import notifies the editor host");

    service.Shutdown();
    assetManager.Clear();
    fs::remove_all(project, ec);

    if (ok) std::cout << "[PASS] AutomationCharacterTests\n";
    return ok ? 0 : 1;
}
