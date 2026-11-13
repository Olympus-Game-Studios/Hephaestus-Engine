#include "PluginAPI/PluginAPI.h"
#include "../../Common/AssetImportText.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <string>

using namespace Hephaestus::Plugins;

namespace {

constexpr const char* kFab = "https://www.fab.com/";
constexpr const char* kPackExtensions = "*.zip;*.fbx;*.glb;*.gltf;*.obj";
constexpr const char* kListenSetting = "launcherListen";
constexpr const char* kPortSetting = "launcherPort";
constexpr const char* kLicenseSetting = "launcherLicense";
constexpr quint16 kLauncherStatusPort = 24563; // where the launcher's export plugins report back
constexpr qsizetype kMaxPayloadBytes = 16 * 1024 * 1024;

struct LicenseChoice {
    const char* label;
    const char* url;
};
constexpr LicenseChoice kLicenses[] = {
    {"Fab Standard License", "https://www.fab.com/eula"},
    {"Fab Personal Use License", "https://www.fab.com/eula"},
    {"CC Attribution", "http://creativecommons.org/licenses/by/4.0/"},
    {"CC0 Public Domain", "http://creativecommons.org/publicdomain/zero/1.0/"},
};

bool IsPersonalOnly(const QString& license) {
    return license.contains("Personal", Qt::CaseInsensitive) ||
           license.contains("NonCommercial", Qt::CaseInsensitive);
}

QString NameFor(const QString& path) {
    const QFileInfo info(path);
    return info.isDir() ? info.fileName() : info.completeBaseName();
}

bool ImportPack(IPluginContext& context, const QString& path, const QString& targetFolder, const QString& title,
                const QString& listing, const QString& seller, const QString& license, QString& summary) {
    QString licenseUrl;
    for (const LicenseChoice& choice : kLicenses) {
        if (license == choice.label) {
            licenseUrl = choice.url;
        }
    }
    const QByteArray source = path.toUtf8();
    const QByteArray target = targetFolder.toUtf8();
    const QByteArray titleText = title.toUtf8();
    const QByteArray listingText = listing.toUtf8();
    const QByteArray sellerText = seller.toUtf8();
    const QByteArray licenseText = license.toUtf8();
    const QByteArray licenseUrlText = licenseUrl.toUtf8();
    AssetPackImportRequest request;
    request.sourcePath = source.constData();
    request.targetFolder = target.constData();
    request.title = titleText.constData();
    request.source = "Fab";
    request.sourceUrl = listingText.constData();
    request.author = sellerText.constData();
    request.license = licenseText.constData();
    request.licenseUrl = licenseUrlText.constData();
    AssetPackImportResult result;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = context.Assets().ImportAssetPack(request, result);
    QApplication::restoreOverrideCursor();
    summary = ok ? ImportSummary(result) : QObject::tr("Import failed: %1").arg(QString::fromUtf8(result.message));
    return ok;
}

/// The port set for the Custom (socket port) export target in Fab in Launcher, read from the launcher's settings.
quint16 LauncherSocketPort() {
#ifdef __APPLE__
    const QString folder = "Epic/FabPlugins/settings_v1.json";
#else
    const QString folder = "FabPlugins/settings_v1.json";
#endif
    const QString settings = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(folder);
    QFile file(settings);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const int port = QJsonDocument::fromJson(file.readAll())
                         .object()["plugins"].toObject()["socketExport"].toObject()["port"].toInt();
    return port >= 1024 && port <= 65535 ? static_cast<quint16>(port) : 0;
}

bool HasImages(const QString& folder) {
    QDirIterator it(folder, {"*.png", "*.jpg", "*.jpeg", "*.tga", "*.bmp"}, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext();
}

/// The listing's folder in the launcher's download location ("<title>-<8 hex digits>", the export path itself or up
/// to three folders above it, wherever the user keeps downloads), which also holds the listing's additional files
/// the launcher extracted beside the export; empty when none is found.
QString ListingFolder(const QString& path) {
    static const QRegularExpression listingName(QStringLiteral("-[0-9a-fA-F]{8}$"));
    QDir dir(path);
    for (int depth = 0; depth < 4; ++depth) {
        if (listingName.match(dir.dirName()).hasMatch()) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

/// Receives Fab in Launcher's Custom (socket port) exports: after the launcher downloads a listing it connects to
/// 127.0.0.1:<port> and sends one JSON document {"Assets": [...]} (see docs/code-notes for the fields used).
class LauncherBridge final : public QObject {
public:
    using Report = std::function<void(const QString& status)>;

    explicit LauncherBridge(IPluginContext& context) : m_context(context), m_server(new QTcpServer(this)) {
        connect(m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_server->nextPendingConnection()) {
                Accept(socket);
            }
        });
    }

    bool Listening() const { return m_server->isListening(); }
    quint16 Port() const { return m_server->serverPort(); }
    void SetReport(Report report) { m_report = std::move(report); }

    /// Applies the plugin's settings: on/off and the port (empty = the launcher's own setting).
    void Apply() {
        m_server->close();
        const bool listen = QString::fromUtf8(m_context.Settings().Get(kListenSetting)) != "false";
        bool parsed = false;
        quint16 port = static_cast<quint16>(QString::fromUtf8(m_context.Settings().Get(kPortSetting)).toUInt(&parsed));
        if (!parsed || port < 1024) {
            port = LauncherSocketPort();
        }
        if (!listen) {
            Status(tr("Not listening for Fab Launcher exports."));
        } else if (port == 0) {
            Status(tr("In the Epic Games Launcher, set Fab's export target to Custom (socket port), or enter its port "
                      "here."));
        } else if (!m_server->listen(QHostAddress::LocalHost, port)) {
            Status(tr("Could not listen on port %1: %2").arg(port).arg(m_server->errorString()));
        } else {
            Status(tr("Listening for Fab Launcher exports on port %1. Click Export on a listing in the launcher to "
                      "import it into the open project.").arg(port));
        }
    }

private:
    void Status(const QString& text) {
        m_status = text;
        if (m_report) {
            m_report(text);
        }
    }

    void Accept(QTcpSocket* socket) {
        socket->setParent(this);
        auto buffer = std::make_shared<QByteArray>();
        auto handled = std::make_shared<bool>(false);
        auto tryHandle = [this, socket, buffer, handled](bool finished) {
            if (*handled) {
                return;
            }
            QByteArray text = *buffer;
            while (!text.isEmpty() && (text.back() == '\0' || text.back() == '\n' || text.back() == '\r')) {
                text.chop(1);
            }
            QJsonParseError error{};
            const QJsonDocument document = QJsonDocument::fromJson(text, &error);
            if (error.error != QJsonParseError::NoError) {
                if (finished && !text.isEmpty()) {
                    *handled = true;
                    Status(tr("Ignored a Fab Launcher message that was not valid JSON."));
                }
                return;
            }
            *handled = true;
            socket->disconnectFromHost();
            KeepLastMessage(text);
            HandlePayload(document.isArray() ? document.array() : document.object()["Assets"].toArray());
        };
        connect(socket, &QTcpSocket::readyRead, this, [socket, buffer, tryHandle] {
            buffer->append(socket->readAll());
            if (buffer->size() > kMaxPayloadBytes) {
                socket->abort();
                return;
            }
            tryHandle(false);
        });
        connect(socket, &QTcpSocket::disconnected, this, [socket, tryHandle] {
            tryHandle(true);
            socket->deleteLater();
        });
    }

    // The last export, kept in the project's .hephaestus folder for diagnosing an import that went wrong.
    void KeepLastMessage(const QByteArray& text) {
        if (!*m_context.Project().ProjectRoot()) {
            return;
        }
        const QDir project(QString::fromUtf8(m_context.Project().ProjectRoot()));
        project.mkpath(".hephaestus");
        QFile file(project.filePath(".hephaestus/fab-launcher-last.json"));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(text);
        }
    }

    // Fab in Launcher sends a bare array of assets; Epic's Unreal receiver also reads {"Assets": [...]}.
    void HandlePayload(const QJsonArray& assets) {
        if (assets.isEmpty()) {
            Status(tr("A Fab Launcher export arrived with no assets (saved in .hephaestus/fab-launcher-last.json)."));
            return;
        }
        for (const QJsonValue& value : assets) {
            const QJsonObject asset = value.toObject();
            const QString id = asset["id"].toString();
            const QString path = asset["path"].toString();
            const QJsonObject fab = asset["metadata"].toObject()["fab"].toObject();
            const int replyPort = asset["metadata"].toObject()["launcher"].toObject()["listening_port"].toInt();
            QString title = fab["listing"].toObject()["title"].toString();
            if (title.isEmpty()) {
                title = id.isEmpty() ? NameFor(path) : id;
            }
            QString summary;
            bool ok = false;
            if (!*m_context.Project().ProjectRoot()) {
                summary = tr("Open a project in Hephaestus before exporting from Fab.");
            } else if (path.isEmpty() || !QFileInfo(path).exists()) {
                summary = tr("The exported files were not found at %1").arg(path);
            } else {
                const QString name = QString::fromStdString(SafeFileName(title.toStdString()));
                const QString license = QString::fromUtf8(m_context.Settings().Get(kLicenseSetting));
                // Some sellers keep the textures in additional files only; import the whole listing then.
                const QString listing = ListingFolder(path);
                const QString source = !HasImages(path) && !listing.isEmpty() && HasImages(listing) ? listing : path;
                ok = ImportPack(m_context, source, "assets/Imported/Fab/" + name, title, {}, {},
                                license.isEmpty() ? QString(kLicenses[0].label) : license, summary);
            }
            Status(tr("%1: %2").arg(title, summary));
            ReplyToLauncher(ok ? "success" : "critical", summary, id, path,
                            replyPort > 0 && replyPort <= 65535 ? static_cast<quint16>(replyPort) : kLauncherStatusPort);
        }
    }

    // The launcher shows each export plugin's result; it listens for one JSON status per connection.
    void ReplyToLauncher(const QString& status, const QString& message, const QString& id, const QString& path,
                         quint16 port) {
        const QJsonObject reply = {{"status", status},
                                   {"id", id},
                                   {"path", path},
                                   {"plugin_version", "1.0.0"},
                                   {"message", message.left(400)},
                                   {"app_name", "Hephaestus Engine"},
                                   {"app_version", ""}};
        auto* sender = new QTcpSocket(this);
        connect(sender, &QTcpSocket::connected, sender, [sender, reply] {
            sender->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\0');
            sender->disconnectFromHost();
        });
        connect(sender, &QTcpSocket::disconnected, sender, &QObject::deleteLater);
        connect(sender, &QTcpSocket::errorOccurred, sender, &QObject::deleteLater);
        sender->connectToHost(QHostAddress::LocalHost, port);
    }

    IPluginContext& m_context;
    QTcpServer* m_server = nullptr;
    Report m_report;
    QString m_status;

public:
    const QString& StatusText() const { return m_status; }
};

class FabPanel final : public QWidget {
public:
    FabPanel(IPluginContext& context, LauncherBridge& bridge, QWidget* parent)
        : QWidget(parent), m_context(context), m_bridge(&bridge) {
        setAcceptDrops(true);
        auto* layout = new QVBoxLayout(this);

        auto* launcherTitle = new QLabel(tr("<b>From the Epic Games Launcher</b>"), this);
        layout->addWidget(launcherTitle);
        auto* launcherIntro = new QLabel(
            tr("In the launcher, open Fab's settings and set the export target to Custom (socket port). Then "
               "Export on any listing in your library lands in the open project as prefabs with materials and a "
               "credit.\n\nTip: many sellers keep textures in additional content. For the best result click "
               "Download only, then the ... menu > Download additional content, then Export."),
            this);
        launcherIntro->setWordWrap(true);
        layout->addWidget(launcherIntro);
        auto* launcherForm = new QFormLayout();
        m_listen = new QCheckBox(tr("Listen for launcher exports"), this);
        m_listen->setChecked(QString::fromUtf8(context.Settings().Get(kListenSetting)) != "false");
        launcherForm->addRow(m_listen);
        m_port = new QSpinBox(this);
        m_port->setRange(0, 65535);
        m_port->setSpecialValueText(tr("Same as the launcher"));
        m_port->setValue(QString::fromUtf8(context.Settings().Get(kPortSetting)).toInt());
        launcherForm->addRow(tr("Port"), m_port);
        m_launcherLicense = new QComboBox(this);
        m_launcherLicense->setEditable(true);
        for (const LicenseChoice& choice : kLicenses) {
            m_launcherLicense->addItem(QString::fromUtf8(choice.label));
        }
        const QString savedLicense = QString::fromUtf8(context.Settings().Get(kLicenseSetting));
        if (!savedLicense.isEmpty()) {
            m_launcherLicense->setCurrentText(savedLicense);
        }
        launcherForm->addRow(tr("Credit licence"), m_launcherLicense);
        layout->addLayout(launcherForm);
        m_launcherStatus = new QLabel(bridge.StatusText(), this);
        m_launcherStatus->setWordWrap(true);
        layout->addWidget(m_launcherStatus);

        auto* fileTitle = new QLabel(tr("<b>From a download</b>"), this);
        layout->addWidget(fileTitle);
        auto* intro = new QLabel(
            tr("Or download a pack from Fab in a format such as FBX, glTF or OBJ rather than Unreal Engine, then "
               "drop the .zip, folder or model here. Check each listing's licence: some content may only be used in "
               "Unreal Engine."),
            this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto* openFab = new QPushButton(tr("Open Fab"), this);
        layout->addWidget(openFab, 0, Qt::AlignLeft);

        auto* form = new QFormLayout();
        auto* sourceRow = new QHBoxLayout();
        m_source = new QLineEdit(this);
        m_source->setPlaceholderText(tr("Drop a .zip, folder or model here"));
        auto* chooseFile = new QPushButton(tr("File..."), this);
        auto* chooseFolder = new QPushButton(tr("Folder..."), this);
        sourceRow->addWidget(m_source, 1);
        sourceRow->addWidget(chooseFile);
        sourceRow->addWidget(chooseFolder);
        form->addRow(tr("Pack"), sourceRow);
        m_name = new QLineEdit(this);
        form->addRow(tr("Name"), m_name);
        m_listing = new QLineEdit(this);
        m_listing->setPlaceholderText(tr("https://www.fab.com/listings/..."));
        form->addRow(tr("Fab listing"), m_listing);
        m_seller = new QLineEdit(this);
        form->addRow(tr("Seller"), m_seller);
        m_license = new QComboBox(this);
        m_license->setEditable(true);
        for (const LicenseChoice& choice : kLicenses) {
            m_license->addItem(QString::fromUtf8(choice.label));
        }
        form->addRow(tr("Licence"), m_license);
        layout->addLayout(form);
        m_credit = new QCheckBox(tr("Add to the game's credits"), this);
        m_credit->setChecked(true);
        layout->addWidget(m_credit);
        m_warning = new QLabel(this);
        m_warning->setWordWrap(true);
        layout->addWidget(m_warning);
        m_import = new QPushButton(tr("Import"), this);
        layout->addWidget(m_import, 0, Qt::AlignRight);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
        layout->addStretch(1);

        QPointer<FabPanel> self = this;
        bridge.SetReport([self](const QString& status) {
            if (self) {
                self->m_launcherStatus->setText(status);
            }
        });
        connect(m_listen, &QCheckBox::toggled, this, [this](bool on) {
            m_context.Settings().Set(kListenSetting, on ? "true" : "false");
            if (m_bridge) m_bridge->Apply();
        });
        connect(m_port, &QSpinBox::editingFinished, this, [this] {
            m_context.Settings().Set(kPortSetting, m_port->value() == 0
                                                       ? ""
                                                       : QByteArray::number(m_port->value()).constData());
            if (m_bridge) m_bridge->Apply();
        });
        connect(m_launcherLicense, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            m_context.Settings().Set(kLicenseSetting, text.toUtf8().constData());
        });
        connect(openFab, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(kFab)); });
        connect(chooseFile, &QPushButton::clicked, this, [this] {
            SetSource(QFileDialog::getOpenFileName(this, tr("Import Fab pack"), {},
                                                   tr("Asset packs (*.zip *.fbx *.glb *.gltf *.obj)")));
        });
        connect(chooseFolder, &QPushButton::clicked, this,
                [this] { SetSource(QFileDialog::getExistingDirectory(this, tr("Import Fab pack folder"))); });
        connect(m_source, &QLineEdit::textChanged, this, [this] { Update(); });
        connect(m_name, &QLineEdit::textChanged, this, [this] { Update(); });
        connect(m_license, &QComboBox::currentTextChanged, this, [this] { Update(); });
        connect(m_import, &QPushButton::clicked, this, [this] { Import(); });
        Update();
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls() && event->mimeData()->urls().first().isLocalFile()) {
            event->acceptProposedAction();
        }
    }
    void dropEvent(QDropEvent* event) override {
        SetSource(event->mimeData()->urls().first().toLocalFile());
        event->acceptProposedAction();
    }

private:
    void SetSource(const QString& path) {
        if (path.isEmpty()) {
            return;
        }
        m_source->setText(QDir::toNativeSeparators(path));
        m_name->setText(NameFor(path));
        m_status->clear();
    }

    void Update() {
        m_import->setEnabled(!m_source->text().trimmed().isEmpty() && !m_name->text().trimmed().isEmpty());
        m_warning->setText(IsPersonalOnly(m_license->currentText())
                               ? tr("This licence does not allow use in a game you sell.")
                               : QString());
    }

    void Import() {
        if (!*m_context.Project().ProjectRoot()) {
            QMessageBox::warning(this, tr("Fab"), tr("Open a project before importing packs."));
            return;
        }
        const QString name = QString::fromStdString(SafeFileName(m_name->text().trimmed().toStdString()));
        const bool credit = m_credit->isChecked();
        QString summary;
        ImportPack(m_context, QDir::fromNativeSeparators(m_source->text().trimmed()), "assets/Imported/Fab/" + name,
                   credit ? m_name->text().trimmed() : QString(), credit ? m_listing->text().trimmed() : QString(),
                   credit ? m_seller->text().trimmed() : QString(), credit ? m_license->currentText() : QString(),
                   summary);
        m_status->setText(summary);
    }

    IPluginContext& m_context;
    QPointer<LauncherBridge> m_bridge;
    QCheckBox* m_listen = nullptr;
    QSpinBox* m_port = nullptr;
    QComboBox* m_launcherLicense = nullptr;
    QLabel* m_launcherStatus = nullptr;
    QLineEdit* m_source = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_listing = nullptr;
    QLineEdit* m_seller = nullptr;
    QComboBox* m_license = nullptr;
    QCheckBox* m_credit = nullptr;
    QLabel* m_warning = nullptr;
    QPushButton* m_import = nullptr;
    QLabel* m_status = nullptr;
};

class FabPlugin final : public IHephaestusPlugin {
public:
    bool OnLoad(IPluginContext& context) override {
        m_context = &context;
        bool ok = context.Settings().Register({kListenSetting, "Listen for Fab Launcher exports",
                                               "Import listings exported from Fab in the Epic Games Launcher with "
                                               "the Custom (socket port) target.",
                                               "true", SettingScope::Editor});
        ok &= context.Settings().Register({kPortSetting, "Fab Launcher port",
                                           "Empty uses the port set in the launcher.", "", SettingScope::Editor});
        ok &= context.Settings().Register({kLicenseSetting, "Fab Launcher credit licence",
                                           "Licence recorded for assets exported from the launcher.",
                                           kLicenses[0].label, SettingScope::Editor});
        // Created before the panel, whose factory runs inside RegisterPanel.
        m_bridge = new LauncherBridge(context);
        m_bridge->Apply();
        ok &= context.Commands().Register({"fab.open", "Fab", "Import packs downloaded from Fab", "", &Open, this});
        ok &= context.UI().RegisterPanel({"fab.importer", "Fab", "", 2, &MakePanel, this});
        ok &= context.UI().RegisterMenuAction({"Window/Asset Stores", "fab.open", "open"});
        ok &= context.Assets().RegisterImporter(
            {"Asset pack with materials (Fab, Megascans, Sketchfab download)", kPackExtensions, "fab.pack",
             &ImportFromBrowser, this});
        return ok;
    }
    void OnUnload() override {
        delete m_bridge;
        m_bridge = nullptr;
        m_context = nullptr;
    }

private:
    static void Open(void* self) { static_cast<FabPlugin*>(self)->m_context->UI().ShowPanel("fab.importer"); }
    static QWidget* MakePanel(QWidget* parent, void* self) {
        auto* plugin = static_cast<FabPlugin*>(self);
        return new FabPanel(*plugin->m_context, *plugin->m_bridge, parent);
    }
    // Content Browser > Import: the pack lands in the folder being browsed, without a credit.
    static bool ImportFromBrowser(const char* source, const char* directory, void* self) {
        const QString path = QString::fromUtf8(source);
        QString folder = QString::fromUtf8(directory ? directory : "");
        folder = folder.isEmpty() ? QStringLiteral("assets") : "assets/" + folder;
        const QString target = folder + "/" + QString::fromStdString(SafeFileName(NameFor(path).toStdString()));
        QString summary;
        const bool ok = ImportPack(*static_cast<FabPlugin*>(self)->m_context, path, target, {}, {}, {}, {}, summary);
        if (ok) {
            QMessageBox::information(nullptr, QObject::tr("Import"), summary);
        } else {
            QMessageBox::warning(nullptr, QObject::tr("Import"), summary);
        }
        return ok;
    }
    IPluginContext* m_context = nullptr;
    LauncherBridge* m_bridge = nullptr;
};

} // namespace

HP_DECLARE_PLUGIN(FabPlugin)
