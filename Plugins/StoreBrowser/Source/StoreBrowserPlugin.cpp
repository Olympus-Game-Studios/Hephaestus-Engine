#include "PluginAPI/PluginAPI.h"
#include "../../Common/AssetImportText.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEngineDownloadRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QWidget>

#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

using namespace Hephaestus::Plugins;

namespace {

/// A store site shown in its own panel. Downloads of asset packs from it are imported into the open project.
struct Store {
    const char* id;          // panel and command id suffix
    const char* title;       // panel title
    const char* home;        // start page
    const char* source;      // credit source name
    const char* titleSuffix; // removed from page titles to get the asset's name
    const char* license;     // licence recorded when the page does not say
    const char* licenseUrl;
    const char* settingsCommand; // another plugin's command for the store's import settings, or nullptr
};

constexpr Store kStores[] = {
    {"fab", "Fab Store", "https://www.fab.com/", "Fab", " | Fab", "Fab Standard License", "https://www.fab.com/eula",
     "fab.open"},
};

constexpr const char* kProfileName = "HephaestusStoreBrowser";

// Design-system icons draw in currentColor; give them the palette's text colour so they show on dark themes.
QIcon ThemedIcon(const QString& svgPath, const QColor& color) {
    QFile file(svgPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray svg = file.readAll();
    svg.replace("currentColor", color.name().toUtf8());
    QSvgRenderer renderer(svg);
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.end();
    return QIcon(pixmap);
}

QString GeometryKey(const Store& store) {
    return QStringLiteral("plugins/com.hephaestus.storebrowser/%1/geometry").arg(QString::fromUtf8(store.id));
}

bool IsAssetPack(const QString& fileName) {
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return suffix == "zip" || suffix == "fbx" || suffix == "glb" || suffix == "gltf" || suffix == "obj";
}

/// One persistent profile for every store panel, so sign-ins and cookies survive restarts. It belongs to the
/// application, not the plugin, because pages can only be released before their profile.
QWebEngineProfile* SharedProfile() {
    if (auto* existing = qApp->findChild<QWebEngineProfile*>(kProfileName, Qt::FindDirectChildrenOnly)) {
        return existing;
    }
    auto* profile = new QWebEngineProfile(kProfileName, qApp);
    profile->setObjectName(kProfileName);
    const QString root = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                             .filePath("Hephaestus/StoreBrowser");
    profile->setPersistentStoragePath(root + "/storage");
    profile->setCachePath(root + "/cache");
    profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    return profile;
}

/// Opens sign-in and other pop-up windows in a dialog instead of dropping them.
class StorePage final : public QWebEnginePage {
public:
    StorePage(QWebEngineProfile* profile, QWidget* dialogParent)
        : QWebEnginePage(profile, dialogParent), m_dialogParent(dialogParent) {}

protected:
    QWebEnginePage* createWindow(WebWindowType) override {
        auto* dialog = new QDialog(m_dialogParent);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(900, 720);
        auto* layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* view = new QWebEngineView(dialog);
        auto* page = new StorePage(profile(), dialog);
        view->setPage(page);
        layout->addWidget(view);
        connect(page, &QWebEnginePage::windowCloseRequested, dialog, &QDialog::close);
        connect(page, &QWebEnginePage::titleChanged, dialog, &QDialog::setWindowTitle);
        dialog->show();
        return page;
    }

private:
    QWidget* m_dialogParent = nullptr;
};

class StorePanel final : public QWidget {
public:
    StorePanel(const Store& store, ICommandService& commands, const QDir& iconFolder, QWidget* parent)
        : QWidget(parent), m_store(store), m_iconFolder(iconFolder) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* bar = new QHBoxLayout();
        bar->setContentsMargins(4, 4, 4, 4);
        auto button = [this, bar](const QString& text, const QString& tip) {
            auto* b = new QToolButton(this);
            b->setText(text);
            b->setToolTip(tip);
            bar->addWidget(b);
            return b;
        };
        QToolButton* back = button(tr("<"), tr("Back"));
        QToolButton* forward = button(tr(">"), tr("Forward"));
        QToolButton* reload = button(tr("Reload"), tr("Reload"));
        QToolButton* home = button(tr("Home"), tr("Go to %1").arg(QString::fromUtf8(store.title)));
        m_address = new QLineEdit(this);
        m_address->setReadOnly(true);
        bar->addWidget(m_address, 1);
        QToolButton* external = button(tr("Open in browser"), tr("Open this page in your web browser"));
        if (store.settingsCommand) {
            QToolButton* more = button(QString(), tr("More"));
            more->setIcon(ThemedIcon(m_iconFolder.filePath("more.svg"), palette().color(QPalette::ButtonText)));
            more->setPopupMode(QToolButton::InstantPopup);
            more->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0px; }"));
            auto* menu = new QMenu(more);
            QAction* settings = menu->addAction(tr("Import settings..."));
            connect(settings, &QAction::triggered, this, [this, &commands] {
                if (!commands.Execute(m_store.settingsCommand)) {
                    SetStatus(tr("The import settings belong to the Fab plugin, which is turned off."));
                }
            });
            more->setMenu(menu);
        }
        layout->addLayout(bar);

        m_view = new QWebEngineView(this);
        m_view->setPage(new StorePage(SharedProfile(), this));
        layout->addWidget(m_view, 1);
        m_status = new QLabel(tr("Downloads of FBX, glTF, OBJ or .zip packs are imported into the open project."),
                              this);
        m_status->setWordWrap(true);
        m_status->setContentsMargins(6, 2, 6, 4);
        layout->addWidget(m_status);

        connect(back, &QToolButton::clicked, m_view, &QWebEngineView::back);
        connect(forward, &QToolButton::clicked, m_view, &QWebEngineView::forward);
        connect(reload, &QToolButton::clicked, m_view, &QWebEngineView::reload);
        connect(home, &QToolButton::clicked, this, [this] { m_view->load(QUrl(QString::fromUtf8(m_store.home))); });
        connect(external, &QToolButton::clicked, this, [this] { QDesktopServices::openUrl(m_view->url()); });
        connect(m_view, &QWebEngineView::urlChanged, this,
                [this](const QUrl& url) { m_address->setText(url.toString()); });
        m_view->load(QUrl(QString::fromUtf8(store.home)));
    }

    const Store& StoreInfo() const { return m_store; }
    QWebEnginePage* Page() const { return m_view->page(); }
    void SetStatus(const QString& text) { m_status->setText(text); }
    /// The asset's name from the page it was downloaded from ("Grenades pack | Fab" → "Grenades pack").
    QString AssetTitle() const {
        QString title = m_view->title();
        const QString suffix = QString::fromUtf8(m_store.titleSuffix);
        if (title.endsWith(suffix)) {
            title.chop(suffix.size());
        }
        return title.trimmed();
    }
    QString PageUrl() const { return m_view->url().toString(); }

protected:
    void closeEvent(QCloseEvent* event) override {
        QSettings("Hephaestus", "Editor").setValue(GeometryKey(m_store), saveGeometry());
        QWidget::closeEvent(event);
    }

private:
    const Store& m_store;
    QDir m_iconFolder;
    QWebEngineView* m_view = nullptr;
    QLineEdit* m_address = nullptr;
    QLabel* m_status = nullptr;
};

class StoreBrowserPlugin final : public IHephaestusPlugin {
public:
    bool OnLoad(IPluginContext& context) override {
        m_context = &context;
#ifdef _WIN32
        // Chromium cannot be unloaded from a running process, and the editor unloads every plugin when a project
        // opens; pinning this module (and with it Qt WebEngine) keeps it mapped until the editor exits.
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&SharedProfile), &self);
#endif
        // Qt WebEngine and its helper process ship in this plugin's bin folder, not beside the editor.
        const QDir bin(QDir(QString::fromUtf8(context.Project().PluginRoot())).filePath("bin"));
        qputenv("QTWEBENGINEPROCESS_PATH", bin.filePath("QtWebEngineProcess.exe").toLocal8Bit());
        qputenv("QTWEBENGINE_RESOURCES_PATH", bin.filePath("resources").toLocal8Bit());
        qputenv("QTWEBENGINE_LOCALES_PATH", bin.filePath("translations/qtwebengine_locales").toLocal8Bit());

        m_profile = SharedProfile();
        m_downloads = QObject::connect(m_profile, &QWebEngineProfile::downloadRequested, m_profile,
                                       [this](QWebEngineDownloadRequest* download) { OnDownload(download); });
        bool ok = true;
        for (const Store& store : kStores) {
            m_entries.push_back({std::string("storebrowser.open.") + store.id, &store, this});
        }
        for (Entry& entry : m_entries) {
            ok &= context.Commands().Register({entry.command.c_str(), entry.store->title,
                                               "Browse the store and import downloads into the project", "",
                                               &Open, &entry});
            ok &= context.UI().RegisterMenuAction({"Window/Asset Stores", entry.command.c_str(), "open"});
        }
        return ok;
    }

    void OnUnload() override {
        // The profile outlives the plugin; its download signal must not call into unloaded code.
        QObject::disconnect(m_downloads);
        // Store windows are top-level, so the editor does not own them; close them with the plugin.
        for (const QPointer<StorePanel>& panel : m_panels) {
            if (panel) {
                QSettings("Hephaestus", "Editor").setValue(GeometryKey(panel->StoreInfo()), panel->saveGeometry());
                delete panel.data();
            }
        }
        m_panels.clear();
        m_context = nullptr;
    }

private:
    struct Entry {
        std::string command;
        const Store* store = nullptr;
        StoreBrowserPlugin* plugin = nullptr;
    };

    // Each store is its own window with the system's title bar, remembering where it was left.
    static void Open(void* data) {
        auto* entry = static_cast<Entry*>(data);
        StoreBrowserPlugin* self = entry->plugin;
        for (const QPointer<StorePanel>& panel : self->m_panels) {
            if (panel && &panel->StoreInfo() == entry->store) {
                panel->showNormal();
                panel->raise();
                panel->activateWindow();
                return;
            }
        }
        auto* panel = new StorePanel(*entry->store, self->m_context->Commands(),
                                     QDir(QString::fromUtf8(self->m_context->Project().PluginRoot())).filePath("Resources"),
                                     nullptr);
        panel->setWindowFlag(Qt::Window);
        panel->setAttribute(Qt::WA_DeleteOnClose);
        // A store window must not keep the editor running once the editor's own window is closed.
        panel->setAttribute(Qt::WA_QuitOnClose, false);
        panel->setWindowTitle(QString::fromUtf8(entry->store->title));
        panel->setWindowIcon(QIcon(QDir(QString::fromUtf8(self->m_context->Project().PluginRoot()))
                                       .filePath("Resources/store-browser.svg")));
        if (!panel->restoreGeometry(QSettings("Hephaestus", "Editor").value(GeometryKey(*entry->store)).toByteArray())) {
            panel->resize(1280, 860);
        }
        self->m_panels.push_back(panel);
        panel->show();
    }

    StorePanel* PanelFor(QWebEnginePage* page) const {
        for (const QPointer<StorePanel>& panel : m_panels) {
            if (panel && panel->Page() == page) {
                return panel;
            }
        }
        return nullptr;
    }

    void OnDownload(QWebEngineDownloadRequest* download) {
        StorePanel* panel = PanelFor(download->page());
        if (!panel || !m_context) {
            return; // not one of our pages (or a pop-up): leave it to the default handling, which cancels it
        }
        if (!IsAssetPack(download->downloadFileName())) {
            panel->SetStatus(tr("%1 is not a model or .zip pack, so it was not downloaded.")
                                 .arg(download->downloadFileName()));
            download->cancel();
            return;
        }
        if (!*m_context->Project().ProjectRoot()) {
            panel->SetStatus(tr("Open a project before downloading assets."));
            download->cancel();
            return;
        }
        const QString folder =
            QDir(QString::fromUtf8(m_context->Project().ProjectRoot())).filePath(".hephaestus/downloads/store");
        QDir().mkpath(folder);
        download->setDownloadDirectory(folder);
        download->accept();
        const QString title = panel->AssetTitle().isEmpty() ? QFileInfo(download->downloadFileName()).completeBaseName()
                                                            : panel->AssetTitle();
        const QString pageUrl = panel->PageUrl();
        QPointer<StorePanel> target = panel;
        panel->SetStatus(tr("Downloading %1...").arg(title));
        QObject::connect(download, &QWebEngineDownloadRequest::receivedBytesChanged, download, [download, target, title] {
            if (target && download->totalBytes() > 0) {
                target->SetStatus(tr("Downloading %1... %2%").arg(title).arg(
                    download->receivedBytes() * 100 / download->totalBytes()));
            }
        });
        QObject::connect(download, &QWebEngineDownloadRequest::isFinishedChanged, download,
                         [this, download, target, title, pageUrl] {
            if (!download->isFinished() || !target) {
                return;
            }
            if (download->state() != QWebEngineDownloadRequest::DownloadCompleted) {
                target->SetStatus(tr("Download of %1 did not finish: %2").arg(title, download->interruptReasonString()));
                return;
            }
            const QString path = QDir(download->downloadDirectory()).filePath(download->downloadFileName());
            target->SetStatus(Import(target->StoreInfo(), path, title, pageUrl));
            QFile::remove(path);
        });
    }

    QString Import(const Store& store, const QString& path, const QString& title, const QString& pageUrl) {
        const QByteArray source = path.toUtf8();
        const QByteArray target =
            ("assets/Imported/" + QString::fromUtf8(store.source) + "/" +
             QString::fromStdString(SafeFileName(title.toStdString())))
                .toUtf8();
        const QByteArray titleText = title.toUtf8();
        const QByteArray url = pageUrl.toUtf8();
        AssetPackImportRequest request;
        request.sourcePath = source.constData();
        request.targetFolder = target.constData();
        request.title = titleText.constData();
        request.source = store.source;
        request.sourceUrl = url.constData();
        request.license = store.license;
        request.licenseUrl = store.licenseUrl;
        AssetPackImportResult result;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = m_context->Assets().ImportAssetPack(request, result);
        QApplication::restoreOverrideCursor();
        return ok ? ImportSummary(result) : tr("Import failed: %1").arg(QString::fromUtf8(result.message));
    }

    static QString tr(const char* text) { return QObject::tr(text); }

    IPluginContext* m_context = nullptr;
    QWebEngineProfile* m_profile = nullptr;
    QMetaObject::Connection m_downloads;
    std::vector<Entry> m_entries;
    std::vector<QPointer<StorePanel>> m_panels;
};

} // namespace

HP_DECLARE_PLUGIN(StoreBrowserPlugin)
