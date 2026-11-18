#include "Assets/ArchiveFileSystem.h"
#include "Core/SimulationStep.h"
#include "Assets/MeshAsset.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Scripting/LuaScriptEngine.h"
#include "Scripting/PluginScriptHost.h"
#include "Scripting/ScriptLanguageRegistry.h"
#include "GameModuleTestPlugin.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

extern "C" const HPGameModule* const* HP_GetGameModules(uint32_t hostApiVersion, uint32_t* count);

namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

bool Check(bool condition, const std::string& message)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << "\n";
    return condition;
}

uint32_t Crc32(const Bytes& data)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

Bytes Text(const std::string& text)
{
    return Bytes(text.begin(), text.end());
}

struct ZipEntry {
    std::string name;
    Bytes stored;        // Bytes as written into the archive.
    uint32_t size = 0;   // Uncompressed size.
    uint32_t crc = 0;
    uint16_t method = 0;
    uint16_t flags = 0;
};

ZipEntry Stored(const std::string& name, const std::string& text)
{
    const Bytes data = Text(text);
    return {name, data, static_cast<uint32_t>(data.size()), Crc32(data), 0, 0};
}

void Put16(Bytes& out, uint32_t v) { out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF); }
void Put32(Bytes& out, uint32_t v) { Put16(out, v & 0xFFFF); Put16(out, v >> 16); }

void WriteZip(const fs::path& path, const std::vector<ZipEntry>& entries)
{
    Bytes out;
    Bytes directory;
    for (const ZipEntry& e : entries) {
        const uint32_t offset = static_cast<uint32_t>(out.size());
        Put32(out, 0x04034b50u); Put16(out, 20); Put16(out, e.flags); Put16(out, e.method);
        Put32(out, 0); Put32(out, e.crc); Put32(out, static_cast<uint32_t>(e.stored.size())); Put32(out, e.size);
        Put16(out, static_cast<uint32_t>(e.name.size())); Put16(out, 0);
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), e.stored.begin(), e.stored.end());

        Put32(directory, 0x02014b50u); Put16(directory, 20); Put16(directory, 20); Put16(directory, e.flags);
        Put16(directory, e.method); Put32(directory, 0); Put32(directory, e.crc);
        Put32(directory, static_cast<uint32_t>(e.stored.size())); Put32(directory, e.size);
        Put16(directory, static_cast<uint32_t>(e.name.size())); Put16(directory, 0); Put16(directory, 0);
        Put16(directory, 0); Put16(directory, 0); Put32(directory, 0); Put32(directory, offset);
        directory.insert(directory.end(), e.name.begin(), e.name.end());
    }
    const uint32_t directoryOffset = static_cast<uint32_t>(out.size());
    out.insert(out.end(), directory.begin(), directory.end());
    Put32(out, 0x06054b50u); Put16(out, 0); Put16(out, 0);
    Put16(out, static_cast<uint32_t>(entries.size())); Put16(out, static_cast<uint32_t>(entries.size()));
    Put32(out, static_cast<uint32_t>(directory.size())); Put32(out, directoryOffset); Put16(out, 0);
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(out.data()),
                                                                  static_cast<std::streamsize>(out.size()));
}

// Raw deflate of kScriptText, made with zlib at level 9.
const std::string kScriptText = "ICARUS script: wait 1000; wait 1000; wait 1000; wait 1000;";
const Bytes kScriptDeflated = {0xf3, 0x74, 0x76, 0x0c, 0x0a, 0x0d, 0x56, 0x28, 0x4e, 0x2e, 0xca, 0x2c, 0x28, 0xb1, 0x52,
                               0x28, 0x4f, 0xcc, 0x2c, 0x51, 0x30, 0x34, 0x30, 0x30, 0xb0, 0x26, 0xc8, 0x04, 0x00};

ZipEntry DeflatedScript(const std::string& name)
{
    return {name, kScriptDeflated, static_cast<uint32_t>(kScriptText.size()), Crc32(Text(kScriptText)), 8, 0};
}

bool RejectsArchive(const fs::path& path, const std::string& expected)
{
    Hephaestus::ArchiveFileSystem archives;
    std::string error;
    const bool rejected = !archives.Mount(path, error) && error.find(expected) != std::string::npos;
    if (!rejected) {
        std::cerr << "  error was: " << error << "\n";
    }
    return rejected;
}

std::string ReadText(const Hephaestus::ArchiveFileSystem& archives, const std::string& path)
{
    Bytes bytes;
    std::string error;
    return archives.Read(path, bytes, error) ? std::string(bytes.begin(), bytes.end()) : "<" + error + ">";
}

bool RejectsModule(const HPGameModule& module)
{
    std::string error;
    return !ScriptLanguageRegistry::Instance().RegisterGameModule(&module, error) && !error.empty();
}

} // namespace

int main(int argc, char** argv)
{
    bool pass = true;
    const fs::path root = fs::temp_directory_path() / "hephaestus_game_module_tests";
    fs::remove_all(root);

    // ── Archives ────────────────────────────────────────────────────────────
    WriteZip(root / "pak0.pk3", {Stored("maps/borg1.bsp", "first"), DeflatedScript("scripts/Test.txt"),
                                 Stored("textures/", "")});
    WriteZip(root / "pak1.pk3", {Stored("MAPS/BORG1.BSP", "override")});
    {
        Hephaestus::ArchiveFileSystem archives;
        std::string error;
        pass &= Check(archives.Mount(root / "pak0.pk3", error), "a ZIP archive mounts: " + error);
        pass &= Check(archives.Files() == std::vector<std::string>{"maps/borg1.bsp", "scripts/Test.txt"},
                      "files are listed sorted, without directory entries");
        pass &= Check(ReadText(archives, "maps/borg1.bsp") == "first", "a stored entry reads back");
        pass &= Check(ReadText(archives, "SCRIPTS\\test.TXT") == kScriptText,
                      "a deflated entry inflates, looked up ignoring case and slash style");
        pass &= Check(archives.Mount(root / "pak1.pk3", error) && ReadText(archives, "maps/borg1.bsp") == "override" &&
                      archives.Files().size() == 2 && archives.Files()[0] == "MAPS/BORG1.BSP",
                      "a later mount overrides the same path");
        Bytes bytes;
        pass &= Check(!archives.Read("maps/missing.bsp", bytes, error) && !error.empty(),
                      "a missing file reports an error");
        archives.Clear();
        pass &= Check(archives.Files().empty() && archives.MountCount() == 0, "Clear unmounts everything");
    }

    ZipEntry badCrc = Stored("a.txt", "abc");
    badCrc.crc ^= 1u;
    WriteZip(root / "badcrc.zip", {badCrc});
    {
        Hephaestus::ArchiveFileSystem archives;
        std::string error;
        Bytes bytes;
        pass &= Check(archives.Mount(root / "badcrc.zip", error) && !archives.Read("a.txt", bytes, error) &&
                      error.find("CRC") != std::string::npos && bytes.empty(),
                      "a corrupt entry fails its CRC check");
    }
    ZipEntry encrypted = Stored("secret.txt", "x");
    encrypted.flags = 1;
    WriteZip(root / "encrypted.zip", {encrypted});
    pass &= Check(RejectsArchive(root / "encrypted.zip", "encrypted"), "encrypted entries are rejected");
    WriteZip(root / "escape.zip", {Stored("../evil.txt", "x")});
    pass &= Check(RejectsArchive(root / "escape.zip", "unsafe"), "paths leaving the archive are rejected");
    {
        std::ofstream(root / "notzip.pk3") << "this is not a zip archive at all";
    }
    pass &= Check(RejectsArchive(root / "notzip.pk3", "end record"), "a file that is not a ZIP is rejected");
    pass &= Check(RejectsArchive(root / "missing.pk3", "cannot open"), "a missing archive is rejected");

    // ── Registration rules ──────────────────────────────────────────────────
    auto& registry = ScriptLanguageRegistry::Instance();
    registry.Clear();
    uint32_t count = 0;
    const HPGameModule* const* modules = HP_GetGameModules(HP_GAME_MODULE_API_VERSION, &count);
    pass &= Check(count == 2, "the test plugin exports two game modules");
    HPGameModule broken = *modules[0];
    broken.apiVersion = HP_GAME_MODULE_API_VERSION + 1;
    pass &= Check(RejectsModule(broken), "a game module built for another API version is rejected");
    broken = *modules[0];
    broken.update = nullptr;
    pass &= Check(RejectsModule(broken), "a game module missing a callback is rejected");
    std::string error;
    pass &= Check(registry.RegisterGameModule(modules[0], error) && registry.RegisterGameModule(modules[1], error),
                  "valid game modules register");
    pass &= Check(RejectsModule(*modules[0]), "the same game module id cannot register twice");

    // ── Play sessions ───────────────────────────────────────────────────────
    WriteZip(root / "project" / "base.pk3", {DeflatedScript("scripts/test.txt"), Stored("maps/a.bsp", "a")});
    GameModuleTestStats& stats = *TestGameModule_Stats();
    {
        Scene scene("GameModules");
        LuaScriptEngine lua;
        PluginScriptHost host;
        std::vector<size_t> uploads;
        std::vector<size_t> releases;
        std::size_t uploadedIndices = 0;
        host.SetMeshSink(PluginScriptHost::MeshSink{
            [&](const std::string&, const MeshData& mesh) {
                uploadedIndices = mesh.indices.size();
                uploads.push_back(40 + uploads.size());
                return uploads.back();
            },
            [&](size_t handle) { releases.push_back(handle); }});
        lua.Initialize(&scene, nullptr);
        lua.SetProjectRoot(root / "project");
        lua.SetPluginScriptHost(&host);
        host.SetLuaEngine(&lua);
        lua.OnSimulationStarted();

        Hephaestus::SimulationStepContext step;
        step.scene = &scene;
        step.lua = &lua;
        step.pluginScripts = &host;
        Hephaestus::RunSimulationStep(step, 0.25f);
        Hephaestus::RunSimulationStep(step, 0.25f);
        pass &= Check(stats.begins == 1 && stats.updates == 2 && stats.lastDelta == 0.25f,
                      "a game module starts once per session and updates every frame");
        pass &= Check(stats.mounted == 1 && stats.files == 2 && stats.scriptBytes == static_cast<int>(kScriptText.size()),
                      "a game module mounts an archive and reads from it through the host API");
        pass &= Check(stats.refusals == 1 && stats.refusedCalls == 0 && host.GameSessionCount() == 1,
                      "a module that refuses to start gets no update or endPlay");
        const auto meshEntity = static_cast<entt::entity>(static_cast<uint32_t>(stats.meshEntity - 1));
        const auto* renderer = stats.meshEntity ? scene.GetRegistry().try_get<MeshRendererComponent>(meshEntity) : nullptr;
        pass &= Check(renderer && uploads.size() == 1 && uploadedIndices == 3 && renderer->meshHandle == 40 &&
                          renderer->submeshCount == 1 && renderer->appliedMaterialPaths.size() == 1 &&
                          renderer->appliedMaterialPaths[0] == "materials/floor.hematerial" &&
                          renderer->localBounds.min.z == -1.0f && renderer->localBounds.max.x == 1.0f,
                      "a game module creates a mesh entity with its material slots and bounds");
        pass &= Check(stats.badMeshRejected == 1 && uploads.size() == 1, "a mesh with an index past its vertices is rejected");

        host.OnSimulationStopped();
        lua.OnSimulationStopped();
        pass &= Check(stats.ends == 1 && host.GameSessionCount() == 0 && host.Archives().MountCount() == 0,
                      "stopping play ends the session and unmounts its archives");
        pass &= Check(releases == std::vector<size_t>{40}, "stopping play frees the session's meshes");

        lua.OnSimulationStarted();
        Hephaestus::RunSimulationStep(step, 0.25f);
        pass &= Check(stats.begins == 2 && stats.refusals == 2 && host.GameSessionCount() == 1,
                      "the next play session starts the modules again");
        host.OnSimulationStopped();
        lua.OnSimulationStopped();
        lua.Shutdown();
    }

    // ── Loading a game module from disk ─────────────────────────────────────
    if (argc > 1) {
        registry.Clear();
        error.clear();
        const bool loaded = registry.LoadModule(argv[1], error);
        if (!loaded) std::cerr << error << "\n";
        pass &= Check(loaded && registry.GameModules().size() == 2 && registry.Languages().empty(),
                      "a module that exports only game modules loads and registers them");
    } else {
        pass &= Check(false, "the test game module path was passed on the command line");
    }

    registry.Clear();
    fs::remove_all(root);
    std::cout << (pass ? "[PASS] GameModuleTests\n" : "[FAIL] GameModuleTests\n");
    return pass ? 0 : 1;
}
