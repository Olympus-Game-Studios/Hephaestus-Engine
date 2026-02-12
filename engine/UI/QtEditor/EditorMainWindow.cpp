/**
 * @file EditorMainWindow.cpp
 * @brief Implementation of the Qt-based Hephaestus Editor main window.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorMainWindow.h"
#include "QtViewportWindow.h"
#include "QtEditorEngine.h"
#include "AssetBrowserWidget.h"
#include "EditorAssetActions.h"
#include "LightInspector.h"
#include "EnvironmentInspector.h"
#include "ComponentInspector.h"
#include "MeshRendererInspector.h"
#include "ScriptInspector.h"
#include "LuaScriptInspector.h"
#include "PhysicsInspectors.h"
#include "CameraInspector.h"
#include "EditorUndoCommand.h"

#include "../../Core/EditorContext.h"
#include "../../Core/ProjectContext.h"
#include "../../Assets/AssetManager.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"
#include "../../Scene/Entity.h"
#include "../../Scripting/ScriptComponent.h"
#include "../../Scripting/LuaScriptComponent.h"

#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QDockWidget>
#include <QTreeWidget>
#include <QListWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QCloseEvent>
#include <QScrollArea>
#include <QDir>
#include <iostream>
#include <cmath>
#include <filesystem>

namespace Hephaestus {
namespace QtEditor {

EditorMainWindow::EditorMainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_projectContext(std::make_unique<ProjectContext>())
{
    // Load recent projects from settings
    m_projectContext->LoadRecentProjectsFromFile();

    // Set window properties
    setWindowTitle("Hephaestus Engine Editor (Qt)");
    resize(1280, 720);

    // Setup UI components
    setupMenuBar();
    setupDockWidgets();
    setupCentralWidget();
    setupStatusBar();

    // Initialize the engine after UI is setup
    initializeEngine();
}

EditorMainWindow::~EditorMainWindow()
{
    // Engine cleanup happens in its destructor via unique_ptr
    std::cout << "[EditorMainWindow] Destructor called\n";
}

void EditorMainWindow::closeEvent(QCloseEvent* event)
{
    std::cout << "[EditorMainWindow] Close event received\n";
    
    // Stop and cleanup the engine before closing
    if (m_engine) {
        m_engine->Shutdown();
    }
    
    event->accept();
}

void EditorMainWindow::setupMenuBar()
{
    QMenuBar* menuBar = this->menuBar();

    // ─────────────────────────────────────────────────────────────────────────
    // File Menu
    // ─────────────────────────────────────────────────────────────────────────
    QMenu* fileMenu = menuBar->addMenu(tr("&File"));

    // --- Project Section ---
    QAction* newProjectAction = fileMenu->addAction(tr("New &Project..."));
    connect(newProjectAction, &QAction::triggered, this, &EditorMainWindow::onNewProject);

    QAction* openProjectAction = fileMenu->addAction(tr("&Open Project..."));
    openProjectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(openProjectAction, &QAction::triggered, this, &EditorMainWindow::onOpenProject);

    QAction* closeProjectAction = fileMenu->addAction(tr("Close Project"));
    connect(closeProjectAction, &QAction::triggered, this, &EditorMainWindow::onCloseProject);

    // Recent Projects submenu
    m_recentProjectsMenu = fileMenu->addMenu(tr("Recent Projects"));
    rebuildRecentProjectsMenu();

    fileMenu->addSeparator();

    // --- Level Section ---
    QAction* newAction = fileMenu->addAction(tr("&New Level"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &EditorMainWindow::onNewLevel);

    QAction* openAction = fileMenu->addAction(tr("Open &Level..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &EditorMainWindow::onOpenLevel);

    fileMenu->addSeparator();

    QAction* saveAction = fileMenu->addAction(tr("&Save Level"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &EditorMainWindow::onSaveLevel);

    QAction* saveAsAction = fileMenu->addAction(tr("Save Level &As..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &EditorMainWindow::onSaveLevelAs);

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &EditorMainWindow::onExit);

    // ─────────────────────────────────────────────────────────────────────────
    // Edit Menu
    // ─────────────────────────────────────────────────────────────────────────
    QMenu* editMenu = menuBar->addMenu(tr("&Edit"));

    QAction* undoAction = editMenu->addAction(tr("&Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, &EditorMainWindow::onUndo);

    QAction* redoAction = editMenu->addAction(tr("&Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, &EditorMainWindow::onRedo);

    // ─────────────────────────────────────────────────────────────────────────
    // View Menu
    // ─────────────────────────────────────────────────────────────────────────
    QMenu* viewMenu = menuBar->addMenu(tr("&View"));

    // These will be populated after dock widgets are created
    // (see setupDockWidgets)

    // ─────────────────────────────────────────────────────────────────────────
    // Help Menu
    // ─────────────────────────────────────────────────────────────────────────
    QMenu* helpMenu = menuBar->addMenu(tr("&Help"));

    QAction* aboutAction = helpMenu->addAction(tr("&About"));
    connect(aboutAction, &QAction::triggered, this, &EditorMainWindow::onAbout);
}

void EditorMainWindow::setupDockWidgets()
{
    // Allow docking on all sides
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);

    // ─────────────────────────────────────────────────────────────────────────
    // Outliner Dock (Left) - Scene Hierarchy
    // ─────────────────────────────────────────────────────────────────────────
    m_outlinerDock = new QDockWidget(tr("Outliner"), this);
    m_outlinerDock->setObjectName("OutlinerDock");
    m_outlinerDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_outlinerTree = new QTreeWidget(m_outlinerDock);
    m_outlinerTree->setHeaderLabel(tr("Scene"));
    m_outlinerTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_outlinerDock->setWidget(m_outlinerTree);
    
    // Connect selection changed signal
    connect(m_outlinerTree, &QTreeWidget::itemSelectionChanged, 
            this, &EditorMainWindow::onOutlinerSelectionChanged);

    addDockWidget(Qt::LeftDockWidgetArea, m_outlinerDock);

    // ─────────────────────────────────────────────────────────────────────────
    // Details Dock (Right) - Entity Inspector
    // ─────────────────────────────────────────────────────────────────────────
    m_detailsDock = new QDockWidget(tr("Details"), this);
    m_detailsDock->setObjectName("DetailsDock");
    m_detailsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // Scroll area wrapper so the inspector is scrollable when many components are shown
    m_detailsScrollArea = new QScrollArea(m_detailsDock);
    m_detailsScrollArea->setWidgetResizable(true);
    m_detailsScrollArea->setFrameShape(QFrame::NoFrame);

    m_detailsWidget = new QWidget();
    m_detailsLayout = new QVBoxLayout(m_detailsWidget);
    m_detailsLayout->setContentsMargins(6, 6, 6, 6);
    m_detailsLayout->setSpacing(6);

    // ── Header row: Entity ID + Lock checkbox ───────────────────────────
    {
        auto* headerRow = new QHBoxLayout();
        m_entityIdLabel = new QLabel(tr("(No selection)"));
        m_entityIdLabel->setStyleSheet("color: gray; font-size: 10px;");
        headerRow->addWidget(m_entityIdLabel);

        headerRow->addStretch();

        m_lockCheck = new QCheckBox(tr("Lock"));
        m_lockCheck->setToolTip(tr("Lock prevents selection changes from altering the Details view"));
        connect(m_lockCheck, &QCheckBox::toggled, this, [this](bool checked) {
            m_detailsLocked = checked;
            if (checked && m_engine) {
                auto* ctx = m_engine->GetEditorContext();
                if (ctx && ctx->HasSelection()) {
                    m_lockedEntity = ctx->GetSelectedEntity();
                }
            }
        });
        headerRow->addWidget(m_lockCheck);

        m_detailsLayout->addLayout(headerRow);
    }

    // Name field
    QFormLayout* nameLayout = new QFormLayout();
    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText(tr("(No selection)"));
    m_nameEdit->setEnabled(false);
    nameLayout->addRow(tr("Name:"), m_nameEdit);
    m_detailsLayout->addLayout(nameLayout);
    
    connect(m_nameEdit, &QLineEdit::textChanged, this, &EditorMainWindow::onNameChanged);

    // Transform group
    m_transformGroup = new QGroupBox(tr("Transform"));
    m_transformGroup->setEnabled(false);
    QFormLayout* transformLayout = new QFormLayout(m_transformGroup);

    // Helper lambda to create a row of 3 spin boxes
    auto createVec3Row = [this](QDoubleSpinBox*& x, QDoubleSpinBox*& y, QDoubleSpinBox*& z, 
                                double min, double max, double step, int decimals) {
        QWidget* rowWidget = new QWidget();
        QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto createSpinBox = [&](QDoubleSpinBox*& sb) {
            sb = new QDoubleSpinBox();
            sb->setRange(min, max);
            sb->setSingleStep(step);
            sb->setDecimals(decimals);
            sb->setMinimumWidth(60);
            rowLayout->addWidget(sb);
        };

        createSpinBox(x);
        createSpinBox(y);
        createSpinBox(z);
        return rowWidget;
    };

    // Position (large range)
    QWidget* posRow = createVec3Row(m_posX, m_posY, m_posZ, -100000.0, 100000.0, 0.1, 3);
    transformLayout->addRow(tr("Position:"), posRow);
    
    // Rotation (degrees)
    QWidget* rotRow = createVec3Row(m_rotX, m_rotY, m_rotZ, -360.0, 360.0, 1.0, 2);
    transformLayout->addRow(tr("Rotation:"), rotRow);
    
    // Scale
    QWidget* scaleRow = createVec3Row(m_scaleX, m_scaleY, m_scaleZ, 0.001, 10000.0, 0.1, 3);
    transformLayout->addRow(tr("Scale:"), scaleRow);

    // Connect position spinbox signals
    connect(m_posX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onPositionChanged);
    connect(m_posY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onPositionChanged);
    connect(m_posZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onPositionChanged);
    
    // Connect rotation spinbox signals
    connect(m_rotX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onRotationChanged);
    connect(m_rotY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onRotationChanged);
    connect(m_rotZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onRotationChanged);
    
    // Connect scale spinbox signals
    connect(m_scaleX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onScaleChanged);
    connect(m_scaleY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onScaleChanged);
    connect(m_scaleZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onScaleChanged);

    m_detailsLayout->addWidget(m_transformGroup);
    
    // Component inspector widgets will be inserted here dynamically
    // (LightInspector, EnvironmentInspector, MeshRendererInspector, etc.)

    // ── Add Component + Delete Entity buttons ───────────────────────────
    {
        auto* actionRow = new QHBoxLayout();
        m_addComponentBtn = new QPushButton(tr("+ Add Component"));
        m_addComponentBtn->setEnabled(false);
        connect(m_addComponentBtn, &QPushButton::clicked, this, &EditorMainWindow::showAddComponentMenu);
        actionRow->addWidget(m_addComponentBtn);

        actionRow->addStretch();

        m_deleteEntityBtn = new QPushButton(tr("Delete Entity"));
        m_deleteEntityBtn->setStyleSheet("color: #cc3333;");
        m_deleteEntityBtn->setEnabled(false);
        connect(m_deleteEntityBtn, &QPushButton::clicked, this, [this]() {
            if (!m_engine) return;
            auto* ctx = m_engine->GetEditorContext();
            if (!ctx || !ctx->HasActiveScene() || !ctx->HasSelection()) return;

            EditorUndoManager::PushSnapshot(ctx, "Delete entity");

            entt::entity entity = ctx->GetSelectedEntity();
            ctx->ClearSelection();
            ctx->GetActiveScene()->DestroyEntity(Entity(entity, ctx->GetActiveScene()));
            ctx->MarkDirty();

            rebuildOutliner();
            clearDetailsPanel();
        });
        actionRow->addWidget(m_deleteEntityBtn);

        m_detailsLayout->addLayout(actionRow);
    }

    // Add stretch to push everything to top
    m_detailsLayout->addStretch();

    m_detailsScrollArea->setWidget(m_detailsWidget);
    m_detailsDock->setWidget(m_detailsScrollArea);
    addDockWidget(Qt::RightDockWidgetArea, m_detailsDock);

    // ─────────────────────────────────────────────────────────────────────────
    // Asset Browser Dock (Bottom)
    // ─────────────────────────────────────────────────────────────────────────
    m_assetBrowserDock = new QDockWidget(tr("Asset Browser"), this);
    m_assetBrowserDock->setObjectName("AssetBrowserDock");
    m_assetBrowserDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    // Create the new asset browser widget
    m_assetBrowser = new AssetBrowserWidget(this, m_assetBrowserDock);
    m_assetBrowserDock->setWidget(m_assetBrowser);
    m_assetBrowserDock->setMinimumHeight(200);

    // Create asset actions handler
    m_assetActions = std::make_unique<EditorAssetActions>(this);

    // Connect asset browser to actions
    connect(m_assetBrowser, &AssetBrowserWidget::assetOpenRequested,
            this, [this](const QString& relativePath) {
                m_assetActions->Open(relativePath);
            });

    addDockWidget(Qt::BottomDockWidgetArea, m_assetBrowserDock);

    // ─────────────────────────────────────────────────────────────────────────
    // Add dock visibility toggles to View menu
    // ─────────────────────────────────────────────────────────────────────────
    QMenu* viewMenu = menuBar()->findChild<QMenu*>();
    for (QMenu* menu : menuBar()->findChildren<QMenu*>()) {
        if (menu->title() == tr("&View")) {
            viewMenu = menu;
            break;
        }
    }
    
    if (viewMenu) {
        viewMenu->addAction(m_outlinerDock->toggleViewAction());
        viewMenu->addAction(m_detailsDock->toggleViewAction());
        viewMenu->addAction(m_assetBrowserDock->toggleViewAction());
        
        // Viewport rendering toggles are added after engine initialization
        // (see setupViewportRenderingToggles called from initializeEngine)
    }
}

void EditorMainWindow::setupCentralWidget()
{
    // Create the native Qt window for Vulkan rendering
    m_viewportWindow = new QtViewportWindow();
    
    // Embed the QWindow into a QWidget container
    m_viewportContainer = QWidget::createWindowContainer(m_viewportWindow, this);
    m_viewportContainer->setMinimumSize(200, 200);
    m_viewportContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Connect viewport signals
    connect(m_viewportWindow, &QtViewportWindow::viewportReady, 
            this, &EditorMainWindow::onViewportReady);
    
    setCentralWidget(m_viewportContainer);
    
    std::cout << "[EditorMainWindow] Viewport widget created\n";
}

void EditorMainWindow::setupStatusBar()
{
    QStatusBar* status = statusBar();
    status->showMessage(tr("Initializing engine..."));
}

void EditorMainWindow::initializeEngine()
{
    m_engine = std::make_unique<QtEditorEngine>(this);
    
    // Set up viewport rendering toggles now that engine context is available
    setupViewportRenderingToggles();

    // Register all component inspector factories
    registerComponentInspectors();
    
    // Connect engine signals
    connect(m_engine.get(), &QtEditorEngine::engineInitialized,
            this, &EditorMainWindow::onEngineInitialized);
    connect(m_engine.get(), &QtEditorEngine::frameRendered,
            this, &EditorMainWindow::onFrameRendered);
    connect(m_engine.get(), &QtEditorEngine::engineError,
            this, &EditorMainWindow::onEngineError);
    
    // Start Vulkan initialization
    if (!m_engine->InitializeVulkan()) {
        statusBar()->showMessage(tr("Failed to initialize Vulkan"));
        return;
    }
    
    statusBar()->showMessage(tr("Vulkan initialized, waiting for viewport..."));
}

void EditorMainWindow::setupEditorCallbacks()
{
    if (!m_engine) return;
    
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;

    // When scene changes, rebuild outliner
    ctx->SetSceneChangedCallback([this]() {
        rebuildOutliner();
        clearDetailsPanel();
        updateWindowTitle();
    });

    // When selection changes, refresh details
    ctx->SetSelectionChangedCallback([this](entt::entity) {
        refreshDetailsPanel();
    });

    // When dirty flag changes, update title
    ctx->SetDirtyChangedCallback([this](bool) {
        updateWindowTitle();
    });
}

void EditorMainWindow::setupViewportRenderingToggles()
{
    if (!m_engine) return;
    
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;
    
    // Find the View menu
    QMenu* viewMenu = nullptr;
    for (QMenu* menu : menuBar()->findChildren<QMenu*>()) {
        if (menu->title() == tr("&View")) {
            viewMenu = menu;
            break;
        }
    }
    if (!viewMenu) return;
    
    viewMenu->addSeparator();
    QMenu* viewportMenu = viewMenu->addMenu(tr("Viewport Rendering"));
    
    auto& vs = ctx->GetViewportSettings();
    
    auto addToggle = [&](const QString& label, bool& setting) {
        QAction* action = viewportMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(setting);
        connect(action, &QAction::toggled, this, [&setting](bool checked) {
            setting = checked;
        });
    };
    
    addToggle(tr("Shadows"), vs.showShadows);
    addToggle(tr("Grid"), vs.showGrid);
    addToggle(tr("Gizmo"), vs.showGizmo);
    addToggle(tr("Selection Outline"), vs.showSelectionOutline);
    addToggle(tr("Skybox"), vs.showSkybox);
    addToggle(tr("Light Billboards"), vs.showLightBillboards);
}

void EditorMainWindow::onViewportReady()
{
    std::cout << "[EditorMainWindow] Viewport ready, attaching engine\n";
    
    if (!m_engine) {
        statusBar()->showMessage(tr("Engine not initialized"));
        return;
    }
    
    if (m_engine->AttachToViewport(m_viewportWindow)) {
        m_engine->StartRenderLoop();
        
        // Setup callbacks after engine is ready
        setupEditorCallbacks();
        setupProjectCallbacks();
        
        // Initial UI state
        rebuildOutliner();
        updateWindowTitle();
        
        statusBar()->showMessage(tr("Rendering started"));
    } else {
        statusBar()->showMessage(tr("Failed to attach to viewport"));
    }
}

void EditorMainWindow::onEngineInitialized(bool success)
{
    if (success) {
        statusBar()->showMessage(tr("Engine initialized successfully"));
    } else {
        statusBar()->showMessage(tr("Engine initialization failed"));
    }
}

void EditorMainWindow::onFrameRendered(float dt)
{
    m_lastDt = dt;
    m_frameCount++;
    
    // Update FPS display every 30 frames
    if (m_frameCount % 30 == 0) {
        float fps = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
        statusBar()->showMessage(QString("FPS: %1").arg(fps, 0, 'f', 1));
    }
}

void EditorMainWindow::onEngineError(const QString& message)
{
    statusBar()->showMessage(QString("Error: %1").arg(message));
    QMessageBox::warning(this, tr("Engine Error"), message);
}

// ─────────────────────────────────────────────────────────────────────────────
// Outliner
// ─────────────────────────────────────────────────────────────────────────────

void EditorMainWindow::rebuildOutliner()
{
    m_outlinerTree->clear();
    
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    
    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    
    // Create root item with scene name
    QTreeWidgetItem* sceneRoot = new QTreeWidgetItem(m_outlinerTree);
    sceneRoot->setText(0, QString::fromStdString(scene->GetName()));
    sceneRoot->setFlags(sceneRoot->flags() & ~Qt::ItemIsSelectable);
    sceneRoot->setExpanded(true);
    
    // Iterate all entities with TagComponent
    auto view = registry.view<TagComponent>();
    for (auto entity : view) {
        auto& tag = view.get<TagComponent>(entity);
        
        // Check if this entity has a parent (if it does, skip for now - we'll add via parent)
        bool hasParent = false;
        if (registry.all_of<TransformComponent>(entity)) {
            auto& transform = registry.get<TransformComponent>(entity);
            if (transform.parent != entt::null) {
                hasParent = true;
            }
        }
        
        if (!hasParent) {
            populateOutlinerItem(sceneRoot, entity);
        }
    }
}

void EditorMainWindow::populateOutlinerItem(QTreeWidgetItem* parent, entt::entity entity)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    
    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    
    // Get name
    std::string name = "Entity";
    if (registry.all_of<TagComponent>(entity)) {
        name = registry.get<TagComponent>(entity).tag;
    }
    
    QTreeWidgetItem* item = new QTreeWidgetItem(parent);
    item->setText(0, QString::fromStdString(name));
    item->setData(0, Qt::UserRole, static_cast<uint32_t>(entity));
    
    // Add children if this entity has any
    if (registry.all_of<TransformComponent>(entity)) {
        auto& transform = registry.get<TransformComponent>(entity);
        for (auto child : transform.children) {
            populateOutlinerItem(item, child);
        }
    }
    
    item->setExpanded(true);
}

void EditorMainWindow::onOutlinerSelectionChanged()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;
    
    QList<QTreeWidgetItem*> selected = m_outlinerTree->selectedItems();
    if (selected.isEmpty()) {
        ctx->ClearSelection();
        return;
    }
    
    QTreeWidgetItem* item = selected.first();
    QVariant data = item->data(0, Qt::UserRole);
    
    if (data.isValid()) {
        entt::entity entity = static_cast<entt::entity>(data.toUInt());
        ctx->SelectEntity(entity);
        std::cout << "[EditorMainWindow] Selected entity: " << static_cast<uint32_t>(entity) << "\n";
    } else {
        // Clicked on scene root or invalid item
        ctx->ClearSelection();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Details Panel
// ─────────────────────────────────────────────────────────────────────────────

void EditorMainWindow::blockDetailSignals(bool block)
{
    m_nameEdit->blockSignals(block);
    m_posX->blockSignals(block);
    m_posY->blockSignals(block);
    m_posZ->blockSignals(block);
    m_rotX->blockSignals(block);
    m_rotY->blockSignals(block);
    m_rotZ->blockSignals(block);
    m_scaleX->blockSignals(block);
    m_scaleY->blockSignals(block);
    m_scaleZ->blockSignals(block);
}

void EditorMainWindow::clearDetailsPanel()
{
    m_updatingDetails = true;
    blockDetailSignals(true);
    
    m_nameEdit->clear();
    m_nameEdit->setPlaceholderText(tr("(No selection)"));
    m_nameEdit->setEnabled(false);
    
    m_posX->setValue(0.0);
    m_posY->setValue(0.0);
    m_posZ->setValue(0.0);
    m_rotX->setValue(0.0);
    m_rotY->setValue(0.0);
    m_rotZ->setValue(0.0);
    m_scaleX->setValue(1.0);
    m_scaleY->setValue(1.0);
    m_scaleZ->setValue(1.0);
    
    m_transformGroup->setEnabled(false);

    m_entityIdLabel->setText(tr("(No selection)"));
    m_addComponentBtn->setEnabled(false);
    m_deleteEntityBtn->setEnabled(false);
    
    // Destroy legacy component inspectors
    if (m_lightInspector) {
        m_detailsLayout->removeWidget(m_lightInspector);
        delete m_lightInspector;
        m_lightInspector = nullptr;
    }
    if (m_environmentInspector) {
        m_detailsLayout->removeWidget(m_environmentInspector);
        delete m_environmentInspector;
        m_environmentInspector = nullptr;
    }

    // Destroy registry-based component inspectors
    for (auto* insp : m_componentInspectors) {
        m_detailsLayout->removeWidget(insp);
        delete insp;
    }
    m_componentInspectors.clear();

    blockDetailSignals(false);
    m_updatingDetails = false;
}

void EditorMainWindow::refreshDetailsPanel()
{
    if (!m_engine) {
        clearDetailsPanel();
        return;
    }
    
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) {
        clearDetailsPanel();
        return;
    }

    // Lock support: if locked, show the locked entity instead of current selection
    entt::entity entity = entt::null;
    if (m_detailsLocked && m_lockedEntity != entt::null) {
        entity = m_lockedEntity;
    } else if (ctx->HasSelection()) {
        entity = ctx->GetSelectedEntity();
    } else {
        clearDetailsPanel();
        return;
    }
    
    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    
    if (!registry.valid(entity)) {
        if (m_detailsLocked) {
            // Locked entity was deleted — unlock
            m_detailsLocked = false;
            m_lockedEntity = entt::null;
            m_lockCheck->blockSignals(true);
            m_lockCheck->setChecked(false);
            m_lockCheck->blockSignals(false);
        }
        clearDetailsPanel();
        return;
    }
    
    m_updatingDetails = true;
    blockDetailSignals(true);
    
    // Entity ID
    m_entityIdLabel->setText(QString("#%1").arg(static_cast<uint32_t>(entity)));

    // Enable action buttons
    m_addComponentBtn->setEnabled(true);
    m_deleteEntityBtn->setEnabled(true);

    // Name
    m_nameEdit->setEnabled(true);
    if (registry.all_of<TagComponent>(entity)) {
        auto& tag = registry.get<TagComponent>(entity);
        m_nameEdit->setText(QString::fromStdString(tag.tag));
    } else {
        m_nameEdit->setText(QString("Entity %1").arg(static_cast<uint32_t>(entity)));
    }
    
    // Transform
    m_transformGroup->setEnabled(true);
    if (registry.all_of<TransformComponent>(entity)) {
        auto& transform = registry.get<TransformComponent>(entity);
        
        m_posX->setValue(transform.position.x);
        m_posY->setValue(transform.position.y);
        m_posZ->setValue(transform.position.z);
        
        // Convert radians to degrees for display
        constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979f;
        m_rotX->setValue(transform.rotation.x * RAD_TO_DEG);
        m_rotY->setValue(transform.rotation.y * RAD_TO_DEG);
        m_rotZ->setValue(transform.rotation.z * RAD_TO_DEG);
        
        m_scaleX->setValue(transform.scale.x);
        m_scaleY->setValue(transform.scale.y);
        m_scaleZ->setValue(transform.scale.z);
    } else {
        // No transform component - show defaults
        m_posX->setValue(0.0);
        m_posY->setValue(0.0);
        m_posZ->setValue(0.0);
        m_rotX->setValue(0.0);
        m_rotY->setValue(0.0);
        m_rotZ->setValue(0.0);
        m_scaleX->setValue(1.0);
        m_scaleY->setValue(1.0);
        m_scaleZ->setValue(1.0);
        m_transformGroup->setEnabled(false);
    }
    
    blockDetailSignals(false);
    m_updatingDetails = false;

    // ── Rebuild all component inspectors ─────────────────────────────────
    rebuildComponentInspectors();
}

void EditorMainWindow::onNameChanged(const QString& text)
{
    if (m_updatingDetails || !m_engine) return;
    
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene() || !ctx->HasSelection()) return;
    
    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    entt::entity entity = ctx->GetSelectedEntity();
    
    if (!registry.valid(entity)) return;
    
    // Update TagComponent
    if (registry.all_of<TagComponent>(entity)) {
        auto& tag = registry.get<TagComponent>(entity);
        tag.tag = text.toStdString();
    } else {
        registry.emplace<TagComponent>(entity, text.toStdString());
    }
    
    ctx->MarkDirty();
    
    // Update outliner display
    QList<QTreeWidgetItem*> selected = m_outlinerTree->selectedItems();
    if (!selected.isEmpty()) {
        selected.first()->setText(0, text);
    }
}

void EditorMainWindow::onPositionChanged()
{
    if (m_updatingDetails || !m_engine) return;
    
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene() || !ctx->HasSelection()) return;
    
    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    entt::entity entity = ctx->GetSelectedEntity();
    
    if (!registry.valid(entity) || !registry.all_of<TransformComponent>(entity)) return;

    EditorUndoManager::PushSnapshot(ctx, "Move entity");
    
    auto& transform = registry.get<TransformComponent>(entity);
    transform.position.x = static_cast<float>(m_posX->value());
    transform.position.y = static_cast<float>(m_posY->value());
    transform.position.z = static_cast<float>(m_posZ->value());
    
    ctx->MarkDirty();
}

void EditorMainWindow::onRotationChanged()
{
    if (m_updatingDetails || !m_engine) return;
    
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene() || !ctx->HasSelection()) return;
    
    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    entt::entity entity = ctx->GetSelectedEntity();
    
    if (!registry.valid(entity) || !registry.all_of<TransformComponent>(entity)) return;

    EditorUndoManager::PushSnapshot(ctx, "Rotate entity");
    
    auto& transform = registry.get<TransformComponent>(entity);
    
    // Convert degrees to radians
    constexpr float DEG_TO_RAD = 3.14159265358979f / 180.0f;
    transform.rotation.x = static_cast<float>(m_rotX->value()) * DEG_TO_RAD;
    transform.rotation.y = static_cast<float>(m_rotY->value()) * DEG_TO_RAD;
    transform.rotation.z = static_cast<float>(m_rotZ->value()) * DEG_TO_RAD;
    
    ctx->MarkDirty();
}

void EditorMainWindow::onScaleChanged()
{
    if (m_updatingDetails || !m_engine) return;
    
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene() || !ctx->HasSelection()) return;
    
    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    entt::entity entity = ctx->GetSelectedEntity();
    
    if (!registry.valid(entity) || !registry.all_of<TransformComponent>(entity)) return;

    EditorUndoManager::PushSnapshot(ctx, "Scale entity");
    
    auto& transform = registry.get<TransformComponent>(entity);
    transform.scale.x = static_cast<float>(m_scaleX->value());
    transform.scale.y = static_cast<float>(m_scaleY->value());
    transform.scale.z = static_cast<float>(m_scaleZ->value());
    
    ctx->MarkDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Window Title
// ─────────────────────────────────────────────────────────────────────────────

void EditorMainWindow::updateWindowTitle()
{
    QString title = "Hephaestus Engine Editor (Qt)";
    
    // Add project name if a project is open
    if (m_projectContext && m_projectContext->HasProject()) {
        title += " - " + QString::fromStdString(m_projectContext->GetProjectName());
    }
    
    // Add scene info
    if (m_engine) {
        auto* ctx = m_engine->GetEditorContext();
        if (ctx && ctx->HasActiveScene()) {
            Scene* scene = ctx->GetActiveScene();
            title += " [" + QString::fromStdString(scene->GetName()) + "]";
            
            if (ctx->IsDirty()) {
                title += " *";
            }
        }
    }
    
    setWindowTitle(title);
}

// ─────────────────────────────────────────────────────────────────────────────
// File Menu Slots
// ─────────────────────────────────────────────────────────────────────────────

void EditorMainWindow::onNewLevel()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;
    
    // Check for unsaved changes
    if (ctx->IsDirty()) {
        auto result = QMessageBox::question(this, tr("Unsaved Changes"),
            tr("The current scene has unsaved changes. Do you want to save before creating a new level?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        
        if (result == QMessageBox::Save) {
            onSaveLevel();
            if (ctx->IsDirty()) return; // Save was cancelled or failed
        } else if (result == QMessageBox::Cancel) {
            return;
        }
    }
    
    ctx->NewScene("New Scene");
    statusBar()->showMessage(tr("New level created"), 3000);
}

void EditorMainWindow::onOpenLevel()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;
    
    // Check for unsaved changes
    if (ctx->IsDirty()) {
        auto result = QMessageBox::question(this, tr("Unsaved Changes"),
            tr("The current scene has unsaved changes. Do you want to save before opening another level?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        
        if (result == QMessageBox::Save) {
            onSaveLevel();
            if (ctx->IsDirty()) return;
        } else if (result == QMessageBox::Cancel) {
            return;
        }
    }
    
    QString filepath = QFileDialog::getOpenFileName(this, tr("Open Level"),
        QString::fromStdString("assets/scenes"),
        tr("Scene Files (*.hpscene *.hescene);;All Files (*)"));
    
    if (filepath.isEmpty()) return;
    
    if (ctx->LoadScene(filepath.toStdString())) {
        // Rebuild mesh library for the new scene
        m_engine->RebuildSceneMeshes();
        statusBar()->showMessage(tr("Level loaded: %1").arg(filepath), 3000);
    } else {
        QMessageBox::warning(this, tr("Open Level Failed"),
            tr("Failed to load level from: %1").arg(filepath));
    }
}

void EditorMainWindow::onSaveLevel()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    
    if (ctx->GetScenePath().empty()) {
        // No existing path, use Save As
        onSaveLevelAs();
        return;
    }
    
    if (ctx->SaveScene()) {
        statusBar()->showMessage(tr("Level saved"), 3000);
    } else {
        QMessageBox::warning(this, tr("Save Failed"),
            tr("Failed to save the level."));
    }
}

void EditorMainWindow::onSaveLevelAs()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    
    QString filepath = QFileDialog::getSaveFileName(this, tr("Save Level As"),
        QString::fromStdString("assets/scenes/" + ctx->GetActiveScene()->GetName() + ".hpscene"),
        tr("Scene Files (*.hpscene);;All Files (*)"));
    
    if (filepath.isEmpty()) return;
    
    // Ensure .hpscene extension
    if (!filepath.endsWith(".hpscene", Qt::CaseInsensitive)) {
        filepath += ".hpscene";
    }
    
    if (ctx->SaveSceneAs(filepath.toStdString())) {
        statusBar()->showMessage(tr("Level saved: %1").arg(filepath), 3000);
    } else {
        QMessageBox::warning(this, tr("Save Failed"),
            tr("Failed to save the level to: %1").arg(filepath));
    }
}

void EditorMainWindow::onExit()
{
    close();
}

// ─────────────────────────────────────────────────────────────────────────────
// Project Menu Slots
// ─────────────────────────────────────────────────────────────────────────────

bool EditorMainWindow::promptSaveDirtyScene()
{
    if (!m_engine) return true;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->IsDirty()) return true;

    auto result = QMessageBox::question(this, tr("Unsaved Changes"),
        tr("The current scene has unsaved changes. Do you want to save before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (result == QMessageBox::Save) {
        onSaveLevel();
        // If still dirty, save was cancelled or failed
        return !ctx->IsDirty();
    } else if (result == QMessageBox::Cancel) {
        return false;
    }
    // Discard - continue without saving
    return true;
}

void EditorMainWindow::onNewProject()
{
    // TODO: Implement project creation wizard
    QMessageBox::information(this, tr("New Project"),
        tr("Project creation is not yet implemented.\n\n"
           "To create a project manually:\n"
           "1. Create a folder for your project\n"
           "2. Create a 'project.heproject' file with JSON:\n"
           "   {\n"
           "     \"formatVersion\": 1,\n"
           "     \"name\": \"YourProjectName\",\n"
           "     \"description\": \"...\",\n"
           "     \"engineVersion\": \"1.0.0\"\n"
           "   }\n"
           "3. Use File → Open Project... to open it"));
}

void EditorMainWindow::onOpenProject()
{
    // Prompt to save dirty scene
    if (!promptSaveDirtyScene()) {
        return;
    }

    // Open file dialog
    QString startDir = QDir::currentPath();
    if (m_projectContext && m_projectContext->HasProject()) {
        startDir = QString::fromStdString(m_projectContext->GetProjectRootDir());
    }

    QString filepath = QFileDialog::getOpenFileName(this, tr("Open Project"),
        startDir,
        tr("Hephaestus Project (*.heproject);;All Files (*)"));

    if (filepath.isEmpty()) {
        return;
    }

    // Load the project
    std::string error;
    if (m_projectContext->LoadProject(filepath.toStdString(), error)) {
        statusBar()->showMessage(tr("Project opened: %1").arg(
            QString::fromStdString(m_projectContext->GetProjectName())), 5000);
        
        // Update UI
        updateWindowTitle();
        refreshAssetBrowser();
        rebuildRecentProjectsMenu();
        
        // Load last scene if available
        const std::string& lastScene = m_projectContext->GetLastScene();
        if (!lastScene.empty() && m_engine) {
            auto* ctx = m_engine->GetEditorContext();
            if (ctx) {
                // Try to load relative to project root
                std::filesystem::path scenePath = 
                    std::filesystem::path(m_projectContext->GetProjectRootDir()) / lastScene;
                if (std::filesystem::exists(scenePath)) {
                    ctx->LoadScene(scenePath.string());
                }
            }
        }
    } else {
        QMessageBox::critical(this, tr("Open Project Failed"),
            tr("Failed to open project:\n%1").arg(QString::fromStdString(error)));
    }
}

void EditorMainWindow::onCloseProject()
{
    if (!m_projectContext || !m_projectContext->HasProject()) {
        return;
    }

    // Prompt to save dirty scene
    if (!promptSaveDirtyScene()) {
        return;
    }

    // Close the project
    m_projectContext->CloseProject();

    // Clear the scene
    if (m_engine) {
        auto* ctx = m_engine->GetEditorContext();
        if (ctx) {
            ctx->NewScene("New Scene");
        }
    }

    // Update UI
    updateWindowTitle();
    refreshAssetBrowser();
    
    statusBar()->showMessage(tr("Project closed"), 3000);
}

void EditorMainWindow::onOpenRecentProject()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) return;

    QString projectPath = action->data().toString();
    if (projectPath.isEmpty()) return;

    // Prompt to save dirty scene
    if (!promptSaveDirtyScene()) {
        return;
    }

    // Check if file exists
    if (!std::filesystem::exists(projectPath.toStdString())) {
        QMessageBox::warning(this, tr("Project Not Found"),
            tr("The project file no longer exists:\n%1\n\n"
               "It will be removed from recent projects.").arg(projectPath));
        
        // Remove from recent list (by reloading without this entry)
        // For now, user can clear recent projects
        return;
    }

    // Load the project
    std::string error;
    if (m_projectContext->LoadProject(projectPath.toStdString(), error)) {
        statusBar()->showMessage(tr("Project opened: %1").arg(
            QString::fromStdString(m_projectContext->GetProjectName())), 5000);
        
        // Update UI
        updateWindowTitle();
        refreshAssetBrowser();
        rebuildRecentProjectsMenu();
        
        // Load last scene if available
        const std::string& lastScene = m_projectContext->GetLastScene();
        if (!lastScene.empty() && m_engine) {
            auto* ctx = m_engine->GetEditorContext();
            if (ctx) {
                std::filesystem::path scenePath = 
                    std::filesystem::path(m_projectContext->GetProjectRootDir()) / lastScene;
                if (std::filesystem::exists(scenePath)) {
                    ctx->LoadScene(scenePath.string());
                }
            }
        }
    } else {
        QMessageBox::critical(this, tr("Open Project Failed"),
            tr("Failed to open project:\n%1").arg(QString::fromStdString(error)));
    }
}

void EditorMainWindow::onClearRecentProjects()
{
    auto result = QMessageBox::question(this, tr("Clear Recent Projects"),
        tr("Are you sure you want to clear the recent projects list?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result == QMessageBox::Yes) {
        m_projectContext->ClearRecentProjects();
        rebuildRecentProjectsMenu();
        statusBar()->showMessage(tr("Recent projects cleared"), 2000);
    }
}

void EditorMainWindow::rebuildRecentProjectsMenu()
{
    if (!m_recentProjectsMenu) return;

    m_recentProjectsMenu->clear();

    const auto& recentProjects = m_projectContext->GetRecentProjects();
    
    if (recentProjects.empty()) {
        QAction* emptyAction = m_recentProjectsMenu->addAction(tr("(No Recent Projects)"));
        emptyAction->setEnabled(false);
    } else {
        for (const auto& projectPath : recentProjects) {
            // Extract project name from path for display
            std::filesystem::path path(projectPath);
            QString displayName = QString::fromStdString(path.parent_path().filename().string());
            if (displayName.isEmpty()) {
                displayName = QString::fromStdString(path.filename().string());
            }
            
            QAction* action = m_recentProjectsMenu->addAction(displayName);
            action->setToolTip(QString::fromStdString(projectPath));
            action->setData(QString::fromStdString(projectPath));
            connect(action, &QAction::triggered, this, &EditorMainWindow::onOpenRecentProject);
        }
        
        m_recentProjectsMenu->addSeparator();
        
        QAction* clearAction = m_recentProjectsMenu->addAction(tr("Clear Recent Projects"));
        connect(clearAction, &QAction::triggered, this, &EditorMainWindow::onClearRecentProjects);
    }
}

void EditorMainWindow::refreshAssetBrowser()
{
    if (!m_assetBrowser) return;

    // Update the project context reference
    m_assetBrowser->SetProjectContext(m_projectContext.get());
    
    // Refresh the browser (will update from AssetManager)
    m_assetBrowser->Refresh();
}

void EditorMainWindow::setupProjectCallbacks()
{
    if (!m_projectContext) return;

    m_projectContext->SetProjectChangedCallback([this]() {
        updateWindowTitle();
        refreshAssetBrowser();
    });

    // Set up scene loading callback for asset actions
    if (m_assetActions) {
        m_assetActions->SetLoadSceneCallback([this](const QString& absolutePath) -> bool {
            if (!m_engine) return false;
            
            auto* ctx = m_engine->GetEditorContext();
            if (!ctx) return false;

            // Check for unsaved changes
            if (ctx->IsDirty()) {
                auto result = QMessageBox::question(this, tr("Unsaved Changes"),
                    tr("The current scene has unsaved changes. Save before loading new scene?"),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                    QMessageBox::Save);

                if (result == QMessageBox::Save) {
                    onSaveLevel();
                    if (ctx->IsDirty()) return false; // Save cancelled
                } else if (result == QMessageBox::Cancel) {
                    return false;
                }
            }

            // Load the scene
            if (ctx->LoadScene(absolutePath.toStdString())) {
                // Rebuild mesh library for the new scene
                m_engine->RebuildSceneMeshes();
                statusBar()->showMessage(tr("Scene loaded: %1").arg(absolutePath), 3000);
                return true;
            } else {
                QMessageBox::warning(this, tr("Load Failed"),
                    tr("Failed to load scene: %1").arg(absolutePath));
                return false;
            }
        });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Edit Menu Slots
// ─────────────────────────────────────────────────────────────────────────────

void EditorMainWindow::onUndo()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;

    std::string error;
    if (EditorUndoManager::Undo(ctx, error)) {
        rebuildOutliner();
        refreshDetailsPanel();
        statusBar()->showMessage(tr("Undo"), 2000);
    } else {
        statusBar()->showMessage(QString::fromStdString("Undo: " + error), 2000);
    }
}

void EditorMainWindow::onRedo()
{
    // Redo not yet implemented (the engine uses a LIFO-only undo stack)
    statusBar()->showMessage(tr("Redo (not yet implemented)"), 2000);
}

// ─────────────────────────────────────────────────────────────────────────────
// Help Menu Slots
// ─────────────────────────────────────────────────────────────────────────────

void EditorMainWindow::onAbout()
{
    QMessageBox::about(this, tr("About Hephaestus Engine Editor"),
        tr("<h2>Hephaestus Engine Editor</h2>"
           "<p>Version 1.0.0 (Qt UI)</p>"
           "<p>A 3D game engine editor built with Qt Widgets.</p>"
           "<p>Vulkan rendering integrated via EngineFrameRenderer.</p>"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Component Inspector Registry + Helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Register all known component inspector factories.
 * Called once during engine initialization.
 * To add a new component inspector, add a Register() call here.
 */
void EditorMainWindow::registerComponentInspectors()
{
    auto& reg = ComponentInspectorRegistry::Instance();
    reg.Clear();

    // ── MeshRenderer (order 10) ─────────────────────────────────────────
    reg.Register("MeshRenderer", 10,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<MeshRendererComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new MeshRendererInspector(ctx, e, parent);
        });

    // ── MeshCollider (order 20) ─────────────────────────────────────────
    reg.Register("MeshCollider", 20,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<MeshColliderComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new MeshColliderInspector(ctx, e, parent);
        });

    // ── RigidBody (order 30) ────────────────────────────────────────────
    reg.Register("RigidBody", 30,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<RigidBodyComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new RigidBodyInspector(ctx, e, parent);
        });

    // ── PhysicsShape (order 35) ─────────────────────────────────────────
    reg.Register("PhysicsShape", 35,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<PhysicsShapeComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new PhysicsShapeInspector(ctx, e, parent);
        });

    // ── Scripts (C++) (order 40) ────────────────────────────────────────
    reg.Register("Scripts", 40,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<ScriptComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new ScriptInspector(ctx, e, parent);
        });

    // ── LuaScript (order 45) ────────────────────────────────────────────
    reg.Register("LuaScript", 45,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<LuaScriptComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new LuaScriptInspector(ctx, e, parent);
        });

    // ── Camera (order 50) ───────────────────────────────────────────────
    reg.Register("Camera", 50,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<CameraComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new CameraInspector(ctx, e, parent);
        });

    // NOTE: LightInspector and EnvironmentInspector are still created
    // via the legacy path (m_lightInspector / m_environmentInspector)
    // because they pre-date the registry.  They could be migrated here
    // in a future refactor.
}

/**
 * Rebuild all component inspectors for the currently viewed entity.
 * Removes old inspector widgets, creates new ones via the registry,
 * and also creates legacy Light/Environment inspectors.
 */
void EditorMainWindow::rebuildComponentInspectors()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;

    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();

    entt::entity entity = entt::null;
    if (m_detailsLocked && m_lockedEntity != entt::null)
        entity = m_lockedEntity;
    else if (ctx->HasSelection())
        entity = ctx->GetSelectedEntity();

    if (entity == entt::null || !registry.valid(entity)) return;

    // --- Remove old inspectors ---
    if (m_lightInspector) {
        m_detailsLayout->removeWidget(m_lightInspector);
        delete m_lightInspector;
        m_lightInspector = nullptr;
    }
    if (m_environmentInspector) {
        m_detailsLayout->removeWidget(m_environmentInspector);
        delete m_environmentInspector;
        m_environmentInspector = nullptr;
    }
    for (auto* insp : m_componentInspectors) {
        m_detailsLayout->removeWidget(insp);
        delete insp;
    }
    m_componentInspectors.clear();

    // Find the stretch item index (insert before the final stretch)
    int stretchIndex = m_detailsLayout->count(); // fallback: end
    for (int i = 0; i < m_detailsLayout->count(); ++i) {
        QLayoutItem* item = m_detailsLayout->itemAt(i);
        if (item && item->spacerItem()) {
            stretchIndex = i;
            break;
        }
    }

    // --- Legacy inspectors (Light, Environment) ---
    if (registry.all_of<LightComponent>(entity)) {
        m_lightInspector = new LightInspector(ctx, entity, m_detailsWidget);
        m_detailsLayout->insertWidget(stretchIndex, m_lightInspector);
        ++stretchIndex;
    }
    if (registry.all_of<EnvironmentComponent>(entity)) {
        m_environmentInspector = new EnvironmentInspector(ctx, entity, m_detailsWidget);
        m_detailsLayout->insertWidget(stretchIndex, m_environmentInspector);
        ++stretchIndex;
    }

    // --- Registry-based inspectors ---
    auto inspectors = ComponentInspectorRegistry::Instance().CreateInspectors(
        ctx, entity, registry, m_detailsWidget);

    for (auto* insp : inspectors) {
        // Wire up the componentRemoved signal so the panel refreshes
        connect(insp, &ComponentInspector::componentRemoved,
                this, &EditorMainWindow::onComponentRemoved);

        m_detailsLayout->insertWidget(stretchIndex, insp);
        ++stretchIndex;
        m_componentInspectors.push_back(insp);
    }
}

/**
 * Called when a component inspector removes (or adds) a component.
 * Refreshes the entire Details panel to reflect the new component set.
 */
void EditorMainWindow::onComponentRemoved(entt::entity /*entity*/, const QString& /*name*/)
{
    // Defer refresh to avoid deleting the inspector while its signal is live
    QMetaObject::invokeMethod(this, [this]() {
        refreshDetailsPanel();
        rebuildOutliner();
    }, Qt::QueuedConnection);
}

/**
 * Show a popup menu listing components that can be added to the selected entity.
 * Only shows components the entity doesn't already have.
 */
void EditorMainWindow::showAddComponentMenu()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene() || !ctx->HasSelection()) return;

    auto& reg = ctx->GetActiveScene()->GetRegistry();
    entt::entity entity = ctx->GetSelectedEntity();
    if (!reg.valid(entity)) return;

    QMenu menu(tr("Add Component"), this);

    // MeshRenderer
    if (!reg.all_of<MeshRendererComponent>(entity)) {
        menu.addAction(tr("Mesh Renderer"), [this, ctx, entity, &reg]() {
            EditorUndoManager::PushSnapshot(ctx, "Add MeshRenderer");
            reg.emplace<MeshRendererComponent>(entity);
            if (!reg.all_of<MaterialComponent>(entity))
                reg.emplace<MaterialComponent>(entity);
            ctx->MarkDirty();
            refreshDetailsPanel();
        });
    }

    // Light
    if (!reg.all_of<LightComponent>(entity)) {
        menu.addAction(tr("Light"), [this, ctx, entity, &reg]() {
            EditorUndoManager::PushSnapshot(ctx, "Add Light");
            reg.emplace<LightComponent>(entity);
            ctx->MarkDirty();
            refreshDetailsPanel();
        });
    }

    // Camera
    if (!reg.all_of<CameraComponent>(entity)) {
        menu.addAction(tr("Camera"), [this, ctx, entity, &reg]() {
            EditorUndoManager::PushSnapshot(ctx, "Add Camera");
            reg.emplace<CameraComponent>(entity);
            ctx->MarkDirty();
            refreshDetailsPanel();
        });
    }

    menu.addSeparator();

    // Rigid Body
    if (!reg.all_of<RigidBodyComponent>(entity)) {
        menu.addAction(tr("Rigid Body"), [this, ctx, entity, &reg]() {
            EditorUndoManager::PushSnapshot(ctx, "Add RigidBody");
            reg.emplace<RigidBodyComponent>(entity);
            // Auto-add PhysicsShape if missing
            if (!reg.all_of<PhysicsShapeComponent>(entity))
                reg.emplace<PhysicsShapeComponent>(entity);
            ctx->MarkDirty();
            refreshDetailsPanel();
        });
    }

    // Physics Shape
    if (!reg.all_of<PhysicsShapeComponent>(entity)) {
        menu.addAction(tr("Physics Shape"), [this, ctx, entity, &reg]() {
            EditorUndoManager::PushSnapshot(ctx, "Add PhysicsShape");
            reg.emplace<PhysicsShapeComponent>(entity);
            ctx->MarkDirty();
            refreshDetailsPanel();
        });
    }

    // Mesh Collider
    if (!reg.all_of<MeshColliderComponent>(entity)) {
        menu.addAction(tr("Mesh Collider"), [this, ctx, entity, &reg]() {
            EditorUndoManager::PushSnapshot(ctx, "Add MeshCollider");
            reg.emplace<MeshColliderComponent>(entity);
            ctx->MarkDirty();
            refreshDetailsPanel();
        });
    }

    menu.addSeparator();

    // Scripts (C++)
    if (!reg.all_of<ScriptComponent>(entity)) {
        menu.addAction(tr("Script Component"), [this, ctx, entity, &reg]() {
            EditorUndoManager::PushSnapshot(ctx, "Add ScriptComponent");
            reg.emplace<ScriptComponent>(entity);
            ctx->MarkDirty();
            refreshDetailsPanel();
        });
    }

    // Lua Script
    if (!reg.all_of<LuaScriptComponent>(entity)) {
        menu.addAction(tr("Lua Script"), [this, ctx, entity, &reg]() {
            EditorUndoManager::PushSnapshot(ctx, "Add LuaScript");
            reg.emplace<LuaScriptComponent>(entity);
            ctx->MarkDirty();
            refreshDetailsPanel();
        });
    }

    menu.addSeparator();

    // Environment (typically only one per scene, but allow adding)
    if (!reg.all_of<EnvironmentComponent>(entity)) {
        menu.addAction(tr("Environment"), [this, ctx, entity, &reg]() {
            EditorUndoManager::PushSnapshot(ctx, "Add Environment");
            reg.emplace<EnvironmentComponent>(entity);
            ctx->MarkDirty();
            refreshDetailsPanel();
        });
    }

    if (menu.isEmpty()) {
        menu.addAction(tr("(All components already present)"))->setEnabled(false);
    }

    menu.exec(m_addComponentBtn->mapToGlobal(QPoint(0, m_addComponentBtn->height())));
}

/**
 * Handle an asset drag-drop onto a Details panel field.
 * Validates asset type and assigns to the appropriate component.
 */
void EditorMainWindow::handleDragDrop(const QString& assetPath, const QString& targetField)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene() || !ctx->HasSelection()) return;

    auto& reg = ctx->GetActiveScene()->GetRegistry();
    entt::entity entity = ctx->GetSelectedEntity();
    if (!reg.valid(entity)) return;

    if (targetField == "lua_script" && assetPath.endsWith(".lua", Qt::CaseInsensitive)) {
        EditorUndoManager::PushSnapshot(ctx, "Drag-drop lua script");
        if (!reg.all_of<LuaScriptComponent>(entity)) {
            reg.emplace<LuaScriptComponent>(entity, assetPath.toStdString());
        } else {
            reg.get<LuaScriptComponent>(entity).scriptPath = assetPath.toStdString();
        }
        ctx->MarkDirty();
        refreshDetailsPanel();
    } else if (targetField == "mesh" &&
               (assetPath.endsWith(".obj", Qt::CaseInsensitive) ||
                assetPath.endsWith(".fbx", Qt::CaseInsensitive) ||
                assetPath.endsWith(".gltf", Qt::CaseInsensitive) ||
                assetPath.endsWith(".glb", Qt::CaseInsensitive))) {
        EditorUndoManager::PushSnapshot(ctx, "Drag-drop mesh");
        if (!reg.all_of<MeshRendererComponent>(entity)) {
            reg.emplace<MeshRendererComponent>(entity);
        }
        reg.get<MeshRendererComponent>(entity).meshAssetPath = assetPath.toStdString();
        ctx->MarkDirty();
        refreshDetailsPanel();
    } else if (targetField.startsWith("material_slot_") &&
               (assetPath.endsWith(".hematerial", Qt::CaseInsensitive) ||
                assetPath.endsWith(".hpmat", Qt::CaseInsensitive) ||
                assetPath.endsWith(".material", Qt::CaseInsensitive) ||
                assetPath.endsWith(".mat", Qt::CaseInsensitive))) {
        bool ok = false;
        int slot = targetField.mid(14).toInt(&ok);
        if (!ok || !reg.all_of<MeshRendererComponent>(entity)) return;

        EditorUndoManager::PushSnapshot(ctx, "Drag-drop material");
        auto& mr = reg.get<MeshRendererComponent>(entity);
        while (static_cast<int>(mr.appliedMaterialPaths.size()) <= slot) {
            mr.appliedMaterialPaths.push_back("");
        }
        mr.appliedMaterialPaths[slot] = assetPath.toStdString();
        ctx->MarkDirty();
        refreshDetailsPanel();
    } else {
        statusBar()->showMessage(
            tr("Cannot drop '%1' onto '%2'").arg(assetPath, targetField), 3000);
    }
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
