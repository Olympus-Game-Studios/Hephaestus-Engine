/**
 * @file AssetBrowserWidget.h
 * @brief Qt-based asset browser widget for the Hephaestus Editor.
 *
 * Provides a two-panel browser with:
 * - Left: Folder tree view
 * - Right: Contents list/grid view
 * - Top: Navigation bar (breadcrumb, up, refresh)
 * - Bottom: Status bar
 */

#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QWidget>
#include <QTreeView>
#include <QListView>
#include <QStandardItemModel>
#include <QSplitter>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QToolBar>
#include <QComboBox>
#include <QToolButton>
#include <QHBoxLayout>
#include <QString>
#include <functional>

class QFileSystemWatcher;
struct HPScriptLanguage;
class QTimer;

namespace Hephaestus {

// Forward declaration
class ProjectContext;

namespace QtEditor {

class EditorMainWindow;
class AssetThumbnailCache;

/**
 * @brief Asset browser widget with folder tree and contents view.
 *
 * Features:
 * - Folder tree on the left (from AssetManager)
 * - Contents grid/list on the right
 * - Navigation: up, refresh, breadcrumb
 * - Double-click to open assets (via handlers)
 * - Empty state when no project is loaded
 */
class AssetBrowserWidget : public QWidget
{
    Q_OBJECT

public:
    using OpenAssetCallback = std::function<void(const QString& relativePath)>;

    explicit AssetBrowserWidget(EditorMainWindow* mainWindow, QWidget* parent = nullptr);
    ~AssetBrowserWidget() override;

    /**
     * @brief Set the project context (for checking if project is open).
     */
    void SetProjectContext(ProjectContext* context);

    /**
     * @brief Refresh the browser from AssetManager.
     * Preserves current directory if still valid.
     */
    void Refresh();

    /**
     * @brief Navigate to a specific directory.
     * @param relativePath Relative path from asset root (empty = root)
     */
    void NavigateTo(const QString& relativePath);
    void FindReferences(const QString& relativePath);
    void RevealAsset(const QString& relativePath);

    /** Shows the contents as asset cards (true) or a compact list (false). */
    void SetGridView(bool grid);

    /** Large cards with the folder tree hidden (Artist mode), or the normal layout. */
    void SetLargeCards(bool large);

    /**
     * @brief Get current directory path.
     */
    QString GetCurrentPath() const { return m_currentPath; }

    /**
     * @brief Set callback for when an asset should be opened.
     */
    void SetOpenAssetCallback(OpenAssetCallback callback);

    /// Model roles used by the contents / folder views (also used by drag helpers).
    enum ItemDataRole {
        RelativePathRole = Qt::UserRole + 1,
        AssetTypeRole,
        IsDirectoryRole,
        SearchResultRole,
        DetailRole,
        TypeIconRole,
        ThumbnailRole
    };

signals:
    /**
     * @brief Emitted when the user wants to open an asset.
     * @param relativePath Path relative to asset root
     */
    void assetOpenRequested(const QString& relativePath);
    void assetOpenInIDERequested(const QString& relativePath);

    /**
     * @brief Emitted when the user wants to spawn a mesh into the scene.
     * @param relativePath Mesh asset path relative to asset root
     */
    void meshSpawnRequested(const QString& relativePath);

    /**
     * @brief Emitted when selection changes.
     */
    void selectionChanged(const QString& relativePath);

private slots:
    void onFolderTreeClicked(const QModelIndex& index);
    void onContentsDoubleClicked(const QModelIndex& index);
    void onContentsClicked(const QModelIndex& index);
    void onUpButtonClicked();
    void onRefreshButtonClicked();
    void onOpenProjectButtonClicked();
    void onContentsContextMenu(const QPoint& pos);
    void onFolderTreeContextMenu(const QPoint& pos);

private:
    void setupUI();
    void updateEmptyState();
    void rebuildFolderTree();
    void rebuildContentsView();
    void updateBreadcrumb();
    void updateStatusBar();
    void applyViewAndFilter();
    void setThumbnail(QStandardItem* item);
    void onThumbnailReady(const QString& relativePath);

    // Get icon for asset type  
    QIcon getAssetIcon(int assetType, bool isDirectory) const;

    // Context menu helpers
    void showCreateMenu(QMenu& menu, const QString& targetDirectory);
    void createNewFolder(const QString& parentRelativePath);
    void createNewLuaScript(const QString& parentRelativePath);
    void createNewPluginScript(const QString& parentRelativePath, const HPScriptLanguage& language);
    void createNewScene(const QString& parentRelativePath);
    void createNewMaterial(const QString& parentRelativePath);
    void createNewUIFile(const QString& parentRelativePath);
    void createNewPrefab(const QString& parentRelativePath);
    void createNewCharacter(const QString& parentRelativePath);
    void importAssets();
    void importCharacter();
    void duplicateAsset(const QString& relativePath, bool isDirectory);
    void copyAssetPath(const QString& relativePath);
    void deleteAsset(const QString& relativePath, bool isDirectory);
    void restoreFromTrash();
    void watchCurrentDirectory();
    void renameAsset(const QString& relativePath, bool isDirectory);
    void findAssetReferences(const QString& relativePath);
    void showInExplorer(const QString& relativePath);
    void showInFolder(const QString& relativePath);
    void syncFolderTreeSelection();
    QModelIndex findFolderIndexByPath(const QString& relativePath) const;

    // Main window reference for opening assets
    EditorMainWindow* m_mainWindow = nullptr;
    ProjectContext* m_projectContext = nullptr;

    // Current directory (relative to asset root)
    QString m_currentPath;

    QFileSystemWatcher* m_directoryWatcher = nullptr;
    QTimer* m_directoryRefreshTimer = nullptr;

    // UI Elements
    QSplitter* m_splitter = nullptr;

    // Left panel - folder tree
    QTreeView* m_folderTree = nullptr;
    QStandardItemModel* m_folderModel = nullptr;

    // Right panel - contents
    QWidget* m_contentsPanel = nullptr;
    QListView* m_contentsView = nullptr;
    QStandardItemModel* m_contentsModel = nullptr;
    AssetThumbnailCache* m_thumbnails = nullptr;

    // Top bar
    QToolBar* m_toolbar = nullptr;
    QToolButton* m_gridButton = nullptr;
    QToolButton* m_listButton = nullptr;
    QAbstractItemDelegate* m_cardDelegate = nullptr;
    QAbstractItemDelegate* m_listDelegate = nullptr;
    QPushButton* m_importButton = nullptr;
    QComboBox* m_filterCombo = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QLabel* m_breadcrumbLabel = nullptr;
    QWidget* m_breadcrumbBar = nullptr;
    QHBoxLayout* m_breadcrumbLayout = nullptr;
    QLineEdit* m_searchBox = nullptr;

    bool m_gridView = true;
    bool m_largeCards = false;

    // Bottom status
    QLabel* m_statusLabel = nullptr;

    // Empty state widget
    QWidget* m_emptyStateWidget = nullptr;
    QPushButton* m_openProjectButton = nullptr;

    // Callback
    OpenAssetCallback m_openAssetCallback;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
