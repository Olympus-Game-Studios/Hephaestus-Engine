/**
 * @file AddActorsPanel.cpp
 * @brief Implementation of the "Add Actors" dockable panel.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "AddActorsPanel.h"
#include "EditorTheme.h"
#include "EditorMainWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QStyle>
#include <QPixmap>
#include <QPainter>

#include <filesystem>
#include <algorithm>
#include <iostream>

namespace Hephaestus {
namespace QtEditor {

// ─────────────────────────────────────────────────────────────────────────────
// SpawnableEntry serialisation
// ─────────────────────────────────────────────────────────────────────────────

QByteArray SpawnableEntry::toMimePayload() const
{
    QJsonObject obj;
    obj["kind"]        = static_cast<int>(kind);
    obj["displayName"] = displayName;
    obj["assetPath"]   = assetPath;
    obj["typeId"]      = typeId;
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

SpawnableEntry SpawnableEntry::fromMimePayload(const QByteArray& data)
{
    SpawnableEntry entry;
    QJsonObject obj = QJsonDocument::fromJson(data).object();
    entry.kind        = static_cast<SpawnableEntry::Kind>(obj["kind"].toInt(0));
    entry.displayName = obj["displayName"].toString();
    entry.assetPath   = obj["assetPath"].toString();
    entry.typeId      = obj["typeId"].toString();
    return entry;
}

// ─────────────────────────────────────────────────────────────────────────────
// DraggableItemList – initiates QDrag on items
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int SpawnableEntryRole = Qt::UserRole + 100;

void DraggableItemList::startDrag(Qt::DropActions /*supportedActions*/)
{
    QListWidgetItem* item = currentItem();
    if (!item) return;

    QByteArray payload = item->data(SpawnableEntryRole).toByteArray();
    if (payload.isEmpty()) return;

    auto* mimeData = new QMimeData();
    mimeData->setData(kSpawnableMimeType, payload);

    auto* drag = new QDrag(this);
    drag->setMimeData(mimeData);

    // Build a drag pixmap: icon + name
    QString name = item->text();
    QPixmap pixmap(180, 36);
    pixmap.fill(QColor(37, 39, 45, 220));
    {
        QPainter p(&pixmap);
        p.setPen(QColor(230, 232, 235));
        p.setFont(font());
        QIcon ic = item->icon();
        if (!ic.isNull())
            ic.paint(&p, 4, 4, 28, 28);
        p.drawText(36, 0, 140, 36, Qt::AlignVCenter | Qt::AlignLeft, name);
    }
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(12, 18));

    drag->exec(Qt::CopyAction);
}

// ─────────────────────────────────────────────────────────────────────────────
// AddActorsPanel
// ─────────────────────────────────────────────────────────────────────────────

AddActorsPanel::AddActorsPanel(EditorMainWindow* mainWindow, QWidget* parent)
    : QWidget(parent)
    , m_mainWindow(mainWindow)
{
    setObjectName("AddActorsPanel");
    setupUI();
    buildCategories();
    populateItemList();
}

void AddActorsPanel::setupUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── Search bar ──────────────────────────────────────────────────────
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText(tr("Search actors…"));
    m_searchBox->setClearButtonEnabled(true);
    connect(m_searchBox, &QLineEdit::textChanged, this, &AddActorsPanel::applySearchFilter);
    root->addWidget(m_searchBox);

    // ── Splitter: categories | items ────────────────────────────────────
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_categoryList = new QListWidget(m_splitter);
    m_categoryList->setObjectName("AddActorsCategoryList");
    m_categoryList->setMaximumWidth(160);
    m_categoryList->setMinimumWidth(100);
    connect(m_categoryList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(m_categories.size())) {
            m_selectedCategory = row;
            populateItemList();
        }
    });

    // Use our DraggableItemList so items can be dragged
    m_itemList = new DraggableItemList(m_splitter);
    m_itemList->setObjectName("AddActorsItemList");
    m_itemList->setViewMode(QListView::IconMode);
    m_itemList->setIconSize(QSize(48, 48));
    m_itemList->setGridSize(QSize(100, 80));
    m_itemList->setResizeMode(QListView::Adjust);
    m_itemList->setWrapping(true);
    m_itemList->setDragEnabled(true);
    m_itemList->setDragDropMode(QAbstractItemView::DragOnly);
    m_itemList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_itemList->setSpacing(4);

    m_splitter->addWidget(m_categoryList);
    m_splitter->addWidget(m_itemList);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    root->addWidget(m_splitter, 1);

    // ── Status label ────────────────────────────────────────────────────
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("AddActorsStatus");
    root->addWidget(m_statusLabel);
}

// ─────────────────────────────────────────────────────────────────────────────
// Category / entry building
// ─────────────────────────────────────────────────────────────────────────────

void AddActorsPanel::buildCategories()
{
    m_categories.clear();

    // ── 1. Meshes ───────────────────────────────────────────────────────
    {
        SpawnableCategory cat;
        cat.name = tr("Meshes");

        // Scan the project's .engine/meshes directory for default engine meshes.
        // These are synchronised from the exe's assets/meshes/ folder when
        // the project is opened (see ProjectContext::SyncEngineAssets).
        namespace fs = std::filesystem;
        fs::path meshDir;
        bool usingProjectEnginePath = false;

        if (!m_projectRoot.empty()) {
            fs::path candidate = fs::path(m_projectRoot) / ".engine" / "meshes";
            if (fs::is_directory(candidate)) {
                meshDir = candidate;
                usingProjectEnginePath = true;
            }
        }

        // Fallback: if no project is open yet, scan next to the executable
        // so the panel isn't completely empty before a project is loaded.
        if (meshDir.empty()) {
            fs::path exeDir = fs::path(QCoreApplication::applicationDirPath().toStdWString());
            meshDir = exeDir / "assets" / "meshes";
        }

        if (fs::is_directory(meshDir)) {
            for (auto& entry : fs::directory_iterator(meshDir)) {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".glb" && ext != ".gltf") continue;

                std::string stem = entry.path().stem().string();
                QString display = QString::fromStdString(stem);

                SpawnableEntry se;
                // Camera.glb is treated as a Camera actor
                if (stem == "Camera" || stem == "camera") {
                    se.kind = SpawnableEntry::Kind::Camera;
                } else {
                    se.kind = SpawnableEntry::Kind::Mesh;
                }
                se.displayName = display;
                // Use the correct relative prefix depending on source
                QString prefix = usingProjectEnginePath
                    ? QStringLiteral(".engine/meshes/")
                    : QStringLiteral("assets/meshes/");
                se.assetPath = prefix + QString::fromStdString(entry.path().filename().string());
                se.icon      = Theme::Icon(se.kind == SpawnableEntry::Kind::Camera ? QStringLiteral("camera") : QStringLiteral("cube"));

                cat.entries.push_back(std::move(se));
            }

            // Sort alphabetically, but put Camera last
            std::sort(cat.entries.begin(), cat.entries.end(), [](const SpawnableEntry& a, const SpawnableEntry& b) {
                if (a.kind == SpawnableEntry::Kind::Camera && b.kind != SpawnableEntry::Kind::Camera) return false;
                if (a.kind != SpawnableEntry::Kind::Camera && b.kind == SpawnableEntry::Kind::Camera) return true;
                return a.displayName.toLower() < b.displayName.toLower();
            });
        } else {
            std::cerr << "[AddActors] Meshes directory not found: " << meshDir.generic_string() << "\n";
        }

        m_categories.push_back(std::move(cat));
    }

    // ── 2. Lights ───────────────────────────────────────────────────────
    {
        SpawnableCategory cat;
        cat.name = tr("Lights");

        const QIcon lightIcon = Theme::Icon(QStringLiteral("light"));

        SpawnableEntry dir;
        dir.kind        = SpawnableEntry::Kind::Light;
        dir.displayName = tr("Directional Light");
        dir.typeId      = "DirectionalLight";
        dir.icon        = lightIcon;
        cat.entries.push_back(dir);

        SpawnableEntry pt;
        pt.kind         = SpawnableEntry::Kind::Light;
        pt.displayName  = tr("Point Light");
        pt.typeId       = "PointLight";
        pt.icon         = lightIcon;
        cat.entries.push_back(pt);

        SpawnableEntry sp;
        sp.kind         = SpawnableEntry::Kind::Light;
        sp.displayName  = tr("Spot Light");
        sp.typeId       = "SpotLight";
        sp.icon         = lightIcon;
        cat.entries.push_back(sp);

        m_categories.push_back(std::move(cat));
    }

    // ── 3. Prefabs ──────────────────────────────────────────────────────
    {
        SpawnableCategory cat;
        cat.name = tr("Prefabs");

        namespace fs = std::filesystem;
        fs::path prefabDir;
        if (!m_projectRoot.empty()) {
            fs::path candidate = fs::path(m_projectRoot) / "assets" / "prefabs";
            if (fs::is_directory(candidate)) {
                prefabDir = candidate;
            }
        }

        if (!prefabDir.empty() && fs::is_directory(prefabDir)) {
            const QIcon prefabIcon = Theme::Icon(QStringLiteral("prefab"));
            for (auto& entry : fs::directory_iterator(prefabDir)) {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".hpprefab" && ext != ".hpblueprint") continue;

                SpawnableEntry se;
                se.kind = SpawnableEntry::Kind::Prefab;
                se.displayName = QString::fromStdString(entry.path().stem().string());
                se.assetPath = QStringLiteral("assets/prefabs/") +
                               QString::fromStdString(entry.path().filename().string());
                se.icon = prefabIcon;
                cat.entries.push_back(std::move(se));
            }
            std::sort(cat.entries.begin(), cat.entries.end(),
                      [](const SpawnableEntry& a, const SpawnableEntry& b) {
                          return a.displayName.toLower() < b.displayName.toLower();
                      });
        }

        m_categories.push_back(std::move(cat));
    }

    // ── 4-9. Future placeholders (empty) ────────────────────────────────
    for (const char* name : { "Audio", "Volumes", "Environment", "FX", "UI", "Gameplay" }) {
        SpawnableCategory cat;
        cat.name = QString::fromUtf8(name);
        m_categories.push_back(std::move(cat));
    }

    // ── Populate category list ──────────────────────────────────────────
    m_categoryList->clear();
    for (const auto& cat : m_categories) {
        auto* item = new QListWidgetItem(cat.name, m_categoryList);
        item->setFlags(item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    }
    if (!m_categories.empty()) {
        m_categoryList->setCurrentRow(0);
        m_selectedCategory = 0;
    }
}

void AddActorsPanel::populateItemList()
{
    m_itemList->clear();

    if (m_selectedCategory < 0 || m_selectedCategory >= static_cast<int>(m_categories.size()))
        return;

    const auto& cat = m_categories[static_cast<size_t>(m_selectedCategory)];
    const QString filter = m_searchBox ? m_searchBox->text().trimmed() : QString();

    int count = 0;
    for (const auto& entry : cat.entries) {
        // Filter
        if (!filter.isEmpty() && !entry.displayName.contains(filter, Qt::CaseInsensitive))
            continue;

        auto* item = new QListWidgetItem(entry.icon, entry.displayName, m_itemList);
        item->setData(SpawnableEntryRole, entry.toMimePayload());
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        ++count;
    }

    m_statusLabel->setText(tr("%1 item(s)").arg(count));
}

void AddActorsPanel::Refresh()
{
    buildCategories();
    populateItemList();
}

void AddActorsPanel::SetProjectRoot(const std::string& projectRoot)
{
    m_projectRoot = projectRoot;
}

void AddActorsPanel::applySearchFilter()
{
    populateItemList();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
