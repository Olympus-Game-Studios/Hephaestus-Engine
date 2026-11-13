// Loads the built Fab plugin with a minimal plugin context and sends it a Fab in Launcher "Custom (socket port)"
// export the way the launcher does: one JSON document over a local TCP connection. Checks the pack is imported
// through AssetIntake and that a status reply goes back to the launcher's listening port.

#include "Assets/AssetIntake.h"
#include "PluginAPI/PluginAPI.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <map>
#include <string>

namespace fs = std::filesystem;
using namespace Hephaestus::Plugins;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
    }
    return condition;
}

class TestContext final : public IPluginContext, ICommandService, IUIService, IAssetService, ISceneService,
                          IUndoService, IProjectService, ISettingsService {
public:
    explicit TestContext(fs::path project) : m_project(std::move(project)), m_root(m_project.string()) {}

    const char* PluginId() const override { return "com.hephaestus.fab"; }
    ICommandService& Commands() override { return *this; }
    IUIService& UI() override { return *this; }
    IAssetService& Assets() override { return *this; }
    ISceneService& Scene() override { return *this; }
    IUndoService& Undo() override { return *this; }
    IProjectService& Project() override { return *this; }
    ISettingsService& Settings() override { return *this; }

    bool Register(const CommandDescriptor&) override { return true; }
    bool Execute(const char*) override { return false; }
    bool RegisterPanel(const PanelDescriptor&) override { return true; }
    bool RegisterMenuAction(const MenuActionDescriptor&) override { return true; }
    bool RegisterToolbarAction(const ToolbarActionDescriptor&) override { return true; }
    bool RegisterContextAction(const ContextActionDescriptor&) override { return true; }
    bool ShowPanel(const char*) override { return false; }
    bool RegisterAssetType(const AssetTypeDescriptor&) override { return true; }
    bool RegisterImporter(const ImporterDescriptor&) override { return true; }
    bool ImportAssetPack(const AssetPackImportRequest& request, AssetPackImportResult& result) override {
        Hephaestus::AssetIntakeRequest intake;
        intake.source = fs::path(std::u8string(reinterpret_cast<const char8_t*>(request.sourcePath)));
        intake.projectRoot = m_project;
        intake.targetFolder = request.targetFolder;
        intake.credit.title = request.title ? request.title : "";
        intake.credit.source = request.source ? request.source : "";
        intake.credit.license = request.license ? request.license : "";
        Hephaestus::AssetIntakeResult out;
        std::string error;
        const bool ok = Hephaestus::AssetIntake::Import(intake, out, error);
        std::strncpy(result.folder, out.folder.c_str(), sizeof(result.folder) - 1);
        std::strncpy(result.message, error.c_str(), sizeof(result.message) - 1);
        result.prefabCount = static_cast<std::uint32_t>(out.prefabs.size());
        result.materialCount = static_cast<std::uint32_t>(out.materials.size());
        ++imports;
        lastFolder = out.folder;
        return ok;
    }
    bool HasScene() const override { return false; }
    std::uint64_t CreatePrimitive(PrimitiveKind, const char*) override { return 0; }
    bool DeleteEntity(std::uint64_t) override { return false; }
    bool SelectEntity(std::uint64_t) override { return false; }
    std::uint64_t Begin(const char*) override { return 0; }
    bool Commit(std::uint64_t) override { return false; }
    bool Rollback(std::uint64_t) override { return false; }
    const char* ProjectRoot() const override { return m_root.c_str(); }
    const char* PluginRoot() const override { return ""; }
    bool ResolveAssetPath(const char*, char*, std::size_t) const override { return false; }
    bool Register(const SettingDescriptor& descriptor) override {
        m_settings.try_emplace(descriptor.id, descriptor.defaultValue ? descriptor.defaultValue : "");
        return true;
    }
    const char* Get(const char* id) const override {
        const auto found = m_settings.find(id);
        return found == m_settings.end() ? "" : found->second.c_str();
    }
    bool Set(const char* id, const char* value) override {
        m_settings[id] = value ? value : "";
        return true;
    }

    int imports = 0;
    std::string lastFolder;
    std::map<std::string, std::string> m_settings;

private:
    fs::path m_project;
    std::string m_root;
};

void WaitFor(QApplication& app, const std::function<bool()>& done, int milliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < milliseconds) {
        app.processEvents(QEventLoop::AllEvents, 20);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: FabLauncherBridgeTests <repo_root> <HephaestusFab library>\n";
        return 2;
    }
    QApplication app(argc, argv);
    const fs::path repo = argv[1];
    const fs::path scratch = repo / "build" / "fab_launcher_bridge_test_out";
    fs::remove_all(scratch);
    const fs::path project = scratch / "Project";
    fs::create_directories(project / "assets");
    // Laid out like the launcher's library: the export folder holds only the model, and the listing's additional
    // files (extracted beside it) hold the textures.
    const fs::path listing = scratch / "FabDownloads" / "Rusty Barrel-1a2b3c4d"; // a custom download location
    const fs::path download = listing / "source";
    fs::create_directories(download);
    std::ofstream(download / "Rusty_Barrel.obj") << "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nf 1/1 2/1 3/1\n";
    const fs::path extras = listing / "seller_barrel" / "Textures";
    fs::create_directories(extras);
    const fs::path png = repo / "assets" / "textures" / "light_bulb.png";
    fs::copy_file(png, extras / "RustyBarrel_BaseColor.png");
    fs::copy_file(png, extras / "RustyBarrel_Normal.png");

    bool ok = true;
    QLibrary library(QString::fromStdString(argv[2]));
    auto getExports = reinterpret_cast<GetPluginExportsFn>(library.resolve("HP_GetPluginExports"));
    const PluginExports* exports = getExports ? getExports(HP_PLUGIN_API_VERSION) : nullptr;
    if (!Check(exports != nullptr, "the Fab plugin library loads: " + library.errorString().toStdString())) {
        return 1;
    }

    QTcpServer launcher;
    ok &= Check(launcher.listen(QHostAddress::LocalHost), "fake launcher status port opens");
    QByteArray statusReply;
    QObject::connect(&launcher, &QTcpServer::newConnection, [&] {
        QTcpSocket* socket = launcher.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, [socket, &statusReply] { statusReply += socket->readAll(); });
    });

    TestContext context(project);
    context.m_settings["launcherPort"] = "47213";
    IHephaestusPlugin* plugin = exports->create();
    ok &= Check(plugin->OnLoad(context), "the plugin loads");

    // Fab in Launcher 0.115 sends a bare array, after an empty connection that only checks the port is open.
    const QJsonArray payload = {
         QJsonObject{
             {"id", "0f6e3b2a-test"},
             {"path", QString::fromStdString(download.generic_string())},
             {"native_files", QJsonArray{}},
             {"additional_textures", QJsonArray{}},
             {"meshes", QJsonArray{}},
             {"materials", QJsonArray{}},
             {"metadata",
              QJsonObject{{"fab", QJsonObject{{"format", "fbx"},
                                              {"isQuixel", true},
                                              {"quality", "2K"},
                                              {"listing", QJsonObject{{"title", "Rusty Barrel"},
                                                                      {"listingType", "3d-model"},
                                                                      {"category", QJsonObject{{"slug", "props"}}}}}}},
                          {"launcher", QJsonObject{{"version", "test"}, {"listening_port", launcher.serverPort()}}},
                          {"megascans", QJsonObject{}}}}}};
    {
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, 47213);
        ok &= Check(probe.waitForConnected(3000), "the plugin listens on the configured local port");
        probe.disconnectFromHost();
        WaitFor(app, [] { return false; }, 100);
    }
    QTcpSocket sender;
    sender.connectToHost(QHostAddress::LocalHost, 47213);
    ok &= Check(sender.waitForConnected(3000), "the plugin accepts the export after the probe");
    // The launcher may split a large document across writes; send it in two parts with a terminating NUL.
    const QByteArray text = QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\0';
    sender.write(text.left(text.size() / 2));
    sender.flush();
    WaitFor(app, [] { return false; }, 100);
    sender.write(text.mid(text.size() / 2));
    sender.flush();

    WaitFor(app, [&] { return context.imports > 0 && !statusReply.isEmpty(); }, 10000);
    ok &= Check(context.imports == 1 && context.lastFolder == "assets/Imported/Fab/Rusty Barrel",
                "an export is imported into assets/Imported/Fab/<listing title>");
    ok &= Check(fs::exists(project / "assets/Imported/Fab/Rusty Barrel/credits.json"),
                "the export gets a credit with the default Fab licence");
    ok &= Check(fs::exists(project / ".hephaestus/fab-launcher-last.json"), "the last export is kept for diagnosis");
    const fs::path material = project / "assets/Imported/Fab/Rusty Barrel/Materials/Rusty_Barrel.hematerial";
    std::ifstream materialFile(material);
    const std::string materialText((std::istreambuf_iterator<char>(materialFile)), std::istreambuf_iterator<char>());
    ok &= Check(materialText.find("\"useAlbedoTexture\": true") != std::string::npos,
                "textures in the listing's additional files reach a model exported without them");
    while (!statusReply.isEmpty() && statusReply.back() == '\0') {
        statusReply.chop(1);
    }
    const QJsonObject status = QJsonDocument::fromJson(statusReply).object();
    ok &= Check(status["status"].toString() == "success" && status["id"].toString() == "0f6e3b2a-test" &&
                    status["app_name"].toString() == "Hephaestus Engine",
                "the launcher is told the export succeeded: " + statusReply.toStdString());

    plugin->OnUnload();
    exports->destroy(plugin);
    std::cout << (ok ? "FabLauncherBridgeTests passed\n" : "FabLauncherBridgeTests FAILED\n");
    return ok ? 0 : 1;
}
