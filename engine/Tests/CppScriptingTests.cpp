#include "Core/EngineLog.h"
#include "Core/SimulationStep.h"
#include "Physics/PhysicsContactEvents.h"
#include "Scene/Components.h"
#include "Scene/CharacterComponent.h"
#include "Scene/CharacterControllerComponent.h"
#include "Scene/Entity.h"
#include "Scene/PrefabAsset.h"
#include "Scene/PrefabOverrideSystem.h"
#include "Scene/PrefabSerializer.h"
#include "Scene/Scene.h"
#include "Scripting/LuaScriptComponent.h"
#include "Scripting/LuaScriptEngine.h"
#include "Scripting/PluginScriptHost.h"
#include "Scripting/ScriptLanguageRegistry.h"
#include "UI/HephaestusUI/UIButton.h"
#include "UI/HephaestusUI/UICanvas.h"
#include "UI/HephaestusUI/UIContext.h"
#include "UI/HephaestusUI/UIRenderer.h"
#include "UI/HephaestusUI/UIScreen.h"
#include "UI/HephaestusUI/UISystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
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
    HP_FIELD(bool, active, true); // on (by default)
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

const char* kProbe = R"cpp(#include "HephaestusScript.h"
#include <string>

static void Report(const std::string& key, const std::string& value) { hp::Log("probe:" + key + "=" + value); }
static std::string Flag(bool value) { return value ? "1" : "0"; }
static std::string Number(float value) { return std::to_string(static_cast<int>(value * 100.0f + (value < 0 ? -0.5f : 0.5f))); }

class Probe : public hp::Script {
public:
    HP_FIELD(std::string, prefab, "");
    int frame = 0;

    void OnAwake() override {
        hp::Entity self = GetEntity();
        self.SetName("ProbeRenamed");
        Report("name", self.GetName());
        Report("dt", Number(hp::Time::DeltaTime()));
        Report("enemies", std::to_string(hp::Scene::FindAllWithPrefix("Enemy").size()));

        hp::Entity mesh = hp::Scene::Find("Mesh");
        mesh.SetVisible(false);
        Report("meshVisible", Flag(mesh.IsVisible()));
        Report("selfHasMesh", Flag(self.SetVisible(true)));

        Report("cameras", std::to_string(hp::Scene::FindCameras().size()));
        hp::Entity cam2 = hp::Scene::Find("Cam2");
        hp::Scene::SetActiveCamera(cam2);
        Report("activeIsCam2", Flag(hp::Scene::GetActiveCamera() == cam2));
        auto camera = cam2.GetCamera();
        Report("cam2HasCamera", Flag(camera.has_value()));
        if (camera) {
            camera->SetFov(75.0f);
            Report("fov", Number(camera->GetFov()));
            Report("cam1Active", Flag(hp::Scene::Find("Cam1").GetCamera()->IsActive()));
        }
        Report("meshHasNoCamera", Flag(!mesh.GetCamera().has_value()));

        hp::ViewCamera::SetPosition({1, 2, 3});
        hp::ViewCamera::Translate({1, 0, 0});
        Report("viewX", Number(hp::ViewCamera::GetPosition().x));

        hp::Entity ball = hp::Scene::Find("Ball");
        hp::Body body = ball.GetBody();
        Report("ballBody", Flag(body.Exists()));
        Report("selfBody", Flag(self.GetBody().Exists()));
        Report("ballVelY", Number(body.GetLinearVelocity().y));
        body.SetLinearVelocity({0, 9, 0});
        auto hit = Raycast({0, 0, 0}, {0, -1, 0}, 10.0f, "Ground");
        Report("hit", Flag(hit.has_value()));
        if (hit) {
            Report("hitIsBall", Flag(hit->entity == ball));
            Report("hitDistance", Number(hit->distance));
        }
        Report("missWithBadLayer", Flag(!Raycast({0, 0, 0}, {0, -1, 0}, 10.0f, "Nope").has_value()));

        auto controller = hp::Scene::Find("Hero").GetController();
        Report("heroController", Flag(controller.has_value()));
        if (controller) {
            controller->Move(0.5f, -1.0f);
            Report("groundState", controller->GetGroundState());
            Report("speedMultiplier", Number(controller->GetSpeedMultiplier()));
        }
        Report("selfNoController", Flag(!self.GetController().has_value()));
        Report("noAnimator", Flag(!self.GetAnimator().has_value()));
        Report("noSocket", Flag(!self.GetSocket("hand").has_value()));

        Report("audio", Flag(hp::Audio::IsAvailable()));
        Report("sound", std::to_string(hp::Audio::Play("missing.wav")));
        Report("key", Flag(hp::Input::IsKeyDown("space")));

        hp::SpawnOptions options;
        options.position = hp::Vec3{5, 0, 0};
        options.vars.emplace_back("health", 40);
        hp::Entity crate = hp::Scene::Instantiate(prefab, options);
        Report("spawned", Flag(crate.IsValid()));
        Report("spawnX", Number(crate.GetPosition().x));
        Report("missingPrefab", Flag(!hp::Scene::Instantiate("nope.hpprefab")));
    }

    void OnUpdate(float) override {
        if (++frame == 1) {
            hp::Entity doomed = hp::Scene::Find("Doomed");
            hp::Scene::Destroy(doomed);
            Report("doomedDuringPass", Flag(doomed.IsValid()));
            hp::Input::SetCursorMode(hp::CursorMode::Captured);
            Report("cursor", Flag(hp::Input::GetCursorMode() == hp::CursorMode::Captured));
            hp::Application::Quit();
        }
    }
};

HP_SCRIPT(Probe)
)cpp";

const char* kHud = R"cpp(#include "HephaestusScript.h"
#include <string>

static void Report(const std::string& key, bool value) { hp::Log("hud:" + key + "=" + (value ? "1" : "0")); }

class Hud : public hp::Script {
public:
    HP_FIELD(std::string, layout, "");

    void OnAwake() override {
        Report("available", hp::UI::IsAvailable());
        hp::Screen screen = hp::UI::CreateScreen("Hud");
        hp::Widget panel = screen.GetRoot().AddPanel();
        Report("panel", static_cast<bool>(panel));
        Report("background", panel.SetBackgroundColor({1, 0, 0, 1}));
        Report("anchor", panel.SetAnchor(hp::UIAnchor::TopCenter));
        Report("padding", panel.SetPadding(4, 8, 4, 8));
        hp::Widget button = panel.AddButton("Play");
        Report("click", SetOnClick(button, "OnPlay"));
        Report("hover", SetOnHover(button, "OnHoverPlay"));
        Report("buttonStyle", button.SetButtonStyle(hp::ButtonState::Hovered,
                                                    hp::UIStyle().Set("backgroundColor", hp::Color{0, 1, 0, 1})));
        hp::Widget text = panel.AddText("Score");
        Report("text", text.SetText("Score: 10") && text.SetFontSize(24) && text.SetTextShadow(true, {0, 0, 0, 1}));
        Report("wrongKind", !text.SetGridColumns(3));
        Report("textClick", !SetOnClick(text, "OnPlay"));
        Report("theme", hp::UI::RegisterThemeStyle("Danger", hp::UIStyle().Set("backgroundColor", hp::Color{1, 0, 0, 1})
                                                                        .Set("padding", 6)) &&
                        panel.ApplyThemeStyle("Danger"));
        Report("missingTheme", !panel.ApplyThemeStyle("Nope"));
        hp::Scene::Find("LuaTarget").Call("remember", text.Handle());

        hp::Screen shown = hp::UI::Display(layout);
        Report("displayed", static_cast<bool>(shown));
        Report("widget", shown.GetWidget("Title").SetText("Hello"));
        Report("missingWidget", !shown.GetWidget("Nope"));
        std::string error;
        Report("missingFile", !hp::UI::Display("assets/ui/Nope.hui", &error) && !error.empty());
        Report("cursor", hp::UI::SetCursorMode(hp::CursorMode::Normal) &&
                         hp::UI::GetCursorMode() == hp::CursorMode::Normal);
        Report("destroyCurrent", hp::UI::DestroyCurrent());
    }

    bool OnCall(std::string_view function, hp::Args args) override {
        if (function == "OnPlay") {
            Report("clicked", true);
            return true;
        }
        if (function == "OnHoverPlay") {
            Report("hovered", !args.empty() && args[0].AsBool());
            return true;
        }
        return false;
    }
};

HP_SCRIPT(Hud)
)cpp";

const char* kController = R"cpp(#include "HephaestusScript.h"

class Walker : public hp::Script {
public:
    HP_FIELD(float, speed, 1.0f);
    void OnUpdate(float dt) override { GetEntity().Translate({0, 0, speed * dt}); }
    void OnAnimationFinished(std::string_view clip) override { hp::Log("controller finished " + std::string(clip)); }
    bool OnCall(std::string_view function, hp::Args args) override {
        if (function != "Boost") return false;
        GetEntity().Translate({args.empty() ? 0.0f : args[0].AsFloat(), 0, 0});
        return true;
    }
};

HP_SCRIPT(Walker)
)cpp";

const char* kLifecycle = R"cpp(#include "HephaestusScript.h"

class Life : public hp::Script {
public:
    void OnAwake() override { hp::Log("life:awake"); GetEntity().CallController("Boost", 1); }
    void OnEnable() override { hp::Log("life:enable"); }
    void OnDisable() override { hp::Log("life:disable"); }
    void OnDestroy() override { hp::Log("life:destroy"); }
    void OnAnimationFinished(std::string_view clip) override { hp::Log("gameplay finished " + std::string(clip)); }
};

HP_SCRIPT(Life)
)cpp";

const char* kDoomed = R"cpp(#include "HephaestusScript.h"

class Doomed : public hp::Script {
public:
    HP_FIELD(bool, killer, false);
    std::string name;
    void OnAwake() override { name = GetEntity().GetName(); }
    void OnUpdate(float) override {
        if (killer) hp::Scene::Destroy(hp::Scene::Find("Victim"));
    }
    void OnLateUpdate(float) override { hp::Log("late:" + name); }
    void OnDestroy() override { hp::Log("destroyed script"); }
};

HP_SCRIPT(Doomed)
)cpp";

const char* kReloader = R"cpp(#include "HephaestusScript.h"

class Reloader : public hp::Script {
public:
    void OnAwake() override { hp::Log(hp::Scene::Reload() ? "reload accepted" : "reload refused"); }
    void OnUpdate(float) override { hp::Log("updated after reload"); }
};

HP_SCRIPT(Reloader)
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

    {
        log.clear();
        const fs::path probePath = root / "scripts" / "probe.cpp";
        WriteFile(probePath, kProbe);

        Scene scratch("Scratch");
        Entity crateRoot = scratch.CreateEntity("Crate");
        crateRoot.AddComponent<LuaScriptComponent>("scripts/crate.lua").variables["health"] = PropertyValue(10.0);
        PrefabAsset crate;
        const fs::path cratePath = root / "assets" / "prefabs" / "Crate.hpprefab";
        fs::create_directories(cratePath.parent_path());
        std::string prefabError;
        pass &= Check(PrefabOverrideSystem::CaptureHierarchy(scratch, crateRoot, crate) &&
                      PrefabSerializer::Serialize(crate, cratePath, prefabError),
                      "test prefab saved " + prefabError);
        PrefabOverrideSystem::InvalidateAssetCache(cratePath.generic_string());

        Scene scene("Probe");
        Entity prober = scene.CreateEntity("Prober");
        prober.AddComponent<LuaScriptComponent>("scripts/probe.cpp").variables["prefab"] =
            PropertyValue(cratePath.generic_string());
        scene.CreateEntity("Enemy1");
        scene.CreateEntity("Enemy2");
        scene.CreateEntity("Mesh").AddComponent<MeshRendererComponent>();
        scene.CreateEntity("Cam1").AddComponent<CameraComponent>().isActive = true;
        scene.CreateEntity("Cam2").AddComponent<CameraComponent>();
        Entity ball = scene.CreateEntity("Ball");
        Entity hero = scene.CreateEntity("Hero");
        hero.AddComponent<CharacterControllerComponent>();
        Entity doomed = scene.CreateEntity("Doomed");
        const uint32_t ballId = static_cast<uint32_t>(static_cast<entt::entity>(ball));
        const uint32_t heroId = static_cast<uint32_t>(static_cast<entt::entity>(hero));

        LuaScriptEngine lua;
        PluginScriptHost host;
        lua.Initialize(&scene, nullptr);
        lua.SetProjectRoot(root);
        lua.SetPluginScriptHost(&host);
        host.SetLuaEngine(&lua);

        Vec3 ballVelocity(0.0f, 2.0f, 0.0f);
        PhysicsBinding physics;
        physics.hasBody = [&](uint32_t id) { return id == ballId; };
        physics.getLinearVelocity = [&](uint32_t id, Vec3& out) { out = ballVelocity; return id == ballId; };
        physics.setLinearVelocity = [&](uint32_t id, const Vec3& v, bool) {
            if (id == ballId) ballVelocity = v;
            return id == ballId;
        };
        physics.raycast = [&](const std::vector<uint32_t>& ignore, const Vec3&, const Vec3&, float,
                              PhysicsRaycastHit& hit, const char* layer, std::string& error, bool) {
            if (layer && std::string(layer) != "Ground") {
                error = std::string("unknown layer ") + layer;
                return false;
            }
            hit.hit = std::find(ignore.begin(), ignore.end(), ballId) == ignore.end();
            hit.distance = 2.5f;
            hit.entityId = ballId;
            return true;
        };
        lua.SetPhysicsBinding(physics);
        Vec3 heroWish(0.0f, 0.0f, 0.0f);
        CharacterBinding characters;
        characters.move = [&](uint32_t id, float x, float z) { heroWish = Vec3(x, 0.0f, z); return id == heroId; };
        characters.getGroundState = [&](uint32_t id, std::string& out) { out = "walking"; return id == heroId; };
        characters.getSpeedMultiplier = [&](uint32_t id, float& out) { out = 1.5f; return id == heroId; };
        lua.SetCharacterBinding(characters);
        Vec3 viewPosition(0.0f, 0.0f, 0.0f);
        CameraBinding view;
        view.setPosition = [&](const Vec3& v) { viewPosition = v; };
        view.getPosition = [&]() { return viewPosition; };
        lua.SetCameraBinding(view);
        bool quit = false;
        lua.SetQuitRequestCallback([&]() { quit = true; });
        lua.OnSimulationStarted();

        Hephaestus::SimulationStepContext step;
        step.scene = &scene;
        step.lua = &lua;
        step.pluginScripts = &host;
        Hephaestus::RunSimulationStep(step, 0.25f);

        const auto probe = [&log](const std::string& key, const std::string& value) {
            const std::string line = "probe:" + key + "=" + value;
            return std::any_of(log.begin(), log.end(), [&](const std::string& entry) { return entry == line; });
        };
        pass &= Check(probe("name", "ProbeRenamed"), "entities can be renamed");
        pass &= Check(probe("dt", "25"), "Time::DeltaTime matches the frame");
        pass &= Check(probe("enemies", "2"), "Scene::FindAllWithPrefix finds entities by name");
        pass &= Check(probe("meshVisible", "0") && probe("selfHasMesh", "0"),
                      "meshes can be hidden, and entities without one report it");
        pass &= Check(probe("cameras", "2") && probe("activeIsCam2", "1") && probe("cam2HasCamera", "1") &&
                      probe("cam1Active", "0") && probe("meshHasNoCamera", "1"),
                      "cameras are found and activating one deactivates the others");
        pass &= Check(probe("fov", "7500"), "camera lenses can be changed");
        pass &= Check(probe("viewX", "200") && Near(viewPosition.z, 3.0f), "the view camera moves through its binding");
        pass &= Check(probe("ballBody", "1") && probe("selfBody", "0") && probe("ballVelY", "200") &&
                      Near(ballVelocity.y, 9.0f),
                      "physics bodies report and take velocities");
        pass &= Check(probe("hit", "1") && probe("hitIsBall", "1") && probe("hitDistance", "250"),
                      "raycasts return the hit and its entity");
        pass &= Check(probe("missWithBadLayer", "1") && logged("unknown layer Nope"),
                      "a raycast with an unknown layer misses and logs why");
        pass &= Check(probe("heroController", "1") && probe("groundState", "walking") &&
                      probe("speedMultiplier", "150") && Near(heroWish.x, 0.5f) && Near(heroWish.z, -1.0f),
                      "character controllers move and report through their binding");
        pass &= Check(probe("selfNoController", "1") && probe("noAnimator", "1") && probe("noSocket", "1"),
                      "missing components come back empty");
        pass &= Check(probe("audio", "0") && probe("sound", "0") && probe("key", "0"),
                      "audio and input answer safely without a backend");
        pass &= Check(probe("spawned", "1") && probe("spawnX", "500") && probe("missingPrefab", "1"),
                      "prefabs spawn where asked, and a missing prefab gives an empty entity");
        bool overridden = false;
        for (Entity spawned : scene.FindEntitiesWithNamePrefix("Crate")) {
            if (!spawned.HasComponent<LuaScriptComponent>()) {
                continue;
            }
            const auto& variables = spawned.GetComponent<LuaScriptComponent>().variables;
            const auto health = variables.find("health");
            overridden |= health != variables.end() && health->second.AsNumber() &&
                          *health->second.AsNumber() == 40.0;
        }
        pass &= Check(overridden, "spawn options override the prefab's Inspector values");
        pass &= Check(probe("doomedDuringPass", "1") && !scene.GetRegistry().valid(static_cast<entt::entity>(doomed)),
                      "Scene::Destroy waits for the end of the script pass");
        pass &= Check(probe("cursor", "1") && lua.GetCursorMode() == 1, "the cursor can be captured");
        pass &= Check(quit, "Application::Quit asks the host to quit");

        host.OnSimulationStopped();
        lua.OnSimulationStopped();
        lua.Shutdown();
    }

    {
        log.clear();
        WriteFile(root / "scripts" / "hud.cpp", kHud);
        const fs::path layoutPath = root / "assets" / "ui" / "Menu.hui";
        WriteFile(layoutPath, R"({"version": 1, "controller": "", "root": {"type": "Panel", "id": "MenuRoot",
            "children": [{"type": "Text", "id": "Title", "properties": {"text": "Menu"}}]}})");

        Scene scene("Hud");
        scene.CreateEntity("HudOwner").AddComponent<LuaScriptComponent>("scripts/hud.cpp").variables["layout"] =
            PropertyValue(layoutPath.generic_string());
        scene.CreateEntity("LuaTarget").AddComponent<LuaScriptComponent>().scriptSource = R"lua(
function remember(handle) _G.luaSetText = UI.setText(handle, "Set from Lua") end
)lua";

        LuaScriptEngine lua;
        PluginScriptHost host;
        lua.Initialize(&scene, nullptr);
        lua.SetProjectRoot(root);
        Hephaestus::HephaestusUI::UISystem uiSystem;
        Hephaestus::HephaestusUI::UIContext uiContext;
        Hephaestus::HephaestusUI::UIRenderer uiRenderer;
        lua.SetUIBindings(&uiSystem, &uiContext, &uiRenderer);
        lua.SetPluginScriptHost(&host);
        host.SetLuaEngine(&lua);
        lua.OnSimulationStarted();

        Hephaestus::SimulationStepContext step;
        step.scene = &scene;
        step.lua = &lua;
        step.pluginScripts = &host;
        Hephaestus::RunSimulationStep(step, 0.1f);
        Hephaestus::RunSimulationStep(step, 0.1f);

        const auto hud = [&log](const std::string& key) {
            const std::string line = "hud:" + key + "=1";
            return std::any_of(log.begin(), log.end(), [&](const std::string& entry) { return entry == line; });
        };
        pass &= Check(hud("available") && hud("panel"), "C++ scripts build UI screens and widgets");
        pass &= Check(hud("background") && hud("anchor") && hud("padding") && hud("text"),
                      "widget properties can be set from C++");
        pass &= Check(hud("wrongKind") && hud("textClick"), "properties a widget does not have are refused");
        pass &= Check(hud("buttonStyle") && hud("theme") && hud("missingTheme"),
                      "button styles and theme styles apply");
        pass &= Check(lua.ExecuteString("assert(_G.luaSetText == true)"),
                      "Lua can change a widget a C++ script made");
        pass &= Check(hud("displayed") && hud("widget") && hud("missingWidget"),
                      "UI::Display shows a .hui layout and finds its widgets by ID");
        pass &= Check(hud("missingFile") && hud("cursor") && hud("destroyCurrent"),
                      "display errors, cursor mode and destroy work");

        Hephaestus::HephaestusUI::UIButton* button = nullptr;
        if (const auto screen = uiSystem.FindScreenByName("Hud")) {
            std::function<void(Hephaestus::HephaestusUI::UIElement*)> find = [&](Hephaestus::HephaestusUI::UIElement* e) {
                if (auto* b = dynamic_cast<Hephaestus::HephaestusUI::UIButton*>(e)) button = b;
                for (auto& child : e->GetChildren()) find(child.get());
            };
            find(screen->GetRootCanvas());
        }
        pass &= Check(button != nullptr, "the C++ button is in the UI system");
        if (button) {
            button->OnClicked();
            button->OnHoverEnter();
        }
        pass &= Check(hud("clicked") && hud("hovered"), "button clicks and hovers reach the C++ script's OnCall");

        host.OnSimulationStopped();
        lua.OnSimulationStopped();
        if (button && uiSystem.FindScreenByName("Hud")) {
            button->OnClicked();
        }
        lua.Shutdown();
    }

    {
        log.clear();
        WriteFile(root / "scripts" / "walker.cpp", kController);
        WriteFile(root / "scripts" / "life.cpp", kLifecycle);
        Scene scene("Controllers");
        Entity hero = scene.CreateEntity("Hero");
        auto& character = hero.AddComponent<CharacterComponent>();
        character.controllerScriptPath = "scripts/walker.cpp";
        character.controllerVariables["speed"] = PropertyValue(4.0);
        hero.AddComponent<LuaScriptComponent>("scripts/life.cpp");
        scene.CreateEntity("LuaCaller").AddComponent<LuaScriptComponent>().scriptSource = R"lua(
function onUpdate(dt)
    if not _G.boosted then
        local hero = Scene.findAllWithPrefix("Hero")[1]
        _G.boosted = hero ~= nil and hero:CallController("Boost", 10)
    end
end
)lua";

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

        const Vec3 position = hero.GetComponent<TransformComponent>().position;
        pass &= Check(host.InstanceCount() == 2, "an entity runs a C++ gameplay script and a C++ character controller");
        pass &= Check(Near(position.z, 4.0f), "the controller script runs with its controller variables");
        pass &= Check(Near(position.x, 11.0f) && lua.ExecuteString("assert(_G.boosted == true)"),
                      "CallController reaches the C++ controller from C++ and from Lua");

        auto& player = hero.GetComponent<CharacterComponent>().player;
        player.finishedThisFrame = true;
        player.finishedClip = "Wave";
        host.DispatchAnimationFinished(scene);
        pass &= Check(logged("controller finished Wave") && logged("gameplay finished Wave"),
                      "OnAnimationFinished reaches both of the entity's C++ scripts");

        hero.GetComponent<LuaScriptComponent>().enabled = false;
        Hephaestus::RunSimulationStep(step, 0.5f);
        std::vector<std::string> life;
        for (const std::string& line : log) {
            if (line.rfind("life:", 0) == 0) life.push_back(line);
        }
        pass &= Check(life == std::vector<std::string>{"life:awake", "life:enable", "life:disable", "life:destroy"},
                      "OnEnable follows OnAwake and OnDisable precedes OnDestroy, as in Lua");
        hero.GetComponent<CharacterComponent>().runController = false;
        Hephaestus::RunSimulationStep(step, 0.5f);
        pass &= Check(host.InstanceCount() == 0, "turning the controller off destroys its script");

        host.OnSimulationStopped();
        lua.OnSimulationStopped();
        lua.Shutdown();
    }

    {
        log.clear();
        WriteFile(root / "scripts" / "doomed.cpp", kDoomed);
        WriteFile(root / "scripts" / "reloader.cpp", kReloader);
        Scene scene("Lifetimes");
        scene.CreateEntity("Killer").AddComponent<LuaScriptComponent>("scripts/doomed.cpp").variables["killer"] =
            PropertyValue(true);
        scene.CreateEntity("Victim").AddComponent<LuaScriptComponent>("scripts/doomed.cpp");

        LuaScriptEngine lua;
        PluginScriptHost host;
        lua.Initialize(&scene, nullptr);
        lua.SetProjectRoot(root);
        lua.SetPluginScriptHost(&host);
        host.SetLuaEngine(&lua);
        lua.SetSceneReloadCallback([](const std::string&) { return true; });
        lua.OnSimulationStarted();
        Hephaestus::SimulationStepContext step;
        step.scene = &scene;
        step.lua = &lua;
        step.pluginScripts = &host;
        Hephaestus::RunSimulationStep(step, 0.1f);
        pass &= Check(logged("late:Killer") && !logged("late:Victim") && logged("destroyed script") &&
                      host.InstanceCount() == 1,
                      "a script destroyed during the update pass gets no later callbacks");

        log.clear();
        scene.CreateEntity("Reloader").AddComponent<LuaScriptComponent>("scripts/reloader.cpp");
        Hephaestus::RunSimulationStep(step, 0.1f);
        pass &= Check(logged("reload accepted") && !logged("updated after reload"),
                      "no script updates in a scene that is being reloaded");

        host.OnSimulationStopped();
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
        packageError[0] = 0;
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
