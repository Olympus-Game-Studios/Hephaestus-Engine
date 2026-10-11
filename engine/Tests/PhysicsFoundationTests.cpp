#include "Core/EngineLog.h"
#include "Physics/PhysicsLayerConfig.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    std::cout << "[PASS] " << message << "\n";
    return true;
}

bool TestCompatibilityAndSeed()
{
    bool pass = true;
    const Physics::PhysicsLayerConfig compatibility = Physics::PhysicsLayerConfig::Compatibility();
    pass &= Check(compatibility.Layers().size() == 1, "missing-file shape is one layer");
    pass &= Check(compatibility.HasLayer(0) && compatibility.Collides(0, 0), "Default collides with itself");
    pass &= Check(!compatibility.HasLayer(1), "compatibility config has no World layer");

    Physics::PhysicsLayerConfig missing;
    const auto missingResult = Physics::PhysicsLayerConfig::LoadFromFile("missing-physics-layers.json", missing);
    pass &= Check(missingResult.missingFile && missingResult.usedCompatibility, "missing file uses compatibility");
    pass &= Check(missing.Collides(0, 0) && missing.Layers().size() == 1, "missing file matrix");

    const Physics::PhysicsLayerConfig seed = Physics::PhysicsLayerConfig::Seed();
    pass &= Check(seed.Layers().size() == 4, "seed has four layers");
    pass &= Check(seed.FindById(0) && seed.FindById(0)->name == "Default" && seed.FindById(0)->builtin, "Default builtin");
    pass &= Check(seed.Collides(0, 0) && seed.Collides(0, 1) && seed.Collides(0, 2) && seed.Collides(0, 3),
                  "Default pairs start on");
    pass &= Check(!seed.Collides(3, 3), "Trigger-vs-Trigger is off");
    pass &= Check(!seed.Collides(1, 1), "World-vs-World is off");
    return pass;
}

bool TestMatrixRoundTrip()
{
    bool pass = true;
    Physics::PhysicsLayerConfig config = Physics::PhysicsLayerConfig::Seed();
    config.SetCollides(Physics::kLayerDefault, Physics::kLayerWorld, false);
    config.SetCollides(Physics::kLayerDefault, Physics::kLayerCharacter, false);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "hephaestus-physics-layers.json";
    std::string error;
    pass &= Check(config.SaveToFile(path, error), "save layer file: " + error);

    Physics::PhysicsLayerConfig loaded;
    const auto result = Physics::PhysicsLayerConfig::LoadFromFile(path, loaded);
    pass &= Check(result.error.empty() && !result.usedCompatibility, "reload layer file");
    pass &= Check(!loaded.Collides(0, 1), "Default-vs-World false survives load");
    pass &= Check(!loaded.Collides(0, 2), "Default-vs-Character false round-trips");
    pass &= Check(loaded.Collides(0, 0), "Default-vs-Default stays on");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return pass;
}

bool TestLayerIdentity()
{
    bool pass = true;
    Physics::PhysicsLayerConfig config = Physics::PhysicsLayerConfig::Seed();
    std::string error;
    pass &= Check(!config.DeleteUserLayer(0, error), "Default cannot be deleted");
    pass &= Check(!config.DeleteUserLayer(1, error), "World cannot be deleted");

    pass &= Check(config.RenameLayer(0, "General", error), "rename keeps the layer");
    pass &= Check(config.FindById(0) && config.FindById(0)->name == "General" && config.FindById(0)->id == 0,
                  "rename does not change id");

    pass &= Check(config.AddUserLayer("Bullet", error), "add user layer: " + error);
    const std::uint16_t bullet = config.FindIdByName("Bullet");
    pass &= Check(bullet == 4, "first user layer is id 4");
    pass &= Check(config.Collides(bullet, 0), "new layer collides with Default");

    pass &= Check(config.DeleteUserLayer(bullet, error), "tombstone user layer");
    pass &= Check(config.IsTombstoned(bullet) && !config.HasLayer(bullet), "id 4 is tombstoned");
    pass &= Check(config.AddUserLayer("Rocket", error), "add after tombstone");
    pass &= Check(config.FindIdByName("Rocket") == 5, "tombstoned id is not reused");

    for (int i = 0; i < 16; ++i) {
        config.AddUserLayer("Extra" + std::to_string(i), error);
    }
    pass &= Check(!config.AddUserLayer("Overflow", error), "17th layer is rejected");
    pass &= Check(config.Resolve(99) == 0, "unknown id resolves to 0");
    pass &= Check(config.Resolve(bullet) == 0, "tombstoned id resolves to 0");
    return pass;
}

} // namespace

int main()
{
    bool pass = true;
    pass &= TestCompatibilityAndSeed();
    pass &= TestMatrixRoundTrip();
    pass &= TestLayerIdentity();
    if (!pass) {
        std::cerr << "[FAIL] PhysicsFoundationTests\n";
        return 1;
    }
    std::cout << "[PASS] PhysicsFoundationTests\n";
    return 0;
}
