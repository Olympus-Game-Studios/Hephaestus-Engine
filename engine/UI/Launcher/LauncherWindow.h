#pragma once

#include "LauncherProject.h"
#include "LauncherProjectManager.h"
#include "LauncherProjectScanner.h"
#include "ProjectCardModel.h"
#include "ProjectCardDelegate.h"

#include <QWidget>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QListView;
class QModelIndex;
class QPoint;
class QTimer;

namespace Hephaestus::Launcher {

class LauncherWindow : public QWidget {
    Q_OBJECT

public:
    explicit LauncherWindow(QWidget* parent = nullptr);

private:
    QWidget* BuildIntroPanel();
    QWidget* BuildProjectsPanel();
    QWidget* BuildModeCards(QWidget* parent);

    void RefreshProjectsAsync();
    void OnProjectsScanned(const QVector<LauncherProject>& projects);
    void SelectLastOpenedProject();
    void UpdateProjectListState();
    void ShowProjectContextMenu(const QPoint& position);

    void OpenProject(const QModelIndex& index);
    void OpenProjectFromDisk();
    void LaunchEditor(const LauncherProject& project);
    void DeleteProject(const QModelIndex& index);
    void ShowProjectInExplorer(const QModelIndex& index);
    void CreateNewProject();
    void AddProjectRoot(const QString& folder);

    QString EnsureExecutableExistsOrPrompt(const QString& currentPath, const QString& dialogTitle);

private:
    LauncherProjectManager m_projectManager;
    LauncherProjectScanner m_projectScanner;
    ProjectCardModel m_projectModel;
    ProjectCardDelegate m_projectDelegate;

    LauncherSettings m_settings;

    QLineEdit* m_projectSearchEdit = nullptr;
    QListView* m_projectList = nullptr;
    QLabel* m_emptyStateLabel = nullptr;
    QButtonGroup* m_modeButtons = nullptr;
    QTimer* m_projectRescanTimer = nullptr;
};

} // namespace Hephaestus::Launcher
