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
#include <QTabBar>
#include <QToolBar>
#include <QWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QToolButton>
#include <QButtonGroup>
#include <QFrame>
#include <QSet>
#include <QPlainTextEdit>
#include <QThread>
#include <QTimer>
#include "BuildSettingsDialog.h"
#include "../../PluginAPI/PluginFirstParty.h"
#include <memory>
#include <atomic>
#include <filesystem>
#include <unordered_set>
#include <functional>
#include <vector>
#include <entt/entt.hpp>

#include "../../Assets/RecoveryStore.h"
#include "../../Assets/EditorViewState.h"
#include "../../ToolsCommon/EngineSettingsManager.h"
#include "../../ToolsCommon/ProjectSettingsManager.h"
#include "../../UI/InspectorSelectionModel.h"

class EditorAssetPipeline;

namespace Hephaestus {

namespace Build {
class BuildService;
}

namespace Plugins {
class PluginSystem;
}

namespace UI {
class SettingsWindow;
}

// Forward declare ProjectContext
class ProjectContext;

namespace Automation {
class AutomationService;
class AutomationBackend;
}

namespace QtEditor {

// Forward declarations
class EditorSystemUsage;
class QtViewportWindow;
class QtEditorEngine;
class AssetBrowserWidget;
class AddActorsPanel;
class CodeEditorPanel;
class DebuggerPanel;
class ProjectSearchPanel;
class EditorAssetActions;
class PrefabEditorManager;
class CharacterEditorManager;
class PrefabInstanceBar;
class RuntimeUIOverlayWidget;
class LightInspector;
class EnvironmentInspector;
class ComponentInspector;
class EditorNotificationService;
class EditorPluginHost;
class PluginManagerDialog;

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
    /** Editor 2.0 workspace modes; each keeps its own saved dock layout. */
    enum class EditorMode { Default, Artist, Programmer };

    explicit EditorMainWindow(
        QWidget* parent = nullptr,
        Hephaestus::Plugins::FirstPartyStartupOptions startupOptions = {});
    ~EditorMainWindow() override;
    void OpenProjectFromPath(const QString& filepath);

    /**
     * Create the engine and initialize Vulkan. Call after show() so a hang
     * or GPU failure cannot leave the user with only a console window.
     */
    bool StartEngine();

    /// Spawn a mesh/prefab/actor from Asset Browser (context menu). Uses camera-forward placement.
    void spawnFromAssetBrowser(const QByteArray& spawnPayload);
    void replaceFromAssetBrowser(const QByteArray& spawnPayload) {
        replaceSelectionWithAsset(spawnPayload);
    }

    EditorAssetPipeline* GetAssetPipeline() { return m_assetPipeline.get(); }
    void rebuildSceneVisuals();
    bool requestCloseOpenCharacter(const QString& absolutePath);
    EditorPluginHost* PluginHost() const { return m_pluginHost.get(); }

    /// Stop the automation service tick pump and drain work before engine teardown.
    void ShutdownAutomation();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
     RuntimeUIOverlayWidget* m_runtimeUIOverlay = nullptr;

    /// Editor-host adapter bridging the automation core to editor subsystems.
    /// Nested so it can reach EditorMainWindow private members; implemented in the .cpp.
    class EditorAutomationBackend;
    /// Internal first-party context handed to first-party plugins (e.g. MCP).
    class FirstPartyContext;

    /// Create the engine-owned automation backend/service and main-thread pump.
    void SetupAutomation();
    void setupMenuBar();
    void setupTopToolbar();
    void setupDockWidgets();
    void setupCentralWidget();
    void setupStatusBar();
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
    void createEmptyEntity();
    void updateSystemUsage();
    void setEditorMode(EditorMode mode);
    /** Switches to Programmer mode if needed and brings @p dock to the front. */
    void showProgrammerPanel(QDockWidget* dock);
    void setupLuaDebugger();
    void runConsoleLua(const QString& code);
    void syncGizmoSpaceButtons();
    void restoreModeLayout(EditorMode mode);
    void arrangeModeLayout(EditorMode mode);
    QWidget* wrapComponentCard(QGroupBox* body, const QString& title, const QString& type,
                               std::function<void()> onReset);
    static QWidget* componentCardOf(QWidget* body);
    void createFolderInOutliner(entt::entity parentEntity);
    void deleteEntityFromOutliner(entt::entity entity);
    void deleteSelectedEntities();
    void reparentEntityToRoot(entt::entity entity);
    void renameEntityInOutliner(entt::entity entity);

    // Details panel management
    void refreshDetailsPanel();
    void clearDetailsPanel();
    void blockDetailSignals(bool block);
    void rebuildComponentInspectors();
    void showAddComponentMenu();
    void applyBatchAddComponent(const QString& typeId);
    void onComponentRemoved(entt::entity entity, const QString& name);
    InspectorSelectionModel resolveInspectorSelection();
    void copyInspectorComponent(const std::string& type);
    void pasteInspectorComponentValues(const std::string& type);
    void resetInspectorComponent(const std::string& type);
    void removeInspectorComponent(const std::string& type);
    void bakeAllProbeVolumes();
    void bakeSelectedProbeVolumes();
    void bakeProbeVolumes(const std::vector<entt::entity>& volumes);
    ComponentInspector* focusedComponentInspector() const;
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
    void onOpenPlugins();
    void updateUndoRedoActions();
    void beginCoalescedTransformUndo(const char* label);
    void endCoalescedTransformUndo();

    // Help menu actions
    void onAbout();

    // Build
    void onBuildProject();
    void onValidateProject();
    void onPackageAndRun();
    void onRunLastPackage();
    void onCleanPackage();
    void startBuild(Hephaestus::Build::BuildAction action);

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
    void pollAssetReimports();

    void setupRecoveryTimers();
    void startRecoverySessionTimers();
    void stopRecoverySession();
    void promptPendingRecovery();
    void onRecoveryHeartbeat();
    void onAutosaveTick();
    void writeRecoverySnapshot();
    int autosaveIntervalSeconds() const;
    void applyPlayPhysicsSetting();
    void loadEditorViewState();
    void saveEditorViewState();
    void hideSelectedInEditor();
    void isolateSelectedInEditor();
    void toggleLockSelectedInEditor();
    void alignSelection(int axis);
    void distributeSelection(int axis);
    void dropSelectionToGround();
    void createEmptyParentForSelection();
    void showViewportContextMenu(float x, float y);
    void replaceSelectionWithAsset(const QByteArray& spawnPayload);
    void saveCameraBookmark(int slot);
    void recallCameraBookmark(int slot);

    // Dock widgets
    QDockWidget* m_outlinerDock = nullptr;
    QDockWidget* m_detailsDock = nullptr;
    QDockWidget* m_assetBrowserDock = nullptr;
    QDockWidget* m_consoleDock = nullptr;
    QDockWidget* m_addActorsDock = nullptr;
    QDockWidget* m_codeEditorDock = nullptr;
    QDockWidget* m_searchDock = nullptr;
    QDockWidget* m_debuggerDock = nullptr;
    ProjectSearchPanel* m_searchPanel = nullptr;
    DebuggerPanel* m_debuggerPanel = nullptr;
    QList<QShortcut*> m_debuggerShortcuts;
    CodeEditorPanel* m_codeEditor = nullptr;

    // Top toolbar
    QMenuBar* m_menuBar = nullptr;
    QToolButton* m_projectButton = nullptr;
    QToolBar* m_topToolbar = nullptr;
    QToolBar* m_artistToolRail = nullptr;
    QButtonGroup* m_modeButtons = nullptr;
    EditorMode m_mode = EditorMode::Default;
    QAction* m_playAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_stepAction = nullptr;
    QAction* m_selectAction = nullptr;
    QAction* m_translateAction = nullptr;
    QAction* m_rotateAction = nullptr;
    QAction* m_scaleAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QToolButton* m_worldLocalBtn = nullptr;
    QToolButton* m_worldBtn = nullptr;
    QToolButton* m_notificationButton = nullptr;
    QTabBar* m_sceneTabs = nullptr;
    QByteArray m_stateBeforeMaximize;
    QToolButton* m_snapToggleBtn = nullptr;
    QComboBox* m_snapValueCombo = nullptr;
    QComboBox* m_cameraSpeedCombo = nullptr;

    // Outliner (scene hierarchy)
    QWidget* m_outlinerPanel = nullptr;
    QLineEdit* m_outlinerSearch = nullptr;
    QLabel* m_outlinerFooter = nullptr;
    QSet<QString> m_collapsedComponents;
    OutlinerTreeWidget* m_outlinerTree = nullptr;
    std::unordered_set<uint32_t> m_expandedOutlinerEntities;
    bool m_hasOutlinerExpansionState = false;

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
    QLabel*      m_detailsSharedHint = nullptr;
    QLabel*      m_detailsAdvancedId = nullptr;
    QLabel*      m_detailsEmptyHint  = nullptr;
    QLabel*      m_detailsPartialHint = nullptr;
    QToolButton* m_visibilityBtn    = nullptr;
    QToolButton* m_lockCheck        = nullptr;
    QPushButton* m_addComponentBtn  = nullptr;
    QToolButton* m_inspectorWorldBtn = nullptr;
    QToolButton* m_inspectorLocalBtn = nullptr;
    bool         m_detailsLocked    = false;
    entt::entity m_lockedEntity     = entt::null;

    // Asset browser (new widget-based)
    AssetBrowserWidget* m_assetBrowser = nullptr;
    std::unique_ptr<EditorAssetActions> m_assetActions;
    std::unique_ptr<PrefabEditorManager> m_prefabEditorManager;
    std::unique_ptr<CharacterEditorManager> m_characterEditorManager;
    std::unique_ptr<EditorPluginHost> m_pluginHost;
    std::unique_ptr<Hephaestus::Plugins::PluginSystem> m_pluginSystem;
    UI::SettingsWindow* m_settingsWindow = nullptr;

    // Build output
    QDockWidget* m_buildOutputDock = nullptr;
    QPlainTextEdit* m_buildOutputText = nullptr;
    QTreeWidget* m_buildProblems = nullptr;
    QThread* m_buildThread = nullptr;
    QToolButton* m_buildBtn = nullptr;
    std::shared_ptr<std::atomic_bool> m_buildCancel;

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
    QAction* m_viewModeLitAction = nullptr;
    QAction* m_viewModeUnlitAction = nullptr;
    QAction* m_viewModeWireframeAction = nullptr;
    QToolButton* m_viewGridBtn = nullptr;
    QToolButton* m_viewStatsBtn = nullptr;
    QLabel* m_overlayFpsLabel = nullptr;
    QLabel* m_overlayDrawCallsLabel = nullptr;
    QLabel* m_overlayRamLabel = nullptr;
    QLabel* m_overlaySceneLabel = nullptr;

    // Last free perspective camera (restored when leaving ortho standard views)
    bool m_hasSavedPerspectiveCamera = false;
    float m_savedPerspPosX = 0.0f;
    float m_savedPerspPosY = 2.0f;
    float m_savedPerspPosZ = 5.0f;
    float m_savedPerspYaw = -90.0f;
    float m_savedPerspPitch = -15.0f;
    float m_savedPerspOrthoHalfExtent = 10.0f;

    // Engine integration
    std::unique_ptr<QtEditorEngine> m_engine;
    
    // Automation (engine-owned; MCP is an optional consumer)
    std::unique_ptr<EditorAutomationBackend> m_automationBackend;
    std::unique_ptr<Hephaestus::Automation::AutomationService> m_automationService;
    std::unique_ptr<FirstPartyContext> m_firstPartyContext;
    Hephaestus::Plugins::FirstPartyStartupOptions m_startupOptions;
    QTimer* m_automationTimer = nullptr;

    // Frame timing for status bar
    float m_lastDt = 0.0f;
    int m_frameCount = 0;
    QLabel* m_statusFps = nullptr;
    QToolButton* m_statusSaved = nullptr;
    QLabel* m_statusRam = nullptr;
    QLabel* m_statusCpu = nullptr;
    QLabel* m_statusGpu = nullptr;
    std::unique_ptr<EditorSystemUsage> m_systemUsage;
    QToolButton* m_statusScene = nullptr;
    QLabel* m_statusSelection = nullptr;

    // Prevent re-entrant updates
    bool m_updatingDetails = false;
    bool m_transformUndoActive = false;

    // Edit menu action pointers (for enable/disable)
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;

    // Project menu
    QMenu* m_recentProjectsMenu = nullptr;
    std::unique_ptr<ProjectContext> m_projectContext;
    std::unique_ptr<EditorAssetPipeline> m_assetPipeline;
    QTimer* m_assetReimportTimer = nullptr;

    Tools::EngineSettingsManager m_engineSettingsManager;
    std::unique_ptr<Tools::ProjectSettingsManager> m_projectSettingsManager;

    // Crash recovery / autosave
    RecoveryStore m_recoveryStore;
    EditorNotificationService* m_notifications = nullptr;
    QTimer* m_recoveryHeartbeatTimer = nullptr;
    QTimer* m_autosaveTimer = nullptr;
    bool m_recoveryBusy = false;
    EditorViewState m_editorViewState;
    std::filesystem::path m_editorViewStatePath;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
