#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QMainWindow>
#include <QCloseEvent>
#include <QString>
#include <memory>
#include <filesystem>
#include <string>

#include "../../Scene/Scene.h"
#include "../../UI/CharacterDocument.h"
#include "../../Core/EditorContext.h"

class QDockWidget;
class QToolBar;
class QAction;
class QActionGroup;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class QLineEdit;
class QSlider;

namespace Hephaestus {
namespace QtEditor {

class EditorMainWindow;
class QtViewportWindow;
class QtEditorEngine;
class CharacterTimelineWidget;
class AssetReferenceEditor;
class NumericPropertyEditor;
class BoolPropertyEditor;
class Vec3PropertyEditor;

class CharacterEditorWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit CharacterEditorWindow(const QString& characterPath,
                                   EditorMainWindow* parentEditor,
                                   QWidget* parent = nullptr);
    ~CharacterEditorWindow() override;

    QString absolutePath() const { return m_pathStr; }
    bool IsDirty() const;
    bool SaveDocument(std::string& outError);
    void RetargetPath(const QString& newAbsolutePath);

signals:
    void windowClosed(const QString& characterPath);
    void characterSaved(const QString& characterPath);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onSave();
    void onUndo();
    void onRedo();
    void onFrameSelected();
    void onFrameAll();
    void onSelectTranslate();
    void onSelectRotate();
    void onToggleLocalWorld(bool local);
    void onToggleSnap(bool enabled);
    void onToggleSkeleton(bool enabled);
    void onToggleBoneNames(bool enabled);
    void onTreeSelectionChanged();
    void onAnimTreeSelectionChanged();
    void onPlayPause();
    void onStop();
    void onScrub(float time);
    void onViewportReady();
    void onFrameRendered(float dt);

private:
    void setupUI();
    void setupToolbar();
    void setupViewport();
    void setupDocks();
    void initializeEngine();
    void shutdownEngine();
    void updateWindowTitle();
    bool openDocument(const QString& path);
    bool promptSaveIfDirty();
    void registerOpenDocument();
    void unregisterOpenDocument();
    void rebuildTree();
    void rebuildAnimationTree();
    void refreshInspector();
    void bindPreview(bool forceGpu);
    void applyPreviewPlayback(float dt);
    void updateDebugOverlay();
    void syncSocketGizmo();
    void pullSocketGizmo();
    void applyTreeItemSelection(QTreeWidgetItem* item);
    CharacterEditorSelection currentSelection() const;
    void selectKind(CharacterEditorItemKind kind, const std::string& id = {}, int index = -1);
    std::filesystem::path projectRoot() const;
    void revealPath(const std::string& portable);
    void importAnimation();
    void createAnimationFolder();
    void reimportCharacter();
    void assignSource();
    Entity previewCharacter() const;

    EditorMainWindow* m_parentEditor = nullptr;
    QString m_pathStr;
    std::unique_ptr<Scene> m_previewScene;
    std::unique_ptr<CharacterDocument> m_document;
    std::unique_ptr<QtEditorEngine> m_engine;
    QtViewportWindow* m_viewportWindow = nullptr;
    QWidget* m_viewportHost = nullptr;
    QWidget* m_emptyOverlay = nullptr;

    QDockWidget* m_treeDock = nullptr;
    QDockWidget* m_inspectorDock = nullptr;
    QDockWidget* m_animationsDock = nullptr;
    QDockWidget* m_timelineDock = nullptr;
    QTreeWidget* m_tree = nullptr;
    QTreeWidget* m_animTree = nullptr;
    QWidget* m_inspectorHost = nullptr;
    CharacterTimelineWidget* m_timeline = nullptr;

    QToolBar* m_toolbar = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_translateAction = nullptr;
    QAction* m_rotateAction = nullptr;
    QAction* m_localWorldAction = nullptr;
    QAction* m_snapAction = nullptr;
    QAction* m_skeletonAction = nullptr;
    QAction* m_boneNamesAction = nullptr;
    QActionGroup* m_gizmoModeGroup = nullptr;
    QLabel* m_statusFps = nullptr;
    QLabel* m_statusPath = nullptr;
    QLabel* m_banner = nullptr;

    CharacterEditorSelection m_selection;
    std::string m_previewClipId;
    bool m_previewPlaying = false;
    bool m_previewPaused = false;
    float m_previewTime = 0.0f;
    bool m_framedOnce = false;
    bool m_gizmoDragging = false;
    bool m_showSkeleton = true;
    bool m_showBoneNames = false;
    GizmoMode m_gizmoMode = GizmoMode::Translate;
    GizmoSpace m_gizmoSpace = GizmoSpace::Local;
    bool m_snapEnabled = false;
    bool m_refreshingInspector = false;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
