/**
 * @file AssetBrowserWidget.cpp
 * @brief Implementation of Qt asset browser widget.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "AssetBrowserWidget.h"
#include "InspectorMime.h"
#include "../MaterialEditor/MaterialAssetSerializer.h"
#include "EditorMainWindow.h"
#include "AddActorsPanel.h"
#include "AssetDependencyDialog.h"
#include "EditorAssetServices.h"
#include "../HephaestusUI/HUIAsset.h"
#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetTypes.h"
#include "../../Assets/EditorAssetPipeline.h"
#include "../../Core/ProjectContext.h"
#include "../../Scene/PrefabAsset.h"
#include "../../Scene/PrefabSerializer.h"
#include "../../Assets/CharacterAsset.h"
#include "../../Assets/CharacterImport.h"
#include "../../Assets/CharacterRuntime.h"
#include "../../Assets/AssetPathPolicy.h"
#include "CharacterImportDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QToolBar>
#include <QAction>
#include <QStyle>
#include <QApplication>
#include <QFont>
#include <QFileIconProvider>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QDrag>
#include <QMimeData>
#include <QPainter>
#include <QFileInfo>
#include <QDir>
#include <QClipboard>
#include <QRegularExpression>
#include <QTimer>
#include <QShortcut>
#include <iostream>
#include <fstream>

namespace Hephaestus {
namespace QtEditor {

namespace {

QString ToProjectRelativeAssetPath(const QString& relativeFromAssetRoot)
{
    QString path = relativeFromAssetRoot;
    path.replace('\\', '/');
    if (!path.startsWith(QStringLiteral("assets/"), Qt::CaseInsensitive) &&
        !path.startsWith(QStringLiteral(".engine/"), Qt::CaseInsensitive)) {
        path = QStringLiteral("assets/") + path;
    }
    return path;
}

/**
 * Asset browser contents view that can drag Prefab / StaticMesh assets into the viewport.
 */
class DraggableAssetListView : public QListView
{
public:
    using QListView::QListView;

protected:
    void startDrag(Qt::DropActions /*supportedActions*/) override
    {
        QModelIndex index = currentIndex();
        if (!index.isValid()) {
            return;
        }

        const bool isDirectory = index.data(AssetBrowserWidget::IsDirectoryRole).toBool();
        if (isDirectory) {
            return;
        }

        const AssetType assetType =
            static_cast<AssetType>(index.data(AssetBrowserWidget::AssetTypeRole).toInt());
        if (assetType != AssetType::Prefab &&
            assetType != AssetType::StaticMesh &&
            assetType != AssetType::Audio &&
            assetType != AssetType::Material &&
            assetType != AssetType::Script &&
            assetType != AssetType::Texture &&
            assetType != AssetType::UILayout &&
            assetType != AssetType::Character) {
            return;
        }

        const QString relativePath = index.data(AssetBrowserWidget::RelativePathRole).toString();
        if (relativePath.isEmpty()) {
            return;
        }

        SpawnableEntry entry;
        switch (assetType) {
            case AssetType::Prefab: entry.kind = SpawnableEntry::Kind::Prefab; break;
            case AssetType::Audio: entry.kind = SpawnableEntry::Kind::Audio; break;
            case AssetType::Material: entry.kind = SpawnableEntry::Kind::Material; break;
            case AssetType::Script: entry.kind = SpawnableEntry::Kind::Script; break;
            case AssetType::Texture: entry.kind = SpawnableEntry::Kind::Texture; break;
            case AssetType::UILayout: entry.kind = SpawnableEntry::Kind::UILayout; break;
            case AssetType::Character: entry.kind = SpawnableEntry::Kind::Character; break;
            default: entry.kind = SpawnableEntry::Kind::Mesh; break;
        }
        entry.displayName = QFileInfo(relativePath).completeBaseName();
        entry.assetPath = ToProjectRelativeAssetPath(relativePath);
        entry.icon = index.data(Qt::DecorationRole).value<QIcon>();

        auto* mimeData = new QMimeData();
        mimeData->setData(kSpawnableMimeType, entry.toMimePayload());
        AttachInspectorAssetMime(mimeData, entry.assetPath, assetType);

        auto* drag = new QDrag(this);
        drag->setMimeData(mimeData);

        QPixmap pixmap(180, 36);
        pixmap.fill(QColor(37, 39, 45, 220));
        {
            QPainter p(&pixmap);
            p.setPen(QColor(230, 232, 235));
            p.setFont(font());
            if (!entry.icon.isNull()) {
                entry.icon.paint(&p, 4, 4, 28, 28);
            }
            p.drawText(36, 0, 140, 36, Qt::AlignVCenter | Qt::AlignLeft, entry.displayName);
        }
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(12, 18));
        drag->exec(Qt::CopyAction);
    }
};

} // namespace

AssetBrowserWidget::AssetBrowserWidget(EditorMainWindow* mainWindow, QWidget* parent)
    : QWidget(parent)
    , m_mainWindow(mainWindow)
{
    setupUI();
    updateEmptyState();
}

AssetBrowserWidget::~AssetBrowserWidget()
{
}

void AssetBrowserWidget::SetProjectContext(ProjectContext* context)
{
    m_projectContext = context;
    updateEmptyState();
}

void AssetBrowserWidget::Refresh()
{
    // Refresh AssetManager index
    AssetManager::GetInstance().Refresh();
    
    // Check if current path still exists
    if (!m_currentPath.isEmpty() && !AssetManager::GetInstance().PathExists(m_currentPath.toStdString())) {
        m_currentPath.clear();
    }
    
    rebuildFolderTree();
    rebuildContentsView();
    updateBreadcrumb();
    updateStatusBar();
    updateEmptyState();
}

void AssetBrowserWidget::NavigateTo(const QString& relativePath)
{
    // Verify path exists
    auto& assetMgr = AssetManager::GetInstance();
    if (!assetMgr.HasValidAssetRoot()) {
        m_currentPath.clear();
    } else if (relativePath.isEmpty() || assetMgr.IsDirectory(relativePath.toStdString())) {
        m_currentPath = relativePath;
        m_currentPath.replace('\\', '/');
        while (m_currentPath.startsWith('/')) {
            m_currentPath.remove(0, 1);
        }
    }
    
    rebuildContentsView();
    updateBreadcrumb();
    updateStatusBar();
    syncFolderTreeSelection();
}

void AssetBrowserWidget::FindReferences(const QString& relativePath)
{
    findAssetReferences(relativePath);
}

void AssetBrowserWidget::RevealAsset(const QString& relativePath)
{
    QString path = relativePath;
    path.replace('\\', '/');
    if (path.startsWith(QStringLiteral("assets/"), Qt::CaseInsensitive)) {
        path = path.mid(QStringLiteral("assets/").size());
    }
    showInFolder(path);
}

void AssetBrowserWidget::SetOpenAssetCallback(OpenAssetCallback callback)
{
    m_openAssetCallback = std::move(callback);
}

void AssetBrowserWidget::setupUI()
{
    setObjectName("AssetBrowserRoot");
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // ─────────────────────────────────────────────────────────────────────────
    // Top Toolbar
    // ─────────────────────────────────────────────────────────────────────────
    m_toolbar = new QToolBar(this);
    m_toolbar->setObjectName("AssetBrowserToolbar");
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);

    m_viewToggleButton = new QToolButton(this);
    m_viewToggleButton->setText(tr("Grid"));
    m_viewToggleButton->setCheckable(true);
    m_viewToggleButton->setChecked(true);
    connect(m_viewToggleButton, &QToolButton::toggled, this, [this](bool checked) {
        m_gridView = checked;
        m_viewToggleButton->setText(checked ? tr("Grid") : tr("List"));
        m_contentsView->setViewMode(checked ? QListView::IconMode : QListView::ListMode);
        m_contentsView->setGridSize(checked ? QSize(140, 110) : QSize());
        m_contentsView->setIconSize(checked ? QSize(56, 56) : QSize(20, 20));
    });
    m_toolbar->addWidget(m_viewToggleButton);

    m_importButton = new QPushButton(tr("Import"), this);
    auto* importMenu = new QMenu(m_importButton);
    importMenu->addAction(tr("Import Assets…"), this, &AssetBrowserWidget::importAssets);
    importMenu->addAction(tr("Import Character…"), this, &AssetBrowserWidget::importCharacter);
    m_importButton->setMenu(importMenu);
    m_importButton->setToolTip(tr("Copy files into the current asset folder, or import a GLB character"));
    m_toolbar->addWidget(m_importButton);

    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItems({
        tr("All"), tr("Meshes"), tr("Textures"), tr("Materials"), tr("Scenes"),
        tr("Scripts"), tr("UI Layouts"), tr("Prefabs"), tr("Characters"), tr("Audio")
    });
    connect(m_filterCombo, &QComboBox::currentTextChanged, this, [this](const QString&) {
        rebuildContentsView();
    });
    m_toolbar->addWidget(m_filterCombo);

    m_toolbar->addSeparator();

    // Up button
    m_upButton = new QPushButton(this);
    m_upButton->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_upButton->setToolTip(tr("Go up one level"));
    m_upButton->setFixedSize(24, 24);
    connect(m_upButton, &QPushButton::clicked, this, &AssetBrowserWidget::onUpButtonClicked);
    m_toolbar->addWidget(m_upButton);

    // Refresh button
    m_refreshButton = new QPushButton(this);
    m_refreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshButton->setToolTip(tr("Refresh"));
    m_refreshButton->setFixedSize(24, 24);
    connect(m_refreshButton, &QPushButton::clicked, this, &AssetBrowserWidget::onRefreshButtonClicked);
    m_toolbar->addWidget(m_refreshButton);

    m_toolbar->addSeparator();

    // Clickable breadcrumb bar
    m_breadcrumbBar = new QWidget(this);
    m_breadcrumbLayout = new QHBoxLayout(m_breadcrumbBar);
    m_breadcrumbLayout->setContentsMargins(0, 0, 0, 0);
    m_breadcrumbLayout->setSpacing(2);
    m_breadcrumbLabel = new QLabel(tr("Assets"), m_breadcrumbBar); // kept for compatibility
    m_breadcrumbLabel->hide();
    m_toolbar->addWidget(m_breadcrumbBar);

    // Spacer
    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);

    // Search box — empty query browses current folder; non-empty searches project-wide
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText(tr("Search project..."));
    m_searchBox->setMaximumWidth(220);
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setToolTip(tr("Search all project assets by name or path. Clear to return to folder browse."));
    connect(m_searchBox, &QLineEdit::textChanged, this, [this](const QString&) {
        rebuildContentsView();
    });
    m_toolbar->addWidget(m_searchBox);

    mainLayout->addWidget(m_toolbar);

    // ─────────────────────────────────────────────────────────────────────────
    // Main Content Area
    // ─────────────────────────────────────────────────────────────────────────
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Left panel - Folder tree
    QWidget* leftPanel = new QWidget(m_splitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    auto* leftHeader = new QHBoxLayout();
    auto* foldersLabel = new QLabel(tr("Folders"), leftPanel);
    leftHeader->addWidget(foldersLabel);
    leftHeader->addStretch();
    m_newFolderButton = new QPushButton(tr("+"), leftPanel);
    m_newFolderButton->setFixedSize(24, 24);
    m_newFolderButton->setToolTip(tr("Create a new folder in the current directory"));
    connect(m_newFolderButton, &QPushButton::clicked, this, [this]() {
        createNewFolder(m_currentPath);
    });
    leftHeader->addWidget(m_newFolderButton);
    leftLayout->addLayout(leftHeader);

    m_folderTree = new QTreeView(leftPanel);
    m_folderModel = new QStandardItemModel(this);
    m_folderModel->setHorizontalHeaderLabels({tr("Folders")});
    m_folderTree->setModel(m_folderModel);
    m_folderTree->setHeaderHidden(true);
    m_folderTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_folderTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_folderTree->setIndentation(18);
    m_folderTree->setAnimated(true);
    m_folderTree->setMinimumHeight(0);
    m_folderTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_folderTree, &QTreeView::clicked, this, &AssetBrowserWidget::onFolderTreeClicked);
    connect(m_folderTree, &QTreeView::customContextMenuRequested, this, &AssetBrowserWidget::onFolderTreeContextMenu);
    leftLayout->addWidget(m_folderTree, 1);
    leftPanel->setMinimumSize(0, 0);
    m_splitter->addWidget(leftPanel);

    // Right panel - Contents
    m_contentsPanel = new QWidget(m_splitter);
    QVBoxLayout* contentsLayout = new QVBoxLayout(m_contentsPanel);
    contentsLayout->setContentsMargins(0, 0, 0, 0);

    m_contentsView = new DraggableAssetListView(m_contentsPanel);
    m_contentsModel = new QStandardItemModel(this);
    m_contentsView->setModel(m_contentsModel);
    m_contentsView->setViewMode(QListView::IconMode);
    m_contentsView->setIconSize(QSize(56, 56));
    m_contentsView->setSpacing(12);
    m_contentsView->setGridSize(QSize(140, 110));
    m_contentsView->setResizeMode(QListView::Adjust);
    m_contentsView->setWordWrap(true);
    m_contentsView->setUniformItemSizes(true);
    m_contentsView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_contentsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_contentsView->setDragEnabled(true);
    m_contentsView->setDragDropMode(QAbstractItemView::DragOnly);
    m_contentsView->setDefaultDropAction(Qt::CopyAction);
    m_contentsView->setMinimumHeight(0);
    m_contentsView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_contentsView, &QListView::doubleClicked, this, &AssetBrowserWidget::onContentsDoubleClicked);
    connect(m_contentsView, &QListView::clicked, this, &AssetBrowserWidget::onContentsClicked);
    connect(m_contentsView, &QListView::customContextMenuRequested, this, &AssetBrowserWidget::onContentsContextMenu);
    contentsLayout->addWidget(m_contentsView);

    m_contentsPanel->setMinimumSize(0, 0);
    m_splitter->addWidget(m_contentsPanel);
    m_splitter->setSizes({180, 600});
    m_splitter->setChildrenCollapsible(true);
    m_splitter->setMinimumHeight(0);
    m_splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    mainLayout->addWidget(m_splitter, 1);

    // ─────────────────────────────────────────────────────────────────────────
    // Empty State Widget (overlay)
    // ─────────────────────────────────────────────────────────────────────────
    m_emptyStateWidget = new QWidget(this);
    m_emptyStateWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* emptyLayout = new QVBoxLayout(m_emptyStateWidget);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setContentsMargins(4, 4, 4, 4);
    emptyLayout->setSpacing(4);

    QLabel* emptyIcon = new QLabel(this);
    emptyIcon->setText(QString::fromUtf8("\xF0\x9F\x93\x81"));
    QFont iconFont = emptyIcon->font();
    iconFont.setPointSize(28);
    emptyIcon->setFont(iconFont);
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyIcon);

    QLabel* emptyLabel = new QLabel(tr("No project open"), this);
    QFont labelFont = emptyLabel->font();
    labelFont.setPointSize(11);
    emptyLabel->setFont(labelFont);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyLabel);

    QLabel* emptySubLabel = new QLabel(tr("Open a project to browse assets"), this);
    emptySubLabel->setAlignment(Qt::AlignCenter);
    emptySubLabel->setStyleSheet("color: gray;");
    emptyLayout->addWidget(emptySubLabel);

    m_openProjectButton = new QPushButton(tr("Open Project..."), this);
    connect(m_openProjectButton, &QPushButton::clicked, this, &AssetBrowserWidget::onOpenProjectButtonClicked);
    emptyLayout->addWidget(m_openProjectButton, 0, Qt::AlignCenter);

    mainLayout->addWidget(m_emptyStateWidget, 1);
    m_emptyStateWidget->hide();

    // ─────────────────────────────────────────────────────────────────────────
    // Status Bar
    // ─────────────────────────────────────────────────────────────────────────
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("AssetBrowserStatus");
    mainLayout->addWidget(m_statusLabel);

    setLayout(mainLayout);
}

void AssetBrowserWidget::updateEmptyState()
{
    bool hasProject = m_projectContext && m_projectContext->HasProject();
    bool hasAssetRoot = AssetManager::GetInstance().HasValidAssetRoot();

    if (!hasProject || !hasAssetRoot) {
        // Show empty state, hide content
        m_splitter->hide();
        m_emptyStateWidget->show();
        m_upButton->setEnabled(false);
        m_refreshButton->setEnabled(false);
    } else {
        // Show content, hide empty state
        m_emptyStateWidget->hide();
        m_splitter->show();
        m_upButton->setEnabled(!m_currentPath.isEmpty());
        m_refreshButton->setEnabled(true);
    }
}

void AssetBrowserWidget::rebuildFolderTree()
{
    m_folderModel->clear();
    m_folderModel->setHorizontalHeaderLabels({tr("Folders")});

    auto& assetMgr = AssetManager::GetInstance();
    if (!assetMgr.HasValidAssetRoot()) {
        return;
    }

    // Root item
    QStandardItem* rootItem = new QStandardItem(style()->standardIcon(QStyle::SP_DirIcon), tr("Assets"));
    rootItem->setData("", RelativePathRole);
    rootItem->setData(true, IsDirectoryRole);
    m_folderModel->appendRow(rootItem);

    // Build folder tree recursively
    std::function<void(QStandardItem*, const std::string&)> buildTree = 
        [&](QStandardItem* parent, const std::string& path) {
        auto entries = assetMgr.ListDirectory(path);
        for (const auto& entry : entries) {
            if (entry.isDirectory) {
                QStandardItem* item = new QStandardItem(
                    style()->standardIcon(QStyle::SP_DirIcon),
                    QString::fromStdString(entry.name)
                );
                item->setData(QString::fromStdString(entry.relativePath), RelativePathRole);
                item->setData(true, IsDirectoryRole);
                parent->appendRow(item);

                // Recurse (limit depth to prevent performance issues)
                buildTree(item, entry.relativePath);
            }
        }
    };

    buildTree(rootItem, "");
    
    // Expand root
    m_folderTree->expand(m_folderModel->index(0, 0));
}

void AssetBrowserWidget::rebuildContentsView()
{
    m_contentsModel->clear();

    auto& assetMgr = AssetManager::GetInstance();
    if (!assetMgr.HasValidAssetRoot()) {
        return;
    }

    const QString query = m_searchBox ? m_searchBox->text().trimmed() : QString();
    const QString filter = m_filterCombo ? m_filterCombo->currentText() : tr("All");
    const bool projectWideSearch = !query.isEmpty();

    auto matchesFilter = [&](AssetType type, bool isDirectory) {
        if (filter == tr("All") || isDirectory) return true;
        if (filter == tr("Meshes")) return type == AssetType::StaticMesh;
        if (filter == tr("Textures")) return type == AssetType::Texture;
        if (filter == tr("Materials")) return type == AssetType::Material;
        if (filter == tr("Scenes")) return type == AssetType::Scene;
        if (filter == tr("Scripts")) return type == AssetType::Script;
        if (filter == tr("UI Layouts")) return type == AssetType::UILayout;
        if (filter == tr("Prefabs")) return type == AssetType::Prefab;
        if (filter == tr("Characters")) return type == AssetType::Character;
        if (filter == tr("Audio")) return type == AssetType::Audio;
        return true;
    };

    auto typeBadge = [](AssetType type, bool isDirectory) -> QString {
        if (isDirectory) return QStringLiteral("[Folder]");
        switch (type) {
            case AssetType::Scene: return QStringLiteral("[Scene]");
            case AssetType::Texture: return QStringLiteral("[Texture]");
            case AssetType::StaticMesh: return QStringLiteral("[Mesh]");
            case AssetType::Material: return QStringLiteral("[Material]");
            case AssetType::Script: return QStringLiteral("[Script]");
            case AssetType::Audio: return QStringLiteral("[Audio]");
            case AssetType::Prefab: return QStringLiteral("[Prefab]");
            case AssetType::Character: return QStringLiteral("[Character]");
            case AssetType::Shader: return QStringLiteral("[Shader]");
            case AssetType::Font: return QStringLiteral("[Font]");
            case AssetType::Text: return QStringLiteral("[Text]");
            case AssetType::UILayout: return QStringLiteral("[UI Layout]");
            default: return QStringLiteral("[Asset]");
        }
    };

    auto parentFolderOf = [](const QString& relativePath) -> QString {
        const int slash = relativePath.lastIndexOf(QLatin1Char('/'));
        if (slash <= 0) {
            return QString();
        }
        return relativePath.left(slash);
    };

    if (projectWideSearch) {
        // Project-wide search uses AssetManager's recursive index (files only).
        const auto allAssets = assetMgr.GetAllAssets();
        const QStringList tokens = query.split(QRegularExpression(QStringLiteral("\\s+")),
                                               Qt::SkipEmptyParts);
        int matchCount = 0;
        for (const auto& asset : allAssets) {
            if (asset.isDirectory) {
                continue;
            }
            const AssetType type = asset.type;
            if (!matchesFilter(type, false)) {
                continue;
            }

            const QString name = QString::fromStdString(asset.name);
            const QString rel = QString::fromStdString(asset.relativePath).replace('\\', '/');
            bool matches = tokens.isEmpty();
            if (!matches) {
                matches = true;
                for (const QString& token : tokens) {
                    if (!name.contains(token, Qt::CaseInsensitive) &&
                        !rel.contains(token, Qt::CaseInsensitive)) {
                        matches = false;
                        break;
                    }
                }
            }
            if (!matches) {
                continue;
            }

            const QString folder = parentFolderOf(rel);
            const QString location = folder.isEmpty() ? tr("assets/") : (QStringLiteral("assets/") + folder);
            QStandardItem* item = new QStandardItem();
            item->setText(QString("%1\n%2  ·  %3")
                              .arg(name, typeBadge(type, false), location));
            item->setIcon(getAssetIcon(static_cast<int>(type), false));
            item->setData(rel, RelativePathRole);
            item->setData(static_cast<int>(type), AssetTypeRole);
            item->setData(false, IsDirectoryRole);
            item->setData(true, SearchResultRole);
            item->setToolTip(QStringLiteral("assets/%1").arg(rel));
            item->setEditable(false);
            m_contentsModel->appendRow(item);
            ++matchCount;
        }

        if (m_statusLabel) {
            m_statusLabel->setText(tr("Search: %1 result(s) project-wide").arg(matchCount));
        }
        return;
    }

    auto entries = assetMgr.ListDirectory(m_currentPath.toStdString());

    for (const auto& entry : entries) {
        const QString name = QString::fromStdString(entry.name);
        const AssetType type = static_cast<AssetType>(entry.type);

        if (!matchesFilter(type, entry.isDirectory)) {
            continue;
        }

        QStandardItem* item = new QStandardItem();
        item->setText(QString("%1\n%2").arg(name, typeBadge(type, entry.isDirectory)));
        item->setIcon(getAssetIcon(static_cast<int>(entry.type), entry.isDirectory));
        item->setData(QString::fromStdString(entry.relativePath), RelativePathRole);
        item->setData(static_cast<int>(entry.type), AssetTypeRole);
        item->setData(entry.isDirectory, IsDirectoryRole);
        item->setData(false, SearchResultRole);
        item->setEditable(false);
        
        m_contentsModel->appendRow(item);
    }

    updateStatusBar();
}

void AssetBrowserWidget::applyViewAndFilter()
{
    rebuildContentsView();
}

void AssetBrowserWidget::updateBreadcrumb()
{
    if (!m_breadcrumbLayout) {
        return;
    }

    while (QLayoutItem* child = m_breadcrumbLayout->takeAt(0)) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    auto addCrumb = [this](const QString& label, const QString& targetPath, bool isLast) {
        auto* btn = new QToolButton(m_breadcrumbBar);
        btn->setAutoRaise(true);
        btn->setText(label);
        btn->setCursor(Qt::PointingHandCursor);
        if (isLast) {
            QFont f = btn->font();
            f.setBold(true);
            btn->setFont(f);
            btn->setEnabled(false);
        } else {
            connect(btn, &QToolButton::clicked, this, [this, targetPath]() {
                NavigateTo(targetPath);
            });
        }
        m_breadcrumbLayout->addWidget(btn);
    };

    addCrumb(tr("Assets"), QString(), m_currentPath.isEmpty());

    if (!m_currentPath.isEmpty()) {
        const QStringList parts = m_currentPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString accum;
        for (int i = 0; i < parts.size(); ++i) {
            auto* sep = new QLabel(QStringLiteral("/"), m_breadcrumbBar);
            m_breadcrumbLayout->addWidget(sep);
            if (!accum.isEmpty()) accum += QLatin1Char('/');
            accum += parts[i];
            addCrumb(parts[i], accum, i == parts.size() - 1);
        }
    }

    m_breadcrumbLayout->addStretch();
    m_upButton->setEnabled(!m_currentPath.isEmpty());
}

void AssetBrowserWidget::updateStatusBar()
{
    auto& assetMgr = AssetManager::GetInstance();
    if (!assetMgr.HasValidAssetRoot()) {
        m_statusLabel->setText(tr("No project loaded"));
        return;
    }

    auto entries = assetMgr.ListDirectory(m_currentPath.toStdString());
    
    int folderCount = 0;
    int fileCount = 0;
    for (const auto& entry : entries) {
        if (entry.isDirectory) folderCount++;
        else fileCount++;
    }

    QString status;
    if (folderCount > 0 && fileCount > 0) {
        status = tr("%1 folders, %2 files").arg(folderCount).arg(fileCount);
    } else if (folderCount > 0) {
        status = tr("%1 folders").arg(folderCount);
    } else if (fileCount > 0) {
        status = tr("%1 files").arg(fileCount);
    } else {
        status = tr("Empty folder");
    }

    m_statusLabel->setText(status);
}

QIcon AssetBrowserWidget::getAssetIcon(int assetType, bool isDirectory) const
{
    QStyle* s = style();
    
    if (isDirectory) {
        return s->standardIcon(QStyle::SP_DirIcon);
    }

    AssetType type = static_cast<AssetType>(assetType);
    switch (type) {
        case AssetType::Scene:
            return s->standardIcon(QStyle::SP_FileDialogDetailedView);
        case AssetType::Texture:
            return s->standardIcon(QStyle::SP_FileDialogContentsView);
        case AssetType::StaticMesh:
            return s->standardIcon(QStyle::SP_ComputerIcon);
        case AssetType::Material:
            return s->standardIcon(QStyle::SP_DialogApplyButton);
        case AssetType::Script:
            return s->standardIcon(QStyle::SP_FileDialogListView);
        case AssetType::Audio:
            return s->standardIcon(QStyle::SP_MediaVolume);
        case AssetType::Prefab:
            return s->standardIcon(QStyle::SP_VistaShield);
        case AssetType::Character:
            return s->standardIcon(QStyle::SP_DirLinkIcon);
        case AssetType::Shader:
            return s->standardIcon(QStyle::SP_TitleBarMenuButton);
        case AssetType::Font:
            return s->standardIcon(QStyle::SP_FileDialogInfoView);
        case AssetType::Text:
            return s->standardIcon(QStyle::SP_FileIcon);
        case AssetType::UILayout:
            return s->standardIcon(QStyle::SP_DesktopIcon);
        default:
            return s->standardIcon(QStyle::SP_FileIcon);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots
// ─────────────────────────────────────────────────────────────────────────────

void AssetBrowserWidget::onFolderTreeClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;

    QString relativePath = index.data(RelativePathRole).toString();
    bool isDirectory = index.data(IsDirectoryRole).toBool();

    if (isDirectory) {
        NavigateTo(relativePath);
    }
}

void AssetBrowserWidget::onContentsDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;

    QString relativePath = index.data(RelativePathRole).toString();
    bool isDirectory = index.data(IsDirectoryRole).toBool();
    AssetType assetType = static_cast<AssetType>(index.data(AssetTypeRole).toInt());

    if (isDirectory) {
        // Navigate into folder
        NavigateTo(relativePath);
    } else {
        if (assetType == AssetType::StaticMesh) {
            // Spawn mesh into the active scene
            std::cout << "[AssetBrowser] Spawning mesh: " << relativePath.toStdString() << "\n";
            emit meshSpawnRequested(relativePath);
            if (m_statusLabel) {
                m_statusLabel->setText(tr("Spawning mesh: %1").arg(relativePath));
            }
            return;
        }

        // Open asset
        std::cout << "[AssetBrowser] Opening asset: " << relativePath.toStdString() << "\n";
        emit assetOpenRequested(relativePath);
        
        if (m_openAssetCallback) {
            m_openAssetCallback(relativePath);
        }
    }
}

void AssetBrowserWidget::onContentsClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;

    QString relativePath = index.data(RelativePathRole).toString();
    emit selectionChanged(relativePath);
}

void AssetBrowserWidget::onUpButtonClicked()
{
    if (m_currentPath.isEmpty()) {
        return;
    }

    QString parentPath = QString::fromStdString(
        AssetManager::GetInstance().GetParentPath(m_currentPath.toStdString())
    );

    NavigateTo(parentPath);
}

void AssetBrowserWidget::onRefreshButtonClicked()
{
    Refresh();
}

void AssetBrowserWidget::onOpenProjectButtonClicked()
{
    // Trigger main window's Open Project action
    if (m_mainWindow) {
        QMetaObject::invokeMethod(m_mainWindow, "onOpenProject", Qt::QueuedConnection);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Context Menus
// ─────────────────────────────────────────────────────────────────────────────

void AssetBrowserWidget::onContentsContextMenu(const QPoint& pos)
{
    auto& assetMgr = AssetManager::GetInstance();
    if (!assetMgr.HasValidAssetRoot()) return;

    QModelIndex index = m_contentsView->indexAt(pos);
    QMenu menu(this);

    if (index.isValid()) {
        QString relativePath = index.data(RelativePathRole).toString();
        bool isDirectory = index.data(IsDirectoryRole).toBool();
        AssetType assetType = static_cast<AssetType>(index.data(AssetTypeRole).toInt());

        if (isDirectory) {
            menu.addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Open"), [this, relativePath]() {
                NavigateTo(relativePath);
            });
            menu.addSeparator();
            showCreateMenu(menu, relativePath);
            menu.addSeparator();
        } else {
            menu.addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open"), [this, relativePath]() {
                emit assetOpenRequested(relativePath);
            });
            if (assetType == AssetType::Prefab) {
                menu.addAction(tr("Instantiate in Scene"), [this, relativePath]() {
                    SpawnableEntry entry;
                    entry.kind = SpawnableEntry::Kind::Prefab;
                    entry.displayName = QFileInfo(relativePath).completeBaseName();
                    entry.assetPath = ToProjectRelativeAssetPath(relativePath);
                    if (m_mainWindow) {
                        m_mainWindow->spawnFromAssetBrowser(entry.toMimePayload());
                    }
                });
                menu.addAction(tr("Replace Selected With Prefab"), [this, relativePath]() {
                    if (!m_mainWindow) return;
                    SpawnableEntry entry;
                    entry.kind = SpawnableEntry::Kind::Prefab;
                    entry.displayName = QFileInfo(relativePath).completeBaseName();
                    entry.assetPath = ToProjectRelativeAssetPath(relativePath);
                    m_mainWindow->replaceFromAssetBrowser(entry.toMimePayload());
                });
            } else if (assetType == AssetType::StaticMesh) {
                menu.addAction(tr("Spawn in Scene"), [this, relativePath]() {
                    emit meshSpawnRequested(relativePath);
                });
                menu.addAction(tr("Replace Selected Mesh"), [this, relativePath]() {
                    if (!m_mainWindow) return;
                    SpawnableEntry entry;
                    entry.kind = SpawnableEntry::Kind::Mesh;
                    entry.displayName = QFileInfo(relativePath).completeBaseName();
                    entry.assetPath = ToProjectRelativeAssetPath(relativePath);
                    m_mainWindow->replaceFromAssetBrowser(entry.toMimePayload());
                });
            } else if (assetType == AssetType::Character) {
                menu.addAction(tr("Spawn in Scene"), [this, relativePath]() {
                    if (!m_mainWindow) return;
                    SpawnableEntry entry;
                    entry.kind = SpawnableEntry::Kind::Character;
                    entry.displayName = QFileInfo(relativePath).completeBaseName();
                    entry.assetPath = ToProjectRelativeAssetPath(relativePath);
                    m_mainWindow->spawnFromAssetBrowser(entry.toMimePayload());
                });
                menu.addAction(tr("Reimport Character…"), [this, relativePath]() {
                    auto& assetMgr = AssetManager::GetInstance();
                    const QString portable = ToProjectRelativeAssetPath(relativePath);
                    const QString src = QFileDialog::getOpenFileName(
                        this, tr("Reimport Character"), QString(), tr("GLB (*.glb)"));
                    if (src.isEmpty()) {
                        return;
                    }
                    std::string error;
                    if (!Hephaestus::CharacterImporter::Reimport(
                            assetMgr.GetProjectRoot(), portable.toStdString(),
                            std::filesystem::path(src.toStdString()), error)) {
                        QMessageBox::critical(this, tr("Reimport Character"),
                            QString::fromStdString(error));
                        return;
                    }
                    Hephaestus::CharacterRuntime::Invalidate(portable.toStdString());
                    if (m_mainWindow) {
                        m_mainWindow->rebuildSceneVisuals();
                    }
                    EditorAssetServices::Instance().ReferenceIndex().ScanProject(
                        assetMgr.GetProjectRoot());
                    m_statusLabel->setText(tr("Reimported %1").arg(QFileInfo(relativePath).fileName()));
                    Refresh();
                });
            }
            if (assetType == AssetType::UILayout) {
                menu.addAction(tr("Open in IDE"), [this, relativePath]() {
                    emit assetOpenInIDERequested(relativePath);
                });
            }
            const QString portable = ToProjectRelativeAssetPath(relativePath);
            const bool ownedSource =
                Hephaestus::CharacterOwnedSource::IsOwnedSourcePortableKey(portable.toStdString());
            if (!ownedSource &&
                (assetType == AssetType::Texture ||
                 assetType == AssetType::StaticMesh ||
                 assetType == AssetType::Audio)) {
                menu.addAction(tr("Reimport"), [this, relativePath]() {
                    if (!m_mainWindow) {
                        return;
                    }
                    auto* pipeline = m_mainWindow->GetAssetPipeline();
                    if (!pipeline || !pipeline->IsInitialized()) {
                        QMessageBox::information(this, tr("Reimport"),
                            tr("Open a project before reimporting assets."));
                        return;
                    }
                    auto& assetMgr = AssetManager::GetInstance();
                    const std::filesystem::path abs(
                        assetMgr.ToAbsolutePath(relativePath.toStdString()));
                    const auto result = pipeline->RequestReimportByPath(abs, ReimportPolicy::Manual);
                    m_statusLabel->setText(result.success
                        ? tr("Queued reimport: %1").arg(QFileInfo(relativePath).fileName())
                        : tr("Reimport failed: %1").arg(QString::fromStdString(result.message)));
                });
                menu.addAction(tr("Reimport All Changed"), [this]() {
                    if (!m_mainWindow) {
                        return;
                    }
                    auto* pipeline = m_mainWindow->GetAssetPipeline();
                    if (!pipeline || !pipeline->IsInitialized()) {
                        QMessageBox::information(this, tr("Reimport"),
                            tr("Open a project before reimporting assets."));
                        return;
                    }
                    const auto result = pipeline->ReimportAllChanged();
                    if (result.changedAssetIds.empty()) {
                        m_statusLabel->setText(tr("No changed assets to reimport"));
                    } else {
                        m_statusLabel->setText(
                            tr("Queued reimport for %1 changed asset(s)")
                                .arg(static_cast<int>(result.changedAssetIds.size())));
                    }
                });
            }
            menu.addSeparator();
        }

        menu.addAction(tr("Rename"), [this, relativePath, isDirectory]() {
            renameAsset(relativePath, isDirectory);
        });
        if (!isDirectory) {
            menu.addAction(tr("Duplicate"), [this, relativePath]() {
                duplicateAsset(relativePath, false);
            });
            menu.addAction(tr("Copy Asset Path"), [this, relativePath]() {
                copyAssetPath(relativePath);
            });
            menu.addAction(tr("Find References..."), [this, relativePath]() {
                findAssetReferences(relativePath);
            });
            menu.addAction(tr("Open Source Externally"), [this, relativePath]() {
                auto& assetMgr = AssetManager::GetInstance();
                const QString abs = QString::fromStdString(assetMgr.ToAbsolutePath(relativePath.toStdString()));
                QDesktopServices::openUrl(QUrl::fromLocalFile(abs));
            });
        }

        QAction* deleteAct = menu.addAction(tr("Delete"), [this, relativePath, isDirectory]() {
            deleteAsset(relativePath, isDirectory);
        });
        deleteAct->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));

        menu.addSeparator();
        menu.addAction(tr("Show in Folder"), [this, relativePath]() {
            showInFolder(relativePath);
        });
        menu.addAction(tr("Show in Explorer"), [this, relativePath]() {
            showInExplorer(relativePath);
        });
    } else {
        showCreateMenu(menu, m_currentPath);
        menu.addSeparator();
        if (m_mainWindow) {
            menu.addAction(tr("Reimport All Changed"), [this]() {
                auto* pipeline = m_mainWindow->GetAssetPipeline();
                if (!pipeline || !pipeline->IsInitialized()) {
                    QMessageBox::information(this, tr("Reimport"),
                        tr("Open a project before reimporting assets."));
                    return;
                }
                const auto result = pipeline->ReimportAllChanged();
                if (result.changedAssetIds.empty()) {
                    m_statusLabel->setText(tr("No changed assets to reimport"));
                } else {
                    m_statusLabel->setText(
                        tr("Queued reimport for %1 changed asset(s)")
                            .arg(static_cast<int>(result.changedAssetIds.size())));
                }
            });
            menu.addSeparator();
        }
        menu.addAction(tr("Show in Explorer"), [this]() {
            showInExplorer(m_currentPath);
        });
    }

    menu.exec(m_contentsView->viewport()->mapToGlobal(pos));
}

void AssetBrowserWidget::onFolderTreeContextMenu(const QPoint& pos)
{
    auto& assetMgr = AssetManager::GetInstance();
    if (!assetMgr.HasValidAssetRoot()) return;

    QModelIndex index = m_folderTree->indexAt(pos);
    QMenu menu(this);

    QString targetDir;
    if (index.isValid()) {
        targetDir = index.data(RelativePathRole).toString();
    }

    showCreateMenu(menu, targetDir);

    if (index.isValid() && !targetDir.isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("Rename"), [this, targetDir]() {
            renameAsset(targetDir, true);
        });
        QAction* deleteAct = menu.addAction(tr("Delete"), [this, targetDir]() {
            deleteAsset(targetDir, true);
        });
        deleteAct->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    }

    menu.addSeparator();
    menu.addAction(tr("Show in Explorer"), [this, targetDir]() {
        showInExplorer(targetDir);
    });

    menu.exec(m_folderTree->viewport()->mapToGlobal(pos));
}

void AssetBrowserWidget::showCreateMenu(QMenu& menu, const QString& targetDirectory)
{
    menu.addAction(style()->standardIcon(QStyle::SP_DirIcon), tr("New Folder"), [this, targetDirectory]() {
        createNewFolder(targetDirectory);
    });

    menu.addSeparator();

    menu.addAction(style()->standardIcon(QStyle::SP_FileDialogListView), tr("New Lua Script"), [this, targetDirectory]() {
        createNewLuaScript(targetDirectory);
    });

    menu.addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("New Scene"), [this, targetDirectory]() {
        createNewScene(targetDirectory);
    });

    menu.addAction(style()->standardIcon(QStyle::SP_DialogApplyButton), tr("New Material"), [this, targetDirectory]() {
        createNewMaterial(targetDirectory);
    });

    menu.addAction(style()->standardIcon(QStyle::SP_DesktopIcon), tr("New UI File"), [this, targetDirectory]() {
        createNewUIFile(targetDirectory);
    });

    menu.addAction(style()->standardIcon(QStyle::SP_VistaShield), tr("New Prefab"), [this, targetDirectory]() {
        createNewPrefab(targetDirectory);
    });

    menu.addAction(style()->standardIcon(QStyle::SP_DirLinkIcon), tr("New Character"), [this, targetDirectory]() {
        createNewCharacter(targetDirectory);
    });
    menu.addAction(tr("Import Character…"), [this]() {
        importCharacter();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset Creation / Management
// ─────────────────────────────────────────────────────────────────────────────

void AssetBrowserWidget::createNewFolder(const QString& parentRelativePath)
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Folder"),
                                         tr("Folder name:"), QLineEdit::Normal,
                                         tr("New Folder"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    auto& assetMgr = AssetManager::GetInstance();
    std::string parentAbs = parentRelativePath.isEmpty()
        ? assetMgr.GetAssetRoot()
        : assetMgr.ToAbsolutePath(parentRelativePath.toStdString());

    std::filesystem::path folderPath = std::filesystem::path(parentAbs) / name.trimmed().toStdString();

    std::error_code ec;
    if (std::filesystem::exists(folderPath, ec)) {
        QMessageBox::warning(this, tr("New Folder"), tr("A folder with that name already exists."));
        return;
    }

    if (!std::filesystem::create_directories(folderPath, ec)) {
        QMessageBox::critical(this, tr("New Folder"), tr("Failed to create folder: %1").arg(QString::fromStdString(ec.message())));
        return;
    }

    Refresh();
    std::cout << "[AssetBrowser] Created folder: " << folderPath.string() << "\n";
}

void AssetBrowserWidget::createNewLuaScript(const QString& parentRelativePath)
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Lua Script"),
                                         tr("Script name (without .lua):"), QLineEdit::Normal,
                                         tr("NewScript"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    auto& assetMgr = AssetManager::GetInstance();
    std::string parentAbs = parentRelativePath.isEmpty()
        ? assetMgr.GetAssetRoot()
        : assetMgr.ToAbsolutePath(parentRelativePath.toStdString());

    std::filesystem::path filePath = std::filesystem::path(parentAbs) / (name.trimmed().toStdString() + ".lua");

    if (std::filesystem::exists(filePath)) {
        QMessageBox::warning(this, tr("New Lua Script"), tr("A file with that name already exists."));
        return;
    }

    std::ofstream file(filePath);
    if (!file.is_open()) {
        QMessageBox::critical(this, tr("New Lua Script"), tr("Failed to create script file."));
        return;
    }

    std::string scriptName = name.trimmed().toStdString();
    file << "-- " << scriptName << ".lua\n"
         << "-- Hephaestus Lua Script\n\n"
         << "Expose.number(\"moveSpeed\", 5.0)\n\n"
         << "function onAwake()\n"
         << "    -- Called when the script starts (play mode)\n"
         << "end\n\n"
         << "function onUpdate(dt)\n"
         << "    -- Called every frame (dt = delta time in seconds)\n"
         << "end\n\n"
         << "function onDestroy()\n"
         << "    -- Called when the script is destroyed\n"
         << "end\n";
    file.close();

    Refresh();
    std::cout << "[AssetBrowser] Created Lua script: " << filePath.string() << "\n";
}

void AssetBrowserWidget::createNewScene(const QString& parentRelativePath)
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Scene"),
                                         tr("Scene name (without extension):"), QLineEdit::Normal,
                                         tr("NewScene"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    auto& assetMgr = AssetManager::GetInstance();
    std::string parentAbs = parentRelativePath.isEmpty()
        ? assetMgr.GetAssetRoot()
        : assetMgr.ToAbsolutePath(parentRelativePath.toStdString());

    std::filesystem::path filePath = std::filesystem::path(parentAbs) / (name.trimmed().toStdString() + ".hpscene");

    if (std::filesystem::exists(filePath)) {
        QMessageBox::warning(this, tr("New Scene"), tr("A scene with that name already exists."));
        return;
    }

    std::ofstream file(filePath);
    if (!file.is_open()) {
        QMessageBox::critical(this, tr("New Scene"), tr("Failed to create scene file."));
        return;
    }

    file << "{\n"
         << "  \"name\": \"" << name.trimmed().toStdString() << "\",\n"
         << "  \"entities\": []\n"
         << "}\n";
    file.close();

    Refresh();
    std::cout << "[AssetBrowser] Created scene: " << filePath.string() << "\n";
}

void AssetBrowserWidget::createNewMaterial(const QString& parentRelativePath)
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Material"),
                                         tr("Material name (without extension):"), QLineEdit::Normal,
                                         tr("NewMaterial"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    auto& assetMgr = AssetManager::GetInstance();
    std::string parentAbs = parentRelativePath.isEmpty()
        ? assetMgr.GetAssetRoot()
        : assetMgr.ToAbsolutePath(parentRelativePath.toStdString());

    std::filesystem::path filePath = std::filesystem::path(parentAbs) / (name.trimmed().toStdString() + ".hematerial");

    if (std::filesystem::exists(filePath)) {
        QMessageBox::warning(this, tr("New Material"), tr("A material with that name already exists."));
        return;
    }

    MaterialComponent material;
    material.albedo = Vec3(1.0f, 1.0f, 1.0f);
    material.roughness = 0.5f;
    material.metallic = 0.0f;
    material.ao = 1.0f;

    MaterialEditor::Graph graph;
    MaterialEditor::BuildMinimalGraphFromLegacyPbr(material, graph);

    std::string error;
    if (!MaterialEditor::SaveMaterialAssetCompat(filePath, material, name.trimmed().toStdString(), graph, {}, error)) {
        QMessageBox::critical(this, tr("New Material"), tr("Failed to create material file:\n%1").arg(QString::fromStdString(error)));
        return;
    }

    Refresh();
    const QString fileName = name.trimmed() + ".hematerial";
    const QString relativePath = parentRelativePath.isEmpty() ? fileName : parentRelativePath + "/" + fileName;
    emit assetOpenRequested(relativePath);
    std::cout << "[AssetBrowser] Created material: " << filePath.string() << "\n";
}

void AssetBrowserWidget::createNewUIFile(const QString& parentRelativePath)
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New UI File"),
                                         tr("UI file name (without .hui):"), QLineEdit::Normal,
                                         tr("NewUI"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    auto& assetMgr = AssetManager::GetInstance();
    std::string parentAbs = parentRelativePath.isEmpty()
        ? assetMgr.GetAssetRoot()
        : assetMgr.ToAbsolutePath(parentRelativePath.toStdString());

    std::filesystem::path filePath = std::filesystem::path(parentAbs) / (name.trimmed().toStdString() + ".hui");

    if (std::filesystem::exists(filePath)) {
        QMessageBox::warning(this, tr("New UI File"), tr("A file with that name already exists."));
        return;
    }

    std::ofstream file(filePath);
    if (!file.is_open()) {
        QMessageBox::critical(this, tr("New UI File"), tr("Failed to create UI file."));
        return;
    }

    std::string uiName = name.trimmed().toStdString();
    file << Hephaestus::HephaestusUI::generateDefaultHUIContent(uiName);
    file.close();

    Refresh();
    const QString fileName = name.trimmed() + ".hui";
    const QString relativePath = parentRelativePath.isEmpty() ? fileName : parentRelativePath + "/" + fileName;
    emit assetOpenRequested(relativePath);
    std::cout << "[AssetBrowser] Created UI file: " << filePath.string() << "\n";
}

void AssetBrowserWidget::createNewPrefab(const QString& parentRelativePath)
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Prefab"),
                                         tr("Prefab name (without extension):"), QLineEdit::Normal,
                                         tr("NewPrefab"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    auto& assetMgr = AssetManager::GetInstance();
    std::string parentAbs = parentRelativePath.isEmpty()
        ? assetMgr.GetAssetRoot()
        : assetMgr.ToAbsolutePath(parentRelativePath.toStdString());

    std::string prefabName = name.trimmed().toStdString();
    std::filesystem::path filePath = std::filesystem::path(parentAbs) / (prefabName + ".hpprefab");

    if (std::filesystem::exists(filePath)) {
        QMessageBox::warning(this, tr("New Prefab"), tr("A prefab with that name already exists."));
        return;
    }

    PrefabAsset asset;
    asset.assetVersion = 1;
    asset.name = prefabName;
    asset.rootNodeId = "root";

    PrefabNode rootNode;
    rootNode.id = "root";
    rootNode.name = prefabName;
    rootNode.localTransform.position = {0.0f, 0.0f, 0.0f};
    rootNode.localTransform.rotation = {0.0f, 0.0f, 0.0f};
    rootNode.localTransform.scale = {1.0f, 1.0f, 1.0f};
    asset.nodes.push_back(std::move(rootNode));

    std::string error;
    if (!PrefabSerializer::Serialize(asset, filePath, error)) {
        QMessageBox::critical(this, tr("New Prefab"),
            tr("Failed to create prefab: %1").arg(QString::fromStdString(error)));
        return;
    }

    Refresh();
    const QString fileName = QString::fromStdString(prefabName) + ".hpprefab";
    const QString relativePath = parentRelativePath.isEmpty() ? fileName : parentRelativePath + "/" + fileName;
    emit assetOpenRequested(relativePath);
    std::cout << "[AssetBrowser] Created prefab: " << filePath.string() << "\n";
}

void AssetBrowserWidget::createNewCharacter(const QString& parentRelativePath)
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Character"),
                                         tr("Character name (without extension):"), QLineEdit::Normal,
                                         tr("NewCharacter"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    auto& assetMgr = AssetManager::GetInstance();
    std::string parentAbs = parentRelativePath.isEmpty()
        ? assetMgr.GetAssetRoot()
        : assetMgr.ToAbsolutePath(parentRelativePath.toStdString());

    std::string characterName = name.trimmed().toStdString();
    std::filesystem::path filePath = std::filesystem::path(parentAbs) / (characterName + ".hpcharacter");

    if (std::filesystem::exists(filePath)) {
        QMessageBox::warning(this, tr("New Character"), tr("A character with that name already exists."));
        return;
    }

    std::string error;
    const std::filesystem::path projectRoot(assetMgr.GetProjectRoot());
    if (!Hephaestus::CharacterSerializer::CreateNew(filePath, projectRoot, error)) {
        QMessageBox::critical(this, tr("New Character"),
            tr("Failed to create character: %1").arg(QString::fromStdString(error)));
        return;
    }

    Refresh();
    const QString fileName = QString::fromStdString(characterName) + ".hpcharacter";
    const QString relativePath = parentRelativePath.isEmpty() ? fileName : parentRelativePath + "/" + fileName;
    emit assetOpenRequested(relativePath);
    std::cout << "[AssetBrowser] Created character: " << filePath.string() << "\n";
}

void AssetBrowserWidget::deleteAsset(const QString& relativePath, bool isDirectory)
{
    if (relativePath.isEmpty()) return;

    auto& services = EditorAssetServices::Instance();
    if (services.HasProject()) {
        const QString portable = ToProjectRelativeAssetPath(relativePath);
        auto& ops = services.Operations();
        const auto preflight = ops.Preflight(portable.toStdString(), std::string());
        const int referrerCount = static_cast<int>(preflight.referrers.size());

        QString message;
        if (isDirectory) {
            message = tr("Move this folder to the project trash?\n\n%1")
                          .arg(portable);
        } else if (referrerCount > 0) {
            message = tr("Move this asset to trash?\n\n%1\n\n"
                         "Warning: %2 document(s) still reference it. "
                         "References will not be rewritten.")
                          .arg(portable)
                          .arg(referrerCount);
        } else {
            message = tr("Move this asset to trash?\n\n%1\n\n"
                         "You can restore it later from .hephaestus/trash.")
                          .arg(portable);
        }

        auto result = QMessageBox::question(this, tr("Delete Asset"), message,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result != QMessageBox::Yes) {
            return;
        }

        if (!isDirectory && portable.endsWith(QLatin1String(".hpcharacter"), Qt::CaseInsensitive)) {
            auto& assetMgr = AssetManager::GetInstance();
            const QString abs = QString::fromStdString(
                assetMgr.ToAbsolutePath(relativePath.toStdString()));
            if (m_mainWindow && !m_mainWindow->requestCloseOpenCharacter(abs)) {
                return;
            }
        }

        const auto op = ops.MoveToTrash(portable.toStdString());
        if (!op.success) {
            QMessageBox::critical(this, tr("Delete Asset"),
                tr("Failed to move to trash:\n%1").arg(QString::fromStdString(op.error)));
            return;
        }

        std::cout << "[AssetBrowser] Trashed: " << portable.toStdString()
                  << " opId=" << op.opId << "\n";
        Refresh();
        return;
    }

    QString typeName = isDirectory ? tr("folder") : tr("file");
    auto result = QMessageBox::question(this, tr("Delete Asset"),
        tr("Are you sure you want to delete this %1?\n\n%2\n\nThis cannot be undone.")
            .arg(typeName, relativePath),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result != QMessageBox::Yes) return;

    auto& assetMgr = AssetManager::GetInstance();
    std::string absPath = assetMgr.ToAbsolutePath(relativePath.toStdString());

    std::error_code ec;
    if (isDirectory) {
        std::filesystem::remove_all(absPath, ec);
    } else {
        std::filesystem::remove(absPath, ec);
    }

    if (ec) {
        QMessageBox::critical(this, tr("Delete Asset"),
            tr("Failed to delete: %1").arg(QString::fromStdString(ec.message())));
        return;
    }

    std::cout << "[AssetBrowser] Deleted: " << absPath << "\n";
    Refresh();
}

void AssetBrowserWidget::renameAsset(const QString& relativePath, bool isDirectory)
{
    if (relativePath.isEmpty()) return;

    auto& assetMgr = AssetManager::GetInstance();
    std::filesystem::path oldAbsPath(assetMgr.ToAbsolutePath(relativePath.toStdString()));
    QString oldName = QString::fromStdString(oldAbsPath.filename().string());

    bool ok = false;
    QString newName = QInputDialog::getText(this, tr("Rename"),
                                            tr("New name:"), QLineEdit::Normal,
                                            oldName, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == oldName) return;

    std::filesystem::path newAbsPath = oldAbsPath.parent_path() / newName.trimmed().toStdString();

    if (std::filesystem::exists(newAbsPath)) {
        QMessageBox::warning(this, tr("Rename"), tr("An item with that name already exists."));
        return;
    }

    auto& services = EditorAssetServices::Instance();
    if (services.HasProject() && !isDirectory) {
        const QString oldPortable = ToProjectRelativeAssetPath(relativePath);
        const QString newRelative = QString::fromStdString(
            std::filesystem::relative(newAbsPath, assetMgr.GetAssetRoot()).generic_string());
        const QString newPortable = ToProjectRelativeAssetPath(newRelative);

        const auto op = services.Operations().RenameAsset(
            oldPortable.toStdString(), newPortable.toStdString());
        if (!op.success) {
            QMessageBox::critical(this, tr("Rename"),
                tr("Failed to rename:\n%1").arg(QString::fromStdString(op.error)));
            return;
        }
        if (!op.warnings.empty()) {
            QString warn;
            for (const auto& w : op.warnings) {
                warn += QString::fromStdString(w) + QLatin1Char('\n');
            }
            QMessageBox::warning(this, tr("Rename"), warn.trimmed());
        }
        std::cout << "[AssetBrowser] Renamed (service): " << oldPortable.toStdString()
                  << " -> " << newPortable.toStdString() << "\n";
        Refresh();
        return;
    }

    std::error_code ec;
    std::filesystem::rename(oldAbsPath, newAbsPath, ec);
    if (ec) {
        QMessageBox::critical(this, tr("Rename"),
            tr("Failed to rename: %1").arg(QString::fromStdString(ec.message())));
        return;
    }

    std::cout << "[AssetBrowser] Renamed: " << oldAbsPath.string() << " -> " << newAbsPath.string() << "\n";
    Refresh();
}

void AssetBrowserWidget::findAssetReferences(const QString& relativePath)
{
    if (relativePath.isEmpty()) {
        return;
    }

    auto& services = EditorAssetServices::Instance();
    if (!services.HasProject()) {
        QMessageBox::information(this, tr("Find References"),
            tr("Open a project to find asset references."));
        return;
    }

    const QString portable = ToProjectRelativeAssetPath(relativePath);
    AssetDependencyDialog dialog(&services.ReferenceIndex(), portable, this);
    dialog.SetOpenOrRevealCallback([this](const QString& portablePath) {
        QString path = portablePath;
        path.replace('\\', '/');
        if (path.startsWith(QStringLiteral("assets/"), Qt::CaseInsensitive)) {
            path = path.mid(QStringLiteral("assets/").size());
        }
        showInFolder(path);
    });
    dialog.exec();
}

void AssetBrowserWidget::showInFolder(const QString& relativePath)
{
    QString path = relativePath;
    path.replace('\\', '/');
    if (path.isEmpty()) {
        NavigateTo(QString());
        return;
    }

    // Clear search so folder browse mode restores
    if (m_searchBox && !m_searchBox->text().isEmpty()) {
        m_searchBox->blockSignals(true);
        m_searchBox->clear();
        m_searchBox->blockSignals(false);
    }

    const int slash = path.lastIndexOf(QLatin1Char('/'));
    const QString folder = (slash >= 0) ? path.left(slash) : QString();
    const QString fileName = (slash >= 0) ? path.mid(slash + 1) : path;

    NavigateTo(folder);

    // Select the asset in the contents view when present
    if (!m_contentsModel || fileName.isEmpty()) {
        return;
    }
    for (int row = 0; row < m_contentsModel->rowCount(); ++row) {
        QStandardItem* item = m_contentsModel->item(row);
        if (!item) {
            continue;
        }
        const QString itemPath = item->data(RelativePathRole).toString().replace('\\', '/');
        if (itemPath == path || itemPath.endsWith(QLatin1Char('/') + fileName) ||
            itemPath == fileName) {
            const QModelIndex idx = m_contentsModel->indexFromItem(item);
            m_contentsView->setCurrentIndex(idx);
            m_contentsView->scrollTo(idx);
            break;
        }
    }
}

void AssetBrowserWidget::showInExplorer(const QString& relativePath)
{
    auto& assetMgr = AssetManager::GetInstance();
    std::string absPath = relativePath.isEmpty()
        ? assetMgr.GetAssetRoot()
        : assetMgr.ToAbsolutePath(relativePath.toStdString());

    QString qPath = QString::fromStdString(absPath);

#ifdef _WIN32
    if (std::filesystem::is_directory(absPath)) {
        QProcess::startDetached("explorer", {QDir::toNativeSeparators(qPath)});
    } else {
        QProcess::startDetached("explorer", {"/select,", QDir::toNativeSeparators(qPath)});
    }
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QString::fromStdString(std::filesystem::path(absPath).parent_path().string())));
#endif
}

void AssetBrowserWidget::importAssets()
{
    auto& assetMgr = AssetManager::GetInstance();
    if (!assetMgr.HasValidAssetRoot()) {
        QMessageBox::information(this, tr("Import"), tr("Open a project before importing assets."));
        return;
    }

    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Import Assets"),
        QString(),
        tr("Supported Assets (*.obj *.fbx *.gltf *.glb *.png *.jpg *.jpeg *.tga *.hdr *.exr *.wav *.mp3 *.ogg *.lua *.hematerial *.hpscene *.hpprefab *.hui);;All Files (*)"));
    if (files.isEmpty()) {
        return;
    }

    const std::string destDir = m_currentPath.isEmpty()
        ? assetMgr.GetAssetRoot()
        : assetMgr.ToAbsolutePath(m_currentPath.toStdString());

    int imported = 0;
    QStringList failures;
    for (const QString& src : files) {
        const QFileInfo info(src);
        std::filesystem::path dest = std::filesystem::path(destDir) / info.fileName().toStdString();
        if (std::filesystem::exists(dest)) {
            auto result = QMessageBox::question(
                this,
                tr("Overwrite?"),
                tr("\"%1\" already exists in this folder. Overwrite it?").arg(info.fileName()),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                QMessageBox::No);
            if (result == QMessageBox::Cancel) {
                break;
            }
            if (result != QMessageBox::Yes) {
                continue;
            }
            std::error_code removeEc;
            std::filesystem::remove(dest, removeEc);
        }

        std::error_code ec;
        std::filesystem::copy_file(src.toStdString(), dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            failures << QString("%1 (%2)").arg(info.fileName(), QString::fromStdString(ec.message()));
        } else {
            ++imported;
            if (m_mainWindow) {
                if (auto* pipeline = m_mainWindow->GetAssetPipeline()) {
                    pipeline->RegisterCopiedAsset(dest);
                }
            }
        }
    }

    Refresh();
    if (!failures.isEmpty()) {
        QMessageBox::warning(this, tr("Import"),
            tr("Imported %1 file(s).\n\nFailed:\n%2").arg(imported).arg(failures.join('\n')));
    } else {
        m_statusLabel->setText(tr("Imported %1 file(s)").arg(imported));
    }
}

void AssetBrowserWidget::importCharacter()
{
    auto& assetMgr = AssetManager::GetInstance();
    if (!assetMgr.HasValidAssetRoot()) {
        QMessageBox::information(this, tr("Import Character"),
                                 tr("Open a project before importing a character."));
        return;
    }

    const QString src = QFileDialog::getOpenFileName(
        this, tr("Import Character"), QString(), tr("GLB (*.glb)"));
    if (src.isEmpty()) {
        return;
    }

    std::filesystem::path destAbs;
    if (m_currentPath.isEmpty()) {
        destAbs = std::filesystem::path(assetMgr.ToAbsolutePath("characters"));
    } else {
        destAbs = std::filesystem::path(assetMgr.ToAbsolutePath(m_currentPath.toStdString()));
    }

    CharacterImportDialog dialog(std::filesystem::path(src.toStdString()),
                                 assetMgr.GetProjectRoot(), destAbs, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (dialog.importAsStaticMesh()) {
        importAssets();
        return;
    }

    Hephaestus::CharacterImportRequest request;
    request.sourceGlb = std::filesystem::path(src.toStdString());
    request.projectRoot = assetMgr.GetProjectRoot();
    request.destinationFolderAbs = dialog.destinationFolder();
    request.characterName = dialog.characterName().toStdString();
    request.createController = dialog.createController();

    std::string portable;
    std::string error;
    if (!Hephaestus::CharacterImporter::Import(request, portable, error)) {
        QMessageBox::critical(this, tr("Import Character"), QString::fromStdString(error));
        return;
    }

    if (EditorAssetServices::Instance().HasProject()) {
        EditorAssetServices::Instance().ReferenceIndex().ScanProject(assetMgr.GetProjectRoot());
    }
    Refresh();
    const QString relative = QString::fromStdString(portable);
    QString fromAssets = relative;
    if (fromAssets.startsWith(QStringLiteral("assets/"))) {
        fromAssets = fromAssets.mid(7);
    }
    RevealAsset(fromAssets);
    m_statusLabel->setText(tr("Imported character: %1").arg(QFileInfo(relative).fileName()));
}

void AssetBrowserWidget::duplicateAsset(const QString& relativePath, bool isDirectory)
{
    if (relativePath.isEmpty() || isDirectory) {
        return;
    }

    auto& assetMgr = AssetManager::GetInstance();
    std::filesystem::path src(assetMgr.ToAbsolutePath(relativePath.toStdString()));
    const std::string stem = src.stem().string();
    const std::string ext = src.extension().string();
    std::filesystem::path dest;
    int suffix = 1;
    do {
        dest = src.parent_path() / (stem + "_Copy" + (suffix > 1 ? std::to_string(suffix) : "") + ext);
        ++suffix;
    } while (std::filesystem::exists(dest) && suffix < 1000);

    std::error_code ec;
    std::filesystem::copy_file(src, dest, ec);
    if (ec) {
        QMessageBox::critical(this, tr("Duplicate"),
            tr("Could not duplicate asset:\n%1").arg(QString::fromStdString(ec.message())));
        return;
    }

    Refresh();
    m_statusLabel->setText(tr("Duplicated: %1").arg(QString::fromStdString(dest.filename().string())));
}

void AssetBrowserWidget::copyAssetPath(const QString& relativePath)
{
    if (relativePath.isEmpty()) {
        return;
    }
    const QString projectPath = ToProjectRelativeAssetPath(relativePath);
    if (QClipboard* clipboard = QApplication::clipboard()) {
        clipboard->setText(projectPath);
        m_statusLabel->setText(tr("Copied path: %1").arg(projectPath));
    }
}

QModelIndex AssetBrowserWidget::findFolderIndexByPath(const QString& relativePath) const
{
    if (!m_folderModel) {
        return {};
    }

    std::function<QModelIndex(const QModelIndex&)> walk;
    walk = [&](const QModelIndex& parent) -> QModelIndex {
        const int rows = m_folderModel->rowCount(parent);
        for (int r = 0; r < rows; ++r) {
            QModelIndex idx = m_folderModel->index(r, 0, parent);
            const QString path = idx.data(RelativePathRole).toString();
            if (path == relativePath) {
                return idx;
            }
            QModelIndex child = walk(idx);
            if (child.isValid()) {
                return child;
            }
        }
        return {};
    };

    return walk(QModelIndex());
}

void AssetBrowserWidget::syncFolderTreeSelection()
{
    if (!m_folderTree || !m_folderModel) {
        return;
    }

    QModelIndex target;
    if (m_currentPath.isEmpty()) {
        target = m_folderModel->index(0, 0);
    } else {
        target = findFolderIndexByPath(m_currentPath);
    }

    if (target.isValid()) {
        m_folderTree->blockSignals(true);
        m_folderTree->setCurrentIndex(target);
        m_folderTree->expand(target);
        m_folderTree->scrollTo(target);
        m_folderTree->blockSignals(false);
    }
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
