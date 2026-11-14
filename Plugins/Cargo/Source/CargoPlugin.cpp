#include "PluginAPI/PluginAPI.h"
#include "../../Common/AssetImportText.h"
#include "../../Common/ToolWindow.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTreeWidget>
#include <QVersionNumber>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

using namespace Hephaestus::Plugins;

namespace {

constexpr const char* kFolderSetting = "kitsFolder";

/// Cargo's default download location (%APPDATA%/kitbash3d/kits on Windows, ~/.kitbash3d/kits elsewhere).
QString DefaultKitsFolder() {
#ifdef _WIN32
    return QDir(qEnvironmentVariable("APPDATA")).filePath("kitbash3d/kits");
#else
    return QDir::home().filePath(".kitbash3d/kits");
#endif
}

struct CargoAsset {
    QString kit;
    QString name;
    QString usd; ///< <kit>/<version>/Models/<asset>/<asset>.usd
};

/// Every downloaded model: <kits>/<kit>/<version>/Models/<asset>/<asset>.usd, newest version of each kit.
std::vector<CargoAsset> FindAssets(const QString& kitsFolder) {
    std::vector<CargoAsset> assets;
    const QDir kits(kitsFolder);
    for (const QString& kit : kits.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QStringList versions = QDir(kits.filePath(kit)).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        std::sort(versions.begin(), versions.end(), [](const QString& a, const QString& b) {
            return QVersionNumber::fromString(a) < QVersionNumber::fromString(b);
        });
        for (auto version = versions.crbegin(); version != versions.crend(); ++version) {
            const QDir models(kits.filePath(kit + "/" + *version + "/Models"));
            const QStringList names = models.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString& name : names) {
                const QString usd = models.filePath(name + "/" + name + ".usd");
                if (QFileInfo::exists(usd)) {
                    assets.push_back({kit, name, usd});
                }
            }
            if (!names.isEmpty()) {
                break;
            }
        }
    }
    return assets;
}

class CargoPanel final : public QWidget {
public:
    CargoPanel(IPluginContext& context, QWidget* parent) : QWidget(parent), m_context(context) {
        auto* layout = new QVBoxLayout(this);
        auto* intro = new QLabel(
            tr("Download kits or models in the Cargo app (you can skip sending them to another program), then "
               "import them here. Each model comes in as a prefab with its materials and textures under "
               "assets/Imported/Cargo."),
            this);
        intro->setWordWrap(true);
        layout->addWidget(intro);

        auto* folderRow = new QHBoxLayout();
        m_folder = new QLineEdit(this);
        const QString saved = QString::fromUtf8(context.Settings().Get(kFolderSetting));
        m_folder->setText(saved.isEmpty() ? DefaultKitsFolder() : saved);
        auto* choose = new QPushButton(tr("Folder..."), this);
        folderRow->addWidget(new QLabel(tr("Cargo downloads"), this));
        folderRow->addWidget(m_folder, 1);
        folderRow->addWidget(choose);
        layout->addLayout(folderRow);

        m_tree = new QTreeWidget(this);
        m_tree->setHeaderLabels({tr("Kit and model")});
        m_tree->header()->setStretchLastSection(true);
        layout->addWidget(m_tree, 1);

        auto* buttons = new QHBoxLayout();
        auto* refresh = new QPushButton(tr("Refresh"), this);
        m_import = new QPushButton(tr("Import Checked"), this);
        buttons->addWidget(refresh);
        buttons->addStretch(1);
        buttons->addWidget(m_import);
        layout->addLayout(buttons);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);

        connect(choose, &QPushButton::clicked, this, [this] {
            const QString folder = QFileDialog::getExistingDirectory(this, tr("Cargo download folder"), m_folder->text());
            if (!folder.isEmpty()) {
                m_folder->setText(folder);
                Refresh();
            }
        });
        connect(m_folder, &QLineEdit::editingFinished, this, [this] { Refresh(); });
        connect(refresh, &QPushButton::clicked, this, [this] { Refresh(); });
        connect(m_import, &QPushButton::clicked, this, [this] { ImportChecked(); });
        connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
            if (item->childCount() > 0 && !m_updating) {
                m_updating = true;
                for (int i = 0; i < item->childCount(); ++i) {
                    item->child(i)->setCheckState(0, item->checkState(0));
                }
                m_updating = false;
            }
        });
        Refresh();
    }

    void Refresh() {
        m_context.Settings().Set(kFolderSetting, m_folder->text() == DefaultKitsFolder()
                                                     ? ""
                                                     : m_folder->text().toUtf8().constData());
        m_updating = true;
        m_tree->clear();
        m_assets = FindAssets(m_folder->text());
        QTreeWidgetItem* kitItem = nullptr;
        for (size_t i = 0; i < m_assets.size(); ++i) {
            if (!kitItem || kitItem->text(0) != m_assets[i].kit) {
                kitItem = new QTreeWidgetItem(m_tree, {m_assets[i].kit});
                kitItem->setFlags(kitItem->flags() | Qt::ItemIsUserCheckable);
                kitItem->setCheckState(0, Qt::Unchecked);
            }
            auto* item = new QTreeWidgetItem(kitItem, {m_assets[i].name});
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, Qt::Unchecked);
            item->setData(0, Qt::UserRole, static_cast<qulonglong>(i));
        }
        m_tree->expandAll();
        m_updating = false;
        m_status->setText(m_assets.empty() ? tr("No Cargo downloads found in this folder.")
                                           : tr("%n model(s) downloaded.", nullptr, static_cast<int>(m_assets.size())));
    }

private:
    void ImportChecked() {
        if (!*m_context.Project().ProjectRoot()) {
            m_status->setText(tr("Open a project before importing."));
            return;
        }
        std::vector<size_t> chosen;
        for (int k = 0; k < m_tree->topLevelItemCount(); ++k) {
            QTreeWidgetItem* kit = m_tree->topLevelItem(k);
            for (int i = 0; i < kit->childCount(); ++i) {
                if (kit->child(i)->checkState(0) == Qt::Checked) {
                    chosen.push_back(kit->child(i)->data(0, Qt::UserRole).toULongLong());
                }
            }
        }
        if (chosen.empty()) {
            m_status->setText(tr("Tick the models to import."));
            return;
        }
        QStringList lines;
        int imported = 0;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        for (size_t index : chosen) {
            const CargoAsset& asset = m_assets[index];
            m_status->setText(tr("Importing %1...").arg(asset.name));
            m_status->repaint();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            const QByteArray source = asset.usd.toUtf8();
            const QByteArray target = ("assets/Imported/Cargo/" + QString::fromStdString(SafeFileName(asset.kit.toStdString())) +
                                       "/" + QString::fromStdString(SafeFileName(asset.name.toStdString())))
                                          .toUtf8();
            const QByteArray title = asset.name.toUtf8();
            AssetPackImportRequest request;
            request.sourcePath = source.constData();
            request.targetFolder = target.constData();
            request.title = title.constData();
            request.source = "KitBash3D Cargo";
            request.author = "KitBash3D";
            request.license = "KitBash3D licence";
            AssetPackImportResult result;
            if (m_context.Assets().ImportAssetPack(request, result)) {
                ++imported;
                if (result.warningCount > 0) {
                    lines << asset.name + ": " + QString::fromUtf8(result.message);
                }
            } else {
                lines << tr("%1 failed: %2").arg(asset.name, QString::fromUtf8(result.message));
            }
        }
        QApplication::restoreOverrideCursor();
        lines.prepend(tr("Imported %n model(s) into assets/Imported/Cargo.", nullptr, imported));
        m_status->setText(lines.join('\n'));
    }

    IPluginContext& m_context;
    QLineEdit* m_folder = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPushButton* m_import = nullptr;
    QLabel* m_status = nullptr;
    std::vector<CargoAsset> m_assets;
    bool m_updating = false;
};

class CargoPlugin final : public IHephaestusPlugin {
public:
    bool OnLoad(IPluginContext& context) override {
        m_context = &context;
        bool ok = context.Settings().Register({kFolderSetting, "Cargo download folder",
                                               "Where the Cargo app saves kits. Empty uses Cargo's default.", "",
                                               SettingScope::Editor});
        ok &= context.Commands().Register({"cargo.open", "Cargo", "Import models downloaded with KitBash3D Cargo", "",
                                           &Open, this});
        ok &= context.UI().RegisterMenuAction({"Window/Asset Stores", "cargo.open", "open"});
        return ok;
    }
    void OnUnload() override {
        delete m_panel.data();
        m_context = nullptr;
    }

private:
    static void Open(void* self) {
        auto* plugin = static_cast<CargoPlugin*>(self);
        ShowToolWindow(plugin->m_panel, [plugin] { return new CargoPanel(*plugin->m_context, nullptr); },
                       QObject::tr("Cargo"),
                       QDir(QString::fromUtf8(plugin->m_context->Project().PluginRoot())).filePath("Resources/cargo.svg"),
                       520, 620);
    }

    IPluginContext* m_context = nullptr;
    QPointer<QWidget> m_panel;
};

} // namespace

HP_DECLARE_PLUGIN(CargoPlugin)
