#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QMainWindow>
#include <QDockWidget>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QLabel>
#include <QTimer>
#include <QCloseEvent>
#include <QComboBox>

#include <memory>
#include <string>
#include <unordered_set>
#include <filesystem>
#include <functional>
#include <string>

#include "../../Scene/Scene.h"
#include "../../UI/EditorDocument.h"
#include "../../Core/EditorContext.h"

namespace Hephaestus {
namespace QtEditor {

class EditorMainWindow;
class QtViewportWindow;
class QtEditorEngine;
class PrefabHierarchyPanel;
class PrefabInspectorPanel;

class PrefabEditorWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PrefabEditorWindow(const QString& prefabPath,
                                 EditorMainWindow* parentEditor,
                                 QWidget* parent = nullptr);
    ~PrefabEditorWindow() override;

    const std::filesystem::path& GetPrefabPath() const;
    bool IsDirty() const;

    using PrefabSavedCallback = std::function<void(const std::filesystem::path&)>;
    void SetPrefabSavedCallback(PrefabSavedCallback callback);

signals:
    void windowClosed(const QString& prefabPath);
    void prefabSaved(const QString& prefabPath);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // Toolbar actions
    void onSave();
    void onReload();
    void onUndo();
    void onRedo();
    void onFrameSelected();
    void onFrameAll();
    void onAddChildNode();
    void onAddComponent();

    // Gizmo tool selection
    void onSelectTranslate();
    void onSelectRotate();
    void onSelectScale();
    void onToggleLocalWorld();
    void onToggleSnap();

    // Hierarchy callbacks
    void onHierarchySelectionChanged(const std::string& nodeId);
    void onHierarchyAddChild(const std::string& parentNodeId);
    void onHierarchyAddComponent(const std::string& nodeId);
    void onHierarchyDeleteNode(const std::string& nodeId);
    void onHierarchyDuplicateNode(const std::string& nodeId);
    void onHierarchyRenameNode(const std::string& nodeId);
    void onHierarchyResetTransform(const std::string& nodeId);
    void onHierarchyFocusInViewport(const std::string& nodeId);
    void onHierarchyReparent(const std::string& nodeId, const std::string& newParentId);
    void onHierarchyVisibilityToggled(const std::string& nodeId, bool isolate);
    void onHierarchyShowAll();

    // Inspector callbacks
    void onInspectorNameChanged(const std::string& nodeId, const std::string& newName);
    void onInspectorTransformChanged(const std::string& nodeId, const TransformComponent& transform);
    void onInspectorAddComponent(const std::string& nodeId, const std::string& componentType);
    void onInspectorRemoveComponent(const std::string& nodeId, const std::string& componentType);

    // Viewport engine callbacks
    void onViewportReady();
    void onFrameRendered(float dt);

private:
    // Setup
    void setupUI();
    void setupToolbar();
    void setupDockWidgets();
    void setupViewport();
    void setupConnections();
    void initializeEngine();

    // State management
    void updateWindowTitle();
    bool openDocument(const QString& path);
    bool promptSaveIfDirty();

    // Selection and sync
    void selectNode(const std::string& nodeId);
    void syncSelectionToViewport();
    void syncViewportSelectionToHierarchy();
    void refreshInspectorForSelection();
    void syncTransformFromViewportToDocument();
    void syncAllPreviewNodes();

    // Hierarchy helpers
    void rebuildHierarchy();
    void applyEditorVisibility();
    void presentAddComponentPopup(const std::string& nodeId, const QPoint& globalPos);

    // Lifecycle
    void shutdownEngine();

    // Viewport helpers
    void focusViewportOnNode(const std::string& nodeId);

    // Entity lookup
    Entity findEntityByNodeId(const std::string& nodeId);
    std::string findNodeIdByEntity(entt::entity entity);

    // Parent editor
    EditorMainWindow* m_parentEditor = nullptr;
    QString m_prefabPathStr;

    // Document and scene
    std::unique_ptr<Scene> m_previewScene;
    // Editor-only: which nodes the preview hides, by node id so it survives preview rebuilds; never saved.
    std::unordered_set<std::string> m_hiddenNodes;
    std::string m_isolatedNode;
    std::unique_ptr<PrefabDocument> m_document;
    PrefabSavedCallback m_savedCallback;

    // Viewport and rendering engine (dedicated to this prefab editor)
    std::unique_ptr<QtEditorEngine> m_engine;
    QtViewportWindow* m_viewportWindow = nullptr;
    QWidget* m_viewportHost = nullptr;
    QWidget* m_viewportContainer = nullptr;

    // Dock panels
    QDockWidget* m_hierarchyDock = nullptr;
    QDockWidget* m_inspectorDock = nullptr;
    PrefabHierarchyPanel* m_hierarchyPanel = nullptr;
    PrefabInspectorPanel* m_inspectorPanel = nullptr;

    // Toolbar
    QToolBar* m_toolbar = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_reloadAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_translateAction = nullptr;
    QAction* m_rotateAction = nullptr;
    QAction* m_scaleAction = nullptr;
    QAction* m_localWorldAction = nullptr;
    QAction* m_snapAction = nullptr;
    QAction* m_frameSelectedAction = nullptr;
    QAction* m_frameAllAction = nullptr;
    QAction* m_addChildAction = nullptr;
    QAction* m_addComponentAction = nullptr;
    QActionGroup* m_gizmoModeGroup = nullptr;

    // Status bar
    QLabel* m_statusFps = nullptr;
    QLabel* m_statusPath = nullptr;

    // Selection state
    std::string m_selectedNodeId;
    bool m_refreshingInspector = false;

    // Gizmo state
    GizmoMode m_gizmoMode = GizmoMode::Translate;
    GizmoSpace m_gizmoSpace = GizmoSpace::World;
    bool m_snapEnabled = false;

    // Sync flags
    bool m_suppressSelectionSync = false;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
