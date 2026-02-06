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
#include <QString>
#include <functional>

namespace Hephaestus {

// Forward declaration
class ProjectContext;

namespace QtEditor {

class EditorMainWindow;

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

    /**
     * @brief Get current directory path.
     */
    QString GetCurrentPath() const { return m_currentPath; }

    /**
     * @brief Set callback for when an asset should be opened.
     */
    void SetOpenAssetCallback(OpenAssetCallback callback);

signals:
    /**
     * @brief Emitted when the user wants to open an asset.
     * @param relativePath Path relative to asset root
     */
    void assetOpenRequested(const QString& relativePath);

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

private:
    void setupUI();
    void updateEmptyState();
    void rebuildFolderTree();
    void rebuildContentsView();
    void updateBreadcrumb();
    void updateStatusBar();

    // Get icon for asset type  
    QIcon getAssetIcon(int assetType, bool isDirectory) const;

    // Data roles for items
    enum ItemDataRole {
        RelativePathRole = Qt::UserRole + 1,
        AssetTypeRole,
        IsDirectoryRole
    };

    // Main window reference for opening assets
    EditorMainWindow* m_mainWindow = nullptr;
    ProjectContext* m_projectContext = nullptr;

    // Current directory (relative to asset root)
    QString m_currentPath;

    // UI Elements
    QSplitter* m_splitter = nullptr;

    // Left panel - folder tree
    QTreeView* m_folderTree = nullptr;
    QStandardItemModel* m_folderModel = nullptr;

    // Right panel - contents
    QWidget* m_contentsPanel = nullptr;
    QListView* m_contentsView = nullptr;
    QStandardItemModel* m_contentsModel = nullptr;

    // Top bar
    QToolBar* m_toolbar = nullptr;
    QPushButton* m_upButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QLabel* m_breadcrumbLabel = nullptr;
    QLineEdit* m_searchBox = nullptr;

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
