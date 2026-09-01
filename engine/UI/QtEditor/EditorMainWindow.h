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
#include <QDropEvent>
#include <QListWidget>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QToolButton>
#include <QFrame>
#include <QPlainTextEdit>
#include <QProcess>
#include "BuildSettingsDialog.h"
#include <memory>
#include <vector>
#include <entt/entt.hpp>

#include "../../ToolsCommon/EngineSettingsManager.h"
#include "../../ToolsCommon/ProjectSettingsManager.h"

namespace Hephaestus {

namespace UI {
class SettingsWindow;
}

// Forward declare ProjectContext
class ProjectContext;

namespace QtEditor {

// Forward declarations
class QtViewportWindow;
class QtEditorEngine;
class AssetBrowserWidget;
class AddActorsPanel;
class EditorAssetActions;
class PrefabEditorManager;
class PrefabInstanceBar;
class RuntimeUIOverlayWidget;
class LightInspector;
class EnvironmentInspector;
class ComponentInspector;

/**
 * QTreeWidget subclass that emits a signal after drag-and-drop operations.
 * QTreeWidget's internal move uses remove+insert (not moveRows), so the
 * standard rowsMoved signal is never emitted. This subclass overrides
 * dropEvent to emit itemsDropped() after the drop completes.
 */
class OutlinerTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    using QTreeWidget::QTreeWidget;
signals:
    void itemsDropped();
protected:
    void dropEvent(QDropEvent* event) override {
        QTreeWidget::dropEvent(event);
        if (event->isAccepted()) {
            emit itemsDropped();
        }
    }
};

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
    void OpenProjectFromPath(const QString& filepath);

    /**
     * Create the engine and initialize Vulkan. Call after show() so a hang
     * or GPU failure cannot leave the user with only a console window.
     */
    bool StartEngine();

    /// Spawn a mesh/prefab/actor from Asset Browser (context menu). Uses camera-forward placement.
    void spawnFromAssetBrowser(const QByteArray& spawnPayload);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
     RuntimeUIOverlayWidget* m_runtimeUIOverlay = nullptr;
    void setupMenuBar();
    void setupTopToolbar();
    void setupDockWidgets();
    void setupCentralWidget();
    void setupStatusBar();
    void applyUnifiedStyle();
    bool initializeEngine();
    void setupEditorCallbacks();
    void setupViewportRenderingToggles();
    void registerComponentInspectors();

    // Outliner management
    void rebuildOutliner();
    void populateOutlinerItem(QTreeWidgetItem* parent, entt::entity entity);
    void restoreOutlinerTreeSelection();
    QTreeWidgetItem* findOutlinerItemForEntity(entt::entity entity);
    void onOutlinerContextMenu(const QPoint& pos);
    void createFolderInOutliner(entt::entity parentEntity);
    void deleteEntityFromOutliner(entt::entity entity);
    void reparentEntityToRoot(entt::entity entity);
    void renameEntityInOutliner(entt::entity entity);

    // Details panel management
    void refreshDetailsPanel();
    void clearDetailsPanel();
    void blockDetailSignals(bool block);
    void rebuildComponentInspectors();
    void showAddComponentMenu();
    void onComponentRemoved(entt::entity entity, const QString& name);
    void handleDragDrop(const QString& assetPath, const QString& targetField);
    void handleViewportSpawnDrop(const QByteArray& spawnPayload, const QPoint& dropPos);
    void syncPrefabOverridesForSelection();
    void updatePrefabInspectorUi();
    void decoratePrefabComponentInspector(QWidget* widget, const std::string& componentName);
    void createPrefabFromSelection(entt::entity entity);
    bool runPrefabRevert(entt::entity entity, const std::string& component, const std::string& property);
    bool runPrefabApply(entt::entity entity, const std::string& component, const std::string& property, bool confirmAll);

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
    void onCopyEntity();
    void onPasteEntity();
    void onDuplicateEntity();
    void onOpenSettings();
    void updateUndoRedoActions();
    void beginCoalescedTransformUndo(const char* label);
    void endCoalescedTransformUndo();

    // Help menu actions
    void onAbout();

    // Build
    void onBuildProject();

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
    void onVisibilityToggled(bool visible);

private:
    void updateWindowTitle();
    void syncTransformFieldsFromScene();
    void setupProjectCallbacks();
    void rebuildRecentProjectsMenu();
    void refreshAssetBrowser();
    bool promptSaveDirtyScene();  // Returns true if OK to proceed, false if cancelled
    bool openProjectFile(const QString& filepath);

    // Dock widgets
    QDockWidget* m_outlinerDock = nullptr;
    QDockWidget* m_detailsDock = nullptr;
    QDockWidget* m_assetBrowserDock = nullptr;
    QDockWidget* m_addActorsDock = nullptr;

    // Top toolbar
    QToolBar* m_topToolbar = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_translateAction = nullptr;
    QAction* m_rotateAction = nullptr;
    QAction* m_scaleAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QToolButton* m_worldLocalBtn = nullptr;
    QToolButton* m_snapToggleBtn = nullptr;
    QComboBox* m_snapValueCombo = nullptr;
    QComboBox* m_cameraSpeedCombo = nullptr;

    // Outliner (scene hierarchy)
    QWidget* m_outlinerPanel = nullptr;
    QLineEdit* m_outlinerSearch = nullptr;
    QPushButton* m_addEntityBtn = nullptr;
    QPushButton* m_addFolderBtn = nullptr;
    OutlinerTreeWidget* m_outlinerTree = nullptr;

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
    PrefabInstanceBar* m_prefabBar = nullptr;

    // Component inspectors (owned by layout, recreated on selection change)
    LightInspector* m_lightInspector = nullptr;
    EnvironmentInspector* m_environmentInspector = nullptr;
    std::vector<ComponentInspector*> m_componentInspectors;

    // Header/controls
    QLabel*      m_entityIconLabel  = nullptr;
    QLabel*      m_entityIdLabel    = nullptr;
    QToolButton* m_visibilityBtn    = nullptr;
    QCheckBox*   m_lockCheck        = nullptr;
    QPushButton* m_addComponentBtn  = nullptr;
    QPushButton* m_deleteEntityBtn  = nullptr;
    bool         m_detailsLocked    = false;
    entt::entity m_lockedEntity     = entt::null;

    // Asset browser (new widget-based)
    AssetBrowserWidget* m_assetBrowser = nullptr;
    std::unique_ptr<EditorAssetActions> m_assetActions;
    std::unique_ptr<PrefabEditorManager> m_prefabEditorManager;
    UI::SettingsWindow* m_settingsWindow = nullptr;

    // Build output
    QDockWidget* m_buildOutputDock = nullptr;
    QPlainTextEdit* m_buildOutputText = nullptr;
    QProcess* m_buildProcess = nullptr;
    QToolButton* m_buildBtn = nullptr;
    bool m_buildExportPhase = false; // false = compile, true = export
    BuildSettings m_lastBuildSettings;

    // Add Actors panel
    AddActorsPanel* m_addActorsPanel = nullptr;

    // Viewport (real 3D rendering)
    QtViewportWindow* m_viewportWindow = nullptr;
    QWidget* m_viewportHost = nullptr;
    QWidget* m_viewportContainer = nullptr;
    QWidget* m_viewportToolbar = nullptr;
    QFrame* m_overlayBottomLeft = nullptr;
    QComboBox* m_viewPerspectiveCombo = nullptr;
    QToolButton* m_viewModeBtn = nullptr;
    QToolButton* m_viewGridBtn = nullptr;
    QToolButton* m_viewStatsBtn = nullptr;
    QLabel* m_overlayFpsLabel = nullptr;
    QLabel* m_overlayDrawCallsLabel = nullptr;
    QLabel* m_overlayRamLabel = nullptr;

    // Engine integration
    std::unique_ptr<QtEditorEngine> m_engine;
    
    // Frame timing for status bar
    float m_lastDt = 0.0f;
    int m_frameCount = 0;
    QLabel* m_statusFps = nullptr;
    QLabel* m_statusDrawCalls = nullptr;
    QLabel* m_statusRam = nullptr;
    QLabel* m_statusVersion = nullptr;

    // Prevent re-entrant updates
    bool m_updatingDetails = false;
    bool m_transformUndoActive = false;

    // Edit menu action pointers (for enable/disable)
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;

    // Project menu
    QMenu* m_recentProjectsMenu = nullptr;
    std::unique_ptr<ProjectContext> m_projectContext;

    Tools::EngineSettingsManager m_engineSettingsManager;
    std::unique_ptr<Tools::ProjectSettingsManager> m_projectSettingsManager;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
