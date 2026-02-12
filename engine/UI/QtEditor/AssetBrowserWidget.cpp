/**
 * @file AssetBrowserWidget.cpp
 * @brief Implementation of Qt asset browser widget.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "AssetBrowserWidget.h"
#include "EditorMainWindow.h"
#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetTypes.h"
#include "../../Core/ProjectContext.h"

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
#include <QMessageBox>
#include <iostream>

namespace Hephaestus {
namespace QtEditor {

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
    }
    
    rebuildContentsView();
    updateBreadcrumb();
    updateStatusBar();
}

void AssetBrowserWidget::SetOpenAssetCallback(OpenAssetCallback callback)
{
    m_openAssetCallback = std::move(callback);
}

void AssetBrowserWidget::setupUI()
{
    setObjectName("AssetBrowserRoot");
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

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
    connect(m_importButton, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(this, tr("Import Asset"), QString(), tr("All Files (*)"));
        if (!file.isEmpty()) {
            m_statusLabel->setText(tr("Import queued: %1").arg(file));
        }
    });
    m_toolbar->addWidget(m_importButton);

    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItems({ tr("All"), tr("Meshes"), tr("Textures"), tr("Materials"), tr("Scenes"), tr("Scripts") });
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

    // Breadcrumb label
    m_breadcrumbLabel = new QLabel(tr("Assets"), this);
    m_breadcrumbLabel->setMinimumWidth(100);
    QFont breadcrumbFont = m_breadcrumbLabel->font();
    breadcrumbFont.setBold(true);
    m_breadcrumbLabel->setFont(breadcrumbFont);
    m_toolbar->addWidget(m_breadcrumbLabel);

    // Spacer
    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);

    // Search box (optional - placeholder for future)
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText(tr("Search assets..."));
    m_searchBox->setMaximumWidth(200);
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
    connect(m_newFolderButton, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, tr("New Folder"), tr("Folder creation UI is ready; backend folder creation will be wired next."));
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
    connect(m_folderTree, &QTreeView::clicked, this, &AssetBrowserWidget::onFolderTreeClicked);
    leftLayout->addWidget(m_folderTree, 1);
    m_splitter->addWidget(leftPanel);

    // Right panel - Contents
    m_contentsPanel = new QWidget(m_splitter);
    QVBoxLayout* contentsLayout = new QVBoxLayout(m_contentsPanel);
    contentsLayout->setContentsMargins(0, 0, 0, 0);

    m_contentsView = new QListView(m_contentsPanel);
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
    connect(m_contentsView, &QListView::doubleClicked, this, &AssetBrowserWidget::onContentsDoubleClicked);
    connect(m_contentsView, &QListView::clicked, this, &AssetBrowserWidget::onContentsClicked);
    contentsLayout->addWidget(m_contentsView);

    m_splitter->addWidget(m_contentsPanel);
    m_splitter->setSizes({220, 760});

    mainLayout->addWidget(m_splitter);

    // ─────────────────────────────────────────────────────────────────────────
    // Empty State Widget (overlay)
    // ─────────────────────────────────────────────────────────────────────────
    m_emptyStateWidget = new QWidget(this);
    QVBoxLayout* emptyLayout = new QVBoxLayout(m_emptyStateWidget);
    emptyLayout->setAlignment(Qt::AlignCenter);

    QLabel* emptyIcon = new QLabel(this);
    emptyIcon->setText(QString::fromUtf8("📁"));
    QFont iconFont = emptyIcon->font();
    iconFont.setPointSize(48);
    emptyIcon->setFont(iconFont);
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyIcon);

    QLabel* emptyLabel = new QLabel(tr("No project open"), this);
    QFont labelFont = emptyLabel->font();
    labelFont.setPointSize(14);
    emptyLabel->setFont(labelFont);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyLabel);

    QLabel* emptySubLabel = new QLabel(tr("Open a project to browse assets"), this);
    emptySubLabel->setAlignment(Qt::AlignCenter);
    emptySubLabel->setStyleSheet("color: gray;");
    emptyLayout->addWidget(emptySubLabel);

    m_openProjectButton = new QPushButton(tr("Open Project..."), this);
    m_openProjectButton->setMinimumWidth(150);
    connect(m_openProjectButton, &QPushButton::clicked, this, &AssetBrowserWidget::onOpenProjectButtonClicked);
    emptyLayout->addWidget(m_openProjectButton, 0, Qt::AlignCenter);

    mainLayout->addWidget(m_emptyStateWidget);
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

    auto entries = assetMgr.ListDirectory(m_currentPath.toStdString());

    const QString query = m_searchBox ? m_searchBox->text().trimmed() : QString();
    const QString filter = m_filterCombo ? m_filterCombo->currentText() : tr("All");

    auto matchesFilter = [&](AssetType type, bool isDirectory) {
        if (filter == tr("All") || isDirectory) return true;
        if (filter == tr("Meshes")) return type == AssetType::StaticMesh;
        if (filter == tr("Textures")) return type == AssetType::Texture;
        if (filter == tr("Materials")) return type == AssetType::Material;
        if (filter == tr("Scenes")) return type == AssetType::Scene;
        if (filter == tr("Scripts")) return type == AssetType::Script;
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
            case AssetType::Blueprint: return QStringLiteral("[Blueprint]");
            case AssetType::Shader: return QStringLiteral("[Shader]");
            case AssetType::Font: return QStringLiteral("[Font]");
            case AssetType::Text: return QStringLiteral("[Text]");
            default: return QStringLiteral("[Asset]");
        }
    };

    for (const auto& entry : entries) {
        const QString name = QString::fromStdString(entry.name);
        const AssetType type = static_cast<AssetType>(entry.type);

        if (!query.isEmpty() && !name.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        if (!matchesFilter(type, entry.isDirectory)) {
            continue;
        }

        QStandardItem* item = new QStandardItem();
        item->setText(QString("%1\n%2").arg(name, typeBadge(type, entry.isDirectory)));
        item->setIcon(getAssetIcon(static_cast<int>(entry.type), entry.isDirectory));
        item->setData(QString::fromStdString(entry.relativePath), RelativePathRole);
        item->setData(static_cast<int>(entry.type), AssetTypeRole);
        item->setData(entry.isDirectory, IsDirectoryRole);
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
    if (m_currentPath.isEmpty()) {
        m_breadcrumbLabel->setText(tr("Assets"));
    } else {
        QString display = "Assets / " + m_currentPath;
        display.replace("/", " / ");
        m_breadcrumbLabel->setText(display);
    }

    // Enable/disable up button
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
        case AssetType::Blueprint:
            return s->standardIcon(QStyle::SP_VistaShield);
        case AssetType::Shader:
            return s->standardIcon(QStyle::SP_TitleBarMenuButton);
        case AssetType::Font:
            return s->standardIcon(QStyle::SP_FileDialogInfoView);
        case AssetType::Text:
            return s->standardIcon(QStyle::SP_FileIcon);
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

    if (isDirectory) {
        // Navigate into folder
        NavigateTo(relativePath);
    } else {
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
        // Use QMetaObject to invoke the slot
        QMetaObject::invokeMethod(m_mainWindow, "onOpenProject", Qt::QueuedConnection);
    }
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
