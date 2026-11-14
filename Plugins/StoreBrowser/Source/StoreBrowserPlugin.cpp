#include "PluginAPI/PluginAPI.h"
#include "../../Common/AssetImportText.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
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
};

constexpr Store kStores[] = {
    {"fab", "Fab Store", "https://www.fab.com/", "Fab", " | Fab", "Fab Standard License", "https://www.fab.com/eula"},
};

constexpr const char* kProfileName = "HephaestusStoreBrowser";

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
    StorePanel(const Store& store, QWidget* parent) : QWidget(parent), m_store(store) {
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

private:
    const Store& m_store;
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
            m_entries.push_back({std::string("storebrowser.open.") + store.id, std::string("storebrowser.") + store.id,
                                 &store, this});
        }
        for (Entry& entry : m_entries) {
            ok &= context.Commands().Register({entry.command.c_str(), entry.store->title,
                                               "Browse the store and import downloads into the project", "",
                                               &Open, &entry});
            ok &= context.UI().RegisterPanel({entry.panel.c_str(), entry.store->title, "", 2, &MakePanel, &entry});
            ok &= context.UI().RegisterMenuAction({"Window/Asset Stores", entry.command.c_str(), "open"});
        }
        return ok;
    }

    void OnUnload() override {
        // The profile outlives the plugin; its download signal must not call into unloaded code.
        QObject::disconnect(m_downloads);
        m_panels.clear();
        m_context = nullptr;
    }

private:
    struct Entry {
        std::string command;
        std::string panel;
        const Store* store = nullptr;
        StoreBrowserPlugin* plugin = nullptr;
    };

    static void Open(void* data) {
        auto* entry = static_cast<Entry*>(data);
        entry->plugin->m_context->UI().ShowPanel(entry->panel.c_str());
    }

    static QWidget* MakePanel(QWidget* parent, void* data) {
        auto* entry = static_cast<Entry*>(data);
        auto* panel = new StorePanel(*entry->store, parent);
        entry->plugin->m_panels.push_back(panel);
        return panel;
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
