#if HEPHAESTUS_EDITOR_QT_UI

#include "CharacterEditorWindow.h"
#include "CharacterTimelineWidget.h"
#include "CharacterAnimationImportDialog.h"
#include "CharacterEditorManager.h"
#include "EditorMainWindow.h"
#include "EditorAssetServices.h"
#include "EditorUndoCommand.h"
#include "QtViewportWindow.h"
#include "QtEditorEngine.h"
#include "AssetReferenceEditor.h"
#include "PropertyEditors.h"
#include "MaterialGraphQtEditorDialog.h"

#include "../../Assets/AssetPathPolicy.h"
#include "../../Assets/CharacterImport.h"
#include "../../Assets/CharacterRuntime.h"
#include "../../Assets/SkeletalModelImporter.h"
#include "../../Animation/CharacterPlayback.h"
#include "../../Scene/Components.h"

#include <QActionGroup>
#include <QComboBox>
#include <QDockWidget>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QAbstractItemView>
#include <QDesktopServices>
#include <QUrl>
#include <QVBoxLayout>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "../../Core/Math.h"
#include "../../Scene/Entity.h"
#include "../../Animation/AnimationClip.h"

namespace Hephaestus {
namespace QtEditor {
namespace {

constexpr int kKindRole = Qt::UserRole;
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kIndexRole = Qt::UserRole + 2;

QTreeWidgetItem* AddItem(QTreeWidgetItem* parent, const QString& text, CharacterEditorItemKind kind,
                         const std::string& id = {}, int index = -1) {
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, text);
    item->setData(0, kKindRole, static_cast<int>(kind));
    item->setData(0, kIdRole, QString::fromStdString(id));
    item->setData(0, kIndexRole, index);
    return item;
}

QTreeWidgetItem* AddTop(QTreeWidget* tree, const QString& text, CharacterEditorItemKind kind,
                        const std::string& id = {}) {
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, text);
    item->setData(0, kKindRole, static_cast<int>(kind));
    item->setData(0, kIdRole, QString::fromStdString(id));
    return item;
}

} // namespace

CharacterEditorWindow::CharacterEditorWindow(const QString& characterPath,
                                             EditorMainWindow* parentEditor,
                                             QWidget* parent)
    : QMainWindow(parent)
    , m_parentEditor(parentEditor)
    , m_pathStr(characterPath)
{
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose, true);
    resize(1480, 920);
    setMinimumSize(900, 640);

    m_previewScene = std::make_unique<Scene>();
    m_document = std::make_unique<CharacterDocument>(m_previewScene.get());
    m_document->SetProjectRoot(projectRoot());

    EditorUndoManager::SetCustomPushHandler(m_previewScene.get(), [this](const std::string&) {
        if (m_document && !m_gizmoDragging) {
            m_document->PushUndoSnapshot();
        }
        if (m_undoAction) m_undoAction->setEnabled(m_document && m_document->CanUndo());
        if (m_redoAction) m_redoAction->setEnabled(m_document && m_document->CanRedo());
    });

    setupUI();
    if (!openDocument(characterPath)) {
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }
    registerOpenDocument();
    updateWindowTitle();
    rebuildTree();
}

CharacterEditorWindow::~CharacterEditorWindow() {
    unregisterOpenDocument();
    if (m_previewScene) {
        EditorUndoManager::ClearCustomPushHandler(m_previewScene.get());
    }
    shutdownEngine();
}

bool CharacterEditorWindow::IsDirty() const {
    return m_document && m_document->IsDirty();
}

bool CharacterEditorWindow::SaveDocument(std::string& outError) {
    if (!m_document) {
        outError = "No character document.";
        return false;
    }
    if (!m_document->OnSave(outError)) {
        return false;
    }
    EditorAssetServices::Instance().Documents().SetDirty(m_pathStr.toStdString(), false);
    updateWindowTitle();
    emit characterSaved(m_pathStr);
    bindPreview(true);
    return true;
}

void CharacterEditorWindow::RetargetPath(const QString& newAbsolutePath) {
    unregisterOpenDocument();
    m_pathStr = newAbsolutePath;
    if (m_document) {
        std::string error;
        m_document->SetProjectRoot(projectRoot());
        m_document->OnOpen(std::filesystem::path(newAbsolutePath.toStdString()), error);
    }
    registerOpenDocument();
    updateWindowTitle();
}

void CharacterEditorWindow::closeEvent(QCloseEvent* event) {
    if (!promptSaveIfDirty()) {
        event->ignore();
        return;
    }
    shutdownEngine();
    unregisterOpenDocument();
    emit windowClosed(m_pathStr);
    QMainWindow::closeEvent(event);
}

void CharacterEditorWindow::setupUI() {
    setStyleSheet(
        "QMainWindow { background: #1e1e1e; }"
        "QDockWidget { color: #ccc; font-weight: bold; }"
        "QDockWidget::title { background: #2a2a2a; padding: 6px; border-bottom: 1px solid #3a3a3a; }"
        "QToolBar { background: #2d2d2d; border-bottom: 1px solid #3a3a3a; spacing: 3px; }"
        "QToolBar QToolButton { color: #ccc; padding: 3px 6px; }"
        "QStatusBar { background: #2d2d2d; color: #888; }"
    );
    setupToolbar();
    setupViewport();
    setupDocks();
    auto* status = statusBar();
    m_statusPath = new QLabel(status);
    m_statusFps = new QLabel(tr("FPS --"), status);
    status->addWidget(m_statusPath, 1);
    status->addPermanentWidget(m_statusFps);
    initializeEngine();
}

void CharacterEditorWindow::setupToolbar() {
    m_toolbar = new QToolBar(tr("Character Tools"), this);
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(16, 16));
    addToolBar(m_toolbar);

    m_saveAction = m_toolbar->addAction(tr("Save"));
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &CharacterEditorWindow::onSave);
    m_undoAction = m_toolbar->addAction(tr("Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, &CharacterEditorWindow::onUndo);
    m_redoAction = m_toolbar->addAction(tr("Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this, &CharacterEditorWindow::onRedo);
    m_toolbar->addSeparator();

    m_gizmoModeGroup = new QActionGroup(this);
    m_translateAction = m_toolbar->addAction(tr("Move"));
    m_translateAction->setCheckable(true);
    m_translateAction->setChecked(true);
    m_gizmoModeGroup->addAction(m_translateAction);
    connect(m_translateAction, &QAction::triggered, this, &CharacterEditorWindow::onSelectTranslate);
    m_rotateAction = m_toolbar->addAction(tr("Rotate"));
    m_rotateAction->setCheckable(true);
    m_gizmoModeGroup->addAction(m_rotateAction);
    connect(m_rotateAction, &QAction::triggered, this, &CharacterEditorWindow::onSelectRotate);
    m_localWorldAction = m_toolbar->addAction(tr("Local"));
    m_localWorldAction->setCheckable(true);
    m_localWorldAction->setChecked(true);
    connect(m_localWorldAction, &QAction::toggled, this, &CharacterEditorWindow::onToggleLocalWorld);
    m_snapAction = m_toolbar->addAction(tr("Snap"));
    m_snapAction->setCheckable(true);
    connect(m_snapAction, &QAction::toggled, this, &CharacterEditorWindow::onToggleSnap);
    m_toolbar->addSeparator();
    m_toolbar->addAction(tr("Frame"), this, &CharacterEditorWindow::onFrameSelected);
    m_toolbar->addAction(tr("Frame All"), this, &CharacterEditorWindow::onFrameAll);
    m_skeletonAction = m_toolbar->addAction(tr("Show Skeleton"));
    m_skeletonAction->setCheckable(true);
    m_skeletonAction->setChecked(true);
    connect(m_skeletonAction, &QAction::toggled, this, &CharacterEditorWindow::onToggleSkeleton);
    m_boneNamesAction = m_toolbar->addAction(tr("Bone Names"));
    m_boneNamesAction->setCheckable(true);
    connect(m_boneNamesAction, &QAction::toggled, this, &CharacterEditorWindow::onToggleBoneNames);
}

void CharacterEditorWindow::setupViewport() {
    m_viewportWindow = new QtViewportWindow();
    m_viewportHost = new QWidget(this);
    auto* container = QWidget::createWindowContainer(m_viewportWindow, m_viewportHost);
    container->setMinimumSize(200, 200);
    auto* stack = new QStackedLayout(m_viewportHost);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->addWidget(container);
    m_emptyOverlay = new QWidget(m_viewportHost);
    auto* emptyLayout = new QVBoxLayout(m_emptyOverlay);
    emptyLayout->setAlignment(Qt::AlignCenter);
    m_banner = new QLabel(tr("No character mesh assigned."), m_emptyOverlay);
    m_banner->setStyleSheet("color:#ccc; font-size:16px;");
    auto* assignBtn = new QPushButton(tr("Import Character Source"), m_emptyOverlay);
    connect(assignBtn, &QPushButton::clicked, this, [this]() { assignSource(); });
    emptyLayout->addWidget(m_banner, 0, Qt::AlignCenter);
    emptyLayout->addWidget(assignBtn, 0, Qt::AlignCenter);
    stack->addWidget(m_emptyOverlay);
    setCentralWidget(m_viewportHost);
}

void CharacterEditorWindow::setupDocks() {
    m_treeDock = new QDockWidget(tr("Character"), this);
    m_treeDock->setObjectName("CharacterTreeDock");
    m_tree = new QTreeWidget(m_treeDock);
    m_tree->setHeaderHidden(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &CharacterEditorWindow::onTreeSelectionChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_tree->itemAt(pos);
        if (!item || !m_document) return;
        const auto kind = static_cast<CharacterEditorItemKind>(item->data(0, kKindRole).toInt());
        QMenu menu(this);
        if (kind == CharacterEditorItemKind::Sockets || kind == CharacterEditorItemKind::Bone) {
            menu.addAction(tr("Add Socket"), this, [this, item, kind]() {
                std::string bone;
                if (kind == CharacterEditorItemKind::Bone) bone = item->data(0, kIdRole).toString().toStdString();
                const QString name = QInputDialog::getText(this, tr("Socket"), tr("Name"));
                std::string error;
                if (!name.isEmpty()) {
                    if (bone.empty()) bone = "Root";
                    m_document->CreateSocket(name.toStdString(), bone, error);
                    if (!error.empty()) QMessageBox::warning(this, tr("Socket"), QString::fromStdString(error));
                    rebuildTree();
                    updateWindowTitle();
                }
            });
        }
        if (kind == CharacterEditorItemKind::Socket) {
            menu.addAction(tr("Delete Socket"), this, [this, item]() {
                std::string error;
                m_document->DeleteSocket(item->data(0, kIdRole).toString().toStdString(), error);
                rebuildTree();
                refreshInspector();
                updateWindowTitle();
            });
        }
        if (!menu.actions().isEmpty()) menu.exec(m_tree->mapToGlobal(pos));
    });
    m_treeDock->setWidget(m_tree);
    addDockWidget(Qt::LeftDockWidgetArea, m_treeDock);

    m_inspectorDock = new QDockWidget(tr("Inspector"), this);
    m_inspectorDock->setObjectName("CharacterInspectorDock");
    auto* scroll = new QScrollArea(m_inspectorDock);
    scroll->setWidgetResizable(true);
    m_inspectorHost = new QWidget(scroll);
    new QVBoxLayout(m_inspectorHost);
    scroll->setWidget(m_inspectorHost);
    m_inspectorDock->setWidget(scroll);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

    m_animationsDock = new QDockWidget(tr("Animations"), this);
    m_animationsDock->setObjectName("CharacterAnimationsDock");
    auto* animHost = new QWidget(m_animationsDock);
    auto* animLayout = new QVBoxLayout(animHost);
    animLayout->setContentsMargins(8, 8, 8, 8);
    auto* animButtons = new QHBoxLayout();
    auto* importBtn = new QPushButton(tr("Import Animation"), animHost);
    connect(importBtn, &QPushButton::clicked, this, &CharacterEditorWindow::importAnimation);
    auto* folderBtn = new QPushButton(tr("Create Folder"), animHost);
    connect(folderBtn, &QPushButton::clicked, this, &CharacterEditorWindow::createAnimationFolder);
    animButtons->addWidget(importBtn);
    animButtons->addWidget(folderBtn);
    animButtons->addStretch();
    animLayout->addLayout(animButtons);
    m_animTree = new QTreeWidget(animHost);
    m_animTree->setHeaderHidden(true);
    m_animTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_animTree->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::DoubleClicked);
    connect(m_animTree, &QTreeWidget::itemSelectionChanged, this,
            &CharacterEditorWindow::onAnimTreeSelectionChanged);
    connect(m_animTree, &QTreeWidget::itemChanged, this, &CharacterEditorWindow::onAnimTreeItemChanged);
    connect(m_animTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_document) return;
        QTreeWidgetItem* item = m_animTree->itemAt(pos);
        QMenu menu(this);
        menu.addAction(tr("Import Animation"), this, &CharacterEditorWindow::importAnimation);
        menu.addAction(tr("Create Folder"), this, &CharacterEditorWindow::createAnimationFolder);
        if (item) {
            const auto kind = static_cast<CharacterEditorItemKind>(item->data(0, kKindRole).toInt());
            if (kind == CharacterEditorItemKind::Folder) {
                menu.addAction(tr("Delete Folder"), this, [this, item]() {
                    std::string error;
                    m_document->DeleteEmptyAnimationFolder(
                        item->data(0, kIdRole).toString().toStdString(), error);
                    if (!error.empty()) {
                        QMessageBox::warning(this, tr("Folder"), QString::fromStdString(error));
                    }
                    rebuildAnimationTree();
                    updateWindowTitle();
                });
            }
            if (kind == CharacterEditorItemKind::Clip) {
                menu.addAction(tr("Rename"), this, [this, item]() { m_animTree->editItem(item, 0); });
                menu.addAction(tr("Duplicate"), this, [this, item]() {
                    std::string id, error;
                    m_document->DuplicateClip(item->data(0, kIdRole).toString().toStdString(), id, error);
                    rebuildAnimationTree();
                    updateWindowTitle();
                });
                menu.addAction(tr("Delete"), this, [this, item]() {
                    std::string error;
                    m_document->DeleteClip(item->data(0, kIdRole).toString().toStdString(), error);
                    rebuildAnimationTree();
                    refreshInspector();
                    updateWindowTitle();
                });
            }
        }
        menu.exec(m_animTree->mapToGlobal(pos));
    });
    animLayout->addWidget(m_animTree, 1);
    m_animationsDock->setWidget(animHost);
    addDockWidget(Qt::RightDockWidgetArea, m_animationsDock);
    tabifyDockWidget(m_inspectorDock, m_animationsDock);
    m_animationsDock->raise();

    m_timelineDock = new QDockWidget(tr("Playback"), this);
    auto* timelineHost = new QWidget(m_timelineDock);
    auto* timelineLayout = new QVBoxLayout(timelineHost);
    auto* controls = new QHBoxLayout();
    auto* play = new QPushButton(tr("Play/Pause"), timelineHost);
    auto* stop = new QPushButton(tr("Stop"), timelineHost);
    connect(play, &QPushButton::clicked, this, &CharacterEditorWindow::onPlayPause);
    connect(stop, &QPushButton::clicked, this, &CharacterEditorWindow::onStop);
    controls->addWidget(play);
    controls->addWidget(stop);
    controls->addStretch();
    m_timeline = new CharacterTimelineWidget(timelineHost);
    connect(m_timeline, &CharacterTimelineWidget::timeScrubbed, this, &CharacterEditorWindow::onScrub);
    timelineLayout->addLayout(controls);
    timelineLayout->addWidget(m_timeline);
    m_timelineDock->setWidget(timelineHost);
    addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);
}

void CharacterEditorWindow::initializeEngine() {
    m_engine = std::make_unique<QtEditorEngine>(this);
    connect(m_engine.get(), &QtEditorEngine::frameRendered, this, &CharacterEditorWindow::onFrameRendered);
    connect(m_viewportWindow, &QtViewportWindow::viewportReady, this, &CharacterEditorWindow::onViewportReady);
    m_engine->InitializeVulkan();
    m_engine->SetPreRenderCallback([this](float dt) {
        applyPreviewPlayback(dt);
        updateDebugOverlay();
        syncSocketGizmo();
    });
}

void CharacterEditorWindow::shutdownEngine() {
    if (!m_engine) return;
    if (m_viewportWindow) m_viewportWindow->SetInputEventCallback(nullptr);
    if (auto* ctx = m_engine->GetEditorContext()) {
        ctx->SetSelectionChangedCallback(nullptr);
        ctx->SetSceneEditedCallback(nullptr);
        ctx->SetExternalScene(nullptr);
    }
    m_engine->SetPreRenderCallback(nullptr);
    m_engine->Shutdown();
    m_engine.reset();
}

void CharacterEditorWindow::onViewportReady() {
    if (!m_engine || !m_engine->AttachToViewport(m_viewportWindow)) return;
    auto* ctx = m_engine->GetEditorContext();
    ctx->SetExternalScene(m_previewScene.get());
    ctx->SetGizmoMode(m_gizmoMode);
    ctx->SetGizmoSpace(m_gizmoSpace);
    ctx->GetSnapSettings().enableSnap = m_snapEnabled;
    auto& vs = ctx->GetViewportSettings();
    vs.showGrid = true;
    vs.showGizmo = true;
    vs.showSelectionOutline = true;
    vs.showShadows = true;
    vs.showSkybox = true;
    m_engine->StartRenderLoop();
    const auto root = projectRoot();
    if (!root.empty()) {
        m_engine->SetProjectRoot(root.generic_string());
        if (auto* fr = m_engine->GetFrameRenderer()) fr->SetProjectRoot(root.generic_string());
    }
    ctx->SetSceneEditedCallback([this]() {
        if (m_selection.kind == CharacterEditorItemKind::Socket) {
            pullSocketGizmo();
        }
    });
    bindPreview(true);
    if (!m_framedOnce && m_document && m_document->HasMeshSource()) {
        m_engine->FocusAll();
        m_framedOnce = true;
    }
}

bool CharacterEditorWindow::openDocument(const QString& path) {
    if (!m_document) return false;
    std::string error;
    m_document->SetProjectRoot(projectRoot());
    if (!m_document->OnOpen(std::filesystem::path(path.toStdString()), error)) {
        QMessageBox::critical(this, tr("Character Editor"), QString::fromStdString(error));
        return false;
    }
    m_statusPath->setText(path);
    if (auto* stack = qobject_cast<QStackedLayout*>(m_viewportHost->layout())) {
        stack->setCurrentIndex(m_document->HasMeshSource() ? 0 : 1);
    }
    return true;
}

void CharacterEditorWindow::registerOpenDocument() {
    EditorAssetServices::Instance().Documents().Register(
        DocumentKind::Character, m_pathStr.toStdString(), IsDirty());
}

void CharacterEditorWindow::unregisterOpenDocument() {
    EditorAssetServices::Instance().Documents().Unregister(m_pathStr.toStdString());
}

void CharacterEditorWindow::updateWindowTitle() {
    QString name = m_document ? QString::fromStdString(m_document->GetDisplayName()) : tr("Character");
    if (IsDirty()) name += " *";
    setWindowTitle(tr("Character: %1").arg(name));
    if (m_undoAction) m_undoAction->setEnabled(m_document && m_document->CanUndo());
    if (m_redoAction) m_redoAction->setEnabled(m_document && m_document->CanRedo());
    EditorAssetServices::Instance().Documents().SetDirty(m_pathStr.toStdString(), IsDirty());
}

bool CharacterEditorWindow::promptSaveIfDirty() {
    if (!IsDirty()) return true;
    const auto result = QMessageBox::question(this, tr("Unsaved Changes"),
        tr("The character '%1' has unsaved changes.\nSave before closing?")
            .arg(QFileInfo(m_pathStr).fileName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (result == QMessageBox::Save) {
        std::string error;
        if (!SaveDocument(error)) {
            QMessageBox::critical(this, tr("Save Error"), QString::fromStdString(error));
            return false;
        }
        return true;
    }
    return result == QMessageBox::Discard;
}

std::filesystem::path CharacterEditorWindow::projectRoot() const {
    return EditorAssetServices::Instance().Documents().GetProjectRoot();
}

void CharacterEditorWindow::onSave() {
    std::string error;
    if (!SaveDocument(error)) {
        QMessageBox::critical(this, tr("Save Error"), QString::fromStdString(error));
    }
}

void CharacterEditorWindow::onUndo() {
    if (!m_document) return;
    std::string error;
    if (!m_document->Undo(error)) return;
    bindPreview(false);
    rebuildTree();
    refreshInspector();
    updateWindowTitle();
}

void CharacterEditorWindow::onRedo() {
    if (!m_document) return;
    std::string error;
    if (!m_document->Redo(error)) return;
    bindPreview(false);
    rebuildTree();
    refreshInspector();
    updateWindowTitle();
}

void CharacterEditorWindow::onFrameSelected() {
    if (m_engine) m_engine->FocusOnSelection();
}

void CharacterEditorWindow::onFrameAll() {
    if (m_engine) m_engine->FocusAll();
}

void CharacterEditorWindow::onSelectTranslate() {
    m_gizmoMode = GizmoMode::Translate;
    if (m_engine && m_engine->GetEditorContext()) m_engine->GetEditorContext()->SetGizmoMode(m_gizmoMode);
}

void CharacterEditorWindow::onSelectRotate() {
    m_gizmoMode = GizmoMode::Rotate;
    if (m_engine && m_engine->GetEditorContext()) m_engine->GetEditorContext()->SetGizmoMode(m_gizmoMode);
}

void CharacterEditorWindow::onToggleLocalWorld(bool local) {
    m_gizmoSpace = local ? GizmoSpace::Local : GizmoSpace::World;
    m_localWorldAction->setText(local ? tr("Local") : tr("World"));
    if (m_engine && m_engine->GetEditorContext()) m_engine->GetEditorContext()->SetGizmoSpace(m_gizmoSpace);
}

void CharacterEditorWindow::onToggleSnap(bool enabled) {
    m_snapEnabled = enabled;
    if (m_engine && m_engine->GetEditorContext()) m_engine->GetEditorContext()->GetSnapSettings().enableSnap = enabled;
}

void CharacterEditorWindow::onToggleSkeleton(bool enabled) { m_showSkeleton = enabled; }
void CharacterEditorWindow::onToggleBoneNames(bool enabled) { m_showBoneNames = enabled; }

void CharacterEditorWindow::onPlayPause() {
    Entity entity = previewCharacter();
    if (!entity) return;
    auto& character = entity.GetComponent<CharacterComponent>();
    if (m_previewPlaying && !m_previewPaused) {
        CharacterAnimationSystem::Pause(character);
        m_previewPaused = true;
        return;
    }
    if (m_previewClipId.empty()) return;
    std::string error;
    if (!m_previewPlaying) {
        CharacterAnimationSystem::Play(character, m_previewClipId, error);
        CharacterAnimationSystem::TickEntity(character, 0.0f);
    } else {
        CharacterAnimationSystem::Resume(character);
    }
    m_previewPlaying = true;
    m_previewPaused = false;
}

void CharacterEditorWindow::onStop() {
    Entity entity = previewCharacter();
    if (!entity) return;
    CharacterAnimationSystem::Stop(entity.GetComponent<CharacterComponent>());
    m_previewPlaying = false;
    m_previewPaused = false;
    m_previewTime = 0.0f;
}

void CharacterEditorWindow::onScrub(float time) {
    Entity entity = previewCharacter();
    if (!entity || m_previewClipId.empty()) return;
    std::string error;
    CharacterAnimationSystem::SampleAtTime(entity.GetComponent<CharacterComponent>(), m_previewClipId, time, error);
    m_previewTime = time;
    if (m_previewPlaying && !m_previewPaused) {
        entity.GetComponent<CharacterComponent>().player.playing = true;
        entity.GetComponent<CharacterComponent>().player.paused = false;
    }
}

void CharacterEditorWindow::onFrameRendered(float dt) {
    if (dt > 0.0f) m_statusFps->setText(QString("FPS %1").arg(1.0f / dt, 0, 'f', 1));
}

void CharacterEditorWindow::applyPreviewPlayback(float dt) {
    Entity entity = previewCharacter();
    if (!entity) return;
    auto& character = entity.GetComponent<CharacterComponent>();
    character.suppressAutoPlay = true;
    if (m_previewPlaying && !m_previewPaused) {
        CharacterAnimationSystem::TickEntity(character, dt);
        m_previewTime = character.player.time;
        if (const CharacterClipEntry* clip = m_document->FindClipById(m_previewClipId)) {
            m_previewTime = clip->startTime + character.player.time;
            m_timeline->setRange(std::max(clip->endTime, clip->startTime + 0.001f), clip->startTime,
                                 clip->endTime > clip->startTime ? clip->endTime : clip->startTime + 1.0f,
                                 m_previewTime);
        }
    }
}

void CharacterEditorWindow::bindPreview(bool forceGpu) {
    if (!m_document) return;
    std::string error;
    const bool ok = m_document->BindPreview(m_engine ? m_engine->GetFrameRenderer() : nullptr, true,
                                            forceGpu, error);
    if (auto* stack = qobject_cast<QStackedLayout*>(m_viewportHost->layout())) {
        stack->setCurrentIndex(m_document->HasMeshSource() && ok ? 0 : 1);
    }
    if (!ok && !error.empty()) m_banner->setText(QString::fromStdString(error));
    if (m_engine) m_engine->RebuildSceneMeshes();
}

Entity CharacterEditorWindow::previewCharacter() const {
    return m_document ? m_document->FindPreviewCharacter() : Entity{};
}

void CharacterEditorWindow::updateDebugOverlay() {
    if (!m_engine) return;
    std::vector<EditorDebugLine> lines;
    std::vector<EditorDebugPoint> points;
    Entity entity = previewCharacter();
    if (entity && m_showSkeleton) {
        std::string selectedBone;
        if (m_selection.kind == CharacterEditorItemKind::Bone) selectedBone = m_selection.id;
        FillSkeletonDebugOverlay(entity.GetComponent<CharacterComponent>(), selectedBone, lines, points);
    }
    if (entity && m_document) {
        auto& character = entity.GetComponent<CharacterComponent>();
        for (const CharacterSocket& socket : m_document->GetAsset().sockets) {
            Mat4 world;
            std::string error;
            if (!EvaluateSocketWorld(character, socket, world, error)) continue;
            const Vec3 origin(world.m[12], world.m[13], world.m[14]);
            const Vec3 axisX(world.m[0], world.m[1], world.m[2]);
            const Vec3 axisY(world.m[4], world.m[5], world.m[6]);
            const Vec3 axisZ(world.m[8], world.m[9], world.m[10]);
            const bool selected = m_selection.kind == CharacterEditorItemKind::Socket && m_selection.id == socket.name;
            const float scale = selected ? 0.18f : 0.12f;
            lines.push_back({origin, vadd(origin, vscale(axisX, scale)), Vec3(1, 0.2f, 0.2f)});
            lines.push_back({origin, vadd(origin, vscale(axisY, scale)), Vec3(0.2f, 1, 0.2f)});
            lines.push_back({origin, vadd(origin, vscale(axisZ, scale)), Vec3(0.3f, 0.5f, 1)});
        }
    }
    m_engine->SetDebugOverlay(std::move(lines), std::move(points));
}

void CharacterEditorWindow::syncSocketGizmo() {
    if (!m_document || !m_engine) return;
    Entity gizmo = m_document->FindSocketGizmo();
    Entity preview = previewCharacter();
    if (!gizmo || !preview) return;
    auto* ctx = m_engine->GetEditorContext();
    if (m_selection.kind != CharacterEditorItemKind::Socket) {
        ctx->ClearSelection();
        return;
    }
    const CharacterSocket* socket = m_document->FindSocketByName(m_selection.id);
    if (!socket) return;
    Mat4 world;
    std::string error;
    if (!EvaluateSocketWorld(preview.GetComponent<CharacterComponent>(), *socket, world, error)) return;
    gizmo.GetComponent<TransformComponent>().SetFromMatrix(world);
    ctx->SelectEntity(static_cast<entt::entity>(gizmo));
}

void CharacterEditorWindow::pullSocketGizmo() {
    if (!m_document || m_selection.kind != CharacterEditorItemKind::Socket) return;
    Entity gizmo = m_document->FindSocketGizmo();
    Entity preview = previewCharacter();
    const CharacterSocket* socket = m_document->FindSocketByName(m_selection.id);
    if (!gizmo || !preview || !socket) return;
    const auto& character = preview.GetComponent<CharacterComponent>();
    const SkeletonBone* bone = character.skeleton.FindBone(socket->bone);
    if (!bone || bone->evalIndex < 0 || bone->evalIndex >= static_cast<int>(character.worldPose.size())) return;
    const Mat4 boneInv = Mat4::Invert(character.worldPose[static_cast<size_t>(bone->evalIndex)]);
    const Mat4 local = Mat4::Multiply(boneInv, gizmo.GetComponent<TransformComponent>().GetLocalMatrix());
    CharacterSocket edited = *socket;
    DecomposeSocketLocal(local, edited);
    std::string error;
    CharacterSocket* dest = m_document->FindSocketByName(m_selection.id);
    if (!dest) {
        return;
    }
    if (!m_gizmoDragging) {
        m_gizmoDragging = true;
        m_document->BeginAuthoredEdit();
    }
    dest->position[0] = edited.position[0];
    dest->position[1] = edited.position[1];
    dest->position[2] = edited.position[2];
    dest->rotation[0] = edited.rotation[0];
    dest->rotation[1] = edited.rotation[1];
    dest->rotation[2] = edited.rotation[2];
    m_document->MarkAuthoredChanged();
    updateWindowTitle();
}

CharacterEditorSelection CharacterEditorWindow::currentSelection() const { return m_selection; }

void CharacterEditorWindow::applyTreeItemSelection(QTreeWidgetItem* item) {
    if (!item || !m_document) return;
    m_selection.kind = static_cast<CharacterEditorItemKind>(item->data(0, kKindRole).toInt());
    m_selection.id = item->data(0, kIdRole).toString().toStdString();
    m_selection.index = item->data(0, kIndexRole).toInt();
    if (m_selection.kind == CharacterEditorItemKind::Clip) {
        m_previewClipId = m_selection.id;
        if (const CharacterClipEntry* clip = m_document->FindClipById(m_previewClipId)) {
            m_previewTime = clip->startTime;
            if (m_timeline) {
                m_timeline->setRange(std::max(clip->endTime, 0.001f), clip->startTime,
                                     clip->endTime > clip->startTime ? clip->endTime : clip->startTime + 1.0f,
                                     clip->startTime);
            }
        }
        Entity entity = previewCharacter();
        if (entity) {
            std::string error;
            CharacterAnimationSystem::SampleAtTime(entity.GetComponent<CharacterComponent>(),
                                                   m_previewClipId, m_previewTime, error);
        }
    }
    m_gizmoDragging = false;
}

void CharacterEditorWindow::onTreeSelectionChanged() {
    QList<QTreeWidgetItem*> items = m_tree->selectedItems();
    if (items.isEmpty()) return;
    applyTreeItemSelection(items.front());
    if (m_animTree) {
        QSignalBlocker blocker(m_animTree);
        m_animTree->clearSelection();
    }
    if (m_inspectorDock) m_inspectorDock->raise();
    refreshInspector();
}

void CharacterEditorWindow::onAnimTreeSelectionChanged() {
    if (!m_animTree) return;
    QList<QTreeWidgetItem*> items = m_animTree->selectedItems();
    if (items.isEmpty()) return;
    applyTreeItemSelection(items.front());
    if (m_tree) {
        QSignalBlocker blocker(m_tree);
        m_tree->clearSelection();
    }
    refreshInspector();
}

void CharacterEditorWindow::onAnimTreeItemChanged(QTreeWidgetItem* item, int) {
    if (!item || !m_document) return;
    const auto kind = static_cast<CharacterEditorItemKind>(item->data(0, kKindRole).toInt());
    if (kind != CharacterEditorItemKind::Clip) return;
    const std::string oldId = item->data(0, kIdRole).toString().toStdString();
    const std::string typed = item->text(0).trimmed().toStdString();
    if (const CharacterClipEntry* clip = m_document->FindClipById(oldId)) {
        if (typed == clip->displayName) {
            return;
        }
    }
    std::string newId;
    std::string error;
    if (!m_document->RenameClip(oldId, typed, newId, error)) {
        if (!error.empty()) {
            QMessageBox::warning(this, tr("Rename"), QString::fromStdString(error));
        }
        rebuildAnimationTree();
        return;
    }
    m_selection.kind = CharacterEditorItemKind::Clip;
    m_selection.id = newId;
    m_selection.index = -1;
    if (m_previewClipId == oldId) {
        m_previewClipId = newId;
    }
    rebuildAnimationTree();
    refreshInspector();
    updateWindowTitle();
}

void CharacterEditorWindow::rebuildTree() {
    if (!m_tree || !m_document) return;
    QSignalBlocker blocker(m_tree);
    m_tree->clear();
    const CharacterAsset& asset = m_document->GetAsset();
    auto* meshItem = AddTop(m_tree, tr("Mesh"), CharacterEditorItemKind::Mesh);
    std::vector<std::string> meshPartNames;
    for (const CharacterSubmeshSlot& slot : asset.mesh.submeshes) {
        if (slot.meshPart.empty()) {
            continue;
        }
        if (std::find(meshPartNames.begin(), meshPartNames.end(), slot.meshPart) ==
            meshPartNames.end()) {
            meshPartNames.push_back(slot.meshPart);
        }
    }
    for (const std::string& partName : meshPartNames) {
        AddItem(meshItem, QString::fromStdString(partName), CharacterEditorItemKind::MeshPart,
                partName);
    }
    auto* skeleton = AddTop(m_tree, tr("Skeleton"), CharacterEditorItemKind::Skeleton);
    Entity preview = previewCharacter();
    if (preview) {
        const auto& bones = preview.GetComponent<CharacterComponent>().skeleton.bones;
        std::vector<QTreeWidgetItem*> items(bones.size(), nullptr);
        for (size_t i = 0; i < bones.size(); ++i) {
            QTreeWidgetItem* parent = skeleton;
            if (bones[i].parentEval >= 0 && static_cast<size_t>(bones[i].parentEval) < items.size() &&
                items[static_cast<size_t>(bones[i].parentEval)]) {
                parent = items[static_cast<size_t>(bones[i].parentEval)];
            }
            items[i] = AddItem(parent, QString::fromStdString(bones[i].name), CharacterEditorItemKind::Bone,
                               bones[i].name);
        }
    }
    auto* materials = AddTop(m_tree, tr("Materials"), CharacterEditorItemKind::Materials);
    std::string currentPart;
    QTreeWidgetItem* partItem = materials;
    for (int i = 0; i < static_cast<int>(asset.mesh.submeshes.size()); ++i) {
        const CharacterSubmeshSlot& slot = asset.mesh.submeshes[static_cast<size_t>(i)];
        if (!slot.meshPart.empty() && slot.meshPart != currentPart) {
            currentPart = slot.meshPart;
            partItem = AddItem(materials, QString::fromStdString(currentPart),
                               CharacterEditorItemKind::MeshPart, currentPart);
        }
        QTreeWidgetItem* parent = slot.meshPart.empty() ? materials : partItem;
        AddItem(parent, QString::fromStdString(slot.name), CharacterEditorItemKind::MaterialSlot,
                slot.name, i);
    }
    auto* sockets = AddTop(m_tree, tr("Sockets"), CharacterEditorItemKind::Sockets);
    for (const CharacterSocket& socket : asset.sockets) {
        AddItem(sockets, QString::fromStdString(socket.name), CharacterEditorItemKind::Socket, socket.name);
    }
    AddTop(m_tree, tr("Controller"), CharacterEditorItemKind::Controller);
    m_tree->expandAll();
    rebuildAnimationTree();
}

void CharacterEditorWindow::rebuildAnimationTree() {
    if (!m_animTree || !m_document) return;
    const std::string selectedId =
        (m_selection.kind == CharacterEditorItemKind::Clip ||
         m_selection.kind == CharacterEditorItemKind::Folder)
            ? m_selection.id
            : std::string{};
    const CharacterEditorItemKind selectedKind = m_selection.kind;
    QSignalBlocker blocker(m_animTree);
    m_animTree->clear();
    const CharacterAsset& asset = m_document->GetAsset();
    std::unordered_map<std::string, QTreeWidgetItem*> folders;
    QTreeWidgetItem* restore = nullptr;
    for (const std::string& folder : asset.animationFolders) {
        QTreeWidgetItem* item =
            AddTop(m_animTree, QString::fromStdString(folder), CharacterEditorItemKind::Folder, folder);
        folders[folder] = item;
        if (selectedKind == CharacterEditorItemKind::Folder && selectedId == folder) {
            restore = item;
        }
    }
    for (const CharacterClipEntry& clip : asset.clips) {
        QTreeWidgetItem* parent = nullptr;
        if (!clip.folder.empty() && folders.count(clip.folder)) parent = folders[clip.folder];
        const QString label = QString::fromStdString(clip.displayName.empty() ? clip.id : clip.displayName);
        QTreeWidgetItem* item = parent
            ? AddItem(parent, label, CharacterEditorItemKind::Clip, clip.id)
            : AddTop(m_animTree, label, CharacterEditorItemKind::Clip, clip.id);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        if (selectedKind == CharacterEditorItemKind::Clip && selectedId == clip.id) {
            restore = item;
        }
    }
    m_animTree->expandAll();
    if (restore) {
        m_animTree->setCurrentItem(restore);
    }
}

void CharacterEditorWindow::refreshInspector() {
    if (!m_inspectorHost || !m_document) return;
    m_refreshingInspector = true;
    auto* layout = qobject_cast<QVBoxLayout*>(m_inspectorHost->layout());
    QLayoutItem* child = nullptr;
    while ((child = layout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    const CharacterAsset& asset = m_document->GetAsset();
    auto addLabel = [&](const QString& text) {
        auto* label = new QLabel(text, m_inspectorHost);
        label->setWordWrap(true);
        layout->addWidget(label);
        return label;
    };

    if (m_selection.kind == CharacterEditorItemKind::Mesh) {
        const std::string source = asset.mesh.source.empty() ? asset.skeleton.source : asset.mesh.source;
        QString format = tr("GLB");
        if (source.size() >= 4 &&
            (source.rfind(".fbx") == source.size() - 4 || source.rfind(".FBX") == source.size() - 4)) {
            format = tr("FBX");
        }
        addLabel(tr("Source Format: %1").arg(format));
        addLabel(tr("Mesh: %1").arg(QString::fromStdString(asset.mesh.source)));
        addLabel(tr("Skeleton: %1").arg(QString::fromStdString(asset.skeleton.source)));
        Entity preview = previewCharacter();
        const int bones = preview ? preview.GetComponent<CharacterComponent>().skeleton.BoneCount() : 0;
        addLabel(tr("Bones: %1").arg(bones));
        addLabel(tr("Clips: %1").arg(asset.clips.size()));
        addLabel(tr("Skinned Meshes: %1").arg([&]() {
            int count = 0;
            std::string last;
            for (const CharacterSubmeshSlot& slot : asset.mesh.submeshes) {
                if (!slot.meshPart.empty() && slot.meshPart != last) {
                    last = slot.meshPart;
                    ++count;
                }
            }
            return count > 0 ? count : 1;
        }()));
        addLabel(tr("Slots: %1").arg(asset.mesh.submeshes.size()));
        addLabel(tr("Signature: %1").arg(QString::fromStdString(asset.skeleton.signature)));
        auto* reveal = new QPushButton(tr("Reveal Source"), m_inspectorHost);
        connect(reveal, &QPushButton::clicked, this, [this, asset]() { revealPath(asset.mesh.source); });
        auto* reimport = new QPushButton(tr("Reimport Character"), m_inspectorHost);
        connect(reimport, &QPushButton::clicked, this, [this]() { reimportCharacter(); });
        layout->addWidget(reveal);
        layout->addWidget(reimport);
    } else if (m_selection.kind == CharacterEditorItemKind::Bone) {
        addLabel(tr("Bone: %1").arg(QString::fromStdString(m_selection.id)));
        Entity preview = previewCharacter();
        if (preview) {
            if (const SkeletonBone* bone = preview.GetComponent<CharacterComponent>().skeleton.FindBone(m_selection.id)) {
                addLabel(tr("Parent eval: %1").arg(bone->parentEval));
                addLabel(tr("Eval index: %1").arg(bone->evalIndex));
            }
        }
        auto* addSocket = new QPushButton(tr("Add Socket"), m_inspectorHost);
        connect(addSocket, &QPushButton::clicked, this, [this]() {
            const QString name = QInputDialog::getText(this, tr("Socket"), tr("Name"));
            std::string error;
            if (!name.isEmpty()) m_document->CreateSocket(name.toStdString(), m_selection.id, error);
            rebuildTree();
            updateWindowTitle();
        });
        layout->addWidget(addSocket);
    } else if (m_selection.kind == CharacterEditorItemKind::MeshPart) {
        addLabel(tr("Mesh Part: %1").arg(QString::fromStdString(m_selection.id)));
        int slotCount = 0;
        for (const CharacterSubmeshSlot& slot : asset.mesh.submeshes) {
            if (slot.meshPart == m_selection.id) {
                ++slotCount;
            }
        }
        addLabel(tr("Material slots: %1").arg(slotCount));
    } else if (m_selection.kind == CharacterEditorItemKind::MaterialSlot) {
        if (m_selection.index >= 0 && m_selection.index < static_cast<int>(asset.mesh.submeshes.size())) {
            const CharacterSubmeshSlot& slot = asset.mesh.submeshes[static_cast<size_t>(m_selection.index)];
            if (!slot.meshPart.empty()) {
                addLabel(tr("Mesh Part: %1").arg(QString::fromStdString(slot.meshPart)));
            }
            auto* mat = new AssetReferenceEditor(tr("Material"), AssetType::Material, m_inspectorHost);
            mat->setAllowEmpty(true);
            mat->setPath(QString::fromStdString(asset.mesh.submeshes[static_cast<size_t>(m_selection.index)].material));
            connect(mat, &AssetReferenceEditor::pathEdited, this, [this](const QString& path) {
                std::string error;
                m_document->SetMaterialSlot(m_selection.index, path.toStdString(), error);
                updateWindowTitle();
                bindPreview(false);
            });
            connect(mat, &AssetReferenceEditor::openRequested, this, [this](const QString& portable) {
                const auto abs = AssetPathPolicy::ResolvePortableKey(projectRoot(), portable.toStdString());
                OpenMaterialGraphEditorDialog(this, QString::fromStdString(abs.generic_string()));
            });
            layout->addWidget(mat);
        }
    } else if (m_selection.kind == CharacterEditorItemKind::Clip) {
        CharacterClipEntry* clip = m_document->FindClipById(m_selection.id);
        if (clip) {
            auto* name = new QLineEdit(QString::fromStdString(clip->id), m_inspectorHost);
            connect(name, &QLineEdit::editingFinished, this, [this, name]() {
                const std::string typed = name->text().trimmed().toStdString();
                if (typed == m_selection.id) return;
                std::string error;
                std::string newId;
                if (!m_document->RenameClip(m_selection.id, typed, newId, error)) {
                    if (!error.empty()) {
                        QMessageBox::warning(this, tr("Rename"), QString::fromStdString(error));
                    }
                    refreshInspector();
                    return;
                }
                if (m_previewClipId == m_selection.id) {
                    m_previewClipId = newId;
                }
                m_selection.id = newId;
                rebuildAnimationTree();
                updateWindowTitle();
            });
            layout->addWidget(new QLabel(tr("Name"), m_inspectorHost));
            layout->addWidget(name);
            addLabel(tr("Used as the clip id for playback and controllers."));
            auto* folder = new QLineEdit(QString::fromStdString(clip->folder), m_inspectorHost);
            connect(folder, &QLineEdit::editingFinished, this, [this, folder]() {
                std::string error;
                m_document->SetClipFolder(m_selection.id, folder->text().toStdString(), error);
                rebuildAnimationTree();
                updateWindowTitle();
            });
            layout->addWidget(new QLabel(tr("Folder"), m_inspectorHost));
            layout->addWidget(folder);
            addLabel(tr("Source: %1").arg(QString::fromStdString(clip->source)));
            addLabel(tr("Source Clip: %1").arg(QString::fromStdString(clip->sourceClip)));
            auto* loop = new BoolPropertyEditor(tr("Loop"), m_inspectorHost);
            loop->setValue(clip->loop);
            connect(loop, &BoolPropertyEditor::valueEdited, this, [this](bool value) {
                std::string error;
                m_document->SetClipLoop(m_selection.id, value, error);
                m_document->SyncPreviewClipMetadata();
                updateWindowTitle();
            });
            layout->addWidget(loop);
            auto* speed = new NumericPropertyEditor(tr("Playback Speed"), m_inspectorHost);
            speed->setRange(0.01, 10.0);
            speed->setValue(clip->speed);
            connect(speed, &NumericPropertyEditor::valueEdited, this, [this](double value) {
                std::string error;
                m_document->SetClipSpeed(m_selection.id, static_cast<float>(value), error);
                m_document->SyncPreviewClipMetadata();
                updateWindowTitle();
            });
            layout->addWidget(speed);
            auto* start = new NumericPropertyEditor(tr("Trim Start"), m_inspectorHost);
            start->setRange(0.0, 1000.0);
            start->setValue(clip->startTime);
            auto* end = new NumericPropertyEditor(tr("Trim End"), m_inspectorHost);
            end->setRange(0.0, 1000.0);
            end->setValue(clip->endTime);
            auto applyTrim = [this, start, end]() {
                std::string error;
                m_document->SetClipTrim(m_selection.id, static_cast<float>(start->value()),
                                        static_cast<float>(end->value()), error);
                m_document->SyncPreviewClipMetadata();
                updateWindowTitle();
            };
            connect(start, &NumericPropertyEditor::editingFinished, this, applyTrim);
            connect(end, &NumericPropertyEditor::editingFinished, this, applyTrim);
            layout->addWidget(start);
            layout->addWidget(end);
            auto* def = new QPushButton(tr("Set as Default"), m_inspectorHost);
            connect(def, &QPushButton::clicked, this, [this]() {
                std::string error;
                m_document->SetDefaultAnimation(m_selection.id, error);
                updateWindowTitle();
            });
            layout->addWidget(def);
        }
    } else if (m_selection.kind == CharacterEditorItemKind::Socket) {
        CharacterSocket* socket = m_document->FindSocketByName(m_selection.id);
        if (socket) {
            auto* name = new QLineEdit(QString::fromStdString(socket->name), m_inspectorHost);
            connect(name, &QLineEdit::editingFinished, this, [this, name]() {
                std::string error;
                m_document->RenameSocket(m_selection.id, name->text().toStdString(), error);
                m_selection.id = name->text().toStdString();
                rebuildTree();
                updateWindowTitle();
            });
            layout->addWidget(new QLabel(tr("Name"), m_inspectorHost));
            layout->addWidget(name);
            auto* bone = new QComboBox(m_inspectorHost);
            Entity preview = previewCharacter();
            if (preview) {
                for (const auto& skelBone : preview.GetComponent<CharacterComponent>().skeleton.bones) {
                    bone->addItem(QString::fromStdString(skelBone.name));
                }
            }
            bone->setCurrentText(QString::fromStdString(socket->bone));
            connect(bone, &QComboBox::currentTextChanged, this, [this](const QString& value) {
                std::string error;
                m_document->SetSocketBone(m_selection.id, value.toStdString(), error);
                updateWindowTitle();
            });
            layout->addWidget(new QLabel(tr("Bone"), m_inspectorHost));
            layout->addWidget(bone);
            auto* pos = new Vec3PropertyEditor(tr("Position"), m_inspectorHost);
            pos->setValue(Vec3(socket->position[0], socket->position[1], socket->position[2]));
            connect(pos, &Vec3PropertyEditor::axisEdited, this, [this](int axis, double value) {
                CharacterSocket* live = m_document->FindSocketByName(m_selection.id);
                if (!live) return;
                float p[3] = {live->position[0], live->position[1], live->position[2]};
                float r[3] = {live->rotation[0], live->rotation[1], live->rotation[2]};
                p[axis] = static_cast<float>(value);
                std::string error;
                m_document->SetSocketTransform(m_selection.id, p, r, error);
                updateWindowTitle();
            });
            layout->addWidget(pos);
            auto* rot = new Vec3PropertyEditor(tr("Rotation (deg)"), m_inspectorHost);
            rot->setValue(Vec3(socket->rotation[0] * 57.2958f, socket->rotation[1] * 57.2958f,
                               socket->rotation[2] * 57.2958f));
            connect(rot, &Vec3PropertyEditor::axisEdited, this, [this](int axis, double value) {
                CharacterSocket* live = m_document->FindSocketByName(m_selection.id);
                if (!live) return;
                float p[3] = {live->position[0], live->position[1], live->position[2]};
                float r[3] = {live->rotation[0], live->rotation[1], live->rotation[2]};
                r[axis] = static_cast<float>(value) * 0.0174533f;
                std::string error;
                m_document->SetSocketTransform(m_selection.id, p, r, error);
                updateWindowTitle();
            });
            layout->addWidget(rot);
            auto* del = new QPushButton(tr("Delete Socket"), m_inspectorHost);
            connect(del, &QPushButton::clicked, this, [this]() {
                std::string error;
                m_document->DeleteSocket(m_selection.id, error);
                rebuildTree();
                refreshInspector();
                updateWindowTitle();
            });
            layout->addWidget(del);
        }
    } else if (m_selection.kind == CharacterEditorItemKind::Controller) {
        auto* enabled = new BoolPropertyEditor(tr("Enabled"), m_inspectorHost);
        enabled->setValue(asset.controller.enabled);
        connect(enabled, &BoolPropertyEditor::valueEdited, this, [this](bool value) {
            m_document->SetControllerEnabled(value);
            updateWindowTitle();
        });
        layout->addWidget(enabled);
        auto* script = new AssetReferenceEditor(tr("Script"), AssetType::Script, m_inspectorHost);
        script->setPath(QString::fromStdString(asset.controller.scriptPath));
        connect(script, &AssetReferenceEditor::pathEdited, this, [this](const QString& path) {
            m_document->SetControllerScript(path.toStdString());
            updateWindowTitle();
        });
        connect(script, &AssetReferenceEditor::openRequested, this, [this](const QString& portable) {
            const auto abs = AssetPathPolicy::ResolvePortableKey(projectRoot(), portable.toStdString());
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(abs.generic_string())));
        });
        layout->addWidget(script);
        addLabel(tr("Exposed properties are authored on scene instances. Preview does not run the controller; Play Mode does."));
    }
    layout->addStretch();
    m_refreshingInspector = false;
}

void CharacterEditorWindow::revealPath(const std::string& portable) {
    const auto abs = AssetPathPolicy::ResolvePortableKey(projectRoot(), portable);
    if (!abs.empty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(abs.parent_path().generic_string())));
    }
}

void CharacterEditorWindow::importAnimation() {
    if (!m_document || !m_document->HasMeshSource()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Animation"), QString(),
        tr("Character sources (*.glb *.fbx);;GLB (*.glb);;FBX (*.fbx)"));
    if (path.isEmpty()) return;
    std::string error;
    SkeletalImportSettings settings = CharacterImporter::MakeSettings(m_document->GetAsset().import);
    Entity preview = previewCharacter();
    if (!preview) return;
    std::vector<AnimationClip> clips;
    if (!SkeletalModelImporter::LoadClips(std::filesystem::path(path.toStdString()), settings,
                                          preview.GetComponent<CharacterComponent>().skeleton, clips, error)) {
        QMessageBox::warning(this, tr("Import Animation"),
                             tr("Animation cannot be imported.\n\nSkeleton incompatibility detected.\n%1")
                                 .arg(QString::fromStdString(error)));
        return;
    }
    CharacterAnimationImportDialog dialog(clips, m_document->GetAsset().animationFolders,
                                          QFileInfo(path).completeBaseName(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_document->BeginAuthoredEdit();
    std::vector<std::string> added;
    if (!CharacterImporter::ImportAnimationClips(projectRoot(), m_document->GetAsset(),
                                                 m_document->GetPortablePath(),
                                                 std::filesystem::path(path.toStdString()),
                                                 dialog.selectedClips(),
                                                 dialog.destinationFolder().toStdString(), added, error)) {
        QMessageBox::warning(this, tr("Import Animation"), QString::fromStdString(error));
        return;
    }
    m_document->MarkAuthoredChanged();
    CharacterRuntime::Invalidate(m_document->GetPortablePath());
    bindPreview(false);
    if (!added.empty()) {
        m_selection.kind = CharacterEditorItemKind::Clip;
        m_selection.id = added.front();
        m_selection.index = -1;
    }
    rebuildAnimationTree();
    if (m_animTree && m_animTree->currentItem()) {
        applyTreeItemSelection(m_animTree->currentItem());
    }
    if (m_animationsDock) m_animationsDock->raise();
    refreshInspector();
    updateWindowTitle();
}

void CharacterEditorWindow::createAnimationFolder() {
    if (!m_document) return;
    const QString name = QInputDialog::getText(this, tr("Folder"), tr("Name"));
    if (name.isEmpty()) return;
    std::string error;
    if (!m_document->AddAnimationFolder(name.toStdString(), error)) {
        if (!error.empty()) {
            QMessageBox::warning(this, tr("Folder"), QString::fromStdString(error));
        }
        return;
    }
    m_selection.kind = CharacterEditorItemKind::Folder;
    m_selection.id = name.toStdString();
    m_selection.index = -1;
    rebuildAnimationTree();
    updateWindowTitle();
}

void CharacterEditorWindow::reimportCharacter() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Reimport Character"), QString(),
        tr("Character sources (*.glb *.fbx);;GLB (*.glb);;FBX (*.fbx)"));
    if (path.isEmpty() || !m_document) return;
    std::string error;
    if (!CharacterImporter::Reimport(projectRoot(), m_document->GetPortablePath(),
                                     std::filesystem::path(path.toStdString()), error)) {
        QMessageBox::warning(this, tr("Reimport Character"), QString::fromStdString(error));
        return;
    }
    m_document->OnOpen(m_document->GetPath(), error);
    bindPreview(true);
    rebuildTree();
    refreshInspector();
    updateWindowTitle();
}

void CharacterEditorWindow::assignSource() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Character Source"), QString(),
        tr("Character sources (*.glb *.fbx);;GLB (*.glb);;FBX (*.fbx)"));
    if (path.isEmpty() || !m_document) return;
    std::string error;
    if (!CharacterImporter::AssignCharacterSource(projectRoot(), m_document->GetPortablePath(),
                                                  std::filesystem::path(path.toStdString()), error)) {
        QMessageBox::warning(this, tr("Assign Source"), QString::fromStdString(error));
        return;
    }
    m_document->OnOpen(m_document->GetPath(), error);
    bindPreview(true);
    rebuildTree();
    if (m_engine) m_engine->FocusAll();
    updateWindowTitle();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
