#include "PluginAPI/PluginAPI.h"
#include "../../Common/AssetImportText.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

using namespace Hephaestus::Plugins;

namespace {

constexpr const char* kFab = "https://www.fab.com/";
constexpr const char* kPackExtensions = "*.zip;*.fbx;*.glb;*.gltf;*.obj";

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

class FabPanel final : public QWidget {
public:
    FabPanel(IPluginContext& context, QWidget* parent) : QWidget(parent), m_context(context) {
        setAcceptDrops(true);
        auto* layout = new QVBoxLayout(this);
        auto* intro = new QLabel(
            tr("Fab has no download API, so packs come in from files you download yourself. On Fab, choose a "
               "format such as FBX, glTF or OBJ rather than Unreal Engine, download it, then drop the .zip, "
               "folder or model here. Check each listing's licence: some content may only be used in Unreal "
               "Engine."),
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
        bool ok = context.Commands().Register({"fab.open", "Fab", "Import packs downloaded from Fab", "", &Open, this});
        ok &= context.UI().RegisterPanel({"fab.importer", "Fab", "", 2, &MakePanel, this});
        ok &= context.UI().RegisterMenuAction({"Window/Asset Stores", "fab.open", "open"});
        ok &= context.Assets().RegisterImporter(
            {"Asset pack with materials (Fab, Megascans, Sketchfab download)", kPackExtensions, "fab.pack",
             &ImportFromBrowser, this});
        return ok;
    }
    void OnUnload() override { m_context = nullptr; }

private:
    static void Open(void* self) { static_cast<FabPlugin*>(self)->m_context->UI().ShowPanel("fab.importer"); }
    static QWidget* MakePanel(QWidget* parent, void* self) {
        return new FabPanel(*static_cast<FabPlugin*>(self)->m_context, parent);
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
};

} // namespace

HP_DECLARE_PLUGIN(FabPlugin)
