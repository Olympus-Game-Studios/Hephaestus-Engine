#pragma once

#include "LauncherProject.h"
#include "LauncherProjectManager.h"
#include "LauncherProjectScanner.h"
#include "ProjectCardModel.h"
#include "ProjectCardDelegate.h"
#include "../../ToolsCommon/SharedAssetPaths.h"
#include "../../ToolsCommon/EngineSettingsManager.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QListView;
class QListWidget;
class QLineEdit;
class QIcon;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QModelIndex;
class QTimer;

namespace Hephaestus::UI {
class SettingsWindow;
}

namespace Hephaestus::Launcher {

class LauncherWindow : public QWidget {
    Q_OBJECT

public:
    explicit LauncherWindow(QWidget* parent = nullptr);

private:
    void BuildUiRoot();
    void BuildSidebar();
    void BuildPages();
    QWidget* BuildRecentProjectsPage();
    QWidget* BuildNewProjectPage();
    QWidget* BuildTemplatesPage();
    QWidget* BuildSettingsPage();

    void BindSignals();

    void RefreshProjectsAsync();
    void OnProjectsScanned(const QVector<LauncherProject>& projects);
    void OnProjectSelectionChanged();

    void UpdateRecentEmptyState();
    void UpdateProjectDetailsPanel();
    void UpdateNewProjectPathPreview();

    void LoadSettings();
    void SaveSettings() const;
    void ApplyTheme();
    void ApplyFallbackTheme();
    void LoadSharedFonts();

    void NavigateToSidebarItem(int row);
    QModelIndex CurrentProjectIndex() const;
    LauncherProject CurrentProject() const;

    void OpenProject(const QModelIndex& index);
    void SetProjectFavorite(const QModelIndex& index, bool favorite);
    void DeleteSelectedProject();
    void ShowProjectInExplorer();
    void CreateNewProject();
    void OpenSettingsWindow();

    QString EnsureExecutableExistsOrPrompt(const QString& currentPath, const QString& dialogTitle);
    QString ResolveAsset(const QString& relativePath) const;
    QString ResolvePlaceholderThumbnail() const;

    QIcon LoadIcon(const QString& relativePath) const;

private slots:
    void OnBrowseNewProjectLocation();
    void OnBrowseEditorPath();
    void OnBrowseRuntimePath();
    void OnAddProjectRoot();
    void OnRemoveProjectRoot();

private:
    Hephaestus::Tools::SharedAssetPaths m_assets;
    LauncherProjectManager m_projectManager;
    LauncherProjectScanner m_projectScanner;
    ProjectCardModel m_projectModel;
    ProjectCardDelegate m_projectDelegate;

    LauncherSettings m_settings;

    QListWidget* m_sidebar = nullptr;
    QStackedWidget* m_mainContent = nullptr;

    // Recent projects page
    QWidget* m_recentPage = nullptr;
    QLineEdit* m_projectSearchEdit = nullptr;
    QComboBox* m_sortCombo = nullptr;
    QListView* m_projectGrid = nullptr;
    QWidget* m_emptyStateWidget = nullptr;
    QWidget* m_gridHostWidget = nullptr;
    QLabel* m_scanStatusLabel = nullptr;

    QLabel* m_selectedNameLabel = nullptr;
    QLabel* m_selectedDescriptionLabel = nullptr;
    QLabel* m_selectedCreatedLabel = nullptr;
    QLabel* m_selectedModifiedLabel = nullptr;
    QLabel* m_selectedPathLabel = nullptr;

    QPushButton* m_openButton = nullptr;
    QPushButton* m_openBottomButton = nullptr;
    QPushButton* m_openSettingsButton = nullptr;
    QPushButton* m_showExplorerButton = nullptr;
    QPushButton* m_duplicateButton = nullptr;
    QPushButton* m_deleteButton = nullptr;

    // New project page
    QWidget* m_newProjectPage = nullptr;
    QListWidget* m_projectTypeList = nullptr;
    QLineEdit* m_newProjectNameEdit = nullptr;
    QLineEdit* m_newProjectLocationEdit = nullptr;
    QComboBox* m_engineVersionCombo = nullptr;
    QCheckBox* m_createDefaultSceneCheck = nullptr;
    QLabel* m_projectPathPreviewLabel = nullptr;

    // Settings page
    QWidget* m_settingsPage = nullptr;
    QLineEdit* m_editorPathEdit = nullptr;
    QLineEdit* m_runtimePathEdit = nullptr;
    QListWidget* m_projectRootsList = nullptr;
    QComboBox* m_themeCombo = nullptr;
    QCheckBox* m_openLastProjectCheck = nullptr;

    Hephaestus::Tools::EngineSettingsManager m_engineSettingsManager;
    Hephaestus::UI::SettingsWindow* m_settingsWindow = nullptr;

    bool m_isInitializingUi = false;
    QTimer* m_projectRescanTimer = nullptr;

    enum SidebarPage {
        RecentProjectsPage = 0,
        NewProjectPage = 1,
        TemplatesPage = 2,
        SettingsPage = 3
    };
};

} // namespace Hephaestus::Launcher
