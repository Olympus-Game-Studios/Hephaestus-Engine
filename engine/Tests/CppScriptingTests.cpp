#include "Core/EngineLog.h"
#include "Core/SimulationStep.h"
#include "Physics/PhysicsContactEvents.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scripting/LuaScriptComponent.h"
#include "Scripting/LuaScriptEngine.h"
#include "Scripting/PluginScriptHost.h"
#include "Scripting/ScriptLanguageRegistry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

bool Check(bool condition, const std::string& message)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << "\n";
    return condition;
}

void WriteFile(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

void Touch(const fs::path& path)
{
    fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(2));
}

bool Near(float a, float b)
{
    return std::fabs(a - b) < 0.0001f;
}

std::vector<fs::path> CachedDlls(const fs::path& folder, const std::string& prefix)
{
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        const std::string name = entry.path().filename().string();
        if (entry.path().extension() == ".dll" && name.rfind(prefix, 0) == 0) {
            out.push_back(entry.path());
        }
    }
    return out;
}

const char* kMover = R"cpp(#include "HephaestusScript.h"

class Mover : public hp::Script {
public:
    HP_FIELD(float, speed, 1.0f);
    HP_FIELD(hp::Vec3, offset, 0, 1.5f, 0);
    HP_FIELD(std::string, label, "mover, the first");
    HP_FIELD(bool, active, true);
    // HP_FIELD(float, commentedOut, 3.0f);

    void OnAwake() override { hp::Scene::Find("LuaTarget").Call("onPing", 7); }
    void OnUpdate(float dt) override { GetEntity().Translate({speed * dt, 0, 0}); }
    void OnCollisionEnter(hp::Entity, const hp::Contact* contact) override {
        GetEntity().Translate({0, 0, contact ? contact->penetration : -1.0f});
    }
    bool OnCall(std::string_view function, hp::Args args) override {
        if (function != "bump" || args.empty()) return false;
        GetEntity().Translate({0, args[0].AsFloat(), 0});
        return true;
    }
};

HP_SCRIPT(Mover)
)cpp";

const char* kMoverEdited = R"cpp(#include "HephaestusScript.h"

class Mover : public hp::Script {
public:
    HP_FIELD(float, speed, 1.0f);
    void OnUpdate(float dt) override { GetEntity().Translate({0, 0, speed * dt * 10.0f}); }
};

HP_SCRIPT(Mover)
)cpp";

const char* kBroken = R"cpp(#include "HephaestusScript.h"

class Broken : public hp::Script { void OnUpdate(float) override { undefinedThing(); } };
HP_SCRIPT(Broken)
)cpp";

const char* kFaulty = R"cpp(#include "HephaestusScript.h"
#include <stdexcept>

class Faulty : public hp::Script {
public:
    int frame = 0;
    void OnUpdate(float) override {
        ++frame;
        if (frame == 1) throw std::runtime_error("first frame fails");
        if (frame == 2) *static_cast<volatile int*>(nullptr) = 1;
        GetEntity().Translate({1, 0, 0});
    }
};

HP_SCRIPT(Faulty)
)cpp";

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: CppScriptingTests <HephaestusCppScripting.dll>\n";
        return 2;
    }
    bool pass = true;
    auto& registry = ScriptLanguageRegistry::Instance();
    std::string error;
    pass &= Check(registry.LoadModule(argv[1], error), "the C++ language module loads and registers: " + error);
    const HPScriptLanguage* language = registry.FindForPath("scripts/Player.CPP");
    pass &= Check(language && std::string(language->id) == "cpp", ".cpp files route to the C++ language");
    if (!language) {
        return 1;
    }
    pass &= Check(registry.FindForPath("scripts/helpers.h") == nullptr, "headers are not scripts");
    pass &= Check(registry.FindSyntaxForPath("a.cpp") && registry.FindSyntaxForPath("a.cpp")->ruleCount > 0,
                  "the Code panel gets C++ highlighting");
    const auto completions = registry.Complete("a.cpp", "", 1, 1);
    pass &= Check(std::any_of(completions.begin(), completions.end(),
                              [](const auto& c) { return c.label == "OnUpdate"; }),
                  "completion offers the script API");
    pass &= Check(registry.Hover("a.cpp", "    HP_FIELD(float, x, 1.0f);", 1, 6).find("Inspector") != std::string::npos,
                  "hover documents HP_FIELD");

    const fs::path root = fs::temp_directory_path() / "hephaestus_cpp_scripting_tests";
    fs::remove_all(root);
    const fs::path moverPath = root / "scripts" / "mover.cpp";
    const fs::path brokenPath = root / "scripts" / "broken.cpp";
    const fs::path faultyPath = root / "scripts" / "faulty.cpp";
    const fs::path cache = root / ".hephaestus" / "build" / "cpp-scripts";
    WriteFile(moverPath, kMover);
    WriteFile(brokenPath, kBroken);
    WriteFile(faultyPath, kFaulty);

    LuaExposeScanResult fields;
    pass &= Check(ScriptLanguageRegistry::DescribeFields(*language, moverPath, fields) && fields.valid &&
                  fields.fields.size() == 4,
                  "HP_FIELD declarations become Inspector fields, skipping commented ones");
    if (fields.fields.size() == 4) {
        pass &= Check(fields.fields[0].name == "speed" && fields.fields[0].type == LuaExposeType::Number &&
                      fields.fields[1].type == LuaExposeType::Vec3 &&
                      fields.fields[2].type == LuaExposeType::String &&
                      fields.fields[3].type == LuaExposeType::Bool,
                      "field types are read from the declarations");
        const Vec3* offset = fields.fields[1].defaultValue.AsVec3();
        const std::string* label = fields.fields[2].defaultValue.AsString();
        pass &= Check(offset && Near(offset->y, 1.5f) && label && *label == "mover, the first",
                      "field defaults are read, including commas inside strings");
    }

    std::vector<std::string> log;
    const auto subscription = Hephaestus::EngineLog::Subscribe([&log](const Hephaestus::EngineLogEntry& entry) {
        log.push_back(entry.message);
    });
    const auto logged = [&log](const std::string& text) {
        return std::any_of(log.begin(), log.end(), [&](const std::string& line) {
            return line.find(text) != std::string::npos;
        });
    };

    {
        Scene scene("CppScripts");
        Entity mover = scene.CreateEntity("Mover");
        mover.AddComponent<LuaScriptComponent>("scripts/mover.cpp").variables["speed"] = PropertyValue(2.0);
        Entity target = scene.CreateEntity("LuaTarget");
        target.AddComponent<LuaScriptComponent>().scriptSource = R"lua(
function onPing(n) _G.pinged = n end
function onUpdate(dt)
    if not _G.bumped then
        local movers = Scene.findAllWithPrefix("Mover")
        _G.bumped = movers[1] ~= nil and movers[1]:Call("bump", 3)
    end
end
)lua";
        scene.CreateEntity("Broken").AddComponent<LuaScriptComponent>("scripts/broken.cpp");
        Entity faulty = scene.CreateEntity("Faulty");
        faulty.AddComponent<LuaScriptComponent>("scripts/faulty.cpp");

        LuaScriptEngine lua;
        PluginScriptHost host;
        lua.Initialize(&scene, nullptr);
        lua.SetProjectRoot(root);
        lua.SetPluginScriptHost(&host);
        host.SetLuaEngine(&lua);
        lua.OnSimulationStarted();

        Hephaestus::SimulationStepContext step;
        step.scene = &scene;
        step.lua = &lua;
        step.pluginScripts = &host;
        Hephaestus::RunSimulationStep(step, 0.5f);
        Hephaestus::RunSimulationStep(step, 0.5f);

        const Vec3 position = mover.GetComponent<TransformComponent>().position;
        pass &= Check(Near(position.x, 2.0f), "the compiled script runs OnUpdate with its Inspector value");
        pass &= Check(lua.ExecuteString("assert(_G.pinged == 7)"), "a C++ script calls a Lua function");
        pass &= Check(lua.ExecuteString("assert(_G.bumped == true)") && Near(position.y, 3.0f),
                      "Lua calls a C++ script with arguments through entity:Call");
        pass &= Check(CachedDlls(cache, "mover-").size() == 1, "the compiled DLL is cached in the project");
        pass &= Check(logged("scripts/broken.cpp:3:") && logged("undefinedThing"),
                      "compile errors name the script as file:line for the Console");
        pass &= Check(host.InstanceCount() == 2, "a script that fails to compile does not run");
        pass &= Check(logged("first frame fails"), "an exception thrown by a script is reported");
        pass &= Check(logged("crashed in OnUpdate"), "a crashing script is reported instead of taking the host down");
        Hephaestus::RunSimulationStep(step, 0.5f);
        pass &= Check(Near(faulty.GetComponent<TransformComponent>().position.x, 0.0f),
                      "a crashed script stays stopped");

        Physics::PhysicsContactEvent contact;
        contact.kind = Physics::PhysicsEventKind::CollisionEnter;
        contact.receiver = static_cast<uint32_t>(static_cast<entt::entity>(mover));
        contact.hasContact = true;
        contact.penetration = 0.25f;
        host.DispatchContactEvents(scene, {contact});
        pass &= Check(Near(mover.GetComponent<TransformComponent>().position.z, 0.25f),
                      "collision events reach OnCollisionEnter with contact details");

        const std::size_t before = log.size();
        host.RequestReloadAll();
        Hephaestus::RunSimulationStep(step, 0.5f);
        const bool recompiled = std::any_of(log.begin() + static_cast<std::ptrdiff_t>(before), log.end(),
                                            [](const std::string& line) { return line.find("Compiling scripts/mover.cpp") != std::string::npos; });
        pass &= Check(!recompiled, "an unchanged script reuses its cached DLL");

        WriteFile(moverPath, kMoverEdited);
        Touch(moverPath);
        const float zBefore = mover.GetComponent<TransformComponent>().position.z;
        Hephaestus::RunSimulationStep(step, 0.5f);
        pass &= Check(Near(mover.GetComponent<TransformComponent>().position.z, zBefore + 10.0f),
                      "saving the script during play recompiles and reloads it");
        pass &= Check(CachedDlls(cache, "mover-").size() == 1, "the previous build is removed after a reload");

        host.OnSimulationStopped();
        pass &= Check(host.InstanceCount() == 0, "stopping play destroys every C++ script");
        lua.OnSimulationStopped();
        lua.Shutdown();
    }

    const fs::path package = root / "package";
    char packageError[4096] = {};
    pass &= Check(language->packageScript(root.string().c_str(), moverPath.string().c_str(),
                                          package.string().c_str(), packageError, sizeof(packageError)) == 1 &&
                  CachedDlls(package / ".hephaestus" / "build" / "cpp-scripts", "mover-").size() == 1,
                  "packaging copies the compiled script into the package");
    WriteFile(root / "scripts" / "helpers.cpp", "int Twice(int x) { return x * 2; }\n");
    pass &= Check(language->packageScript(root.string().c_str(), (root / "scripts" / "helpers.cpp").string().c_str(),
                                          package.string().c_str(), packageError, sizeof(packageError)) == 1 &&
                  CachedDlls(package / ".hephaestus" / "build" / "cpp-scripts", "helpers-").empty(),
                  "packaging skips .cpp files without HP_SCRIPT");
    pass &= Check(language->packageScript(root.string().c_str(), brokenPath.string().c_str(),
                                          package.string().c_str(), packageError, sizeof(packageError)) == 0 &&
                  std::string(packageError).find("broken.cpp:3:") != std::string::npos,
                  "packaging fails with the compile errors of a broken script");

    const char* scriptTemplate = ScriptLanguageRegistry::NewScriptTemplate(*language);
    pass &= Check(scriptTemplate != nullptr, "the Content Browser can make new C++ scripts");
    if (scriptTemplate) {
        std::string text = scriptTemplate;
        for (size_t at = text.find("$NAME"); at != std::string::npos; at = text.find("$NAME")) {
            text.replace(at, 5, "Spinner");
        }
        const fs::path spinner = root / "scripts" / "Spinner.cpp";
        WriteFile(spinner, text);
        pass &= Check(language->packageScript(root.string().c_str(), spinner.string().c_str(),
                                              package.string().c_str(), packageError, sizeof(packageError)) == 1,
                      std::string("a new script from the template compiles ") + packageError);
    }

    Hephaestus::EngineLog::Unsubscribe(subscription);
    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::cout << (pass ? "All C++ scripting tests passed\n" : "C++ scripting tests FAILED\n");
    return pass ? 0 : 1;
}
