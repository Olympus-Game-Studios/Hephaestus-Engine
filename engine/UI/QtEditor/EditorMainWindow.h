#pragma once

/**
 * @file EditorMainWindow.h
 * @brief Main window for the Qt-based Hephaestus Editor.
 *
 * This provides a QMainWindow with dockable panels for outliner, details,
 * asset browser, and a real 3D viewport powered by the engine's Vulkan renderer.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QMainWindow>
#include <QDockWidget>
#include <QTreeWidget>
#include <QListWidget>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <memory>
#include <vector>
#include <entt/entt.hpp>

namespace Hephaestus {

// Forward declare ProjectContext
class ProjectContext;

namespace QtEditor {

// Forward declarations
class QtViewportWindow;
class QtEditorEngine;
class AssetBrowserWidget;
class EditorAssetActions;
class LightInspector;
class EnvironmentInspector;
class ComponentInspector;

/**
 * @brief Main editor window with dockable panels.
 *
 * Creates a standard editor layout with:
 * - Menu bar (File, Edit, View, Help)
 * - Status bar showing engine state
 * - Left dock: Outliner (scene hierarchy)
 * - Right dock: Details (entity inspector)
 * - Bottom dock: Asset Browser
 * - Central widget: Real 3D viewport rendered by the engine
 */
class EditorMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit EditorMainWindow(QWidget* parent = nullptr);
    ~EditorMainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupMenuBar();
    void setupDockWidgets();
    void setupCentralWidget();
    void setupStatusBar();
    void initializeEngine();
    void setupEditorCallbacks();
    void setupViewportRenderingToggles();
    void registerComponentInspectors();

    // Outliner management
    void rebuildOutliner();
    void populateOutlinerItem(QTreeWidgetItem* parent, entt::entity entity);

    // Details panel management
    void refreshDetailsPanel();
    void clearDetailsPanel();
    void blockDetailSignals(bool block);
    void rebuildComponentInspectors();
    void showAddComponentMenu();
    void onComponentRemoved(entt::entity entity, const QString& name);
    void handleDragDrop(const QString& assetPath, const QString& targetField);

private slots:
    // File menu actions - Level
    void onNewLevel();
    void onOpenLevel();
    void onSaveLevel();
    void onSaveLevelAs();
    void onExit();

    // File menu actions - Project
    void onNewProject();
    void onOpenProject();
    void onCloseProject();
    void onOpenRecentProject();
    void onClearRecentProjects();

    // Edit menu actions
    void onUndo();
    void onRedo();

    // Help menu actions
    void onAbout();

    // Engine callbacks
    void onViewportReady();
    void onEngineInitialized(bool success);
    void onFrameRendered(float dt);
    void onEngineError(const QString& message);

    // Outliner selection
    void onOutlinerSelectionChanged();

    // Details panel edits
    void onNameChanged(const QString& text);
    void onPositionChanged();
    void onRotationChanged();
    void onScaleChanged();

private:
    void updateWindowTitle();
    void setupProjectCallbacks();
    void rebuildRecentProjectsMenu();
    void refreshAssetBrowser();
    bool promptSaveDirtyScene();  // Returns true if OK to proceed, false if cancelled

    // Dock widgets
    QDockWidget* m_outlinerDock = nullptr;
    QDockWidget* m_detailsDock = nullptr;
    QDockWidget* m_assetBrowserDock = nullptr;

    // Outliner (scene hierarchy)
    QTreeWidget* m_outlinerTree = nullptr;

    // Details panel (inspector)
    QWidget* m_detailsWidget = nullptr;
    QScrollArea* m_detailsScrollArea = nullptr;
    QVBoxLayout* m_detailsLayout = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QDoubleSpinBox* m_posX = nullptr;
    QDoubleSpinBox* m_posY = nullptr;
    QDoubleSpinBox* m_posZ = nullptr;
    QDoubleSpinBox* m_rotX = nullptr;
    QDoubleSpinBox* m_rotY = nullptr;
    QDoubleSpinBox* m_rotZ = nullptr;
    QDoubleSpinBox* m_scaleX = nullptr;
    QDoubleSpinBox* m_scaleY = nullptr;
    QDoubleSpinBox* m_scaleZ = nullptr;
    QGroupBox* m_transformGroup = nullptr;

    // Component inspectors (owned by layout, recreated on selection change)
    LightInspector* m_lightInspector = nullptr;
    EnvironmentInspector* m_environmentInspector = nullptr;
    std::vector<ComponentInspector*> m_componentInspectors;

    // Header/controls
    QLabel*      m_entityIdLabel    = nullptr;
    QCheckBox*   m_lockCheck        = nullptr;
    QPushButton* m_addComponentBtn  = nullptr;
    QPushButton* m_deleteEntityBtn  = nullptr;
    bool         m_detailsLocked    = false;
    entt::entity m_lockedEntity     = entt::null;

    // Asset browser (new widget-based)
    AssetBrowserWidget* m_assetBrowser = nullptr;
    std::unique_ptr<EditorAssetActions> m_assetActions;

    // Viewport (real 3D rendering)
    QtViewportWindow* m_viewportWindow = nullptr;
    QWidget* m_viewportContainer = nullptr;

    // Engine integration
    std::unique_ptr<QtEditorEngine> m_engine;
    
    // Frame timing for status bar
    float m_lastDt = 0.0f;
    int m_frameCount = 0;

    // Prevent re-entrant updates
    bool m_updatingDetails = false;

    // Project menu
    QMenu* m_recentProjectsMenu = nullptr;
    std::unique_ptr<ProjectContext> m_projectContext;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
