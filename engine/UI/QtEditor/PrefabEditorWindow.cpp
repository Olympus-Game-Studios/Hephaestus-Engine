#if HEPHAESTUS_EDITOR_QT_UI

#include "PrefabEditorWindow.h"
#include "PrefabHierarchyPanel.h"
#include "PrefabInspectorPanel.h"
#include "EditorMainWindow.h"
#include "EditorUndoCommand.h"
#include "QtViewportWindow.h"
#include "QtEditorEngine.h"

#include "../../Scene/Components.h"
#include "../../Scene/PrefabComponentIO.h"

#include <QMessageBox>
#include <QFileInfo>
#include <QApplication>
#include <QStyle>
#include <QStatusBar>
#include <QInputDialog>
#include <QVBoxLayout>
#include <QTimer>
#include <algorithm>
#include <iostream>

namespace Hephaestus {
namespace QtEditor {

// ═══════════════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════════════════

PrefabEditorWindow::PrefabEditorWindow(const QString& prefabPath,
                                         EditorMainWindow* parentEditor,
                                         QWidget* parent)
    : QMainWindow(parent)
    , m_parentEditor(parentEditor)
    , m_prefabPathStr(prefabPath)
{
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose, true);
    resize(1400, 900);
    setMinimumSize(800, 600);

    m_previewScene = std::make_unique<Scene>();
    m_document = std::make_unique<PrefabDocument>(m_previewScene.get());

    EditorUndoManager::SetCustomPushHandler(m_previewScene.get(), [this](const std::string&) {
        if (m_document) {
            m_document->PushUndoSnapshot();
        }
    });

    m_document->SetPostRebuildCallback([this](Scene& /*scene*/) {
        rebuildHierarchy();
        if (m_engine && m_engine->IsReadyToRender()) {
            m_engine->RebuildSceneMeshes();
        }
    });

    m_document->SetPostSaveCallback([this](const std::filesystem::path& path) {
        if (m_savedCallback) m_savedCallback(path);
        emit prefabSaved(m_prefabPathStr);
    });

    setupUI();

    if (!openDocument(prefabPath)) {
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }

    updateWindowTitle();
    rebuildHierarchy();
}

PrefabEditorWindow::~PrefabEditorWindow() {
    if (m_previewScene) {
        EditorUndoManager::ClearCustomPushHandler(m_previewScene.get());
    }
    shutdownEngine();
    if (m_document) {
        m_document->OnClose();
    }
}

void PrefabEditorWindow::shutdownEngine() {
    if (!m_engine) {
        return;
    }

    // Clear the viewport input callback first — the lambda captures the
    // engine's `this`, so it must not outlive the engine.
    if (m_viewportWindow) {
        m_viewportWindow->SetInputEventCallback(nullptr);
    }

    // Detach the preview scene and clear callbacks so that nothing
    // fires back into partially-destroyed UI during shutdown.
    if (auto* ctx = m_engine->GetEditorContext()) {
        ctx->SetSelectionChangedCallback(nullptr);
        ctx->SetSceneChangedCallback(nullptr);
        ctx->SetSceneEditedCallback(nullptr);
        ctx->ClearSelection();
        ctx->SetExternalScene(nullptr);
    }

    m_engine->Shutdown();
    m_engine.reset();
}

const std::filesystem::path& PrefabEditorWindow::GetPrefabPath() const {
    return m_document->GetPath();
}

bool PrefabEditorWindow::IsDirty() const {
    return m_document && m_document->IsDirty();
}

void PrefabEditorWindow::SetPrefabSavedCallback(PrefabSavedCallback callback) {
    m_savedCallback = std::move(callback);
}

void PrefabEditorWindow::closeEvent(QCloseEvent* event) {
    if (!promptSaveIfDirty()) {
        event->ignore();
        return;
    }

    // Shut down the engine NOW, before the window is hidden and before
    // WA_DeleteOnClose schedules deferred deletion.  Without this, the
    // 0 ms render timer keeps firing between the close event and the
    // actual destructor call, continuously presenting to a hidden /
    // invalid surface.  The tight recreate-render-fail loop starves the
    // main-thread event loop, making the whole editor unresponsive.
    shutdownEngine();

    emit windowClosed(m_prefabPathStr);
    QMainWindow::closeEvent(event);
}

// ═══════════════════════════════════════════════════════════════════════════════
// UI Setup
// ═══════════════════════════════════════════════════════════════════════════════

void PrefabEditorWindow::setupUI() {
    // Apply dark editor style
    setStyleSheet(
        "QMainWindow { background: #1e1e1e; }"
        "QDockWidget { color: #ccc; font-weight: bold; }"
        "QDockWidget::title { background: #2a2a2a; padding: 6px; "
        "  border-bottom: 1px solid #3a3a3a; }"
        "QToolBar { background: #2d2d2d; border-bottom: 1px solid #3a3a3a; spacing: 3px; }"
        "QToolBar QToolButton { background: transparent; border: 1px solid transparent; "
        "  border-radius: 3px; padding: 3px 6px; color: #ccc; }"
        "QToolBar QToolButton:hover { background: #3a3a3a; border-color: #4a4a4a; }"
        "QToolBar QToolButton:checked { background: #264f78; border-color: #3a6a9a; }"
        "QToolBar QToolButton:pressed { background: #1a3a5a; }"
        "QStatusBar { background: #2d2d2d; color: #888; border-top: 1px solid #3a3a3a; }"
        "QStatusBar QLabel { color: #888; padding: 0 8px; }"
    );

    setupToolbar();
    setupViewport();
    setupDockWidgets();

    // Status bar
    auto* status = statusBar();
    m_statusFps = new QLabel(tr("FPS --"), status);
    m_statusPath = new QLabel(QString(), status);
    m_statusPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status->addWidget(m_statusPath, 1);
    status->addPermanentWidget(m_statusFps);

    setupConnections();
    initializeEngine();
}

void PrefabEditorWindow::setupToolbar() {
    m_toolbar = new QToolBar(tr("Prefab Tools"), this);
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addToolBar(Qt::TopToolBarArea, m_toolbar);

    // ── File Operations ──
    m_saveAction = m_toolbar->addAction(tr("Save"));
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setToolTip(tr("Save prefab (Ctrl+S)"));
    connect(m_saveAction, &QAction::triggered, this, &PrefabEditorWindow::onSave);

    m_reloadAction = m_toolbar->addAction(tr("Reload"));
    m_reloadAction->setToolTip(tr("Reload from disk"));
    connect(m_reloadAction, &QAction::triggered, this, &PrefabEditorWindow::onReload);

    m_toolbar->addSeparator();

    // ── Undo / Redo ──
    m_undoAction = m_toolbar->addAction(tr("Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, &PrefabEditorWindow::onUndo);

    m_redoAction = m_toolbar->addAction(tr("Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setEnabled(false);  // Redo stack not yet implemented
    connect(m_redoAction, &QAction::triggered, this, &PrefabEditorWindow::onRedo);

    m_toolbar->addSeparator();

    // ── Transform Gizmo Tools ──
    m_gizmoModeGroup = new QActionGroup(this);
    m_gizmoModeGroup->setExclusive(true);

    m_translateAction = m_toolbar->addAction(tr("Translate"));
    m_translateAction->setCheckable(true);
    m_translateAction->setChecked(true);
    m_translateAction->setToolTip(tr("Translate tool (W)"));
    m_gizmoModeGroup->addAction(m_translateAction);
    connect(m_translateAction, &QAction::triggered, this, &PrefabEditorWindow::onSelectTranslate);

    m_rotateAction = m_toolbar->addAction(tr("Rotate"));
    m_rotateAction->setCheckable(true);
    m_rotateAction->setToolTip(tr("Rotate tool (E)"));
    m_gizmoModeGroup->addAction(m_rotateAction);
    connect(m_rotateAction, &QAction::triggered, this, &PrefabEditorWindow::onSelectRotate);

    m_scaleAction = m_toolbar->addAction(tr("Scale"));
    m_scaleAction->setCheckable(true);
    m_scaleAction->setToolTip(tr("Scale tool (R)"));
    m_gizmoModeGroup->addAction(m_scaleAction);
    connect(m_scaleAction, &QAction::triggered, this, &PrefabEditorWindow::onSelectScale);

    m_toolbar->addSeparator();

    // ── Coordinate Space ──
    m_localWorldAction = m_toolbar->addAction(tr("World"));
    m_localWorldAction->setCheckable(true);
    m_localWorldAction->setToolTip(tr("Toggle Local/World space"));
    connect(m_localWorldAction, &QAction::triggered, this, &PrefabEditorWindow::onToggleLocalWorld);

    // ── Snap ──
    m_snapAction = m_toolbar->addAction(tr("Snap"));
    m_snapAction->setCheckable(true);
    m_snapAction->setToolTip(tr("Toggle grid snapping"));
    connect(m_snapAction, &QAction::triggered, this, &PrefabEditorWindow::onToggleSnap);

    m_toolbar->addSeparator();

    // ── Viewport Navigation ──
    m_frameSelectedAction = m_toolbar->addAction(tr("Frame"));
    m_frameSelectedAction->setShortcut(Qt::Key_F);
    m_frameSelectedAction->setToolTip(tr("Frame selected node (F)"));
    connect(m_frameSelectedAction, &QAction::triggered, this, &PrefabEditorWindow::onFrameSelected);

    m_frameAllAction = m_toolbar->addAction(tr("Frame All"));
    m_frameAllAction->setToolTip(tr("Frame entire prefab"));
    connect(m_frameAllAction, &QAction::triggered, this, &PrefabEditorWindow::onFrameAll);

    m_toolbar->addSeparator();

    // ── Add Operations ──
    m_addChildAction = m_toolbar->addAction(tr("+ Node"));
    m_addChildAction->setToolTip(tr("Add child node to selected"));
    connect(m_addChildAction, &QAction::triggered, this, &PrefabEditorWindow::onAddChildNode);

    m_addComponentAction = m_toolbar->addAction(tr("+ Component"));
    m_addComponentAction->setToolTip(tr("Add component to selected node"));
    connect(m_addComponentAction, &QAction::triggered, this, &PrefabEditorWindow::onAddComponent);
}

void PrefabEditorWindow::setupViewport() {
    m_viewportWindow = new QtViewportWindow();

    m_viewportHost = new QWidget(this);
    m_viewportHost->setObjectName("PrefabViewportHost");
    m_viewportContainer = QWidget::createWindowContainer(m_viewportWindow, m_viewportHost);
    m_viewportContainer->setMinimumSize(200, 200);
    m_viewportContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* hostLayout = new QVBoxLayout(m_viewportHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);
    hostLayout->addWidget(m_viewportContainer, 1);

    m_viewportHost->setStyleSheet("QWidget#PrefabViewportHost { background: #111; }");

    setCentralWidget(m_viewportHost);
}

void PrefabEditorWindow::setupDockWidgets() {
    // ── Hierarchy Dock (left) ──
    m_hierarchyDock = new QDockWidget(tr("Prefab Hierarchy"), this);
    m_hierarchyDock->setObjectName("PrefabHierarchyDock");
    m_hierarchyDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_hierarchyDock->setMinimumWidth(220);

    m_hierarchyPanel = new PrefabHierarchyPanel(m_hierarchyDock);
    m_hierarchyDock->setWidget(m_hierarchyPanel);

    addDockWidget(Qt::LeftDockWidgetArea, m_hierarchyDock);

    // ── Inspector Dock (right) ──
    m_inspectorDock = new QDockWidget(tr("Inspector"), this);
    m_inspectorDock->setObjectName("PrefabInspectorDock");
    m_inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_inspectorDock->setMinimumWidth(280);

    m_inspectorPanel = new PrefabInspectorPanel(m_inspectorDock);
    m_inspectorDock->setWidget(m_inspectorPanel);

    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

    // Set dock sizes proportionally (hierarchy ~20%, viewport ~55%, inspector ~25%)
    resizeDocks({ m_hierarchyDock, m_inspectorDock }, { 240, 320 }, Qt::Horizontal);
}

void PrefabEditorWindow::setupConnections() {
    // Hierarchy signals
    connect(m_hierarchyPanel, &PrefabHierarchyPanel::selectionChanged,
            this, &PrefabEditorWindow::onHierarchySelectionChanged);
    connect(m_hierarchyPanel, &PrefabHierarchyPanel::addChildRequested,
            this, &PrefabEditorWindow::onHierarchyAddChild);
    connect(m_hierarchyPanel, &PrefabHierarchyPanel::deleteNodeRequested,
            this, &PrefabEditorWindow::onHierarchyDeleteNode);
    connect(m_hierarchyPanel, &PrefabHierarchyPanel::duplicateNodeRequested,
            this, &PrefabEditorWindow::onHierarchyDuplicateNode);
    connect(m_hierarchyPanel, &PrefabHierarchyPanel::renameNodeRequested,
            this, &PrefabEditorWindow::onHierarchyRenameNode);
    connect(m_hierarchyPanel, &PrefabHierarchyPanel::resetTransformRequested,
            this, &PrefabEditorWindow::onHierarchyResetTransform);
    connect(m_hierarchyPanel, &PrefabHierarchyPanel::focusInViewportRequested,
            this, &PrefabEditorWindow::onHierarchyFocusInViewport);
    connect(m_hierarchyPanel, &PrefabHierarchyPanel::reparentRequested,
            this, &PrefabEditorWindow::onHierarchyReparent);

    // Inspector signals
    connect(m_inspectorPanel, &PrefabInspectorPanel::nameChanged,
            this, &PrefabEditorWindow::onInspectorNameChanged);
    connect(m_inspectorPanel, &PrefabInspectorPanel::transformChanged,
            this, &PrefabEditorWindow::onInspectorTransformChanged);
    connect(m_inspectorPanel, &PrefabInspectorPanel::addComponentRequested,
            this, &PrefabEditorWindow::onInspectorAddComponent);
    connect(m_inspectorPanel, &PrefabInspectorPanel::removeComponentRequested,
            this, &PrefabEditorWindow::onInspectorRemoveComponent);
}

void PrefabEditorWindow::initializeEngine() {
    m_engine = std::make_unique<QtEditorEngine>(this);

    // Point the engine's EditorContext at our preview scene
    auto* ctx = m_engine->GetEditorContext();
    // Replace the default scene with our preview scene
    // The engine creates a default scene on swapchain creation, but we'll override
    // it after the viewport is ready.

    connect(m_engine.get(), &QtEditorEngine::frameRendered,
            this, &PrefabEditorWindow::onFrameRendered);
    connect(m_engine.get(), &QtEditorEngine::engineError,
            this, [this](const QString& msg) {
        statusBar()->showMessage(tr("Engine error: %1").arg(msg), 5000);
    });

    connect(m_viewportWindow, &QtViewportWindow::viewportReady,
            this, &PrefabEditorWindow::onViewportReady);

    if (!m_engine->InitializeVulkan()) {
        statusBar()->showMessage(tr("Failed to initialize Vulkan for prefab viewport"), 5000);
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Viewport Engine Callbacks
// ═══════════════════════════════════════════════════════════════════════════════

void PrefabEditorWindow::onViewportReady() {
    if (!m_engine) return;

    if (m_engine->AttachToViewport(m_viewportWindow)) {
        auto* ctx = m_engine->GetEditorContext();

        // Point the engine at our preview scene (externally owned)
        ctx->SetExternalScene(m_previewScene.get());

        // Configure gizmo defaults
        ctx->SetGizmoMode(m_gizmoMode);
        ctx->SetGizmoSpace(m_gizmoSpace);
        auto& snap = ctx->GetSnapSettings();
        snap.enableSnap = m_snapEnabled;

        // Configure viewport settings for prefab editing
        auto& vs = ctx->GetViewportSettings();
        vs.showGrid = true;
        vs.showGizmo = true;
        vs.showSelectionOutline = true;
        vs.showShadows = true;
        vs.showSkybox = true;
        vs.showLightBillboards = true;

        m_engine->StartRenderLoop();

        // Set project root for mesh resolution if parent editor has it
        if (m_parentEditor) {
            auto* mainEngine = m_parentEditor->findChild<QtEditorEngine*>();
            if (mainEngine && mainEngine->GetFrameRenderer()) {
                const auto& projRoot = mainEngine->GetFrameRenderer()->GetProjectRoot();
                if (!projRoot.empty()) {
                    m_engine->SetProjectRoot(projRoot);
                    if (auto* fr = m_engine->GetFrameRenderer()) {
                        fr->SetProjectRoot(projRoot);
                    }
                }
            }
        }

        // Upload meshes for the preview scene (built during OnOpen)
        m_engine->RebuildSceneMeshes();

        // Hook up selection change monitoring
        ctx->SetSelectionChangedCallback([this](entt::entity /*entity*/) {
            if (m_suppressSelectionSync) return;
            syncViewportSelectionToHierarchy();
        });

        ctx->SetSceneEditedCallback([this]() {
            if (m_refreshingInspector || !m_document) {
                return;
            }
            Entity entity = findEntityByNodeId(m_selectedNodeId);
            if (entity) {
                m_document->SyncNodeFromEntity(entity);
                updateWindowTitle();
                if (m_engine) {
                    m_engine->RebuildSceneMeshes();
                }
            }
        });

        refreshInspectorForSelection();

        statusBar()->showMessage(tr("Viewport ready"), 3000);
        std::cout << "[PrefabEditor] Viewport attached and rendering\n";
    }
}

void PrefabEditorWindow::onFrameRendered(float dt) {
    // Update FPS display
    if (dt > 0.0f) {
        m_statusFps->setText(QString("FPS %1").arg(static_cast<int>(1.0f / dt)));
    }

    // Sync gizmo-driven transform changes back to the document
    syncTransformFromViewportToDocument();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Document Management
// ═══════════════════════════════════════════════════════════════════════════════

bool PrefabEditorWindow::openDocument(const QString& path) {
    std::string error;
    if (!m_document->OnOpen(std::filesystem::path(path.toStdString()), error)) {
        QMessageBox::critical(this, tr("Error"),
            tr("Failed to open prefab:\n%1").arg(QString::fromStdString(error)));
        return false;
    }
    if (m_statusPath) {
        m_statusPath->setText(QString::fromStdString(m_document->GetPath().generic_string()));
    }
    return true;
}

bool PrefabEditorWindow::promptSaveIfDirty() {
    if (!m_document || !m_document->IsDirty()) return true;

    auto result = QMessageBox::question(this, tr("Unsaved Changes"),
        tr("The prefab '%1' has unsaved changes.\nSave before closing?")
            .arg(QString::fromStdString(m_document->GetDisplayName())),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (result == QMessageBox::Save) { onSave(); return true; }
    return result == QMessageBox::Discard;
}

void PrefabEditorWindow::updateWindowTitle() {
    QString title;
    if (m_document) {
        title = QString::fromStdString(m_document->GetDisplayName());
        if (m_document->IsDirty()) title += " *";
    }
    title += " \u2014 Prefab Editor";
    setWindowTitle(title);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Toolbar Actions
// ═══════════════════════════════════════════════════════════════════════════════

void PrefabEditorWindow::onSave() {
    if (!m_document) return;

    // Sync any pending viewport/inspector changes
    syncTransformFromViewportToDocument();
    syncAllPreviewNodes();

    std::string error;
    if (!m_document->OnSave(error)) {
        QMessageBox::critical(this, tr("Save Error"),
            tr("Failed to save:\n%1").arg(QString::fromStdString(error)));
        return;
    }
    updateWindowTitle();
    statusBar()->showMessage(tr("Saved"), 3000);
}

void PrefabEditorWindow::onReload() {
    if (!m_document) return;
    if (m_document->IsDirty()) {
        auto result = QMessageBox::question(this, tr("Reload"),
            tr("Discard unsaved changes and reload?"),
            QMessageBox::Yes | QMessageBox::No);
        if (result != QMessageBox::Yes) return;
    }

    m_selectedNodeId.clear();
    std::string error;
    if (!m_document->OnOpen(m_document->GetPath(), error)) {
        QMessageBox::critical(this, tr("Reload Error"),
            tr("Failed to reload:\n%1").arg(QString::fromStdString(error)));
        return;
    }

    rebuildHierarchy();
    m_inspectorPanel->Clear();
    updateWindowTitle();
    statusBar()->showMessage(tr("Reloaded"), 3000);
}

void PrefabEditorWindow::onUndo() {
    if (!m_document) return;
    std::string previous = m_selectedNodeId;
    std::string error;
    if (!m_document->Undo(error)) {
        statusBar()->showMessage(tr("Undo: %1").arg(QString::fromStdString(error)), 3000);
        return;
    }
    rebuildHierarchy();
    updateWindowTitle();
    if (m_engine) {
        m_engine->RebuildSceneMeshes();
    }
    if (!previous.empty()) {
        selectNode(previous);
    } else {
        m_inspectorPanel->Clear();
    }
}

void PrefabEditorWindow::onRedo() {
    statusBar()->showMessage(tr("Redo not yet implemented"), 3000);
}

void PrefabEditorWindow::onFrameSelected() {
    if (m_engine && m_engine->IsReadyToRender()) {
        if (!m_selectedNodeId.empty()) {
            focusViewportOnNode(m_selectedNodeId);
        } else {
            m_engine->FocusOnSelection();
        }
    }
}

void PrefabEditorWindow::onFrameAll() {
    if (!m_engine || !m_engine->IsReadyToRender() || !m_document) return;

    // Focus on root node
    const auto& asset = m_document->GetAsset();
    if (!asset.rootNodeId.empty()) {
        focusViewportOnNode(asset.rootNodeId);
    }
}

void PrefabEditorWindow::onAddChildNode() {
    std::string parentId = m_selectedNodeId;
    if (parentId.empty() && m_document) {
        parentId = m_document->GetAsset().rootNodeId;
    }
    if (parentId.empty()) return;
    onHierarchyAddChild(parentId);
}

void PrefabEditorWindow::onAddComponent() {
    if (m_selectedNodeId.empty()) {
        statusBar()->showMessage(tr("Select a node first"), 3000);
        return;
    }

    QMenu menu(this);
    QStringList types = {
        "MeshRenderer", "Material", "Light", "Camera",
        "RigidBody", "PhysicsShape", "MeshCollider",
        "ScriptComponent", "LuaScript"
    };
    for (const auto& type : types) {
        menu.addAction(type, this, [this, type]() {
            onInspectorAddComponent(m_selectedNodeId, type.toStdString());
        });
    }
    menu.exec(QCursor::pos());
}

// ── Gizmo Tool Selection ──

void PrefabEditorWindow::onSelectTranslate() {
    m_gizmoMode = GizmoMode::Translate;
    if (m_engine) m_engine->GetEditorContext()->SetGizmoMode(m_gizmoMode);
}

void PrefabEditorWindow::onSelectRotate() {
    m_gizmoMode = GizmoMode::Rotate;
    if (m_engine) m_engine->GetEditorContext()->SetGizmoMode(m_gizmoMode);
}

void PrefabEditorWindow::onSelectScale() {
    m_gizmoMode = GizmoMode::Scale;
    if (m_engine) m_engine->GetEditorContext()->SetGizmoMode(m_gizmoMode);
}

void PrefabEditorWindow::onToggleLocalWorld() {
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    ctx->ToggleGizmoSpace();
    m_gizmoSpace = ctx->GetGizmoSpace();
    m_localWorldAction->setText(m_gizmoSpace == GizmoSpace::Local ? tr("Local") : tr("World"));
}

void PrefabEditorWindow::onToggleSnap() {
    if (!m_engine) return;
    m_snapEnabled = m_snapAction->isChecked();
    m_engine->GetEditorContext()->GetSnapSettings().enableSnap = m_snapEnabled;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Hierarchy Callbacks
// ═══════════════════════════════════════════════════════════════════════════════

void PrefabEditorWindow::onHierarchySelectionChanged(const std::string& nodeId) {
    if (m_suppressSelectionSync) return;
    selectNode(nodeId);
}

void PrefabEditorWindow::onHierarchyAddChild(const std::string& parentNodeId) {
    if (!m_document) return;
    m_document->PushUndoSnapshot();

    Entity newEntity;
    if (m_document->AddChildNode(parentNodeId, "New Node", &newEntity)) {
        rebuildHierarchy();
        updateWindowTitle();
        if (m_engine && m_engine->IsReadyToRender()) {
            m_engine->RebuildSceneMeshes();
        }
    }
}

void PrefabEditorWindow::onHierarchyDeleteNode(const std::string& nodeId) {
    if (!m_document || nodeId.empty()) return;
    if (m_document->IsRootNode(nodeId)) return;

    auto result = QMessageBox::question(this, tr("Delete Node"),
        tr("Delete this node and all children?"),
        QMessageBox::Yes | QMessageBox::No);
    if (result != QMessageBox::Yes) return;

    m_document->PushUndoSnapshot();
    if (m_document->DeleteNode(nodeId)) {
        if (m_selectedNodeId == nodeId) {
            m_selectedNodeId.clear();
            m_inspectorPanel->Clear();
            if (m_engine) m_engine->GetEditorContext()->ClearSelection();
        }
        rebuildHierarchy();
        updateWindowTitle();
    }
}

void PrefabEditorWindow::onHierarchyDuplicateNode(const std::string& nodeId) {
    if (!m_document || nodeId.empty() || m_document->IsRootNode(nodeId)) return;

    // Find the node's parent and create a copy
    const auto& asset = m_document->GetAsset();
    const PrefabNode* node = nullptr;
    for (const auto& n : asset.nodes) {
        if (n.id == nodeId) { node = &n; break; }
    }
    if (!node) return;

    m_document->PushUndoSnapshot();
    Entity newEntity;
    if (m_document->AddChildNode(node->parentId, node->name + " (Copy)", &newEntity)) {
        rebuildHierarchy();
        updateWindowTitle();
    }
}

void PrefabEditorWindow::onHierarchyRenameNode(const std::string& nodeId) {
    if (!m_document || nodeId.empty()) return;

    auto& asset = m_document->GetAsset();
    for (auto& node : asset.nodes) {
        if (node.id == nodeId) {
            bool ok;
            QString newName = QInputDialog::getText(this, tr("Rename Node"),
                tr("New name:"), QLineEdit::Normal,
                QString::fromStdString(node.name), &ok);
            if (ok && !newName.isEmpty()) {
                m_document->PushUndoSnapshot();
                node.name = newName.toStdString();
                m_document->SetDirty(true);
                rebuildHierarchy();
                refreshInspectorForSelection();
                updateWindowTitle();
            }
            break;
        }
    }
}

void PrefabEditorWindow::onHierarchyResetTransform(const std::string& nodeId) {
    if (!m_document || nodeId.empty()) return;

    m_document->PushUndoSnapshot();
    auto& asset = m_document->GetAsset();
    for (auto& node : asset.nodes) {
        if (node.id == nodeId) {
            node.localTransform = TransformComponent();
            m_document->SetDirty(true);

            // Update preview scene entity
            Entity entity = findEntityByNodeId(nodeId);
            if (entity) {
                auto& t = entity.GetComponent<TransformComponent>();
                t.position = Vec3(0, 0, 0);
                t.rotation = Vec3(0, 0, 0);
                t.scale = Vec3(1, 1, 1);
            }

            refreshInspectorForSelection();
            updateWindowTitle();
            break;
        }
    }
}

void PrefabEditorWindow::onHierarchyFocusInViewport(const std::string& nodeId) {
    focusViewportOnNode(nodeId);
}

void PrefabEditorWindow::onHierarchyReparent(const std::string& nodeId,
                                               const std::string& newParentId) {
    if (!m_document) return;
    m_document->PushUndoSnapshot();
    if (m_document->ReparentNode(nodeId, newParentId)) {
        rebuildHierarchy();
        updateWindowTitle();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Inspector Callbacks
// ═══════════════════════════════════════════════════════════════════════════════

void PrefabEditorWindow::onInspectorNameChanged(const std::string& nodeId,
                                                  const std::string& newName) {
    if (!m_document) return;
    m_document->PushUndoSnapshot();

    auto& asset = m_document->GetAsset();
    for (auto& node : asset.nodes) {
        if (node.id == nodeId) {
            node.name = newName;
            break;
        }
    }
    m_document->SetDirty(true);

    // Update preview scene entity tag
    Entity entity = findEntityByNodeId(nodeId);
    if (entity && entity.HasComponent<TagComponent>()) {
        entity.GetComponent<TagComponent>().tag = newName;
    }

    rebuildHierarchy();
    updateWindowTitle();
}

void PrefabEditorWindow::onInspectorTransformChanged(const std::string& nodeId,
                                                       const TransformComponent& transform) {
    if (!m_document) return;
    m_document->PushUndoSnapshot();

    auto& asset = m_document->GetAsset();
    for (auto& node : asset.nodes) {
        if (node.id == nodeId) {
            node.localTransform = transform;
            break;
        }
    }
    m_document->SetDirty(true);

    // Update preview scene entity
    Entity entity = findEntityByNodeId(nodeId);
    if (entity) {
        auto& t = entity.GetComponent<TransformComponent>();
        t.position = transform.position;
        t.rotation = transform.rotation;
        t.scale = transform.scale;
    }

    updateWindowTitle();
}

void PrefabEditorWindow::onInspectorAddComponent(const std::string& nodeId,
                                                    const std::string& componentType) {
    if (!m_document) return;
    m_document->PushUndoSnapshot();

    Entity entity = findEntityByNodeId(nodeId);
    if (entity) {
        if (!PrefabComponentIO::AddComponent(entity, componentType)) {
            statusBar()->showMessage(
                tr("Component '%1' already exists").arg(QString::fromStdString(componentType)), 3000);
            return;
        }
        m_document->SyncNodeFromEntity(entity);
    } else {
        auto& asset = m_document->GetAsset();
        for (auto& node : asset.nodes) {
            if (node.id == nodeId) {
                bool exists = false;
                for (const auto& c : node.components) {
                    if (c.type == componentType) { exists = true; break; }
                }
                if (exists) {
                    statusBar()->showMessage(
                        tr("Component '%1' already exists").arg(QString::fromStdString(componentType)), 3000);
                    return;
                }

                PrefabComponentEntry entry;
                entry.type = componentType;
                entry.enabled = true;
                entry.data.type = JsonValue::Type::Object;
                node.components.push_back(std::move(entry));
                break;
            }
        }
        m_document->RebuildPreview();
    }

    m_document->SetDirty(true);
    refreshInspectorForSelection();
    updateWindowTitle();
    if (m_engine) m_engine->RebuildSceneMeshes();
}

void PrefabEditorWindow::onInspectorRemoveComponent(const std::string& nodeId,
                                                       const std::string& componentType) {
    if (!m_document) return;
    m_document->PushUndoSnapshot();

    Entity entity = findEntityByNodeId(nodeId);
    if (entity) {
        PrefabComponentIO::RemoveComponent(entity, componentType);
        m_document->SyncNodeFromEntity(entity);
    } else {
        auto& asset = m_document->GetAsset();
        for (auto& node : asset.nodes) {
            if (node.id == nodeId) {
                node.components.erase(
                    std::remove_if(node.components.begin(), node.components.end(),
                        [&](const PrefabComponentEntry& c) { return c.type == componentType; }),
                    node.components.end());
                break;
            }
        }
        m_document->RebuildPreview();
    }

    m_document->SetDirty(true);
    refreshInspectorForSelection();
    updateWindowTitle();
    if (m_engine) m_engine->RebuildSceneMeshes();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Selection and Sync
// ═══════════════════════════════════════════════════════════════════════════════

void PrefabEditorWindow::selectNode(const std::string& nodeId) {
    m_selectedNodeId = nodeId;

    m_suppressSelectionSync = true;

    // Sync to hierarchy
    m_hierarchyPanel->SetSelectedNodeId(nodeId);

    // Sync to viewport (select the preview entity)
    syncSelectionToViewport();

    // Sync to inspector
    refreshInspectorForSelection();

    m_suppressSelectionSync = false;
}

void PrefabEditorWindow::syncSelectionToViewport() {
    if (!m_engine || !m_engine->IsReadyToRender()) return;
    auto* ctx = m_engine->GetEditorContext();

    if (m_selectedNodeId.empty()) {
        ctx->ClearSelection();
        return;
    }

    Entity entity = findEntityByNodeId(m_selectedNodeId);
    if (entity) {
        ctx->SelectEntity(static_cast<entt::entity>(entity));
    } else {
        ctx->ClearSelection();
    }
}

void PrefabEditorWindow::syncViewportSelectionToHierarchy() {
    if (!m_engine || !m_engine->IsReadyToRender()) return;
    auto* ctx = m_engine->GetEditorContext();

    entt::entity selected = ctx->GetSelectedEntity();
    if (selected == entt::null) {
        if (!m_selectedNodeId.empty()) {
            m_selectedNodeId.clear();
            m_suppressSelectionSync = true;
            m_hierarchyPanel->SetSelectedNodeId({});
            m_inspectorPanel->Clear();
            m_suppressSelectionSync = false;
        }
        return;
    }

    std::string nodeId = findNodeIdByEntity(selected);
    if (nodeId != m_selectedNodeId) {
        m_selectedNodeId = nodeId;
        m_suppressSelectionSync = true;
        m_hierarchyPanel->SetSelectedNodeId(nodeId);
        refreshInspectorForSelection();
        m_suppressSelectionSync = false;
    }
}

void PrefabEditorWindow::refreshInspectorForSelection() {
    if (m_selectedNodeId.empty() || !m_document) {
        m_inspectorPanel->Clear();
        return;
    }

    const auto& asset = m_document->GetAsset();
    const PrefabNode* node = nullptr;
    for (const auto& n : asset.nodes) {
        if (n.id == m_selectedNodeId) { node = &n; break; }
    }

    m_refreshingInspector = true;
    EditorContext* ctx = (m_engine && m_engine->IsReadyToRender()) ? m_engine->GetEditorContext() : nullptr;
    Entity entity = findEntityByNodeId(m_selectedNodeId);
    m_inspectorPanel->ShowEntity(ctx, m_previewScene.get(), entity, node);
    m_refreshingInspector = false;
}

void PrefabEditorWindow::syncAllPreviewNodes() {
    if (!m_document || !m_previewScene) {
        return;
    }
    auto view = m_previewScene->GetEntitiesWith<PrefabNodeComponent>();
    for (auto handle : view) {
        m_document->SyncNodeFromEntity(Entity(handle, m_previewScene.get()));
    }
}

void PrefabEditorWindow::syncTransformFromViewportToDocument() {
    // When the gizmo is used in the viewport, the preview entity's transform
    // changes directly. We need to sync that back to the prefab asset data.
    if (m_selectedNodeId.empty() || !m_document || !m_previewScene) return;

    Entity entity = findEntityByNodeId(m_selectedNodeId);
    if (!entity || !entity.HasComponent<TransformComponent>()) return;

    const auto& transform = entity.GetComponent<TransformComponent>();
    auto& asset = m_document->GetAsset();

    for (auto& node : asset.nodes) {
        if (node.id == m_selectedNodeId) {
            // Only sync if changed (avoid unnecessary dirty marks)
            auto& lt = node.localTransform;
            bool changed = (lt.position.x != transform.position.x ||
                            lt.position.y != transform.position.y ||
                            lt.position.z != transform.position.z ||
                            lt.rotation.x != transform.rotation.x ||
                            lt.rotation.y != transform.rotation.y ||
                            lt.rotation.z != transform.rotation.z ||
                            lt.scale.x != transform.scale.x ||
                            lt.scale.y != transform.scale.y ||
                            lt.scale.z != transform.scale.z);
            if (changed) {
                lt.position = transform.position;
                lt.rotation = transform.rotation;
                lt.scale = transform.scale;
                m_document->SetDirty(true);
                updateWindowTitle();

                // Live-update inspector transform spinboxes
                m_inspectorPanel->RefreshTransform(transform);
            }
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Hierarchy Helpers
// ═══════════════════════════════════════════════════════════════════════════════

void PrefabEditorWindow::rebuildHierarchy() {
    if (!m_document) return;
    m_hierarchyPanel->Rebuild(m_document->GetAsset(), m_selectedNodeId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Viewport Helpers
// ═══════════════════════════════════════════════════════════════════════════════

void PrefabEditorWindow::focusViewportOnNode(const std::string& nodeId) {
    if (!m_engine || !m_engine->IsReadyToRender()) return;

    Entity entity = findEntityByNodeId(nodeId);
    if (!entity) return;

    // Select the entity first so FocusOnSelection works
    auto* ctx = m_engine->GetEditorContext();
    m_suppressSelectionSync = true;
    ctx->SelectEntity(static_cast<entt::entity>(entity));
    m_suppressSelectionSync = false;

    m_engine->FocusOnSelection();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Entity Lookup
// ═══════════════════════════════════════════════════════════════════════════════

Entity PrefabEditorWindow::findEntityByNodeId(const std::string& nodeId) {
    if (!m_previewScene || nodeId.empty()) return {};
    auto view = m_previewScene->GetEntitiesWith<PrefabNodeComponent>();
    for (auto entityHandle : view) {
        Entity entity(entityHandle, m_previewScene.get());
        if (entity.GetComponent<PrefabNodeComponent>().nodeId == nodeId) {
            return entity;
        }
    }
    return {};
}

std::string PrefabEditorWindow::findNodeIdByEntity(entt::entity entity) {
    if (!m_previewScene || entity == entt::null) return {};
    if (!m_previewScene->GetRegistry().valid(entity)) return {};
    Entity e(entity, m_previewScene.get());
    if (e.HasComponent<PrefabNodeComponent>()) {
        return e.GetComponent<PrefabNodeComponent>().nodeId;
    }
    return {};
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
