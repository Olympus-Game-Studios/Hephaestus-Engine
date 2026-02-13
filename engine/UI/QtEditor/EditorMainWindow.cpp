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
#include "../../Assets/AssetTypes.h"
#include "../../Assets/ModelImporter.h"
#include "../../Assets/MeshAsset.h"
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
#include <QToolBar>
#include <QToolButton>
#include <QActionGroup>
#include <QFrame>
#include <QGridLayout>
#include <QStyle>
#include <QMenu>
#include <QCursor>
#include <QPainter>
#include <iostream>
#include <cmath>
#include <filesystem>

namespace Hephaestus {
namespace QtEditor {

static QIcon makeTintedIcon(const QIcon& source, const QSize& size, const QColor& tint)
{
    QPixmap base = source.pixmap(size);
    if (base.isNull()) {
        return source;
    }

    QPixmap tinted(base.size());
    tinted.fill(Qt::transparent);

    QPainter painter(&tinted);
    painter.drawPixmap(0, 0, base);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), tint);
    painter.end();

    return QIcon(tinted);
}

static QIcon makePlayGlyphIcon(const QSize& size, const QColor& color)
{
    QPixmap pix(size);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const qreal w = static_cast<qreal>(size.width());
    const qreal h = static_cast<qreal>(size.height());
    QPolygonF triangle;
    triangle << QPointF(w * 0.34, h * 0.26)
             << QPointF(w * 0.34, h * 0.74)
             << QPointF(w * 0.74, h * 0.50);
    painter.drawPolygon(triangle);
    return QIcon(pix);
}

static QIcon makePauseGlyphIcon(const QSize& size, const QColor& color)
{
    QPixmap pix(size);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const qreal w = static_cast<qreal>(size.width());
    const qreal h = static_cast<qreal>(size.height());
    const QRectF left(w * 0.30, h * 0.24, w * 0.14, h * 0.52);
    const QRectF right(w * 0.56, h * 0.24, w * 0.14, h * 0.52);
    painter.drawRoundedRect(left, 1.2, 1.2);
    painter.drawRoundedRect(right, 1.2, 1.2);
    return QIcon(pix);
}

static QIcon makeStopGlyphIcon(const QSize& size, const QColor& color)
{
    QPixmap pix(size);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const qreal w = static_cast<qreal>(size.width());
    const qreal h = static_cast<qreal>(size.height());
    const QRectF square(w * 0.30, h * 0.30, w * 0.40, h * 0.40);
    painter.drawRoundedRect(square, 1.0, 1.0);
    return QIcon(pix);
}

static QIcon makeUndoGlyphIcon(const QSize& size, const QColor& color)
{
    QPixmap pix(size);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const qreal w = static_cast<qreal>(size.width());
    const qreal h = static_cast<qreal>(size.height());
    QPolygonF tri;
    tri << QPointF(w * 0.22, h * 0.50)
        << QPointF(w * 0.44, h * 0.28)
        << QPointF(w * 0.44, h * 0.72);
    painter.drawPolygon(tri);

    const QRectF body(w * 0.43, h * 0.44, w * 0.36, h * 0.12);
    painter.drawRoundedRect(body, 1.0, 1.0);
    return QIcon(pix);
}

static QIcon makeRedoGlyphIcon(const QSize& size, const QColor& color)
{
    QPixmap pix(size);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const qreal w = static_cast<qreal>(size.width());
    const qreal h = static_cast<qreal>(size.height());
    QPolygonF tri;
    tri << QPointF(w * 0.78, h * 0.50)
        << QPointF(w * 0.56, h * 0.28)
        << QPointF(w * 0.56, h * 0.72);
    painter.drawPolygon(tri);

    const QRectF body(w * 0.21, h * 0.44, w * 0.36, h * 0.12);
    painter.drawRoundedRect(body, 1.0, 1.0);
    return QIcon(pix);
}

static QIcon makeSaveGlyphIcon(const QSize& size, const QColor& color)
{
    QPixmap pix(size);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const qreal w = static_cast<qreal>(size.width());
    const qreal h = static_cast<qreal>(size.height());

    // Floppy body
    const QRectF body(w * 0.24, h * 0.20, w * 0.52, h * 0.60);
    painter.drawRoundedRect(body, 1.2, 1.2);

    // Label cutout
    painter.setBrush(QColor(0, 0, 0, 0));
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    const QRectF labelCutout(w * 0.34, h * 0.29, w * 0.28, h * 0.16);
    painter.drawRoundedRect(labelCutout, 0.8, 0.8);

    // Notch cutout
    const QRectF notchCutout(w * 0.58, h * 0.54, w * 0.12, h * 0.16);
    painter.drawRoundedRect(notchCutout, 0.8, 0.8);

    painter.end();
    return QIcon(pix);
}

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
    setupTopToolbar();
    setupDockWidgets();
    setupCentralWidget();
    setupStatusBar();
    applyUnifiedStyle();

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

void EditorMainWindow::setupTopToolbar()
{
    m_topToolbar = new QToolBar(tr("Editor Toolbar"), this);
    m_topToolbar->setObjectName("EditorTopToolbar");
    m_topToolbar->setMovable(false);
    m_topToolbar->setFloatable(false);
    m_topToolbar->setIconSize(QSize(16, 16));
    addToolBar(Qt::TopToolBarArea, m_topToolbar);

    auto makeSection = [this](const QString& name) {
        QWidget* section = new QWidget(this);
        section->setObjectName(name);
        auto* layout = new QHBoxLayout(section);
        layout->setContentsMargins(8, 4, 8, 4);
        layout->setSpacing(6);
        return std::pair<QWidget*, QHBoxLayout*>(section, layout);
    };

    // Left section: file/playback controls
    {
        auto [leftSection, leftLayout] = makeSection("TopToolbarLeft");
        const QSize commandIconSize(18, 18);
        const QColor commandTint(QStringLiteral("#E6E8EB"));

        auto* saveBtn = new QToolButton(leftSection);
        saveBtn->setText(tr("Save"));
        saveBtn->setIcon(makeSaveGlyphIcon(commandIconSize, commandTint));
        saveBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        saveBtn->setIconSize(commandIconSize);
        saveBtn->setFixedHeight(34);
        connect(saveBtn, &QToolButton::clicked, this, &EditorMainWindow::onSaveLevel);
        leftLayout->addWidget(saveBtn);

        auto* undoBtn = new QToolButton(leftSection);
        undoBtn->setText(tr("Undo"));
        undoBtn->setIcon(makeUndoGlyphIcon(commandIconSize, commandTint));
        undoBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        undoBtn->setIconSize(commandIconSize);
        undoBtn->setFixedHeight(34);
        connect(undoBtn, &QToolButton::clicked, this, &EditorMainWindow::onUndo);
        leftLayout->addWidget(undoBtn);

        auto* redoBtn = new QToolButton(leftSection);
        redoBtn->setText(tr("Redo"));
        redoBtn->setIcon(makeRedoGlyphIcon(commandIconSize, commandTint));
        redoBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        redoBtn->setIconSize(commandIconSize);
        redoBtn->setFixedHeight(34);
        connect(redoBtn, &QToolButton::clicked, this, &EditorMainWindow::onRedo);
        leftLayout->addWidget(redoBtn);

        const QSize mediaIconSize(20, 20);
        const QColor mediaTint(QStringLiteral("#E6E8EB"));
        m_playAction = new QAction(
            makePlayGlyphIcon(mediaIconSize, mediaTint),
            tr("Play"),
            this);
        m_playAction->setCheckable(true);
        m_pauseAction = new QAction(
            makePauseGlyphIcon(mediaIconSize, mediaTint),
            tr("Pause"),
            this);
        m_pauseAction->setCheckable(true);
        m_stopAction = new QAction(
            makeStopGlyphIcon(mediaIconSize, mediaTint),
            tr("Stop"),
            this);

        connect(m_playAction, &QAction::triggered, this, [this](bool checked) {
            if (checked) {
                m_pauseAction->setChecked(false);
            }
            if (m_viewportHost) {
                m_viewportHost->setProperty("playMode", checked);
                m_viewportHost->style()->unpolish(m_viewportHost);
                m_viewportHost->style()->polish(m_viewportHost);
            }
            statusBar()->showMessage(checked ? tr("Play mode enabled") : tr("Play mode disabled"), 2000);
        });
        connect(m_pauseAction, &QAction::triggered, this, [this](bool checked) {
            statusBar()->showMessage(checked ? tr("Play mode paused") : tr("Pause cleared"), 2000);
        });
        connect(m_stopAction, &QAction::triggered, this, [this]() {
            if (m_playAction) m_playAction->setChecked(false);
            if (m_pauseAction) m_pauseAction->setChecked(false);
            if (m_viewportHost) {
                m_viewportHost->setProperty("playMode", false);
                m_viewportHost->style()->unpolish(m_viewportHost);
                m_viewportHost->style()->polish(m_viewportHost);
            }
            statusBar()->showMessage(tr("Play mode stopped"), 2000);
        });

        auto* playBtn = new QToolButton(leftSection);
        playBtn->setObjectName("PlaybackButton");
        playBtn->setDefaultAction(m_playAction);
        playBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        playBtn->setIconSize(mediaIconSize);
        playBtn->setFixedSize(44, 34);
        leftLayout->addWidget(playBtn);
        auto* pauseBtn = new QToolButton(leftSection);
        pauseBtn->setObjectName("PlaybackButton");
        pauseBtn->setDefaultAction(m_pauseAction);
        pauseBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        pauseBtn->setIconSize(mediaIconSize);
        pauseBtn->setFixedSize(44, 34);
        leftLayout->addWidget(pauseBtn);
        auto* stopBtn = new QToolButton(leftSection);
        stopBtn->setObjectName("PlaybackButton");
        stopBtn->setDefaultAction(m_stopAction);
        stopBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        stopBtn->setIconSize(mediaIconSize);
        stopBtn->setFixedSize(44, 34);
        leftLayout->addWidget(stopBtn);

        m_topToolbar->addWidget(leftSection);
    }

    m_topToolbar->addSeparator();

    // Center section: transform and camera controls
    {
        auto [centerSection, centerLayout] = makeSection("TopToolbarCenter");

        QActionGroup* transformGroup = new QActionGroup(this);
        transformGroup->setExclusive(true);

        m_translateAction = new QAction(tr("Translate"), this);
        m_translateAction->setCheckable(true);
        m_rotateAction = new QAction(tr("Rotate"), this);
        m_rotateAction->setCheckable(true);
        m_scaleAction = new QAction(tr("Scale"), this);
        m_scaleAction->setCheckable(true);
        transformGroup->addAction(m_translateAction);
        transformGroup->addAction(m_rotateAction);
        transformGroup->addAction(m_scaleAction);
        m_translateAction->setChecked(true);

        auto* trBtn = new QToolButton(centerSection);
        trBtn->setDefaultAction(m_translateAction);
        centerLayout->addWidget(trBtn);
        auto* rotBtn = new QToolButton(centerSection);
        rotBtn->setDefaultAction(m_rotateAction);
        centerLayout->addWidget(rotBtn);
        auto* scBtn = new QToolButton(centerSection);
        scBtn->setDefaultAction(m_scaleAction);
        centerLayout->addWidget(scBtn);

        m_worldLocalBtn = new QToolButton(centerSection);
        m_worldLocalBtn->setText(tr("World"));
        m_worldLocalBtn->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
        m_worldLocalBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_worldLocalBtn->setCheckable(true);
        connect(m_worldLocalBtn, &QToolButton::toggled, this, [this](bool checked) {
            m_worldLocalBtn->setText(checked ? tr("Local") : tr("World"));
            if (m_engine) {
                if (auto* ctx = m_engine->GetEditorContext()) {
                    ctx->SetGizmoSpace(checked ? GizmoSpace::Local : GizmoSpace::World);
                }
            }
        });
        centerLayout->addWidget(m_worldLocalBtn);

        m_snapToggleBtn = new QToolButton(centerSection);
        m_snapToggleBtn->setText(tr("Snap"));
        m_snapToggleBtn->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
        m_snapToggleBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_snapToggleBtn->setCheckable(true);
        centerLayout->addWidget(m_snapToggleBtn);

        m_snapValueCombo = new QComboBox(centerSection);
        m_snapValueCombo->addItems({ tr("0.1"), tr("0.25"), tr("0.5"), tr("1.0"), tr("5.0"), tr("15°") });
        m_snapValueCombo->setCurrentIndex(3);
        centerLayout->addWidget(m_snapValueCombo);

        m_cameraSpeedCombo = new QComboBox(centerSection);
        m_cameraSpeedCombo->addItems({ tr("Cam x0.5"), tr("Cam x1"), tr("Cam x2"), tr("Cam x4") });
        m_cameraSpeedCombo->setCurrentIndex(1);
        centerLayout->addWidget(m_cameraSpeedCombo);

        connect(m_translateAction, &QAction::triggered, this, [this]() {
            if (m_engine) {
                if (auto* ctx = m_engine->GetEditorContext()) ctx->SetGizmoMode(GizmoMode::Translate);
            }
        });
        connect(m_rotateAction, &QAction::triggered, this, [this]() {
            if (m_engine) {
                if (auto* ctx = m_engine->GetEditorContext()) ctx->SetGizmoMode(GizmoMode::Rotate);
            }
        });
        connect(m_scaleAction, &QAction::triggered, this, [this]() {
            if (m_engine) {
                if (auto* ctx = m_engine->GetEditorContext()) ctx->SetGizmoMode(GizmoMode::Scale);
            }
        });

        connect(m_snapToggleBtn, &QToolButton::toggled, this, [this](bool checked) {
            if (m_engine) {
                if (auto* ctx = m_engine->GetEditorContext()) {
                    ctx->GetSnapSettings().enableSnap = checked;
                }
            }
        });

        connect(m_snapValueCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            bool ok = false;
            QString clean = text;
            clean.remove(QChar(0x00B0));
            float value = clean.toFloat(&ok);
            if (!ok || !m_engine) return;
            if (auto* ctx = m_engine->GetEditorContext()) {
                auto& snap = ctx->GetSnapSettings();
                snap.translateSnap = value;
                snap.scaleSnap = std::max(0.01f, value * 0.1f);
                snap.rotateSnapDeg = std::max(1.0f, value);
            }
        });

        connect(m_cameraSpeedCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
            static const float speeds[] = { 2.5f, 5.0f, 10.0f, 20.0f };
            if (idx < 0 || idx >= 4 || !m_engine) return;
            if (auto* ctx = m_engine->GetEditorContext()) {
                ctx->GetCamera().moveSpeed = speeds[idx];
            }
        });

        m_topToolbar->addWidget(centerSection);
    }

    QWidget* spring = new QWidget(this);
    spring->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_topToolbar->addWidget(spring);

    // Right section: build/settings/project
    {
        auto [rightSection, rightLayout] = makeSection("TopToolbarRight");

        auto* buildBtn = new QToolButton(rightSection);
        buildBtn->setText(tr("Build"));
        const QSize buildIconSize(18, 18);
        buildBtn->setIcon(makePlayGlyphIcon(buildIconSize, QColor(QStringLiteral("#E6E8EB"))));
        buildBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        buildBtn->setIconSize(buildIconSize);
        buildBtn->setFixedHeight(34);
        connect(buildBtn, &QToolButton::clicked, this, [this]() {
            statusBar()->showMessage(tr("Build requested: run the build task from VS Code tasks"), 4000);
        });
        rightLayout->addWidget(buildBtn);

        auto* settingsBtn = new QToolButton(rightSection);
        settingsBtn->setText(tr("Settings"));
        settingsBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
        settingsBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        connect(settingsBtn, &QToolButton::clicked, this, [this]() {
            QMenu menu(this);
            menu.addAction(tr("Editor Settings"));
            menu.addAction(tr("Viewport Preferences"));
            menu.addAction(tr("Input Controls"));
            menu.exec(QCursor::pos());
        });
        rightLayout->addWidget(settingsBtn);

        auto* projectBtn = new QToolButton(rightSection);
        projectBtn->setText(tr("Project Manager"));
        projectBtn->setIcon(style()->standardIcon(QStyle::SP_DirHomeIcon));
        projectBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        connect(projectBtn, &QToolButton::clicked, this, &EditorMainWindow::onOpenProject);
        rightLayout->addWidget(projectBtn);

        m_topToolbar->addWidget(rightSection);
    }
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

    m_outlinerPanel = new QWidget(m_outlinerDock);
    m_outlinerPanel->setObjectName("OutlinerPanel");
    auto* outlinerLayout = new QVBoxLayout(m_outlinerPanel);
    outlinerLayout->setContentsMargins(12, 12, 12, 12);
    outlinerLayout->setSpacing(8);

    auto* outlinerHeader = new QHBoxLayout();
    m_outlinerSearch = new QLineEdit(m_outlinerPanel);
    m_outlinerSearch->setPlaceholderText(tr("Search entities..."));
    outlinerHeader->addWidget(m_outlinerSearch, 1);

    m_addEntityBtn = new QPushButton(tr("+ Add Entity"), m_outlinerPanel);
    outlinerHeader->addWidget(m_addEntityBtn);
    outlinerLayout->addLayout(outlinerHeader);

    m_outlinerTree = new QTreeWidget(m_outlinerPanel);
    m_outlinerTree->setObjectName("OutlinerTree");
    m_outlinerTree->setHeaderLabel(tr("Scene Hierarchy"));
    m_outlinerTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_outlinerTree->setRootIsDecorated(true);
    m_outlinerTree->setIndentation(18);
    m_outlinerTree->setUniformRowHeights(true);
    outlinerLayout->addWidget(m_outlinerTree, 1);

    m_outlinerDock->setWidget(m_outlinerPanel);
    
    // Connect selection changed signal
    connect(m_outlinerTree, &QTreeWidget::itemSelectionChanged, 
            this, &EditorMainWindow::onOutlinerSelectionChanged);

    connect(m_outlinerSearch, &QLineEdit::textChanged, this, [this](const QString& text) {
        auto filterItem = [&](auto&& self, QTreeWidgetItem* item) -> bool {
            bool selfMatch = item->text(0).contains(text, Qt::CaseInsensitive);
            bool childMatch = false;
            for (int i = 0; i < item->childCount(); ++i) {
                childMatch = self(self, item->child(i)) || childMatch;
            }
            bool visible = text.trimmed().isEmpty() || selfMatch || childMatch;
            item->setHidden(!visible);
            return visible;
        };

        for (int i = 0; i < m_outlinerTree->topLevelItemCount(); ++i) {
            filterItem(filterItem, m_outlinerTree->topLevelItem(i));
        }
    });

    connect(m_addEntityBtn, &QPushButton::clicked, this, [this]() {
        if (!m_engine) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasActiveScene()) return;

        EditorUndoManager::PushSnapshot(ctx, "Create entity");
        Entity created = ctx->GetActiveScene()->CreateEntity("Entity");
        ctx->SelectEntity(static_cast<entt::entity>(created));
        ctx->MarkDirty();
        rebuildOutliner();
        refreshDetailsPanel();
    });

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
    m_detailsWidget->setObjectName("DetailsPanel");
    m_detailsLayout = new QVBoxLayout(m_detailsWidget);
    m_detailsLayout->setContentsMargins(12, 12, 12, 12);
    m_detailsLayout->setSpacing(10);

    // ── Header row: Entity ID + Lock checkbox ───────────────────────────
    {
        auto* headerRow = new QHBoxLayout();
        m_entityIconLabel = new QLabel(QString::fromUtf8("◼"));
        m_entityIconLabel->setObjectName("DetailsEntityIcon");
        headerRow->addWidget(m_entityIconLabel);

        m_entityIdLabel = new QLabel(tr("(No selection)"));
        m_entityIdLabel->setObjectName("DetailsEntityId");
        headerRow->addWidget(m_entityIdLabel);

        headerRow->addStretch();

        m_visibilityBtn = new QToolButton();
        m_visibilityBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
        m_visibilityBtn->setToolTip(tr("Entity visibility toggle"));
        m_visibilityBtn->setCheckable(true);
        m_visibilityBtn->setChecked(true);
        headerRow->addWidget(m_visibilityBtn);

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
    m_transformGroup->setObjectName("TransformCard");
    m_transformGroup->setEnabled(false);
    QFormLayout* transformLayout = new QFormLayout(m_transformGroup);
    transformLayout->setContentsMargins(12, 12, 12, 12);
    transformLayout->setSpacing(8);

    auto* transformControls = new QWidget(m_transformGroup);
    auto* transformControlsLayout = new QHBoxLayout(transformControls);
    transformControlsLayout->setContentsMargins(0, 0, 0, 0);
    transformControlsLayout->setSpacing(6);

    auto* detailsSpaceBtn = new QToolButton(transformControls);
    detailsSpaceBtn->setText(tr("World"));
    detailsSpaceBtn->setCheckable(true);
    connect(detailsSpaceBtn, &QToolButton::toggled, this, [this, detailsSpaceBtn](bool checked) {
        detailsSpaceBtn->setText(checked ? tr("Local") : tr("World"));
        if (m_worldLocalBtn && m_worldLocalBtn->isChecked() != checked) {
            m_worldLocalBtn->setChecked(checked);
        }
    });
    transformControlsLayout->addWidget(detailsSpaceBtn);

    auto* uniformScaleBtn = new QToolButton(transformControls);
    uniformScaleBtn->setText(tr("Uniform Scale"));
    uniformScaleBtn->setCheckable(true);
    transformControlsLayout->addWidget(uniformScaleBtn);
    transformControlsLayout->addStretch();

    transformLayout->addRow(QString(), transformControls);

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

    auto makeAxisResetRow = [this](QDoubleSpinBox* x, QDoubleSpinBox* y, QDoubleSpinBox* z,
                                   double defaultValue,
                                   QToolButton* uniformScaleBtn = nullptr) {
        QWidget* resetWidget = new QWidget();
        auto* resetLayout = new QHBoxLayout(resetWidget);
        resetLayout->setContentsMargins(0, 0, 0, 0);
        resetLayout->setSpacing(4);

        auto makeResetBtn = [resetLayout](const QString& axis) {
            auto* btn = new QToolButton();
            btn->setText(axis);
            btn->setToolTip(QObject::tr("Reset %1 axis").arg(axis));
            resetLayout->addWidget(btn);
            return btn;
        };

        auto* resetX = makeResetBtn("X");
        auto* resetY = makeResetBtn("Y");
        auto* resetZ = makeResetBtn("Z");
        resetLayout->addStretch();

        connect(resetX, &QToolButton::clicked, this, [x, defaultValue]() { x->setValue(defaultValue); });
        connect(resetY, &QToolButton::clicked, this, [y, defaultValue]() { y->setValue(defaultValue); });
        connect(resetZ, &QToolButton::clicked, this, [z, defaultValue]() { z->setValue(defaultValue); });

        if (uniformScaleBtn) {
            connect(x, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v) {
                if (!uniformScaleBtn->isChecked()) return;
                y->blockSignals(true); z->blockSignals(true);
                y->setValue(v); z->setValue(v);
                y->blockSignals(false); z->blockSignals(false);
            });
            connect(y, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v) {
                if (!uniformScaleBtn->isChecked()) return;
                x->blockSignals(true); z->blockSignals(true);
                x->setValue(v); z->setValue(v);
                x->blockSignals(false); z->blockSignals(false);
            });
            connect(z, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v) {
                if (!uniformScaleBtn->isChecked()) return;
                x->blockSignals(true); y->blockSignals(true);
                x->setValue(v); y->setValue(v);
                x->blockSignals(false); y->blockSignals(false);
            });
        }

        return resetWidget;
    };

    transformLayout->addRow(QString(), makeAxisResetRow(m_posX, m_posY, m_posZ, 0.0));
    transformLayout->addRow(QString(), makeAxisResetRow(m_rotX, m_rotY, m_rotZ, 0.0));
    transformLayout->addRow(QString(), makeAxisResetRow(m_scaleX, m_scaleY, m_scaleZ, 1.0, uniformScaleBtn));

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
        m_addComponentBtn->setObjectName("PrimaryActionButton");
        m_addComponentBtn->setEnabled(false);
        connect(m_addComponentBtn, &QPushButton::clicked, this, &EditorMainWindow::showAddComponentMenu);
        actionRow->addWidget(m_addComponentBtn);

        actionRow->addStretch();

        m_deleteEntityBtn = new QPushButton(tr("Delete Entity"));
        m_deleteEntityBtn->setObjectName("DangerActionButton");
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

    // Connect mesh spawn (double-click on mesh asset)
    connect(m_assetBrowser, &AssetBrowserWidget::meshSpawnRequested,
            this, [this](const QString& relativePath) {
                if (!m_engine) return;
                auto* ctx = m_engine->GetEditorContext();
                if (!ctx || !ctx->HasActiveScene()) {
                    statusBar()->showMessage(tr("No active scene to spawn mesh into."), 3000);
                    return;
                }

                auto* frameRenderer = m_engine->GetFrameRenderer();
                if (!frameRenderer) {
                    statusBar()->showMessage(tr("Renderer not ready."), 3000);
                    return;
                }

                // Resolve mesh asset path
                namespace fs = std::filesystem;
                auto& assetMgr = AssetManager::GetInstance();
                auto assetInfo = assetMgr.GetAssetInfo(relativePath.toStdString());
                if (!assetInfo) {
                    statusBar()->showMessage(tr("Asset not found: %1").arg(relativePath), 3000);
                    std::cerr << "[MeshSpawn] Asset not found: " << relativePath.toStdString() << "\n";
                    return;
                }

                fs::path meshPath(assetInfo->absolutePath);
                if (!fs::exists(meshPath)) {
                    statusBar()->showMessage(tr("Mesh file not found: %1").arg(relativePath), 3000);
                    std::cerr << "[MeshSpawn] File not found: " << meshPath << "\n";
                    return;
                }

                // Check if mesh is already in the library, or load it
                std::string normalizedKey = meshPath.lexically_normal().generic_string();
                size_t meshHandle = frameRenderer->GetMeshHandle(normalizedKey);

                if (meshHandle == SIZE_MAX) {
                    // Try loading the mesh
                    MeshData meshData;
                    std::string loadError;
                    if (!ModelImporter::LoadModel(meshPath, meshData, loadError) ||
                        meshData.vertices.empty() || meshData.indices.empty()) {
                        statusBar()->showMessage(tr("Failed to load mesh: %1").arg(QString::fromStdString(loadError)), 3000);
                        std::cerr << "[MeshSpawn] Load failed: " << loadError << "\n";
                        return;
                    }

                    // Upload to GPU
                    std::string meshName = meshPath.stem().string();
                    meshHandle = m_engine->uploadMeshToGPU(meshName, meshData, meshPath, true);
                    if (meshHandle == MeshRendererComponent::InvalidMesh) {
                        statusBar()->showMessage(tr("Failed to upload mesh to GPU."), 3000);
                        std::cerr << "[MeshSpawn] GPU upload failed\n";
                        return;
                    }
                }

                // Push undo before modifying the scene
                EditorUndoManager::PushSnapshot(ctx, "Spawn mesh actor");

                Scene* scene = ctx->GetActiveScene();
                std::string baseName = meshPath.stem().string();
                if (baseName.empty()) baseName = "Mesh";

                // Generate unique name
                std::string entityName = baseName;
                if (scene->FindEntityByName(entityName)) {
                    int suffix = 1;
                    do {
                        entityName = baseName + " (" + std::to_string(suffix++) + ")";
                    } while (scene->FindEntityByName(entityName));
                }

                Entity created = scene->CreateEntity(entityName);
                if (!created) {
                    statusBar()->showMessage(tr("Failed to create entity."), 3000);
                    return;
                }

                // Configure MeshRenderer
                MeshGPU* meshGPU = frameRenderer->GetMesh(meshHandle);
                int subCount = (meshGPU && !meshGPU->submeshes.empty())
                    ? static_cast<int>(meshGPU->submeshes.size()) : 1;
                AABB bounds = meshGPU ? meshGPU->bounds : AABB{};

                auto& renderer = created.AddComponent<MeshRendererComponent>();
                renderer.SetMesh(meshHandle, bounds, relativePath.toStdString(), true, subCount);
                renderer.materialIndices.assign(static_cast<size_t>(subCount), -1);
                if (meshGPU && !meshGPU->submeshes.empty()) {
                    for (size_t i = 0; i < meshGPU->submeshes.size(); ++i) {
                        renderer.materialIndices[i] = meshGPU->submeshes[i].materialSlot;
                    }
                }
                renderer.appliedMaterialPaths.resize(static_cast<size_t>(subCount));
                created.AddComponent<MaterialComponent>(Vec3(1.0f, 1.0f, 1.0f));

                // Position in front of editor camera
                auto& transform = created.GetComponent<TransformComponent>();
                const auto& cam = ctx->GetCamera();
                constexpr float kSpawnDistance = 4.0f;
                const float yawR   = cam.yawDeg   * 3.1415926535f / 180.0f;
                const float pitchR = cam.pitchDeg * 3.1415926535f / 180.0f;
                float fx = std::cos(yawR) * std::cos(pitchR);
                float fy = std::sin(pitchR);
                float fz = std::sin(yawR) * std::cos(pitchR);
                float len = std::sqrt(fx*fx + fy*fy + fz*fz);
                if (len > 0.0001f) { fx /= len; fy /= len; fz /= len; }
                transform.position = Vec3(cam.positionX + fx * kSpawnDistance,
                                          cam.positionY + fy * kSpawnDistance,
                                          cam.positionZ + fz * kSpawnDistance);
                transform.rotation = Vec3(0.0f, 0.0f, 0.0f);
                transform.scale    = Vec3(1.0f, 1.0f, 1.0f);

                // Select the newly created entity, mark dirty, refresh UI
                ctx->SelectEntity(static_cast<entt::entity>(created));
                ctx->MarkDirty();
                m_engine->RebuildSceneMeshes();
                rebuildOutliner();
                refreshDetailsPanel();

                std::cout << "[MeshSpawn] Added mesh actor: " << entityName << "\n";
                statusBar()->showMessage(tr("Added mesh actor: %1").arg(QString::fromStdString(entityName)), 3000);
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
    m_viewportHost = new QWidget(this);
    m_viewportHost->setObjectName("ViewportHost");
    m_viewportContainer = QWidget::createWindowContainer(m_viewportWindow, m_viewportHost);
    m_viewportContainer->setObjectName("ViewportContainer");
    m_viewportContainer->setMinimumSize(200, 200);
    m_viewportContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* hostLayout = new QGridLayout(m_viewportHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);
    hostLayout->addWidget(m_viewportContainer, 0, 0);

    auto makeOverlay = [this](const QString& name) {
        auto* overlay = new QFrame(m_viewportHost);
        overlay->setObjectName(name);
        overlay->setAttribute(Qt::WA_StyledBackground, true);
        auto* layout = new QHBoxLayout(overlay);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(6);
        return std::pair<QFrame*, QHBoxLayout*>(overlay, layout);
    };

    {
        auto [overlay, layout] = makeOverlay("ViewportOverlayTopLeft");
        m_overlayTopLeft = overlay;

        m_viewPerspectiveCombo = new QComboBox(overlay);
        m_viewPerspectiveCombo->addItems({ tr("Perspective"), tr("Front"), tr("Top"), tr("Right") });
        layout->addWidget(m_viewPerspectiveCombo);

        m_viewModeBtn = new QToolButton(overlay);
        m_viewModeBtn->setText(tr("Lit"));
        m_viewModeBtn->setPopupMode(QToolButton::MenuButtonPopup);
        auto* modeMenu = new QMenu(m_viewModeBtn);
        modeMenu->addAction(tr("Lit"), [this]() { m_viewModeBtn->setText(tr("Lit")); });
        modeMenu->addAction(tr("Unlit"), [this]() { m_viewModeBtn->setText(tr("Unlit")); });
        modeMenu->addAction(tr("Wireframe"), [this]() { m_viewModeBtn->setText(tr("Wireframe")); });
        m_viewModeBtn->setMenu(modeMenu);
        layout->addWidget(m_viewModeBtn);

        m_viewGridBtn = new QToolButton(overlay);
        m_viewGridBtn->setText(tr("Grid"));
        m_viewGridBtn->setCheckable(true);
        m_viewGridBtn->setChecked(true);
        layout->addWidget(m_viewGridBtn);

        m_viewStatsBtn = new QToolButton(overlay);
        m_viewStatsBtn->setText(tr("Stats"));
        m_viewStatsBtn->setCheckable(true);
        m_viewStatsBtn->setChecked(true);
        layout->addWidget(m_viewStatsBtn);

        hostLayout->addWidget(m_overlayTopLeft, 0, 0, Qt::AlignTop | Qt::AlignLeft);
    }

    {
        auto [overlay, layout] = makeOverlay("ViewportOverlayTopRight");
        m_overlayTopRight = overlay;

        auto* gizmoTranslate = new QToolButton(overlay);
        gizmoTranslate->setText(tr("T"));
        gizmoTranslate->setCheckable(true);
        gizmoTranslate->setChecked(true);
        layout->addWidget(gizmoTranslate);

        auto* gizmoRotate = new QToolButton(overlay);
        gizmoRotate->setText(tr("R"));
        gizmoRotate->setCheckable(true);
        layout->addWidget(gizmoRotate);

        auto* gizmoScale = new QToolButton(overlay);
        gizmoScale->setText(tr("S"));
        gizmoScale->setCheckable(true);
        layout->addWidget(gizmoScale);

        auto* group = new QActionGroup(this);
        group->setExclusive(true);
        auto* aT = new QAction(this); aT->setCheckable(true); aT->setChecked(true);
        auto* aR = new QAction(this); aR->setCheckable(true);
        auto* aS = new QAction(this); aS->setCheckable(true);
        group->addAction(aT); group->addAction(aR); group->addAction(aS);
        gizmoTranslate->setDefaultAction(aT);
        gizmoRotate->setDefaultAction(aR);
        gizmoScale->setDefaultAction(aS);

        connect(aT, &QAction::triggered, this, [this]() { if (m_translateAction) m_translateAction->trigger(); });
        connect(aR, &QAction::triggered, this, [this]() { if (m_rotateAction) m_rotateAction->trigger(); });
        connect(aS, &QAction::triggered, this, [this]() { if (m_scaleAction) m_scaleAction->trigger(); });

        auto* snapBtn = new QToolButton(overlay);
        snapBtn->setText(tr("Snap"));
        snapBtn->setCheckable(true);
        layout->addWidget(snapBtn);
        connect(snapBtn, &QToolButton::toggled, this, [this](bool checked) {
            if (m_snapToggleBtn) m_snapToggleBtn->setChecked(checked);
        });

        auto* snapValue = new QComboBox(overlay);
        snapValue->addItems({ tr("0.1"), tr("0.5"), tr("1.0"), tr("5.0"), tr("15°") });
        snapValue->setCurrentIndex(2);
        connect(snapValue, &QComboBox::currentTextChanged, this, [this](const QString& value) {
            if (m_snapValueCombo) m_snapValueCombo->setCurrentText(value);
        });
        layout->addWidget(snapValue);

        auto* spaceBtn = new QToolButton(overlay);
        spaceBtn->setText(tr("World"));
        spaceBtn->setCheckable(true);
        connect(spaceBtn, &QToolButton::toggled, this, [this, spaceBtn](bool checked) {
            spaceBtn->setText(checked ? tr("Local") : tr("World"));
            if (m_worldLocalBtn) m_worldLocalBtn->setChecked(checked);
        });
        layout->addWidget(spaceBtn);

        hostLayout->addWidget(m_overlayTopRight, 0, 0, Qt::AlignTop | Qt::AlignRight);
    }

    {
        auto [overlay, layout] = makeOverlay("ViewportOverlayBottomLeft");
        m_overlayBottomLeft = overlay;
        m_overlayFpsLabel = new QLabel(tr("FPS 0.0"), overlay);
        m_overlayDrawCallsLabel = new QLabel(tr("Draw 0"), overlay);
        m_overlayRamLabel = new QLabel(tr("RAM --"), overlay);
        layout->addWidget(m_overlayFpsLabel);
        layout->addWidget(m_overlayDrawCallsLabel);
        layout->addWidget(m_overlayRamLabel);

        hostLayout->addWidget(m_overlayBottomLeft, 0, 0, Qt::AlignBottom | Qt::AlignLeft);
    }

    connect(m_viewGridBtn, &QToolButton::toggled, this, [this](bool checked) {
        if (!m_engine) return;
        if (auto* ctx = m_engine->GetEditorContext()) {
            ctx->GetViewportSettings().showGrid = checked;
        }
    });
    connect(m_viewStatsBtn, &QToolButton::toggled, this, [this](bool checked) {
        if (m_overlayBottomLeft) m_overlayBottomLeft->setVisible(checked);
    });
    
    // Connect viewport signals
    connect(m_viewportWindow, &QtViewportWindow::viewportReady, 
            this, &EditorMainWindow::onViewportReady);
    
    setCentralWidget(m_viewportHost);
    
    std::cout << "[EditorMainWindow] Viewport widget created\n";
}

void EditorMainWindow::setupStatusBar()
{
    QStatusBar* status = statusBar();
    status->setObjectName("EditorStatusBar");
    status->setSizeGripEnabled(false);

    m_statusFps = new QLabel(tr("FPS 0.0"), status);
    m_statusDrawCalls = new QLabel(tr("Draw 0"), status);
    m_statusRam = new QLabel(tr("RAM --"), status);
    m_statusVersion = new QLabel(tr("Hephaestus 1.0.0"), status);

    status->addPermanentWidget(m_statusFps);
    status->addPermanentWidget(m_statusDrawCalls);
    status->addPermanentWidget(m_statusRam);
    status->addPermanentWidget(m_statusVersion);
    status->showMessage(tr("Initializing engine..."));
}

void EditorMainWindow::applyUnifiedStyle()
{
    setStyleSheet(QString::fromUtf8(R"(
QMainWindow {
    background: #1E1F24;
    color: #E6E8EB;
}
QMenuBar {
    background: #25272D;
    color: #E6E8EB;
    border-bottom: 1px solid #333740;
}
QMenuBar::item {
    padding: 6px 10px;
    border-radius: 4px;
}
QMenuBar::item:selected { background: #2C2F36; }
QMenu {
    background: #25272D;
    color: #E6E8EB;
    border: 1px solid #333740;
    padding: 6px;
}
QMenu::item { padding: 6px 12px; border-radius: 4px; }
QMenu::item:selected { background: #4C8DFF; color: #E6E8EB; }

QToolBar#EditorTopToolbar {
    background: #25272D;
    border: none;
    border-bottom: 1px solid #333740;
    spacing: 8px;
    padding: 6px 10px;
}
QToolBar#AssetBrowserToolbar {
    background: #25272D;
    border: 1px solid #333740;
    border-radius: 6px;
    spacing: 6px;
    padding: 6px;
}
QLabel#AssetBrowserStatus {
    color: #9DA3AE;
    background: #2C2F36;
    border: 1px solid #333740;
    border-radius: 6px;
    padding: 6px 10px;
}
QToolBar#EditorTopToolbar QToolButton,
QToolBar#EditorTopToolbar QPushButton {
    background: #2C2F36;
    border: 1px solid transparent;
    border-radius: 6px;
    color: #E6E8EB;
    padding: 6px 10px;
}
QToolBar#EditorTopToolbar QToolButton[toolButtonStyle="0"] {
    padding: 0;
    qproperty-autoRaise: false;
}
QToolBar#EditorTopToolbar QToolButton#PlaybackButton {
    padding: 0;
    margin: 0;
    min-width: 40px;
    min-height: 32px;
    qproperty-autoRaise: false;
}
QToolBar#EditorTopToolbar QToolButton:hover,
QToolBar#EditorTopToolbar QPushButton:hover {
    border-color: #4C8DFF;
    background: #32363F;
}
QToolBar#EditorTopToolbar QToolButton:checked,
QToolBar#EditorTopToolbar QToolButton:pressed {
    background: #4C8DFF;
    color: #E6E8EB;
}

QDockWidget {
    background: #25272D;
    border: 1px solid #333740;
    titlebar-close-icon: none;
    titlebar-normal-icon: none;
}
QDockWidget::title {
    background: #25272D;
    color: #9DA3AE;
    padding: 10px 12px;
    border-bottom: 1px solid #333740;
}

QWidget#OutlinerPanel,
QWidget#DetailsPanel,
QWidget#AssetBrowserRoot {
    background: #25272D;
}

QTreeWidget#OutlinerTree,
QTreeView,
QListView,
QTableView,
QTableWidget {
    background: #25272D;
    border: 1px solid #333740;
    border-radius: 6px;
    color: #E6E8EB;
    outline: none;
    padding: 4px;
}
QTreeWidget#OutlinerTree::item,
QTreeView::item,
QListView::item {
    padding: 6px 8px;
    margin: 1px 0;
    border-radius: 4px;
}
QTreeWidget#OutlinerTree::item:hover,
QTreeView::item:hover,
QListView::item:hover {
    background: rgba(76, 141, 255, 0.12);
}
QTreeWidget#OutlinerTree::item:selected,
QTreeView::item:selected,
QListView::item:selected {
    background: rgba(76, 141, 255, 0.24);
    border-left: 3px solid #4C8DFF;
}

QLabel#DetailsEntityId {
    color: #9DA3AE;
    font-size: 10px;
}
QLabel#DetailsEntityIcon {
    color: #4C8DFF;
    font-size: 12px;
}

QLineEdit, QComboBox, QDoubleSpinBox {
    background: #2C2F36;
    color: #E6E8EB;
    border: 1px solid #333740;
    border-radius: 6px;
    padding: 6px 8px;
    min-height: 22px;
}
QLineEdit:disabled, QComboBox:disabled, QDoubleSpinBox:disabled {
    color: #6B7280;
    background: #25272D;
}

QPushButton, QToolButton {
    background: #2C2F36;
    color: #E6E8EB;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 6px 10px;
}
QPushButton:hover, QToolButton:hover {
    background: #32363F;
    border-color: #4C8DFF;
}
QPushButton:pressed, QToolButton:pressed, QToolButton:checked {
    background: #4C8DFF;
    color: #E6E8EB;
}
QPushButton#PrimaryActionButton { background: #4C8DFF; }
QPushButton#DangerActionButton {
    color: #E6A5A5;
    border-color: #6A3C45;
}

QGroupBox {
    background: #2C2F36;
    border: 1px solid #333740;
    border-radius: 6px;
    margin-top: 12px;
    padding-top: 12px;
}
QGroupBox::title {
    color: #E6E8EB;
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
}

QWidget#ViewportHost {
    background: #1E1F24;
    border: 1px solid #333740;
    border-radius: 6px;
}
QWidget#ViewportHost[playMode="true"] {
    border: 1px solid #4C8DFF;
}
QFrame#ViewportOverlayTopLeft,
QFrame#ViewportOverlayTopRight,
QFrame#ViewportOverlayBottomLeft {
    background: rgba(30, 31, 36, 205);
    border: 1px solid #333740;
    border-radius: 6px;
    margin: 10px;
}
QFrame#ViewportOverlayBottomLeft QLabel {
    color: #9DA3AE;
}

QStatusBar#EditorStatusBar {
    background: #25272D;
    color: #9DA3AE;
    border-top: 1px solid #333740;
}
QStatusBar#EditorStatusBar QLabel {
    color: #9DA3AE;
    padding: 0 8px;
}

QScrollArea {
    border: none;
    background: #25272D;
}

QHeaderView::section {
    background: #2C2F36;
    color: #9DA3AE;
    border: none;
    border-bottom: 1px solid #333740;
    padding: 6px;
}

QCheckBox { color: #E6E8EB; spacing: 6px; }
QCheckBox:disabled { color: #6B7280; }
    )"));
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
        if (m_statusFps) {
            m_statusFps->setText(QString("FPS %1").arg(fps, 0, 'f', 1));
        }
        if (m_overlayFpsLabel) {
            m_overlayFpsLabel->setText(QString("FPS %1").arg(fps, 0, 'f', 1));
        }

        // Placeholder lightweight counters for now (visual telemetry surface).
        const int drawCalls = 0;
        const QString ramUsage = tr("RAM --");
        if (m_statusDrawCalls) m_statusDrawCalls->setText(QString("Draw %1").arg(drawCalls));
        if (m_overlayDrawCallsLabel) m_overlayDrawCallsLabel->setText(QString("Draw %1").arg(drawCalls));
        if (m_statusRam) m_statusRam->setText(ramUsage);
        if (m_overlayRamLabel) m_overlayRamLabel->setText(ramUsage);
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
