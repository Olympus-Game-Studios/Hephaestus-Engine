/**
 * @file EditorMainWindow.cpp
 * @brief Implementation of the Qt-based Hephaestus Editor main window.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorMainWindow.h"
#include "AboutDialog.h"
#include "QtViewportWindow.h"
#include "QtEditorEngine.h"
#include "AssetBrowserWidget.h"
#include "AddActorsPanel.h"
#include "EditorAssetActions.h"
#include "EditorAssetServices.h"
#include "PrefabEditorManager.h"
#include "PrefabEditorWindow.h"
#include "CharacterEditorManager.h"
#include "CharacterEditorWindow.h"
#include "PrefabInstanceBar.h"
#include "LightInspector.h"
#include "EnvironmentInspector.h"
#include "ProbeVolumeInspector.h"
#include "../../Gfx/ProbeBake.h"
#include "ComponentInspector.h"
#include "MeshRendererInspector.h"
#include "ScriptInspector.h"
#include "LuaScriptInspector.h"
#include "PhysicsInspectors.h"
#include "CameraInspector.h"
#include "AudioSourceInspector.h"
#include "ParticleEmitterInspector.h"
#include "CharacterInspector.h"
#include "CharacterControllerInspector.h"
#include "AddComponentPopup.h"
#include "PropertyEditors.h"
#include "AssetReferenceEditor.h"
#include "EditorUndoCommand.h"
#include "BuildSettingsDialog.h"
#include "RuntimeUIOverlayWidget.h"
#include "EditorNotificationService.h"
#include "EditorCommandSearch.h"
#include "CodeEditorPanel.h"
#include "DebuggerPanel.h"
#include "ProjectSearchPanel.h"
#include "../../Scripting/LuaDebugger.h"
#include "EditorConsolePanel.h"
#include "EditorSystemUsage.h"
#include "EditorDockTitleBar.h"
#include "EditorTheme.h"
#include "HierarchyEye.h"
#include "EditorPluginHost.h"
#include "PluginManagerDialog.h"
#include "../Shared/SettingsWindow.h"

#include "../../Core/EditorContext.h"
#include "../../Core/EngineLog.h"
#include "../../Core/ProjectContext.h"
#include "../../Scene/SceneSerializer.h"
#include "../../Scene/PrefabSerializer.h"
#include "../../Scene/PrefabInstantiator.h"
#include "../../Scene/PrefabOverrideSystem.h"
#include "../../Scene/PrefabComponentIO.h"
#include "../../ToolsCommon/EngineContext.h"
#include "../../ToolsCommon/VersionInfo.h"
#include "../../ToolsCommon/PathUtils.h"
#include "../../ToolsCommon/JsonUtils.h"
#include "../../ToolsCommon/ProjectCreationService.h"
#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetTypes.h"
#include "../../Assets/EditorAssetPipeline.h"
#include "../../Assets/ModelImporter.h"
#include "../../Assets/MeshAsset.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"
#include "../../Scene/Entity.h"
#include "../../Scene/SceneAuthoring.h"
#include "../../Gfx/VulkanSwapchain.h"
#include "../../Scripting/ScriptComponent.h"
#include "../../Scripting/LuaScriptComponent.h"
#include "../../Scripting/ScriptLanguageRegistry.h"
#include "../../UI/InspectorSelectionModel.h"
#include "../../UI/ComponentClipboard.h"
#include "../../Assets/AssetPathPolicy.h"
#include "../../Assets/AssetReferenceIndex.h"
#include "../../Assets/CharacterRuntime.h"
#include "../../Build/BuildService.h"
#include "../../Plugins/PluginSystem.h"
#include "../../Automation/AutomationBackend.h"
#include "../../Automation/AutomationService.h"
#include "../../Automation/EditorAutomation.h"
#include "../../Automation/ViewportAutomation.h"
#include "../../Automation/AssetAutomation.h"
#include "../../Automation/CharacterAutomation.h"
#include "../../Automation/MaterialAutomation.h"
#include "../../Automation/SceneAutomation.h"
#include "../../Automation/ScenePathPolicy.h"
#include "../../Automation/PlayAutomation.h"
#include "../../Automation/LogAutomation.h"
#include "../../Automation/ScriptAutomation.h"
#include "../../Automation/PhysicsAutomation.h"
#include "../../Automation/UIAutomation.h"
#include "../../Automation/BuildAutomation.h"
#include "../../Automation/TestAutomation.h"
#include "../../Assets/EditorAssetImportService.h"
#include "AppConfig.h"

#include <QClipboard>
#include <QTextCursor>
#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QDockWidget>
#include <QTreeWidget>
#include <QHeaderView>
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
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QScrollArea>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QToolBar>
#include <QToolButton>
#include <QActionGroup>
#include <QGuiApplication>
#include <QScreen>
#include <utility>
#include <QFrame>
#include <QGridLayout>
#include <QSettings>
#include <QButtonGroup>
#include <QHash>
#include <QLocale>
#include <QTabBar>
#include <QPainter>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QClipboard>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QTime>
#include <QStyle>
#include <QMenu>
#include <QCursor>
#include <QInputDialog>
#include <QTimer>
#include <QPushButton>
#include <QProcess>
#include <QDesktopServices>
#include <QSignalBlocker>
#include <QUrl>
#include <iostream>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <mutex>
#include <thread>
#include <string>
#include <string_view>
#include <atomic>
#include <algorithm>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#endif

#include <QTreeWidgetItemIterator>

namespace Hephaestus {
namespace QtEditor {

namespace {

/// Returns process resident memory (working set) in megabytes, or -1.0 on failure.
double getProcessMemoryMB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
    }
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        long pages = 0;
        statm >> pages; // first field = total program size (we want RSS = second)
        statm >> pages;
        return static_cast<double>(pages) * sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0);
    }
#endif
    return -1.0;
}

entt::entity entityFromOutlinerItem(QTreeWidgetItem* item)
{
    if (!item) {
        return entt::null;
    }
    const QVariant data = item->data(0, Qt::UserRole);
    if (!data.isValid()) {
        return entt::null;
    }
    return static_cast<entt::entity>(data.toUInt());
}

void appendSelectedEntitiesInDisplayOrder(QTreeWidgetItem* parent,
                                          const std::unordered_set<uint32_t>& selectedIds,
                                          std::vector<entt::entity>& out)
{
    if (!parent) {
        return;
    }
    const int childCount = parent->childCount();
    for (int i = 0; i < childCount; ++i) {
        QTreeWidgetItem* item = parent->child(i);
        const entt::entity e = entityFromOutlinerItem(item);
        if (e != entt::null && selectedIds.count(static_cast<uint32_t>(e)) != 0) {
            out.push_back(e);
        }
        appendSelectedEntitiesInDisplayOrder(item, selectedIds, out);
    }
}

} // namespace

namespace {

constexpr int kOutlinerTrailingRole = Qt::UserRole + 20;

QString OutlinerIconName(const entt::registry& registry, entt::entity entity)
{
    if (registry.all_of<PrefabInstanceComponent>(entity)) return QStringLiteral("prefab");
    if (registry.all_of<FolderComponent>(entity)) return QStringLiteral("folder");
    if (registry.all_of<EnvironmentComponent>(entity)) return QStringLiteral("globe");
    if (registry.all_of<LightComponent>(entity)) return QStringLiteral("light");
    if (registry.all_of<CameraComponent>(entity)) return QStringLiteral("camera");
    if (registry.any_of<CharacterComponent, CharacterControllerComponent>(entity)) return QStringLiteral("character");
    if (registry.all_of<ProbeVolumeComponent>(entity)) return QStringLiteral("volume");
    if (registry.all_of<AudioSourceComponent>(entity)) return QStringLiteral("audio");
    if (registry.all_of<MeshRendererComponent>(entity)) return QStringLiteral("cube");
    return QStringLiteral("dot");
}

QString ComponentIconName(const QString& type)
{
    static const QHash<QString, QString> icons = {
        { QStringLiteral("Transform"), QStringLiteral("move") },
        { QStringLiteral("MeshRenderer"), QStringLiteral("cube") },
        { QStringLiteral("Light"), QStringLiteral("light") },
        { QStringLiteral("Camera"), QStringLiteral("camera") },
        { QStringLiteral("AudioSource"), QStringLiteral("audio") },
        { QStringLiteral("Environment"), QStringLiteral("globe") },
        { QStringLiteral("ProbeVolume"), QStringLiteral("volume") },
        { QStringLiteral("Character"), QStringLiteral("character") },
        { QStringLiteral("CharacterController"), QStringLiteral("character") },
        { QStringLiteral("RigidBody"), QStringLiteral("cube") },
        { QStringLiteral("PhysicsShape"), QStringLiteral("grid") },
        { QStringLiteral("MeshCollider"), QStringLiteral("grid") },
        { QStringLiteral("ScriptComponent"), QStringLiteral("code") },
        { QStringLiteral("LuaScript"), QStringLiteral("code") },
    };
    return icons.value(type, QStringLiteral("dot"));
}

QString OutlinerTypeLabel(const QString& iconName)
{
    static const QHash<QString, const char*> labels = {
        { QStringLiteral("prefab"), QT_TRANSLATE_NOOP("EditorMainWindow", "Prefab") },
        { QStringLiteral("folder"), QT_TRANSLATE_NOOP("EditorMainWindow", "Folder") },
        { QStringLiteral("globe"), QT_TRANSLATE_NOOP("EditorMainWindow", "Environment") },
        { QStringLiteral("light"), QT_TRANSLATE_NOOP("EditorMainWindow", "Light") },
        { QStringLiteral("camera"), QT_TRANSLATE_NOOP("EditorMainWindow", "Camera") },
        { QStringLiteral("character"), QT_TRANSLATE_NOOP("EditorMainWindow", "Character") },
        { QStringLiteral("volume"), QT_TRANSLATE_NOOP("EditorMainWindow", "Probe volume") },
        { QStringLiteral("audio"), QT_TRANSLATE_NOOP("EditorMainWindow", "Audio source") },
        { QStringLiteral("cube"), QT_TRANSLATE_NOOP("EditorMainWindow", "Static mesh") },
    };
    return QCoreApplication::translate("EditorMainWindow", labels.value(iconName, QT_TRANSLATE_NOOP("EditorMainWindow", "Entity")));
}

/** Draws a row's visibility eye (HierarchyEye) and, left of it, its trailing marker: the Prefab tag or the lock. */
class OutlinerRowDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        HierarchyEye::Paint(painter, option, index);
        const QString trailing = index.data(kOutlinerTrailingRole).toString();
        if (trailing.isEmpty()) return;
        const QRect area = option.rect.adjusted(0, 0, -(8 + HierarchyEye::kWidth), 0);
        if (trailing == QLatin1String("prefab")) {
            painter->save();
            painter->setPen(Theme::Color(Theme::Token::TextMuted));
            QFont font = option.font;
            font.setPixelSize(11);
            painter->setFont(font);
            painter->drawText(area, Qt::AlignRight | Qt::AlignVCenter, QObject::tr("Prefab"));
            painter->restore();
            return;
        }
        const QRect iconRect(area.right() - 16, area.center().y() - 8, 16, 16);
        Theme::Icon(QStringLiteral("lock"), Theme::IconTone::Muted).paint(painter, iconRect);
    }
};

} // namespace

// Bump when the default dock layout changes so older saved layouts are not restored over it.
constexpr int kDockLayoutVersion = 6;
constexpr int kEditorModeCount = 3;
// Also the project file's "editorMode" values, which the launcher writes for new projects.
constexpr const char* kEditorModeNames[kEditorModeCount] = { "default", "artist", "programmer" };

QString ModeStateKey(EditorMainWindow::EditorMode mode)
{
    return QStringLiteral("mainWindow/state/%1").arg(QLatin1String(kEditorModeNames[static_cast<int>(mode)]));
}

class EditorMainWindow::FirstPartyContext final : public Plugins::IFirstPartyPluginContext
{
public:
    explicit FirstPartyContext(EditorMainWindow* owner) : m_owner(owner) {}

    std::uint32_t GetFirstPartyApiVersion() const override
    {
        return Plugins::HP_FIRST_PARTY_API_VERSION;
    }

    Plugins::IAutomationService* GetAutomationService() override
    {
        return m_owner->m_automationService.get();
    }

    const Plugins::FirstPartyStartupOptions& GetStartupOptions() const override
    {
        return m_owner->m_startupOptions;
    }

private:
    EditorMainWindow* m_owner;
};

EditorMainWindow::EditorMainWindow(QWidget* parent,
                                   Hephaestus::Plugins::FirstPartyStartupOptions startupOptions)
    : QMainWindow(parent)
    , m_projectContext(std::make_unique<ProjectContext>())
    , m_startupOptions(std::move(startupOptions))
{
    EngineLog::Info("EditorMainWindow: constructing UI");

    m_engineSettingsManager.Load();
    EngineLog::Info("EditorMainWindow: engine settings loaded");

    // Load recent projects from settings
    m_projectContext->LoadRecentProjectsFromFile();
    EngineLog::Info("EditorMainWindow: recent projects loaded");

    // Set window properties
    setWindowTitle("Hephaestus Engine Editor (Qt)");
    resize(1280, 720);

    // Setup UI components (Vulkan is started after show() via StartEngine)
    setupMenuBar();
    EngineLog::Info("EditorMainWindow: menu bar ready");
    setupTopToolbar();
    EngineLog::Info("EditorMainWindow: toolbar ready");
    setupDockWidgets();
    EngineLog::Info("EditorMainWindow: dock widgets ready");
    setupCentralWidget();
    EngineLog::Info("EditorMainWindow: central viewport widget ready");
    setupStatusBar();

    // Automation must exist before plugins load. MCP is an optional consumer of
    // the Automation API, never an engine dependency.
    SetupAutomation();

    m_pluginSystem = std::make_unique<Plugins::PluginSystem>();
    m_pluginSystem->SetEngineVersion(HEPHAESTUS_VERSION_STRING);
    m_pluginHost = std::make_unique<EditorPluginHost>(
        this, m_pluginSystem.get(),
        [this]() -> EditorContext* { return m_engine ? m_engine->GetEditorContext() : nullptr; },
        m_projectContext.get(), [this]() {
            rebuildSceneVisuals();
            rebuildOutliner();
            refreshDetailsPanel();
            updateUndoRedoActions();
        });
    m_pluginSystem->SetContextFactory([this](const Plugins::PluginManifest& manifest) {
        return m_pluginHost->CreateContext(manifest);
    });
    m_pluginSystem->SetFirstPartyContextFactory([this]() -> Plugins::IFirstPartyPluginContext* {
        return m_firstPartyContext.get();
    });
    m_pluginSystem->SetRuntimeModuleLoader([](const std::filesystem::path& module, std::string& error) {
        return ScriptLanguageRegistry::Instance().LoadModule(module, error);
    });
    m_pluginSystem->Discover(Hephaestus::Tools::PathUtils::ExecutableDir().toStdString(), {});
    m_pluginSystem->LoadEnabled();

    m_notifications = new EditorNotificationService(this);
    m_notifications->SetStatusBar(statusBar());
    m_notifications->SetToastAnchor(this);
    {
        auto* statusBell = new QToolButton(statusBar());
        statusBell->setObjectName(QStringLiteral("StatusItem"));
        statusBell->setIcon(Theme::Icon(QStringLiteral("bell"), Theme::IconTone::Muted));
        statusBell->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        statusBell->setFocusPolicy(Qt::NoFocus);
        statusBell->setAutoRaise(true);
        statusBell->setPopupMode(QToolButton::InstantPopup);
        statusBell->setMenu(m_notificationButton->menu());
        statusBar()->addPermanentWidget(statusBell);
        auto refreshBell = [this, statusBell]() {
            const int unread = m_notifications->UnreadCount();
            const QString count = unread ? QString::number(unread) : QString();
            m_notificationButton->setText(count);
            m_notificationButton->setToolButtonStyle(unread ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
            statusBell->setText(count);
        };
        connect(m_notifications, &EditorNotificationService::historyChanged, this, refreshBell);
        QMenu* history = m_notificationButton->menu();
        connect(history, &QMenu::aboutToShow, this, [this, history]() {
            history->clear();
            const auto& entries = m_notifications->History();
            if (entries.isEmpty()) {
                history->addAction(tr("No notifications"))->setEnabled(false);
            }
            for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
                const char* icon = it->severity == EditorNotificationService::Severity::Error ? "error"
                                 : it->severity == EditorNotificationService::Severity::Warning ? "warning" : "info";
                const Theme::IconTone tone = it->severity == EditorNotificationService::Severity::Error ? Theme::IconTone::Error
                                           : it->severity == EditorNotificationService::Severity::Warning ? Theme::IconTone::Warning
                                                                                                          : Theme::IconTone::Info;
                history->addAction(Theme::Icon(QString::fromLatin1(icon), tone),
                                   QStringLiteral("%1  %2").arg(it->time.toString(QStringLiteral("HH:mm")), it->message));
            }
            m_notifications->MarkRead();
        });
        refreshBell();
    }
    setupRecoveryTimers();

    // Restore previous window geometry / dock layout when available.
    {
        QSettings settings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"));
        const QByteArray geometry = settings.value(QStringLiteral("mainWindow/geometry")).toByteArray();
        if (!geometry.isEmpty()) {
            restoreGeometry(geometry);
        }
        const int savedMode = settings.value(QStringLiteral("mainWindow/mode"), 0).toInt();
        m_mode = savedMode >= 0 && savedMode < kEditorModeCount ? static_cast<EditorMode>(savedMode) : EditorMode::Default;
        if (QAbstractButton* button = m_modeButtons->button(static_cast<int>(m_mode))) {
            const QSignalBlocker blocker(m_modeButtons);
            button->setChecked(true);
        }
        restoreModeLayout(m_mode);
    }

    EngineLog::Info("EditorMainWindow: UI construction complete (window not shown yet)");

    m_assetPipeline = std::make_unique<EditorAssetPipeline>();
    m_assetReimportTimer = new QTimer(this);
    m_assetReimportTimer->setInterval(250);
    connect(m_assetReimportTimer, &QTimer::timeout, this, &EditorMainWindow::pollAssetReimports);
}

EditorMainWindow::~EditorMainWindow()
{
    EngineLog::Info("EditorMainWindow destructor");
    std::cout << "[EditorMainWindow] Destructor called\n";
    if (m_assetReimportTimer) {
        m_assetReimportTimer->stop();
    }
    if (m_assetPipeline) {
        m_assetPipeline->Shutdown();
    }
    if (m_pluginSystem) {
        m_pluginSystem->UnloadAll();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Automation host
// ─────────────────────────────────────────────────────────────────────────────

class EditorMainWindow::EditorAutomationBackend final : public Automation::AutomationBackend
{
public:
    explicit EditorAutomationBackend(EditorMainWindow* owner) : m_owner(owner) {}

    ~EditorAutomationBackend() override
    {
        if (m_testProcess && m_testProcess->state() != QProcess::NotRunning) {
            m_testProcess->kill();
            m_testProcess->waitForFinished(3000);
        }
        if (m_buildThread.joinable()) m_buildThread.join();
    }

    Automation::HostMask Host() const noexcept override { return Automation::HostMask::Editor; }

    Automation::SceneAccess ActiveScene() noexcept override
    {
        Automation::SceneAccess access;
        EditorContext* ctx = Context();
        if (ctx) access.scene = ctx->GetActiveScene();
        return access;
    }

    Hephaestus::PlayState GetPlayState() const noexcept override
    {
        return m_playState.load(std::memory_order_relaxed);
    }

    bool IsRealtime() const noexcept override
    {
        return !m_owner->m_engine || m_owner->m_engine->IsRealtime();
    }

    void UpdatePlayState(Hephaestus::PlayState state)
    {
        m_playState.store(state, std::memory_order_relaxed);
    }

    Automation::Json GetProjectInfo() const override
    {
        Automation::Json info = Automation::Json::object();
        if (m_owner->m_projectContext && m_owner->m_projectContext->HasProject()) {
            info["name"] = m_owner->m_projectContext->GetProjectName();
            info["path"] = m_owner->m_projectContext->GetProjectFilePath();
            info["rootDir"] = m_owner->m_projectContext->GetProjectRootDir();
            info["assetRootDir"] = m_owner->m_projectContext->GetAssetRootDir();
        }
        return info;
    }

    std::string ActiveScenePath() const override
    {
        EditorContext* ctx = Context();
        if (!ctx || ctx->GetScenePath().empty()) return {};
        const std::filesystem::path path(ctx->GetScenePath());
        if (m_owner->m_projectContext && m_owner->m_projectContext->HasProject()) {
            const auto relative = path.lexically_relative(m_owner->m_projectContext->GetProjectRootDir());
            if (!relative.empty() && *relative.begin() != "..") return relative.generic_string();
        }
        return path.generic_string();
    }

    bool SaveActiveScene(std::string& outError) override
    {
        EditorContext* ctx = Context();
        if (!ctx || !ctx->HasActiveScene()) {
            outError = "No active scene";
            return false;
        }
        if (ctx->GetScenePath().empty()) {
            outError = "Scene has no path; call scene.save_as first";
            return false;
        }
        if (!ctx->SaveScene()) {
            outError = "Failed to save scene";
            return false;
        }
        return true;
    }

    bool SaveActiveSceneAs(std::string_view destination, std::string& outPath,
                           std::string& outError) override
    {
        EditorContext* ctx = Context();
        if (!ctx || !ctx->HasActiveScene() || !m_owner->m_projectContext ||
            !m_owner->m_projectContext->HasProject()) {
            outError = "An active project and scene are required";
            return false;
        }
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path root = fs::weakly_canonical(m_owner->m_projectContext->GetProjectRootDir(), ec);
        if (ec) { outError = "Cannot resolve project root"; return false; }
        fs::path absolute;
        if (!Automation::ResolveProjectScenePath(root, destination, absolute, outError)) return false;
        fs::create_directories(absolute.parent_path(), ec);
        if (ec) { outError = "Cannot create scene directory: " + ec.message(); return false; }
        if (!ctx->SaveSceneAs(absolute.string())) {
            outError = "Scene serializer failed to save the scene";
            return false;
        }
        outPath = absolute.lexically_relative(root).generic_string();
        return true;
    }

    bool NewScene(std::string_view name, std::string& outError) override
    {
        EditorContext* ctx = Context();
        if (!ctx || !m_owner->m_projectContext || !m_owner->m_projectContext->HasProject()) {
            outError = "An active project is required";
            return false;
        }
        if (ctx->IsDirty()) { outError = "Save or discard the dirty scene before creating another"; return false; }
        ctx->NewScene(std::string(name));
        return true;
    }

    bool OpenScene(std::string_view destination, std::string& outPath,
                   std::string& outError) override
    {
        EditorContext* ctx = Context();
        if (!ctx || !m_owner->m_projectContext || !m_owner->m_projectContext->HasProject()) {
            outError = "An active project is required";
            return false;
        }
        if (ctx->IsDirty()) { outError = "Save or discard the dirty scene before opening another"; return false; }
        namespace fs = std::filesystem;
        const fs::path root = m_owner->m_projectContext->GetProjectRootDir();
        fs::path absolute;
        if (!Automation::ResolveProjectScenePath(root, destination, absolute, outError)) return false;
        if (!fs::is_regular_file(absolute)) { outError = "Scene file does not exist"; return false; }
        if (!ctx->LoadScene(absolute.string())) { outError = "Scene deserializer could not open the scene"; return false; }
        m_owner->m_engine->RebuildSceneMeshes();
        outPath = ActiveScenePath();
        return true;
    }

    Automation::Json GetEditorState() const override
    {
        EditorContext* ctx = Context();
        const bool loaded = m_owner->m_projectContext && m_owner->m_projectContext->HasProject();
        const auto state = GetPlayState();
        return {{"projectLoaded", loaded},
                {"scene", {{"path", ActiveScenePath()},
                           {"dirty", ctx && ctx->IsDirty()},
                           {"name", ctx && ctx->GetActiveScene() ? ctx->GetActiveScene()->GetName() : ""}}},
                {"playState", state == Hephaestus::PlayState::Editor ? "Editor" :
                              state == Hephaestus::PlayState::Paused ? "Paused" : "Playing"},
                {"viewportAvailable", m_owner->m_engine && m_owner->m_engine->IsReadyToRender() &&
                                      m_owner->m_viewportWindow && m_owner->m_viewportWindow->IsReadyForRendering()}};
    }

    std::vector<entt::entity> GetSelection() const override
    {
        EditorContext* ctx = Context();
        return ctx ? ctx->GetSelectedEntities() : std::vector<entt::entity>{};
    }

    bool SetSelection(const std::vector<entt::entity>& handles, std::string& outError) override
    {
        EditorContext* ctx = Context();
        if (!ctx || !ctx->GetActiveScene()) { outError = "No active scene"; return false; }
        if (handles.empty()) ctx->ClearSelection();
        else ctx->SelectEntities(handles, handles.back());
        m_owner->restoreOutlinerTreeSelection();
        m_owner->refreshDetailsPanel();
        return true;
    }

    Automation::Json GetViewportState() const override
    {
        auto* renderer = m_owner->m_engine ? m_owner->m_engine->GetFrameRenderer() : nullptr;
        auto* swapchain = renderer ? renderer->GetSwapchain() : nullptr;
        const bool available = m_owner->m_engine && m_owner->m_engine->IsReadyToRender() &&
            m_owner->m_viewportWindow && m_owner->m_viewportWindow->IsReadyForRendering() &&
            swapchain && swapchain->extent.width && swapchain->extent.height;
        return {{"available", available}, {"width", available ? swapchain->extent.width : 0u},
                {"height", available ? swapchain->extent.height : 0u}};
    }

    Automation::Json GetViewportCamera() const override
    {
        auto* cam = m_owner->m_engine ? m_owner->m_engine->GetCameraController() : nullptr;
        if (!cam) return Automation::Json::object();
        return {{"position", Automation::Json::array({cam->positionX, cam->positionY, cam->positionZ})},
                {"yawDeg", cam->yawDeg}, {"pitchDeg", cam->pitchDeg}, {"fovDeg", cam->fovDeg},
                {"nearPlane", cam->nearPlane}, {"farPlane", cam->farPlane},
                {"projection", cam->orthographic ? "orthographic" : "perspective"}};
    }

    bool SetViewportCamera(const Automation::Json& values, std::string& outError) override
    {
        auto* cam = m_owner->m_engine ? m_owner->m_engine->GetCameraController() : nullptr;
        if (!cam) { outError = "Viewport camera is unavailable"; return false; }
        if (values.contains("position")) {
            cam->positionX = values["position"][0].get<float>();
            cam->positionY = values["position"][1].get<float>();
            cam->positionZ = values["position"][2].get<float>();
        }
        if (values.contains("yawDeg")) cam->yawDeg = values["yawDeg"].get<float>();
        if (values.contains("pitchDeg")) cam->pitchDeg = values["pitchDeg"].get<float>();
        if (values.contains("fovDeg")) cam->fovDeg = values["fovDeg"].get<float>();
        m_owner->m_viewportWindow->requestUpdate();
        return true;
    }

    bool FocusEntities(const std::vector<entt::entity>& handles, std::string& outError) override
    {
        EditorContext* ctx = Context();
        auto* cam = m_owner->m_engine ? m_owner->m_engine->GetCameraController() : nullptr;
        Scene* scene = ctx ? ctx->GetActiveScene() : nullptr;
        if (!scene || !cam || handles.empty()) { outError = "No entities to frame"; return false; }
        AABB combined{};
        bool found = false;
        for (entt::entity handle : handles) {
            if (!scene->GetRegistry().valid(handle)) continue;
            auto* transform = scene->GetRegistry().try_get<TransformComponent>(handle);
            if (!transform) continue;
            const auto* mesh = scene->GetRegistry().try_get<MeshRendererComponent>(handle);
            const Mat4 world = scene->GetWorldTransform(Entity(handle, scene));
            AABB bounds = mesh ? SceneAuthoring::TransformBounds(mesh->localBounds, world)
                               : AABB{Vec3(world.m[12], world.m[13], world.m[14]),
                                      Vec3(world.m[12], world.m[13], world.m[14])};
            if (!found) { combined = bounds; found = true; }
            else {
                combined.min.x = std::min(combined.min.x, bounds.min.x);
                combined.min.y = std::min(combined.min.y, bounds.min.y);
                combined.min.z = std::min(combined.min.z, bounds.min.z);
                combined.max.x = std::max(combined.max.x, bounds.max.x);
                combined.max.y = std::max(combined.max.y, bounds.max.y);
                combined.max.z = std::max(combined.max.z, bounds.max.z);
            }
        }
        if (!found) { outError = "Selected entities have no world transforms"; return false; }
        const Vec3 center = combined.Center();
        const float dx = combined.max.x - combined.min.x, dy = combined.max.y - combined.min.y,
                    dz = combined.max.z - combined.min.z;
        cam->FocusOnPoint(center.x, center.y, center.z,
                          std::max(0.5f, std::sqrt(dx*dx + dy*dy + dz*dz) * 0.5f));
        m_owner->m_viewportWindow->requestUpdate();
        return true;
    }

    bool RequestViewportCapture(std::string& outError) override
    {
        if (!GetViewportState().value("available", false) || !m_owner->m_engine ||
            !m_owner->m_engine->GetFrameRenderer()) {
            outError = "ViewportUnavailable";
            return false;
        }
        return m_owner->m_engine->GetFrameRenderer()->RequestFrameCapture(outError);
    }

    bool PollViewportCapture(ViewportPixels& pixels, std::string& outError) override
    {
        auto* renderer = m_owner->m_engine ? m_owner->m_engine->GetFrameRenderer() : nullptr;
        if (!renderer) { outError = "ViewportUnavailable"; return true; }
        EngineFrameRenderer::FrameCapture frame;
        if (!renderer->PollFrameCapture(frame, outError)) return false;
        pixels.width = frame.width;
        pixels.height = frame.height;
        pixels.frame = frame.frame;
        pixels.rgba = std::move(frame.rgba);
        return true;
    }

    void CancelViewportCapture() override
    {
        if (m_owner->m_engine && m_owner->m_engine->GetFrameRenderer())
            m_owner->m_engine->GetFrameRenderer()->CancelFrameCapture();
    }

    bool PrepareEditorMutation(std::string_view label, std::string& outError) override
    {
        EditorContext* ctx = Context();
        if (!ctx || !ctx->HasActiveScene()) {
            outError = "No active scene";
            return false;
        }
        EditorUndoManager::PushSnapshot(ctx, std::string(label));
        return true;
    }

    void CompleteEditorMutation(bool structural) override
    {
        EditorContext* ctx = Context();
        if (!ctx) return;
        ctx->MarkDirty();
        m_owner->rebuildSceneVisuals();
        if (structural) {
            m_owner->rebuildOutliner();
        }
        m_owner->refreshDetailsPanel();
        m_owner->updateUndoRedoActions();
    }

    void AbortEditorMutation() override
    {
        EditorContext* ctx = Context();
        if (!ctx) return;
        std::string error;
        if (EditorUndoManager::RestoreAndDiscardLastSnapshot(ctx, error)) {
            m_owner->rebuildSceneVisuals();
            m_owner->rebuildOutliner();
            m_owner->refreshDetailsPanel();
            m_owner->updateUndoRedoActions();
        }
    }

    bool StartPlayMode(std::string& outError) override
    {
        if (!m_owner->m_engine) {
            outError = "Engine is unavailable";
            return false;
        }
        m_owner->m_engine->StartPlayMode();
        return true;
    }

    bool StopPlayMode(std::string& outError) override
    {
        if (!m_owner->m_engine) {
            outError = "Engine is unavailable";
            return false;
        }
        m_owner->m_engine->StopPlayMode();
        return true;
    }

    bool TogglePause(std::string& outError) override
    {
        if (!m_owner->m_engine) {
            outError = "Engine is unavailable";
            return false;
        }
        m_owner->m_engine->TogglePause();
        return true;
    }

    bool StepFrames(std::uint32_t count, float fixedDt, std::string& outError) override
    {
        if (!m_owner->m_engine) {
            outError = "Engine is unavailable";
            return false;
        }
        return m_owner->m_engine->StepFrames(count, fixedDt, outError);
    }

    bool SetRealtime(bool realtime, std::string& outError) override
    {
        if (!m_owner->m_engine) {
            outError = "Engine is unavailable";
            return false;
        }
        m_owner->m_engine->SetRealtime(realtime);
        return true;
    }

    bool SetActionOverride(std::string_view action, float value, std::string& outError) override
    {
        if (!m_owner->m_engine) {
            outError = "Engine is unavailable";
            return false;
        }
        m_owner->m_engine->GetGameplayInput().QueueActionOverride(std::string(action), value);
        return true;
    }

    bool SetAxisOverride(std::string_view axis, float value, std::string& outError) override
    {
        if (!m_owner->m_engine) {
            outError = "Engine is unavailable";
            return false;
        }
        m_owner->m_engine->GetGameplayInput().SetAxisOverride(std::string(axis), value);
        return true;
    }

    bool AddMouseDelta(double x, double y, std::string& outError) override
    {
        if (!m_owner->m_engine) {
            outError = "Engine is unavailable";
            return false;
        }
        m_owner->m_engine->GetGameplayInput().QueueMouseDelta(x, y);
        return true;
    }

    bool ClearInputOverrides(std::string& outError) override
    {
        if (!m_owner->m_engine) {
            outError = "Engine is unavailable";
            return false;
        }
        m_owner->m_engine->GetGameplayInput().ClearOverrides();
        return true;
    }

    // ── Content-authoring services ─────────────────────────────────────────

    Hephaestus::AssetManager* Assets() noexcept override
    {
        return &Hephaestus::AssetManager::GetInstance();
    }

    AudioEngine* Audio() noexcept override
    {
        return m_owner->m_engine ? m_owner->m_engine->GetAudioEngine() : nullptr;
    }

    Hephaestus::AssetReferenceIndex* AssetReferences() noexcept override
    {
        return &EditorAssetServices::Instance().ReferenceIndex();
    }

    Hephaestus::AssetOperationService* AssetOperations() noexcept override
    {
        return &EditorAssetServices::Instance().Operations();
    }

    Automation::IAssetImportService* AssetImport() noexcept override
    {
        EditorAssetPipeline* pipeline = m_owner->GetAssetPipeline();
        if (!pipeline || !pipeline->IsInitialized()) {
            return nullptr;
        }
        if (!m_assetImport) {
            m_assetImport = std::make_unique<EditorAssetImportService>(*pipeline);
        }
        return m_assetImport.get();
    }

    AssetDatabase* ImportDatabase() noexcept override
    {
        EditorAssetPipeline* pipeline = m_owner->GetAssetPipeline();
        return (pipeline && pipeline->IsInitialized()) ? &pipeline->GetDatabase() : nullptr;
    }

    void NotifyAssetChanged() override
    {
        m_owner->refreshAssetBrowser();
        m_owner->rebuildSceneVisuals();
    }

    bool RequestScriptReload(std::string& outError) override
    {
        if (!m_owner->m_engine) {
            outError = "Engine is unavailable";
            return false;
        }
        return m_owner->m_engine->ReloadScripts(outError);
    }

    bool CopyRuntimeUI(std::vector<Automation::AutomationBackend::UIScreenNode>& out,
                       std::string& outError) override
    {
        if (!m_owner->m_engine) { outError = "unavailable"; return false; }
        return m_owner->m_engine->CopyRuntimeUI(out, outError);
    }
    bool ResolveRuntimeUI(const Automation::AutomationBackend::UISelector& selector,
                          Automation::AutomationBackend::UINode& out, std::string& outError) override
    {
        if (!m_owner->m_engine) { outError = "unavailable"; return false; }
        return m_owner->m_engine->ResolveRuntimeUI(selector, out, outError);
    }
    bool QueueRuntimeUIClick(const Automation::AutomationBackend::UISelector& selector,
                             Automation::AutomationBackend::UIBounds& outBounds, float& outX, float& outY,
                             std::uint64_t& outSerial, std::uint64_t& outClickToken,
                             std::string& outId, std::string& outScreen, std::string& outError) override
    {
        if (!m_owner->m_engine) { outError = "unavailable"; return false; }
        return m_owner->m_engine->QueueRuntimeUIClick(selector, outBounds, outX, outY, outSerial,
                                                      outClickToken, outId, outScreen, outError);
    }
    void CancelRuntimeUIClick(std::uint64_t token) override
    {
        if (m_owner->m_engine) m_owner->m_engine->CancelRuntimeUIClick(token);
    }
    std::uint64_t RuntimeUIUpdateSerial() const override
    {
        return m_owner->m_engine ? m_owner->m_engine->RuntimeUIUpdateSerial() : 0;
    }

    bool PhysicsRaycast(const Automation::AutomationBackend::PhysicsVec3& origin,
                        const Automation::AutomationBackend::PhysicsVec3& direction, float maxDistance,
                        entt::entity ignore, Automation::AutomationBackend::PhysicsRaycastResult& out,
                        std::string& outError, const std::string* layerName = nullptr) override
    {
        if (!m_owner->m_engine) { outError = "unavailable"; return false; }
        return m_owner->m_engine->PhysicsRaycast(origin, direction, maxDistance, ignore, out, outError, layerName);
    }
    bool ListPhysicsBodies(std::vector<Automation::AutomationBackend::PhysicsBodySnapshot>& out,
                           std::string& outError) override
    {
        if (!m_owner->m_engine) { outError = "unavailable"; return false; }
        return m_owner->m_engine->ListPhysicsBodies(out, outError);
    }
    bool GetPhysicsBody(entt::entity entity, Automation::AutomationBackend::PhysicsBodySnapshot& out,
                        std::string& outError) override
    {
        if (!m_owner->m_engine) { outError = "unavailable"; return false; }
        return m_owner->m_engine->GetPhysicsBody(entity, out, outError);
    }
    bool GetCharacterState(entt::entity entity, Automation::AutomationBackend::PhysicsBodySnapshot& pose,
                           bool& grounded, std::string& outError) override
    {
        if (!m_owner->m_engine) { outError = "unavailable"; return false; }
        return m_owner->m_engine->GetCharacterState(entity, pose, grounded, outError);
    }

    bool StartBuild(const Automation::AutomationBackend::BuildRequestSpec& request,
                    std::string& outError) override
    {
        if (m_owner->m_buildThread && m_owner->m_buildThread->isRunning()) {
            outError = "busy";
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(m_buildMutex);
            if (m_buildStatus.state == "running") {
                outError = "busy";
                return false;
            }
        }
        if (m_buildThread.joinable()) m_buildThread.join();
        if (!m_owner->m_projectContext || !m_owner->m_projectContext->HasProject()) {
            outError = "no_project";
            return false;
        }
        EditorContext* ctx = Context();
        if ((request.action == "validate" || request.action == "package") && ctx && ctx->IsDirty()) {
            outError = "dirty";
            return false;
        }

        Build::BuildRequest build;
        build.projectRoot = std::filesystem::path(m_owner->m_projectContext->GetProjectRootDir());
        build.productName = m_owner->m_projectContext->GetProjectName();
        if (build.productName.empty()) build.productName = build.projectRoot.filename().string();
        const std::filesystem::path executableDirectory(
            Hephaestus::Tools::PathUtils::ExecutableDir().toStdWString());
        build.runtimeSearchRoot = executableDirectory;
        build.engineRoot = Build::RuntimeLocator::discoverEngineRoot(executableDirectory);
        if (request.outputRoot.empty()) {
            build.outputRoot = build.projectRoot / "Builds";
        } else {
            const std::filesystem::path output(request.outputRoot);
            build.outputRoot = output.is_absolute() ? output : build.projectRoot / output;
        }
        build.startupScene = request.startupScene;
        build.configuration = request.configuration == "Release"
                                  ? Build::BuildConfiguration::Release
                                  : Build::BuildConfiguration::Development;
        build.action = request.action == "clean" ? Build::BuildAction::Clean
                       : request.action == "validate" ? Build::BuildAction::Validate
                                                      : Build::BuildAction::Package;
        build.enableRuntimeAutomation = request.enableRuntimeAutomation;
        build.validateFirst = true;
        build.engineVersion = Tools::VersionInfo::EngineVersion().toStdString();

        {
            std::lock_guard<std::mutex> lock(m_buildMutex);
            m_buildStatus = {};
            m_buildStatus.state = "running";
            m_buildStatus.action = request.action;
            m_buildStatus.configuration = request.configuration;
            m_buildStatus.outputPath = build.outputRoot.generic_string();
        }

        m_buildThread = std::thread([this, build]() {
            Automation::AutomationBackend::BuildStatus finished;
            finished.action = build.action == Build::BuildAction::Clean ? "clean"
                              : build.action == Build::BuildAction::Validate ? "validate"
                                                                             : "package";
            finished.configuration = build.configuration == Build::BuildConfiguration::Release
                                         ? "Release" : "Development";
            try {
                const Build::BuildResult result = Build::BuildService().execute(build);
                finished.outputPath = result.outputPath.generic_string();
                finished.executablePath = result.executablePath.generic_string();
                finished.packageBytes = static_cast<std::uint64_t>(result.packageBytes);
                finished.state = result.cancelled ? "cancelled" : result.success ? "succeeded" : "failed";
                const std::size_t logBegin = result.logSummary.size() > 40
                                                 ? result.logSummary.size() - 40 : 0;
                finished.logSummary.assign(result.logSummary.begin() + static_cast<std::ptrdiff_t>(logBegin),
                                           result.logSummary.end());
                for (const Build::EditorProblem& problem : result.problems) {
                    const char* severity = "info";
                    if (problem.severity == Build::ProblemSeverity::Error) severity = "error";
                    else if (problem.severity == Build::ProblemSeverity::Warning) severity = "warning";
                    finished.problems.push_back(
                        {severity, problem.code, problem.message, problem.detail, problem.assetPath});
                }
            } catch (const std::exception& exception) {
                finished.state = "failed";
                finished.problems.push_back(
                    {"error", "BUILD_EXCEPTION", exception.what(), "", ""});
            } catch (...) {
                finished.state = "failed";
                finished.problems.push_back(
                    {"error", "BUILD_EXCEPTION", "Build failed", "", ""});
            }
            std::lock_guard<std::mutex> lock(m_buildMutex);
            m_buildStatus = std::move(finished);
        });
        return true;
    }

    Automation::AutomationBackend::BuildStatus GetBuildStatus() override
    {
        std::lock_guard<std::mutex> lock(m_buildMutex);
        return m_buildStatus;
    }

    bool ListTests(const std::string& configuration, std::vector<std::string>& out,
                   std::string& outError) override
    {
        out.clear();
        QProcess process;
        process.start("ctest", {"-N", "-C", QString::fromStdString(configuration),
                                "--test-dir", BuildDirectory()});
        if (!process.waitForStarted(5000) || !process.waitForFinished(20000) ||
            process.exitCode() != 0) {
            outError = "unavailable";
            return false;
        }
        const QString text = QString::fromUtf8(process.readAllStandardOutput());
        for (const QString& line : text.split('\n')) {
            const int marker = line.indexOf("Test #");
            if (marker < 0) continue;
            const int colon = line.indexOf(':', marker);
            if (colon < 0) continue;
            const QString name = line.mid(colon + 1).trimmed();
            if (!name.isEmpty()) out.push_back(name.toStdString());
        }
        return true;
    }

    bool StartTest(const std::string& name, const std::string& configuration,
                   std::string& outError) override
    {
        if (m_testProcess && m_testProcess->state() != QProcess::NotRunning) {
            outError = "busy";
            return false;
        }
        std::vector<std::string> names;
        if (!ListTests(configuration, names, outError)) return false;
        bool found = false;
        for (const std::string& candidate : names) {
            if (candidate == name) found = true;
        }
        if (!found) {
            outError = "not_found";
            return false;
        }
        m_testOutput.clear();
        m_testStatus = {};
        m_testStatus.state = "running";
        m_testStatus.name = name;
        m_testStatus.configuration = configuration;
        m_testProcess = std::make_unique<QProcess>();
        m_testProcess->setProcessChannelMode(QProcess::MergedChannels);
        m_testProcess->start("ctest", {"--test-dir", BuildDirectory(), "-C",
                                       QString::fromStdString(configuration), "-R",
                                       QString::fromStdString("^" + name + "$"),
                                       "--output-on-failure"});
        if (!m_testProcess->waitForStarted(5000)) {
            m_testStatus.state = "failed";
            m_testStatus.output = "ctest failed to start";
            outError = "unavailable";
            return false;
        }
        return true;
    }

    Automation::AutomationBackend::TestRunStatus GetTestStatus() override
    {
        if (m_testProcess) {
            m_testOutput += QString::fromUtf8(m_testProcess->readAll()).toStdString();
            if (m_testOutput.size() > 8000) {
                m_testOutput.erase(0, m_testOutput.size() - 8000);
            }
            m_testStatus.output = m_testOutput;
            if (m_testProcess->state() == QProcess::NotRunning && m_testStatus.state == "running") {
                m_testStatus.exitCode = m_testProcess->exitCode();
                m_testStatus.state = m_testStatus.exitCode == 0 ? "passed" : "failed";
            }
        }
        return m_testStatus;
    }

private:
    QString BuildDirectory() const
    {
        const QString executable = Hephaestus::Tools::PathUtils::ExecutableDir();
        const QString parent = QDir(executable).absoluteFilePath("..");
        if (QFileInfo::exists(QDir(parent).filePath("CTestTestfile.cmake"))) {
            return QDir(parent).absolutePath();
        }
        return executable;
    }

    EditorContext* Context() const
    {
        if (!m_owner->m_engine) return nullptr;
        return m_owner->m_engine->GetEditorContext();
    }

    EditorMainWindow* m_owner;
    std::atomic<Hephaestus::PlayState> m_playState{Hephaestus::PlayState::Editor};
    std::unique_ptr<EditorAssetImportService> m_assetImport;
    std::mutex m_buildMutex;
    Automation::AutomationBackend::BuildStatus m_buildStatus;
    std::thread m_buildThread;
    std::unique_ptr<QProcess> m_testProcess;
    Automation::AutomationBackend::TestRunStatus m_testStatus;
    std::string m_testOutput;
};

void EditorMainWindow::SetupAutomation()
{
    if (m_automationService) return;

    m_automationBackend = std::make_unique<EditorAutomationBackend>(this);
    m_automationService = std::make_unique<Automation::AutomationService>(*m_automationBackend);
    Automation::RegisterSceneAutomation(*m_automationService);
    Automation::RegisterEditorAutomation(*m_automationService);
    Automation::RegisterViewportAutomation(*m_automationService);
    Automation::RegisterPlayAutomation(*m_automationService);
    Automation::RegisterLogAutomation(*m_automationService);
    Automation::RegisterAssetAutomation(*m_automationService);
    Automation::RegisterMaterialAutomation(*m_automationService);
    Automation::RegisterCharacterAutomation(*m_automationService);
    Automation::RegisterScriptAutomation(*m_automationService);
    Automation::RegisterUIAutomation(*m_automationService);
    Automation::RegisterPhysicsAutomation(*m_automationService);
    Automation::RegisterRuntimeCapture(*m_automationService);
    Automation::RegisterBuildAutomation(*m_automationService);
    Automation::RegisterTestAutomation(*m_automationService);
    m_firstPartyContext = std::make_unique<FirstPartyContext>(this);

    m_automationTimer = new QTimer(this);
    m_automationTimer->setInterval(16);
    connect(m_automationTimer, &QTimer::timeout, this, [this]() {
        if (m_automationService && m_automationBackend) {
            if (m_engine) {
                m_automationBackend->UpdatePlayState(m_engine->GetPlayState());
            }
            m_automationService->Tick();
        }
    });
    m_automationTimer->start();

    EngineLog::Info("Editor automation service ready (independent of MCP)");
}

void EditorMainWindow::ShutdownAutomation()
{
    if (m_automationTimer) {
        m_automationTimer->stop();
    }
    if (m_automationService) {
        m_automationService->Shutdown();
    }
}

bool EditorMainWindow::StartEngine()
{
    EngineLog::Info("EditorMainWindow::StartEngine");
    if (!initializeEngine()) {
        return false;
    }

    // show() + processEvents() can expose the viewport before Vulkan exists.
    // Attach now if that already happened; otherwise onViewportReady will fire later.
    if (m_viewportWindow && m_viewportWindow->IsReadyForRendering()) {
        EngineLog::Info("Viewport already exposed; attaching engine now");
        onViewportReady();
    }
    return true;
}

void EditorMainWindow::OpenProjectFromPath(const QString& filepath)
{
    if (filepath.isEmpty()) {
        return;
    }

    if (m_projectContext && m_projectContext->HasProject()) {
        if (!promptSaveDirtyScene() || !m_codeEditor->CloseAll()) {
            return;
        }
    }

    openProjectFile(filepath);
}

void EditorMainWindow::closeEvent(QCloseEvent* event)
{
    // Lua is suspended inside the debugger's event loop; unwind it before tearing anything down.
    if (LuaDebugger::Instance().IsPaused()) {
        event->ignore();
        if (m_engine) m_engine->StopPlayMode();
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }
    EngineLog::Info("EditorMainWindow close event");
    std::cout << "[EditorMainWindow] Close event received\n";

    if (m_buildThread && m_buildThread->isRunning()) {
        const auto choice = QMessageBox::question(
            this, tr("Package in progress"),
            tr("A package operation is still running. Cancel it and close the editor?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        if (m_buildCancel) {
            m_buildCancel->store(true);
        }
        m_buildThread->wait();
    }

    if (!promptSaveDirtyScene() || !m_codeEditor->CloseAll()) {
        event->ignore();
        return;
    }

    stopRecoverySession();

    // Persist dock layout and window geometry for the next session.
    {
        QSettings settings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"));
        settings.setValue(QStringLiteral("mainWindow/geometry"), saveGeometry());
        settings.setValue(ModeStateKey(m_mode), saveState(kDockLayoutVersion));
        settings.setValue(QStringLiteral("mainWindow/mode"), static_cast<int>(m_mode));
    }

    // Close all prefab and character editor windows
    if (m_prefabEditorManager) {
        m_prefabEditorManager->CloseAll();
    }
    if (m_characterEditorManager) {
        m_characterEditorManager->CloseAll();
    }

    // Stop the automation endpoint before tearing down the engine.
    ShutdownAutomation();

    // Stop and cleanup the engine before closing
    if (m_engine) {
        m_engine->Shutdown();
    }
    delete m_playWindow;
    m_playWindow = nullptr;

    event->accept();
}

void EditorMainWindow::keyPressEvent(QKeyEvent* event)
{
    // Escape exits play mode even if the viewport does not have focus.
    if (event->key() == Qt::Key_Escape && m_engine && m_engine->IsPlaying()) {
        m_engine->StopPlayMode();
        event->accept();
        return;
    }

    if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) &&
        focusedComponentInspector()) {
        const std::string type = focusedComponentInspector()->TypeName();
        if (event->key() == Qt::Key_C) {
            copyInspectorComponent(type);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_V) {
            pasteInspectorComponentValues(type);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_R) {
            resetInspectorComponent(type);
            event->accept();
            return;
        }
    }

    // Forward key events to the engine during play mode so Lua scripts receive input
    // even when the viewport doesn't have direct keyboard focus.
    if (m_engine && m_engine->IsPlaying()) {
        m_engine->ForwardKeyEvent(event->key(), true);
    }
    QMainWindow::keyPressEvent(event);
}

void EditorMainWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (m_engine && m_engine->IsPlaying()) {
        m_engine->ForwardKeyEvent(event->key(), false);
    }
    QMainWindow::keyReleaseEvent(event);
}

void EditorMainWindow::setupMenuBar()
{
    m_menuBar = new QMenuBar(this);
    m_menuBar->setObjectName(QStringLiteral("EditorMenuBar"));
    QMenuBar* menuBar = m_menuBar;

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
    m_undoAction = undoAction;

    QAction* redoAction = editMenu->addAction(tr("&Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, &EditorMainWindow::onRedo);
    m_redoAction = redoAction;
    updateUndoRedoActions();

    editMenu->addSeparator();

    QAction* copyAction = editMenu->addAction(tr("&Copy Entity"));
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, &EditorMainWindow::onCopyEntity);

    QAction* pasteAction = editMenu->addAction(tr("&Paste Entity"));
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, &EditorMainWindow::onPasteEntity);

    QAction* duplicateAction = editMenu->addAction(tr("&Duplicate Entity"));
    duplicateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(duplicateAction, &QAction::triggered, this, &EditorMainWindow::onDuplicateEntity);

    editMenu->addSeparator();

    m_settingsAction = editMenu->addAction(tr("Settings"));
    m_settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(m_settingsAction, &QAction::triggered, this, &EditorMainWindow::onOpenSettings);

    QAction* pluginsAction = editMenu->addAction(tr("Plugins..."));
    connect(pluginsAction, &QAction::triggered, this, &EditorMainWindow::onOpenPlugins);

    QMenu* sceneMenu = menuBar->addMenu(tr("&Scene"));
    QAction* hideSelectedAction = sceneMenu->addAction(tr("Hide Selected"));
    hideSelectedAction->setShortcut(Qt::Key_H);
    connect(hideSelectedAction, &QAction::triggered,
            this, &EditorMainWindow::hideSelectedInEditor);
    QAction* isolateSelectedAction = sceneMenu->addAction(tr("Isolate Selected"));
    isolateSelectedAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_H));
    connect(isolateSelectedAction, &QAction::triggered,
            this, &EditorMainWindow::isolateSelectedInEditor);
    QAction* showAllAction = sceneMenu->addAction(tr("Show All / Exit Isolation"));
    showAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    connect(showAllAction, &QAction::triggered, this, [this]() {
        if (!m_engine) return;
        if (auto* ctx = m_engine->GetEditorContext()) {
            ctx->ShowAllEditorEntities();
            saveEditorViewState();
            rebuildOutliner();
        }
    });
    QAction* lockSelectedAction = sceneMenu->addAction(tr("Lock / Unlock Selected"));
    connect(lockSelectedAction, &QAction::triggered,
            this, &EditorMainWindow::toggleLockSelectedInEditor);
    sceneMenu->addSeparator();
    QMenu* alignMenu = sceneMenu->addMenu(tr("Align to Active"));
    alignMenu->addAction(tr("X"), [this]() { alignSelection(0); });
    alignMenu->addAction(tr("Y"), [this]() { alignSelection(1); });
    alignMenu->addAction(tr("Z"), [this]() { alignSelection(2); });
    QMenu* distributeMenu = sceneMenu->addMenu(tr("Distribute Evenly"));
    distributeMenu->addAction(tr("X"), [this]() { distributeSelection(0); });
    distributeMenu->addAction(tr("Y"), [this]() { distributeSelection(1); });
    distributeMenu->addAction(tr("Z"), [this]() { distributeSelection(2); });
    QAction* dropAction = sceneMenu->addAction(tr("Drop Selected to Ground"));
    dropAction->setShortcut(Qt::Key_End);
    connect(dropAction, &QAction::triggered,
            this, &EditorMainWindow::dropSelectionToGround);
    sceneMenu->addAction(tr("Create Empty Parent"),
                         this, &EditorMainWindow::createEmptyParentForSelection);
    sceneMenu->addAction(tr("Frame All"), [this]() {
        if (m_engine) m_engine->FocusAll();
    });
    QMenu* bookmarksMenu = sceneMenu->addMenu(tr("Camera Bookmarks"));
    for (int slot = 1; slot <= 9; ++slot) {
        QAction* recall = bookmarksMenu->addAction(
            tr("Recall %1").arg(slot),
            [this, slot]() { recallCameraBookmark(slot); });
        recall->setShortcut(
            QKeySequence(QStringLiteral("Alt+%1").arg(slot)));
        QAction* save = bookmarksMenu->addAction(
            tr("Save %1").arg(slot),
            [this, slot]() { saveCameraBookmark(slot); });
        save->setShortcut(
            QKeySequence(QStringLiteral("Ctrl+Alt+%1").arg(slot)));
    }

    QMenu* buildMenu = menuBar->addMenu(tr("&Build"));
    buildMenu->addAction(Theme::Icon(QStringLiteral("light")), tr("Bake Lighting"), this, &EditorMainWindow::bakeAllProbeVolumes);
    buildMenu->addSeparator();
    buildMenu->addAction(tr("Validate Project"), this, &EditorMainWindow::onValidateProject);
    QAction* packageProjectAction =
        buildMenu->addAction(tr("Package Project"), this, &EditorMainWindow::onBuildProject);
    packageProjectAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+B")));
    buildMenu->addAction(tr("Package && Run"), this, &EditorMainWindow::onPackageAndRun);
    buildMenu->addSeparator();
    buildMenu->addAction(tr("Run Last Package"), this, &EditorMainWindow::onRunLastPackage);
    buildMenu->addAction(tr("Clean Package"), this, &EditorMainWindow::onCleanPackage);

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
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &EditorMainWindow::onAbout);
}

void EditorMainWindow::setupTopToolbar()
{
    m_playAction = new QAction(Theme::Icon(QStringLiteral("play"), Theme::IconTone::Success), tr("Play"), this);
    m_playAction->setCheckable(true);
    m_pauseAction = new QAction(Theme::Icon(QStringLiteral("pause")), tr("Pause"), this);
    m_pauseAction->setCheckable(true);
    m_stopAction = new QAction(Theme::Icon(QStringLiteral("stop"), Theme::IconTone::OnAction), tr("Stop"), this);
    m_stepAction = new QAction(Theme::Icon(QStringLiteral("step")), tr("Step One Frame"), this);
    m_pauseAction->setEnabled(false);
    m_stopAction->setEnabled(false);
    m_stepAction->setEnabled(false);

    {
        connect(m_playAction, &QAction::triggered, this, [this](bool checked) {
            if (!m_engine) return;

            if (checked && m_playInWindowAction && m_playInWindowAction->isChecked()) {
                openPlayWindow();
                return;
            }
            if (checked) {
                // Start play mode
                m_engine->StartPlayMode();
                m_pauseAction->setChecked(false);
                m_pauseAction->setEnabled(true);
                m_stopAction->setEnabled(true);
            } else {
                // Play unchecked = stop
                m_engine->StopPlayMode();
                m_pauseAction->setChecked(false);
                m_pauseAction->setEnabled(false);
                m_stopAction->setEnabled(false);
                rebuildOutliner();
                refreshDetailsPanel();
            }
            if (m_viewportHost) {
                m_viewportHost->setProperty("playMode", checked);
                m_viewportHost->style()->unpolish(m_viewportHost);
                m_viewportHost->style()->polish(m_viewportHost);
            }
            statusBar()->showMessage(checked ? tr("Play Mode started") : tr("Play Mode stopped"), 2000);
        });
        connect(m_pauseAction, &QAction::triggered, this, [this](bool /*checked*/) {
            if (!m_engine) return;
            m_engine->TogglePause();
            bool paused = m_engine->IsPaused();
            m_pauseAction->setChecked(paused);
            statusBar()->showMessage(paused ? tr("Play Mode paused") : tr("Play Mode resumed"), 2000);
        });
        connect(m_stopAction, &QAction::triggered, this, [this]() {
            if (m_engine && m_engine->IsPlaying()) {
                m_engine->StopPlayMode();
                rebuildOutliner();
                refreshDetailsPanel();
            }
            if (m_playAction) m_playAction->setChecked(false);
            if (m_pauseAction) {
                m_pauseAction->setChecked(false);
                m_pauseAction->setEnabled(false);
            }
            m_stopAction->setEnabled(false);
            if (m_viewportHost) {
                m_viewportHost->setProperty("playMode", false);
                m_viewportHost->style()->unpolish(m_viewportHost);
                m_viewportHost->style()->polish(m_viewportHost);
            }
            statusBar()->showMessage(tr("Play Mode stopped"), 2000);
        });

    }
    connect(m_stopAction, &QAction::changed, this, [this]() {
        m_stepAction->setEnabled(m_stopAction->isEnabled());
    });
    connect(m_stepAction, &QAction::triggered, this, [this]() {
        if (!m_engine || !m_engine->IsPlaying()) return;
        if (!m_engine->IsPaused()) {
            m_pauseAction->trigger();
        }
        std::string error;
        if (!m_engine->StepFrames(1, 1.0f / 60.0f, error)) {
            statusBar()->showMessage(QString::fromStdString(error), 3000);
        }
    });

    QSettings playSettings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"));
    auto* playModeGroup = new QActionGroup(this);
    auto* playInViewportAction = new QAction(tr("Play in Viewport"), playModeGroup);
    m_playInWindowAction = new QAction(tr("Play in New Window"), playModeGroup);
    playInViewportAction->setCheckable(true);
    m_playInWindowAction->setCheckable(true);
    m_playInWindowAction->setChecked(playSettings.value(QStringLiteral("play/newWindow"), false).toBool());
    playInViewportAction->setChecked(!m_playInWindowAction->isChecked());
    connect(m_playInWindowAction, &QAction::toggled, this, [](bool inWindow) {
        QSettings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"))
            .setValue(QStringLiteral("play/newWindow"), inWindow);
    });

    auto* buildAndRunAction = new QAction(Theme::Icon(QStringLiteral("build")), tr("Build and Run"), this);
    buildAndRunAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    buildAndRunAction->setToolTip(tr("Package the open scene into Saved/BuildAndRun and start it"));
    connect(buildAndRunAction, &QAction::triggered, this, &EditorMainWindow::onBuildAndRun);
    auto* buildConfigGroup = new QActionGroup(this);
    auto* buildRunDevelopmentAction = new QAction(tr("Development Build"), buildConfigGroup);
    m_buildRunReleaseAction = new QAction(tr("Release Build"), buildConfigGroup);
    buildRunDevelopmentAction->setCheckable(true);
    m_buildRunReleaseAction->setCheckable(true);
    m_buildRunReleaseAction->setChecked(playSettings.value(QStringLiteral("play/buildRunRelease"), false).toBool());
    buildRunDevelopmentAction->setChecked(!m_buildRunReleaseAction->isChecked());
    connect(m_buildRunReleaseAction, &QAction::toggled, this, [](bool release) {
        QSettings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"))
            .setValue(QStringLiteral("play/buildRunRelease"), release);
    });
    auto addPlayModeAndBuild = [=, this](QMenu* menu) {
        menu->addSection(tr("Play Mode"));
        menu->addAction(playInViewportAction);
        menu->addAction(m_playInWindowAction);
        menu->addSection(tr("Standalone"));
        menu->addAction(buildAndRunAction);
        menu->addAction(buildRunDevelopmentAction);
        menu->addAction(m_buildRunReleaseAction);
    };

    m_playAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    auto* playMenuBarMenu = new QMenu(tr("&Play"), m_menuBar);
    playMenuBarMenu->addAction(m_playAction);
    playMenuBarMenu->addAction(m_pauseAction);
    playMenuBarMenu->addAction(m_stopAction);
    playMenuBarMenu->addAction(m_stepAction);
    addPlayModeAndBuild(playMenuBarMenu);
    QAction* insertBefore = nullptr;
    for (QAction* menuAction : m_menuBar->actions()) {
        if (menuAction->text() == tr("&View")) insertBefore = menuAction;
    }
    m_menuBar->insertMenu(insertBefore, playMenuBarMenu);

    // Header: brand, project, menus, command search, history and play controls.
    {
        auto* header = new QWidget(this);
        header->setObjectName(QStringLiteral("EditorHeader"));
        header->setAttribute(Qt::WA_StyledBackground, true);
        auto* layout = new QHBoxLayout(header);
        layout->setContentsMargins(12, 0, 8, 0);
        layout->setSpacing(8);

        auto* logo = new QToolButton(header);
        logo->setObjectName(QStringLiteral("HeaderLogo"));
        logo->setIcon(Theme::BrandLogo());
        logo->setIconSize(QSize(20, 20));
        logo->setAutoRaise(true);
        logo->setToolTip(tr("About Hephaestus"));
        connect(logo, &QToolButton::clicked, this, &EditorMainWindow::onAbout);
        layout->addWidget(logo);
        layout->addWidget(m_menuBar, 0, Qt::AlignVCenter);
        layout->addStretch(1);

        auto* modeSwitch = new QFrame(header);
        modeSwitch->setObjectName(QStringLiteral("ModeSwitch"));
        auto* modeLayout = new QHBoxLayout(modeSwitch);
        modeLayout->setContentsMargins(3, 3, 3, 3);
        modeLayout->setSpacing(2);
        m_modeButtons = new QButtonGroup(this);
        const struct { EditorMode mode; const char* icon; const char* label; const char* tip; } modes[] = {
            { EditorMode::Default, "layout", QT_TR_NOOP("Default"), QT_TR_NOOP("Hierarchy, viewport, Inspector, Content Browser and Console") },
            { EditorMode::Artist, "paint", QT_TR_NOOP("Artist"), QT_TR_NOOP("Viewport first, tools on a rail, large thumbnails") },
            { EditorMode::Programmer, "code", QT_TR_NOOP("Programmer"), QT_TR_NOOP("Project files, viewport, Inspector, Console and build output") },
        };
        for (const auto& entry : modes) {
            auto* button = new QToolButton(modeSwitch);
            button->setObjectName(QStringLiteral("ModeButton"));
            button->setIcon(Theme::Icon(QString::fromLatin1(entry.icon)));
            button->setText(tr(entry.label));
            button->setToolTip(tr(entry.tip));
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            button->setCheckable(true);
            button->setFocusPolicy(Qt::NoFocus);
            m_modeButtons->addButton(button, static_cast<int>(entry.mode));
            modeLayout->addWidget(button);
        }
        m_modeButtons->button(static_cast<int>(EditorMode::Default))->setChecked(true);
        connect(m_modeButtons, &QButtonGroup::idClicked, this, [this](int id) {
            setEditorMode(static_cast<EditorMode>(id));
        });
        modeSwitch->setFixedHeight(26);
        layout->addWidget(modeSwitch, 0, Qt::AlignVCenter);
        layout->addStretch(1);

        auto* search = new EditorCommandSearch(m_menuBar, header);
        search->setFixedWidth(360);
        search->hide();
        layout->addWidget(search);
        auto* searchBtn = new QToolButton(header);
        searchBtn->setIcon(Theme::Icon(QStringLiteral("search")));
        searchBtn->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(searchBtn);
        auto* searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
        searchBtn->setToolTip(tr("Search or run a command (%1)").arg(searchShortcut->key().toString(QKeySequence::NativeText)));
        auto openSearch = [search, searchBtn]() {
            searchBtn->hide();
            search->show();
            search->setFocus(Qt::ShortcutFocusReason);
        };
        connect(searchBtn, &QToolButton::clicked, search, openSearch);
        connect(searchShortcut, &QShortcut::activated, search, openSearch);
        search->SetDismissHandler([search, searchBtn]() {
            search->hide();
            searchBtn->show();
        });

        m_notificationButton = new QToolButton(header);
        m_notificationButton->setObjectName(QStringLiteral("NotificationButton"));
        m_notificationButton->setIcon(Theme::Icon(QStringLiteral("bell")));
        m_notificationButton->setToolTip(tr("Notifications"));
        m_notificationButton->setPopupMode(QToolButton::InstantPopup);
        m_notificationButton->setFocusPolicy(Qt::NoFocus);
        m_notificationButton->setMenu(new QMenu(m_notificationButton));
        layout->addWidget(m_notificationButton);

        auto* playGroup = new QFrame(header);
        playGroup->setObjectName(QStringLiteral("PlayControls"));
        auto* playLayout = new QHBoxLayout(playGroup);
        playLayout->setContentsMargins(0, 0, 0, 0);
        playLayout->setSpacing(2);
        // Play and its options are two plain buttons drawn from one "playing" property, not a
        // checkable MenuButtonPopup, whose checked/pressed sub-control states left stale paint.
        auto* playBtn = new QToolButton(playGroup);
        playBtn->setObjectName(QStringLiteral("PlayButton"));
        playBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        playBtn->setToolTip(m_playAction->toolTip());
        connect(playBtn, &QToolButton::clicked, m_playAction, &QAction::trigger);
        auto* playMenuBtn = new QToolButton(playGroup);
        playMenuBtn->setObjectName(QStringLiteral("PlayMenuButton"));
        playMenuBtn->setPopupMode(QToolButton::InstantPopup);
        playMenuBtn->setToolTip(tr("Play options"));
        playMenuBtn->setFocusPolicy(Qt::NoFocus);
        auto* playMenu = new QMenu(playMenuBtn);
        QAction* realtimeAction = playMenu->addAction(tr("Real-time"));
        realtimeAction->setCheckable(true);
        realtimeAction->setChecked(true);
        realtimeAction->setToolTip(tr("Off: the simulation only advances with Step"));
        connect(realtimeAction, &QAction::toggled, this, [this](bool realtime) {
            if (m_engine) m_engine->SetRealtime(realtime);
        });
        connect(playMenu, &QMenu::aboutToShow, this, [this, realtimeAction]() {
            if (m_engine) realtimeAction->setChecked(m_engine->IsRealtime());
        });
        addPlayModeAndBuild(playMenu);
        playMenuBtn->setMenu(playMenu);
        playBtn->setFocusPolicy(Qt::NoFocus);
        auto* playPair = new QHBoxLayout();
        playPair->setSpacing(0);
        playPair->addWidget(playBtn);
        playPair->addWidget(playMenuBtn);
        playLayout->addLayout(playPair);
        for (QAction* action : { m_pauseAction, m_stopAction, m_stepAction }) {
            auto* button = new QToolButton(playGroup);
            button->setDefaultAction(action);
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            button->setFocusPolicy(Qt::NoFocus);
            if (action == m_stopAction) button->setObjectName(QStringLiteral("StopButton"));
            playLayout->addWidget(button);
        }
        auto showPlayState = [this, playBtn, playMenuBtn](bool playing) {
            playBtn->setText(playing ? tr("Playing") : tr("Play"));
            playBtn->setIcon(playing ? Theme::Icon(QStringLiteral("dot"), Theme::IconTone::Success)
                                     : Theme::Icon(QStringLiteral("play"), Theme::IconTone::OnAction));
            playMenuBtn->setIcon(Theme::Icon(QStringLiteral("chevron-down"),
                                             playing ? Theme::IconTone::Success : Theme::IconTone::OnAction));
            for (QToolButton* button : { playBtn, playMenuBtn }) {
                button->setProperty("playing", playing);
                button->style()->unpolish(button);
                button->style()->polish(button);
            }
        };
        connect(m_playAction, &QAction::toggled, this, showPlayState);
        showPlayState(false);
        layout->addWidget(playGroup);

        m_buildBtn = new QToolButton(header);
        m_buildBtn->setObjectName(QStringLiteral("BuildRunButton"));
        m_buildBtn->setText(tr("Build && Run"));
        m_buildBtn->setIcon(Theme::Icon(QStringLiteral("build"), Theme::IconTone::Primary));
        m_buildBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_buildBtn->setFocusPolicy(Qt::NoFocus);
        m_buildBtn->setToolTip(buildAndRunAction->toolTip());
        auto* buildMenu = new QMenu(m_buildBtn);
        buildMenu->addAction(buildRunDevelopmentAction);
        buildMenu->addAction(m_buildRunReleaseAction);
        buildMenu->addSeparator();
        buildMenu->addAction(tr("Validate Project"), this, &EditorMainWindow::onValidateProject);
        buildMenu->addAction(tr("Package"), this, &EditorMainWindow::onBuildProject);
        buildMenu->addAction(tr("Package && Run"), this, &EditorMainWindow::onPackageAndRun);
        buildMenu->addSeparator();
        buildMenu->addAction(tr("Run Last Package"), this, &EditorMainWindow::onRunLastPackage);
        buildMenu->addAction(tr("Clean Package"), this, &EditorMainWindow::onCleanPackage);
        m_buildBtn->setMenu(buildMenu);
        m_buildBtn->setPopupMode(QToolButton::DelayedPopup);
        connect(m_buildBtn, &QToolButton::clicked, this, &EditorMainWindow::onBuildAndRun);
        layout->addWidget(m_buildBtn);

        setMenuWidget(header);
    }

    // Viewport strip tools; setupCentralWidget places this toolbar above the viewport.
    m_topToolbar = new QToolBar(tr("Tools"), this);
    m_topToolbar->setObjectName("EditorTopToolbar");
    m_topToolbar->setMovable(false);
    m_topToolbar->setFloatable(false);
    m_topToolbar->setIconSize(QSize(16, 16));

    QActionGroup* transformGroup = new QActionGroup(this);
    transformGroup->setExclusive(true);
    m_selectAction = new QAction(Theme::Icon(QStringLiteral("select")), tr("Select (Q)"), this);
    m_translateAction = new QAction(Theme::Icon(QStringLiteral("move")), tr("Move (W)"), this);
    m_rotateAction = new QAction(Theme::Icon(QStringLiteral("rotate")), tr("Rotate (E)"), this);
    m_scaleAction = new QAction(Theme::Icon(QStringLiteral("scale")), tr("Scale (R)"), this);
    for (QAction* action : { m_selectAction, m_translateAction, m_rotateAction, m_scaleAction }) {
        action->setCheckable(true);
        transformGroup->addAction(action);
        m_topToolbar->addAction(action);
    }
    m_translateAction->setChecked(true);
    m_topToolbar->addSeparator();

    m_artistToolRail = new QToolBar(tr("Tool Rail"), this);
    m_artistToolRail->setObjectName(QStringLiteral("ArtistToolRail"));
    m_artistToolRail->setMovable(false);
    m_artistToolRail->setFloatable(false);
    m_artistToolRail->setOrientation(Qt::Vertical);
    m_artistToolRail->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_artistToolRail->setIconSize(QSize(16, 16));
    m_artistToolRail->toggleViewAction()->setEnabled(false);
    for (QAction* action : { m_selectAction, m_translateAction, m_rotateAction, m_scaleAction }) {
        m_artistToolRail->addAction(action);
    }
    addToolBar(Qt::LeftToolBarArea, m_artistToolRail);
    m_artistToolRail->hide();

    auto* spaceSwitch = new QFrame(m_topToolbar);
    spaceSwitch->setObjectName(QStringLiteral("SegmentedControl"));
    spaceSwitch->setFixedHeight(28);
    auto* spaceLayout = new QHBoxLayout(spaceSwitch);
    spaceLayout->setContentsMargins(2, 2, 2, 2);
    spaceLayout->setSpacing(2);
    m_worldBtn = new QToolButton(spaceSwitch);
    m_worldBtn->setText(tr("World"));
    m_worldBtn->setIcon(Theme::Icon(QStringLiteral("globe")));
    m_worldLocalBtn = new QToolButton(spaceSwitch);
    m_worldLocalBtn->setText(tr("Local"));
    m_worldLocalBtn->setIcon(Theme::Icon(QStringLiteral("cube")));
    for (QToolButton* button : { m_worldBtn, m_worldLocalBtn }) {
        button->setObjectName(QStringLiteral("SegmentButton"));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setCheckable(true);
        button->setToolTip(tr("Gizmo space (X)"));
        button->setFocusPolicy(Qt::NoFocus);
        spaceLayout->addWidget(button);
    }
    m_worldBtn->setChecked(true);
    connect(m_worldBtn, &QToolButton::clicked, this, [this]() { m_worldLocalBtn->setChecked(false); syncGizmoSpaceButtons(); });
    connect(m_worldLocalBtn, &QToolButton::toggled, this, [this](bool local) {
        syncGizmoSpaceButtons();
        if (m_engine) {
            if (auto* ctx = m_engine->GetEditorContext()) {
                ctx->SetGizmoSpace(local ? GizmoSpace::Local : GizmoSpace::World);
            }
        }
    });
    connect(m_worldLocalBtn, &QToolButton::clicked, this, [this]() { m_worldLocalBtn->setChecked(true); syncGizmoSpaceButtons(); });
    m_topToolbar->addWidget(spaceSwitch);

    m_snapToggleBtn = new QToolButton(m_topToolbar);
    m_snapToggleBtn->setIcon(Theme::Icon(QStringLiteral("snap")));
    m_snapToggleBtn->setToolTip(tr("Snap to increments"));
    m_snapToggleBtn->setCheckable(true);
    m_topToolbar->addWidget(m_snapToggleBtn);

    m_snapValueCombo = new QComboBox(m_topToolbar);
    m_snapValueCombo->setObjectName(QStringLiteral("FlatCombo"));
    m_snapValueCombo->setToolTip(tr("Snap increment"));
    m_snapValueCombo->addItems({ tr("0.01 m"), tr("0.1 m"), tr("0.25 m"), tr("0.5 m"),
                                 tr("1 m"), tr("5 m"), tr("10 m"), tr("50 m"), tr("100 m") });
    m_snapValueCombo->setCurrentText(tr("1 m"));
    m_topToolbar->addWidget(m_snapValueCombo);

    m_cameraSpeedCombo = new QComboBox(this);
    m_cameraSpeedCombo->setToolTip(tr("Camera speed"));
    m_cameraSpeedCombo->addItems({ QStringLiteral("0.5×"), QStringLiteral("1.0×"),
                                   QStringLiteral("2.0×"), QStringLiteral("4.0×") });
    m_cameraSpeedCombo->setCurrentIndex(1);

    {
        auto updateSnapChoices = [this](GizmoMode mode) {
            if (!m_snapValueCombo) return;
            m_snapValueCombo->blockSignals(true);
            m_snapValueCombo->clear();
            if (mode == GizmoMode::Rotate) {
                m_snapValueCombo->addItems(
                    {tr("1°"), tr("5°"), tr("10°"), tr("15°"),
                     tr("30°"), tr("45°"), tr("90°")});
                m_snapValueCombo->setCurrentText(tr("15°"));
            } else if (mode == GizmoMode::Scale) {
                m_snapValueCombo->addItems(
                    {tr("0.01"), tr("0.05"), tr("0.1"),
                     tr("0.25"), tr("0.5"), tr("1")});
                m_snapValueCombo->setCurrentText(tr("0.1"));
            } else {
                m_snapValueCombo->addItems(
                    {tr("0.01 m"), tr("0.1 m"), tr("0.25 m"), tr("0.5 m"),
                     tr("1 m"), tr("5 m"), tr("10 m"), tr("50 m"), tr("100 m")});
                m_snapValueCombo->setCurrentText(tr("1 m"));
            }
            m_snapValueCombo->blockSignals(false);
        };

        connect(m_selectAction, &QAction::triggered, this, [this]() {
            if (m_engine) {
                if (auto* ctx = m_engine->GetEditorContext()) ctx->SetGizmoMode(GizmoMode::None);
            }
        });
        connect(m_translateAction, &QAction::triggered, this, [this, updateSnapChoices]() {
            if (m_engine) {
                if (auto* ctx = m_engine->GetEditorContext()) ctx->SetGizmoMode(GizmoMode::Translate);
            }
            updateSnapChoices(GizmoMode::Translate);
        });
        connect(m_rotateAction, &QAction::triggered, this, [this, updateSnapChoices]() {
            if (m_engine) {
                if (auto* ctx = m_engine->GetEditorContext()) ctx->SetGizmoMode(GizmoMode::Rotate);
            }
            updateSnapChoices(GizmoMode::Rotate);
        });
        connect(m_scaleAction, &QAction::triggered, this, [this, updateSnapChoices]() {
            if (m_engine) {
                if (auto* ctx = m_engine->GetEditorContext()) ctx->SetGizmoMode(GizmoMode::Scale);
            }
            updateSnapChoices(GizmoMode::Scale);
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
            clean.remove(QStringLiteral(" m"));
            float value = clean.toFloat(&ok);
            if (!ok || !m_engine) return;
            if (auto* ctx = m_engine->GetEditorContext()) {
                auto& snap = ctx->GetSnapSettings();
                switch (ctx->GetGizmoMode()) {
                    case GizmoMode::Rotate: snap.rotateSnapDeg = value; break;
                    case GizmoMode::Scale: snap.scaleSnap = value; break;
                    default: snap.translateSnap = value; break;
                }
            }
        });

        connect(m_cameraSpeedCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
            static const float speeds[] = { 2.5f, 5.0f, 10.0f, 20.0f };
            if (idx < 0 || idx >= 4 || !m_engine) return;
            if (auto* camera = m_engine->GetCameraController())
                camera->baseMoveSpeed = speeds[idx];
        });

    }

    m_topToolbar->addSeparator()->setObjectName(QStringLiteral("PluginToolbarSlot"));
}

void EditorMainWindow::setupDockWidgets()
{
    // Allow docking on all sides
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // ─────────────────────────────────────────────────────────────────────────
    // Outliner Dock (Left) - Scene Hierarchy
    // ─────────────────────────────────────────────────────────────────────────
    m_outlinerDock = new QDockWidget(tr("Hierarchy"), this);
    m_outlinerDock->setObjectName("OutlinerDock");
    m_outlinerDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_outlinerPanel = new QWidget(m_outlinerDock);
    m_outlinerPanel->setObjectName("OutlinerPanel");
    auto* outlinerLayout = new QVBoxLayout(m_outlinerPanel);
    outlinerLayout->setContentsMargins(8, 8, 8, 8);
    outlinerLayout->setSpacing(6);

    m_outlinerSearch = new QLineEdit(m_outlinerPanel);
    m_outlinerSearch->setPlaceholderText(tr("Search objects..."));
    m_outlinerSearch->setClearButtonEnabled(true);
    m_outlinerSearch->addAction(Theme::Icon(QStringLiteral("search"), Theme::IconTone::Muted), QLineEdit::LeadingPosition);
    Theme::AddShortcutHint(m_outlinerSearch, QKeySequence(QKeySequence::Find));
    auto* outlinerFind = new QShortcut(QKeySequence::Find, m_outlinerPanel);
    outlinerFind->setContext(Qt::WidgetWithChildrenShortcut);
    connect(outlinerFind, &QShortcut::activated, m_outlinerSearch, [this]() { m_outlinerSearch->setFocus(Qt::ShortcutFocusReason); });
    outlinerLayout->addWidget(m_outlinerSearch);

    m_outlinerTree = new OutlinerTreeWidget(m_outlinerPanel);
    m_outlinerTree->setObjectName("OutlinerTree");
    m_outlinerTree->setHeaderHidden(true);
    m_outlinerTree->setItemDelegate(new OutlinerRowDelegate(m_outlinerTree));
    HierarchyEye::Install(m_outlinerTree, [this](QTreeWidgetItem* item, bool isolate) {
        toggleOutlinerVisibility(static_cast<entt::entity>(item->data(0, Qt::UserRole).toUInt()), isolate);
    }, true);
    m_outlinerTree->setIconSize(QSize(16, 16));
    m_outlinerTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_outlinerTree->setRootIsDecorated(true);
    m_outlinerTree->setIndentation(18);
    m_outlinerTree->setUniformRowHeights(true);
    m_outlinerTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_outlinerTree->header()->setStretchLastSection(true);
    m_outlinerTree->setTextElideMode(Qt::ElideRight);
    m_outlinerTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_outlinerTree->setDefaultDropAction(Qt::MoveAction);
    m_outlinerTree->setContextMenuPolicy(Qt::CustomContextMenu);
    outlinerLayout->addWidget(m_outlinerTree, 1);

    m_outlinerDock->setWidget(m_outlinerPanel);
    
    // Connect selection changed signal
    connect(m_outlinerTree, &QTreeWidget::itemSelectionChanged, 
            this, &EditorMainWindow::onOutlinerSelectionChanged);

    // Context menu
    connect(m_outlinerTree, &QTreeWidget::customContextMenuRequested,
            this, &EditorMainWindow::onOutlinerContextMenu);

    // Context-aware outliner shortcuts (active only when the tree has focus)
    auto* deleteShortcut = new QShortcut(QKeySequence::Delete, m_outlinerTree);
    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, [this]() {
        if (!m_engine || (m_outlinerSearch && m_outlinerSearch->hasFocus())) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasSelection()) return;
        deleteEntityFromOutliner(ctx->GetSelectedEntity());
    });

    auto* renameShortcut = new QShortcut(Qt::Key_F2, m_outlinerTree);
    renameShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(renameShortcut, &QShortcut::activated, this, [this]() {
        if (!m_engine || (m_outlinerSearch && m_outlinerSearch->hasFocus())) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasSelection()) return;
        renameEntityInOutliner(ctx->GetSelectedEntity());
    });

    auto* duplicateShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), m_outlinerTree);
    duplicateShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(duplicateShortcut, &QShortcut::activated, this, &EditorMainWindow::onDuplicateEntity);

    auto* selectAllShortcut = new QShortcut(QKeySequence::SelectAll, m_outlinerTree);
    selectAllShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(selectAllShortcut, &QShortcut::activated, this, [this]() {
        if (!m_outlinerTree || (m_outlinerSearch && m_outlinerSearch->hasFocus())) return;
        m_outlinerTree->selectAll();
        // Deselect non-entity root row if selected
        if (QTreeWidgetItem* root = m_outlinerTree->topLevelItem(0)) {
            root->setSelected(false);
        }
    });

    auto* escapeShortcut = new QShortcut(Qt::Key_Escape, m_outlinerTree);
    escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this]() {
        if (!m_engine || (m_outlinerSearch && m_outlinerSearch->hasFocus())) return;
        if (auto* ctx = m_engine->GetEditorContext()) {
            ctx->ClearSelection();
            m_outlinerTree->clearSelection();
            refreshDetailsPanel();
        }
    });

    // Handle drag & drop reparenting
    connect(m_outlinerTree, &OutlinerTreeWidget::itemsDropped, this, [this]() {
        // After Qt moves items in the tree, sync the hierarchy back to the scene
        if (!m_engine) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasActiveScene()) return;
        Scene* scene = ctx->GetActiveScene();

        EditorUndoManager::PushSnapshot(ctx, "Reparent entity");

        // Walk the tree and fix parent relationships
        std::function<void(QTreeWidgetItem*, entt::entity)> syncParents;
        syncParents = [&](QTreeWidgetItem* parentItem, entt::entity parentEntity) {
            for (int i = 0; i < parentItem->childCount(); ++i) {
                QTreeWidgetItem* childItem = parentItem->child(i);
                QVariant data = childItem->data(0, Qt::UserRole);
                if (!data.isValid()) continue;
                entt::entity childEntity = static_cast<entt::entity>(data.toUInt());
                Entity child(childEntity, scene);
                if (!child) continue;

                if (parentEntity == entt::null) {
                    scene->RemoveParent(child, true);
                } else {
                    Entity parent(parentEntity, scene);
                    if (parent) scene->SetParent(child, parent, true);
                }
                syncParents(childItem, childEntity);
            }
        };

        for (int i = 0; i < m_outlinerTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* top = m_outlinerTree->topLevelItem(i);
            // Scene root node - children are root-level entities
            for (int j = 0; j < top->childCount(); ++j) {
                QTreeWidgetItem* childItem = top->child(j);
                QVariant data = childItem->data(0, Qt::UserRole);
                if (!data.isValid()) continue;
                entt::entity childEntity = static_cast<entt::entity>(data.toUInt());
                Entity child(childEntity, scene);
                if (child) scene->RemoveParent(child, true);
                syncParents(childItem, childEntity);
            }
        }

        ctx->MarkDirty();
        rebuildOutliner();
    });

    connect(m_outlinerSearch, &QLineEdit::textChanged, this, [this](const QString& text) {
        auto filterItem = [&](auto&& self, QTreeWidgetItem* item) -> bool {
            const QString query = text.trimmed();
            bool selfMatch = item->text(0).contains(query, Qt::CaseInsensitive);
            const QVariant data = item->data(0, Qt::UserRole);
            if (data.isValid() && m_engine) {
                auto* ctx = m_engine->GetEditorContext();
                if (ctx && ctx->HasActiveScene()) {
                    const entt::entity entity =
                        static_cast<entt::entity>(data.toUInt());
                    auto& registry = ctx->GetActiveScene()->GetRegistry();
                    if (query.startsWith("component:", Qt::CaseInsensitive)) {
                        const QString type = query.mid(10).trimmed().toLower();
                        selfMatch =
                            (type == "light" && registry.all_of<LightComponent>(entity)) ||
                            (type == "camera" && registry.all_of<CameraComponent>(entity)) ||
                            (type == "mesh" && registry.all_of<MeshRendererComponent>(entity)) ||
                            (type == "audio" && registry.all_of<AudioSourceComponent>(entity));
                    } else if (query.startsWith("prefab:", Qt::CaseInsensitive)) {
                        const QString prefabQuery = query.mid(7).trimmed();
                        selfMatch = registry.all_of<PrefabInstanceComponent>(entity) &&
                            QString::fromStdString(
                                registry.get<PrefabInstanceComponent>(entity).prefabPath)
                                .contains(prefabQuery, Qt::CaseInsensitive);
                    } else if (query.compare("hidden:", Qt::CaseInsensitive) == 0) {
                        selfMatch = ctx->IsEditorHidden(entity);
                    } else if (query.compare("locked:", Qt::CaseInsensitive) == 0) {
                        selfMatch = ctx->IsEditorLocked(entity);
                    }
                }
            }
            bool childMatch = false;
            for (int i = 0; i < item->childCount(); ++i) {
                childMatch = self(self, item->child(i)) || childMatch;
            }
            bool visible = query.isEmpty() || selfMatch || childMatch;
            item->setHidden(!visible);
            return visible;
        };

        for (int i = 0; i < m_outlinerTree->topLevelItemCount(); ++i) {
            filterItem(filterItem, m_outlinerTree->topLevelItem(i));
        }
    });

    m_outlinerFooter = new QLabel(m_outlinerPanel);
    m_outlinerFooter->setProperty("role", "caption");
    outlinerLayout->addWidget(m_outlinerFooter);

    auto* addObjectBtn = new QToolButton();
    addObjectBtn->setIcon(Theme::Icon(QStringLiteral("plus")));
    addObjectBtn->setToolTip(tr("Add object"));
    addObjectBtn->setPopupMode(QToolButton::InstantPopup);
    auto* addObjectMenu = new QMenu(addObjectBtn);
    addObjectMenu->addAction(Theme::Icon(QStringLiteral("cube")), tr("Empty Entity"), this, &EditorMainWindow::createEmptyEntity);
    addObjectMenu->addAction(Theme::Icon(QStringLiteral("folder")), tr("Folder"), this, [this]() { createFolderInOutliner(entt::null); });
    addObjectBtn->setMenu(addObjectMenu);
    InstallDockTitleBar(m_outlinerDock, { addObjectBtn });

    addDockWidget(Qt::LeftDockWidgetArea, m_outlinerDock);

    // ─────────────────────────────────────────────────────────────────────────
    // Details Dock (Right) - Entity Inspector
    // ─────────────────────────────────────────────────────────────────────────
    m_detailsDock = new QDockWidget(tr("Inspector"), this);
    m_detailsDock->setObjectName("DetailsDock");
    m_detailsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // Scroll area wrapper so the inspector is scrollable when many components are shown
    m_detailsScrollArea = new QScrollArea(m_detailsDock);
    m_detailsScrollArea->setWidgetResizable(true);
    m_detailsScrollArea->setFrameShape(QFrame::NoFrame);
    m_detailsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_detailsWidget = new QWidget();
    m_detailsWidget->setObjectName("DetailsPanel");
    m_detailsLayout = new QVBoxLayout(m_detailsWidget);
    m_detailsLayout->setContentsMargins(8, 8, 8, 8);
    m_detailsLayout->setSpacing(8);

    // Header card: type tile, name and type line, visibility, pin, gizmo space, transform clipboard.
    {
        auto* card = new QFrame(m_detailsWidget);
        card->setObjectName(QStringLiteral("InspectorHeader"));
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(8);

        auto* identityRow = new QHBoxLayout();
        identityRow->setSpacing(10);
        m_entityIconLabel = new QLabel(card);
        m_entityIconLabel->setObjectName("DetailsEntityIcon");
        m_entityIconLabel->setFixedSize(36, 36);
        m_entityIconLabel->setAlignment(Qt::AlignCenter);
        identityRow->addWidget(m_entityIconLabel);

        auto* names = new QVBoxLayout();
        names->setSpacing(0);
        m_nameEdit = new QLineEdit(card);
        m_nameEdit->setObjectName(QStringLiteral("InspectorName"));
        m_nameEdit->setPlaceholderText(tr("(No selection)"));
        m_nameEdit->setEnabled(false);
        names->addWidget(m_nameEdit);
        m_entityIdLabel = new QLabel(tr("Select an entity to inspect."), card);
        m_entityIdLabel->setObjectName("DetailsEntityId");
        names->addWidget(m_entityIdLabel);
        identityRow->addLayout(names, 1);

        m_visibilityBtn = new QToolButton(card);
        m_visibilityBtn->setIcon(Theme::Icon(QStringLiteral("eye")));
        m_visibilityBtn->setToolTip(tr("Toggle mesh renderer visibility"));
        m_visibilityBtn->setCheckable(true);
        m_visibilityBtn->setChecked(true);
        m_visibilityBtn->setEnabled(false);
        m_visibilityBtn->setProperty("quiet", true);
        connect(m_visibilityBtn, &QToolButton::toggled, this, &EditorMainWindow::onVisibilityToggled);
        identityRow->addWidget(m_visibilityBtn);

        m_lockCheck = new QToolButton(card);
        m_lockCheck->setIcon(Theme::Icon(QStringLiteral("unlock")));
        m_lockCheck->setCheckable(true);
        m_lockCheck->setProperty("quiet", true);
        m_lockCheck->setToolTip(
            tr("Pin this entity in the Inspector; this does not lock viewport editing"));
        connect(m_lockCheck, &QToolButton::toggled, this, [this](bool checked) {
            m_lockCheck->setIcon(Theme::Icon(checked ? QStringLiteral("lock") : QStringLiteral("unlock")));
            m_detailsLocked = checked;
            if (checked && m_engine) {
                auto* ctx = m_engine->GetEditorContext();
                if (ctx && ctx->HasSelection()) {
                    m_lockedEntity = ctx->GetSelectedEntity();
                }
            }
            refreshDetailsPanel();
        });
        identityRow->addWidget(m_lockCheck);
        cardLayout->addLayout(identityRow);

        auto* spaceRow = new QHBoxLayout();
        auto* spaceSwitch = new QFrame(card);
        spaceSwitch->setObjectName(QStringLiteral("SegmentedControl"));
        auto* spaceLayout = new QHBoxLayout(spaceSwitch);
        spaceLayout->setContentsMargins(2, 2, 2, 2);
        spaceLayout->setSpacing(2);
        m_inspectorWorldBtn = new QToolButton(spaceSwitch);
        m_inspectorWorldBtn->setText(tr("World"));
        m_inspectorWorldBtn->setIcon(Theme::Icon(QStringLiteral("globe")));
        m_inspectorLocalBtn = new QToolButton(spaceSwitch);
        m_inspectorLocalBtn->setText(tr("Local"));
        m_inspectorLocalBtn->setIcon(Theme::Icon(QStringLiteral("cube")));
        for (QToolButton* button : { m_inspectorWorldBtn, m_inspectorLocalBtn }) {
            button->setObjectName(QStringLiteral("SegmentButton"));
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            button->setCheckable(true);
            button->setFocusPolicy(Qt::NoFocus);
            button->setToolTip(tr("Gizmo space only. Numeric transform fields always show local values."));
            spaceLayout->addWidget(button);
        }
        m_inspectorWorldBtn->setChecked(!m_worldLocalBtn->isChecked());
        m_inspectorLocalBtn->setChecked(m_worldLocalBtn->isChecked());
        connect(m_inspectorWorldBtn, &QToolButton::clicked, this, [this]() { m_worldLocalBtn->setChecked(false); syncGizmoSpaceButtons(); });
        connect(m_inspectorLocalBtn, &QToolButton::clicked, this, [this]() { m_worldLocalBtn->setChecked(true); syncGizmoSpaceButtons(); });
        spaceRow->addWidget(spaceSwitch);
        spaceRow->addStretch(1);
        const struct { const char* icon; const char* tip; } transformTools[] = {
            { "reset", QT_TR_NOOP("Reset transform") },
            { "copy", QT_TR_NOOP("Copy transform") },
            { "paste", QT_TR_NOOP("Paste transform") },
        };
        for (const auto& tool : transformTools) {
            auto* button = new QToolButton(card);
            button->setIcon(Theme::Icon(QString::fromLatin1(tool.icon)));
            button->setToolTip(tr(tool.tip));
            button->setProperty("quiet", true);
            const QString icon = QString::fromLatin1(tool.icon);
            connect(button, &QToolButton::clicked, this, [this, icon]() {
                if (!m_transformGroup || !m_transformGroup->isEnabled()) return;
                if (icon == QLatin1String("reset")) {
                    for (QDoubleSpinBox* field : { m_posX, m_posY, m_posZ, m_rotX, m_rotY, m_rotZ }) field->setValue(0.0);
                    for (QDoubleSpinBox* field : { m_scaleX, m_scaleY, m_scaleZ }) field->setValue(1.0);
                    endCoalescedTransformUndo();
                } else if (icon == QLatin1String("copy")) {
                    copyInspectorComponent("Transform");
                } else {
                    pasteInspectorComponentValues("Transform");
                }
            });
            spaceRow->addWidget(button);
        }
        cardLayout->addLayout(spaceRow);
        m_detailsLayout->addWidget(card);

        m_detailsSharedHint = new QLabel();
        m_detailsSharedHint->setObjectName("DetailsSharedHint");
        m_detailsSharedHint->setProperty("role", "caption");
        m_detailsSharedHint->hide();
        m_detailsLayout->addWidget(m_detailsSharedHint);

        m_detailsAdvancedId = new QLabel();
        m_detailsAdvancedId->setObjectName("DetailsAdvancedId");
        m_detailsAdvancedId->setProperty("role", "caption");
        m_detailsAdvancedId->hide();
        m_detailsLayout->addWidget(m_detailsAdvancedId);

        m_detailsEmptyHint = new QLabel(tr("Select an entity to inspect."));
        m_detailsEmptyHint->setWordWrap(true);
        m_detailsEmptyHint->setProperty("role", "muted");
        m_detailsLayout->addWidget(m_detailsEmptyHint);
    }

    
    // Capture undo before the first character of a Details rename session.
    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString&) {
        if (m_updatingDetails || !m_engine) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx) return;
        if (!m_nameEdit->property("undoPushed").toBool()) {
            EditorUndoManager::PushSnapshot(ctx, "Rename entity");
            m_nameEdit->setProperty("undoPushed", true);
            updateUndoRedoActions();
        }
    });
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
        m_nameEdit->setProperty("undoPushed", false);
    });
    connect(m_nameEdit, &QLineEdit::textEdited, this, &EditorMainWindow::onNameChanged);

    m_prefabBar = new PrefabInstanceBar(m_detailsWidget);
    m_detailsLayout->addWidget(m_prefabBar);
    connect(m_prefabBar, &PrefabInstanceBar::revertPropertyRequested, this, [this](const QString& component, const QString& property) {
        if (!m_engine) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasSelection()) return;
        runPrefabRevert(ctx->GetSelectedEntity(), component.toStdString(), property.toStdString());
    });
    connect(m_prefabBar, &PrefabInstanceBar::revertAllRequested, this, [this]() {
        if (!m_engine) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasSelection()) return;
        runPrefabRevert(ctx->GetSelectedEntity(), "*", "*");
    });
    connect(m_prefabBar, &PrefabInstanceBar::applyPropertyRequested, this, [this](const QString& component, const QString& property) {
        if (!m_engine) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasSelection()) return;
        runPrefabApply(ctx->GetSelectedEntity(), component.toStdString(), property.toStdString(), false);
    });
    connect(m_prefabBar, &PrefabInstanceBar::applyAllRequested, this, [this]() {
        if (!m_engine) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasSelection()) return;
        runPrefabApply(ctx->GetSelectedEntity(), "*", "*", true);
    });
    connect(m_prefabBar, &PrefabInstanceBar::revertNodeRequested, this, [this]() {
        if (!m_engine) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasSelection()) return;
        runPrefabRevert(ctx->GetSelectedEntity(), "", "");
    });
    connect(m_prefabBar, &PrefabInstanceBar::applyNodeRequested, this, [this]() {
        if (!m_engine) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasSelection()) return;
        runPrefabApply(ctx->GetSelectedEntity(), "", "", false);
    });
    connect(m_prefabBar, &PrefabInstanceBar::refreshRequested, this, [this]() {
        if (!m_engine) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasActiveScene() || !ctx->HasSelection()) return;
        Scene* scene = ctx->GetActiveScene();
        Entity entity(ctx->GetSelectedEntity(), scene);
        EditorUndoManager::PushSnapshot(ctx, "Refresh prefab instance");
        PrefabOverrideSystem::SyncInstance(*scene, entity);
        PrefabOverrideSystem::RefreshInstance(*scene, entity);
        m_engine->RebuildSceneMeshes();
        ctx->MarkDirty();
        rebuildOutliner();
        refreshDetailsPanel();
    });
    connect(m_prefabBar, &PrefabInstanceBar::openPrefabRequested, this, [this]() {
        if (!m_engine || !m_prefabEditorManager) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasActiveScene() || !ctx->HasSelection()) return;
        Entity entity(ctx->GetSelectedEntity(), ctx->GetActiveScene());
        auto* inst = PrefabOverrideSystem::TryGetInstance(*ctx->GetActiveScene(), entity);
        if (inst && !inst->prefabPath.empty()) {
            m_prefabEditorManager->OpenPrefab(QString::fromStdString(inst->prefabPath));
        }
    });

    // Transform group
    m_transformGroup = new QGroupBox(tr("Transform"));
    m_transformGroup->setObjectName("TransformCard");
    m_transformGroup->setEnabled(false);
    QFormLayout* transformLayout = new QFormLayout(m_transformGroup);
    transformLayout->setContentsMargins(12, 12, 12, 12);
    transformLayout->setSpacing(8);
    transformLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    transformLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Helper lambda to create a row of 3 spin boxes
    auto createVec3Row = [this](QDoubleSpinBox*& x, QDoubleSpinBox*& y, QDoubleSpinBox*& z, 
                                double min, double max, double step, int decimals, double defaultValue,
                                QWidget* trailing = nullptr) {
        QWidget* rowWidget = new QWidget();
        QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);

        auto createSpinBox = [&](QDoubleSpinBox*& sb, const char* axis) {
            auto* chip = new QToolButton();
            chip->setObjectName(QStringLiteral("AxisChip"));
            chip->setProperty("axis", QString::fromLatin1(axis).toLower());
            chip->setText(QString::fromLatin1(axis));
            chip->setToolTip(tr("Reset %1").arg(QString::fromLatin1(axis)));
            chip->setFocusPolicy(Qt::NoFocus);
            chip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Ignored);
            sb = new QDoubleSpinBox();
            sb->setProperty("axisField", true);
            ConfigureMixedSpin(sb, min, max, step, decimals);
            sb->setMinimumWidth(50);
            QDoubleSpinBox* field = sb;
            connect(chip, &QToolButton::clicked, this, [field, defaultValue]() {
                field->setValue(defaultValue);
                emit field->editingFinished();
            });
            rowLayout->addWidget(chip);
            rowLayout->addWidget(sb, 1);
            if (axis[0] != 'Z') rowLayout->addSpacing(4);
        };

        createSpinBox(x, "X");
        createSpinBox(y, "Y");
        createSpinBox(z, "Z");
        // Every row keeps the same trailing slot so the fields line up whether or not it holds a control.
        QWidget* slot = trailing ? trailing : new QWidget();
        slot->setFixedWidth(28);
        rowLayout->addWidget(slot);
        return rowWidget;
    };

    // Position (large range)
    QWidget* posRow = createVec3Row(m_posX, m_posY, m_posZ, -100000.0, 100000.0, 0.1, 3, 0.0);
    transformLayout->addRow(tr("Position"), posRow);
    
    // Rotation (degrees)
    QWidget* rotRow = createVec3Row(m_rotX, m_rotY, m_rotZ, -360.0, 360.0, 1.0, 2, 0.0);
    m_rotX->setSuffix(QString::fromUtf8("\xC2\xB0"));
    m_rotY->setSuffix(QString::fromUtf8("\xC2\xB0"));
    m_rotZ->setSuffix(QString::fromUtf8("\xC2\xB0"));
    transformLayout->addRow(tr("Rotation"), rotRow);
    
    // Scale
    auto* uniformScaleBtn = new QToolButton();
    uniformScaleBtn->setIcon(Theme::Icon(QStringLiteral("link")));
    uniformScaleBtn->setToolTip(tr("Uniform scale"));
    uniformScaleBtn->setCheckable(true);
    uniformScaleBtn->setProperty("quiet", true);
    QWidget* scaleRow = createVec3Row(m_scaleX, m_scaleY, m_scaleZ, 0.001, 10000.0, 0.1, 3, 1.0, uniformScaleBtn);
    transformLayout->addRow(tr("Scale"), scaleRow);

    auto bindUniformScale = [this](QDoubleSpinBox* x, QDoubleSpinBox* y, QDoubleSpinBox* z,
                                   QToolButton* uniformScaleBtn) {
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
    };

    bindUniformScale(m_scaleX, m_scaleY, m_scaleZ, uniformScaleBtn);

    // Connect position spinbox signals
    connect(m_posX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onPositionChanged);
    connect(m_posY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onPositionChanged);
    connect(m_posZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onPositionChanged);
    connect(m_posX, &QDoubleSpinBox::editingFinished, this, &EditorMainWindow::endCoalescedTransformUndo);
    connect(m_posY, &QDoubleSpinBox::editingFinished, this, &EditorMainWindow::endCoalescedTransformUndo);
    connect(m_posZ, &QDoubleSpinBox::editingFinished, this, &EditorMainWindow::endCoalescedTransformUndo);

    // Connect rotation spinbox signals
    connect(m_rotX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onRotationChanged);
    connect(m_rotY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onRotationChanged);
    connect(m_rotZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onRotationChanged);
    connect(m_rotX, &QDoubleSpinBox::editingFinished, this, &EditorMainWindow::endCoalescedTransformUndo);
    connect(m_rotY, &QDoubleSpinBox::editingFinished, this, &EditorMainWindow::endCoalescedTransformUndo);
    connect(m_rotZ, &QDoubleSpinBox::editingFinished, this, &EditorMainWindow::endCoalescedTransformUndo);

    // Connect scale spinbox signals
    connect(m_scaleX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onScaleChanged);
    connect(m_scaleY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onScaleChanged);
    connect(m_scaleZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &EditorMainWindow::onScaleChanged);
    connect(m_scaleX, &QDoubleSpinBox::editingFinished, this, &EditorMainWindow::endCoalescedTransformUndo);
    connect(m_scaleY, &QDoubleSpinBox::editingFinished, this, &EditorMainWindow::endCoalescedTransformUndo);
    connect(m_scaleZ, &QDoubleSpinBox::editingFinished, this, &EditorMainWindow::endCoalescedTransformUndo);

    m_transformGroup->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_transformGroup, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_engine) return;
        auto* ctx = m_engine->GetEditorContext();
        if (!ctx || !ctx->HasActiveScene() || !ctx->HasSelection()) return;
        Entity entity(ctx->GetSelectedEntity(), ctx->GetActiveScene());
        if (!PrefabOverrideSystem::IsPrefabInstanceEntity(entity) || PrefabOverrideSystem::IsInstanceRoot(entity)) {
            return;
        }
        QMenu menu(this);
        menu.addAction(tr("Revert Position"), this, [this, entity]() {
            runPrefabRevert(static_cast<entt::entity>(entity), "Transform", "position");
        });
        menu.addAction(tr("Revert Rotation"), this, [this, entity]() {
            runPrefabRevert(static_cast<entt::entity>(entity), "Transform", "rotation");
        });
        menu.addAction(tr("Revert Scale"), this, [this, entity]() {
            runPrefabRevert(static_cast<entt::entity>(entity), "Transform", "scale");
        });
        menu.addAction(tr("Revert Transform"), this, [this, entity]() {
            runPrefabRevert(static_cast<entt::entity>(entity), "Transform", "");
        });
        menu.addSeparator();
        menu.addAction(tr("Apply Transform to Prefab"), this, [this, entity]() {
            runPrefabApply(static_cast<entt::entity>(entity), "Transform", "", false);
        });
        menu.exec(m_transformGroup->mapToGlobal(pos));
    });

    m_transformGroup->setTitle(QString());
    m_detailsLayout->addWidget(wrapComponentCard(m_transformGroup, tr("Transform"), QStringLiteral("Transform"), [this]() {
        for (QDoubleSpinBox* field : { m_posX, m_posY, m_posZ, m_rotX, m_rotY, m_rotZ }) field->setValue(0.0);
        for (QDoubleSpinBox* field : { m_scaleX, m_scaleY, m_scaleZ }) field->setValue(1.0);
        endCoalescedTransformUndo();
    }));
    
    // Component inspector widgets will be inserted here dynamically
    // (LightInspector, EnvironmentInspector, MeshRendererInspector, etc.)

    // Component cards are inserted before this stretch, which keeps Add Component at the bottom.
    m_detailsLayout->addStretch();

    m_detailsPartialHint = new QLabel();
    m_detailsPartialHint->setWordWrap(true);
    m_detailsPartialHint->setProperty("role", "caption");
    m_detailsPartialHint->hide();
    m_detailsLayout->addWidget(m_detailsPartialHint);

    m_detailsScrollArea->setWidget(m_detailsWidget);
    auto* inspectorRoot = new QWidget(m_detailsDock);
    inspectorRoot->setObjectName(QStringLiteral("InspectorRoot"));
    auto* inspectorLayout = new QVBoxLayout(inspectorRoot);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(0);
    inspectorLayout->addWidget(m_detailsScrollArea, 1);
    auto* addComponentBar = new QWidget(inspectorRoot);
    addComponentBar->setObjectName(QStringLiteral("AddComponentBar"));
    addComponentBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* addComponentLayout = new QHBoxLayout(addComponentBar);
    addComponentLayout->setContentsMargins(12, 10, 12, 10);
    m_addComponentBtn = new QPushButton(Theme::Icon(QStringLiteral("plus"), Theme::IconTone::Primary), tr("Add Component"), addComponentBar);
    m_addComponentBtn->setEnabled(false);
    connect(m_addComponentBtn, &QPushButton::clicked, this, &EditorMainWindow::showAddComponentMenu);
    addComponentLayout->addWidget(m_addComponentBtn);
    inspectorLayout->addWidget(addComponentBar);
    m_detailsDock->setWidget(inspectorRoot);
    InstallDockTitleBar(m_detailsDock);
    addDockWidget(Qt::RightDockWidgetArea, m_detailsDock);

    // ─────────────────────────────────────────────────────────────────────────
    // Asset Browser Dock (Bottom)
    // ─────────────────────────────────────────────────────────────────────────
    m_assetBrowserDock = new QDockWidget(tr("Content Browser"), this);
    m_assetBrowserDock->setObjectName("AssetBrowserDock");
    m_assetBrowserDock->setAllowedAreas(Qt::AllDockWidgetAreas);

    // Create the new asset browser widget
    m_assetBrowser = new AssetBrowserWidget(this, m_assetBrowserDock);
    m_assetBrowserDock->setWidget(m_assetBrowser);

    // Create asset actions handler
    m_assetActions = std::make_unique<EditorAssetActions>(this);
    m_prefabEditorManager = std::make_unique<PrefabEditorManager>(this, this);
    m_characterEditorManager = std::make_unique<CharacterEditorManager>(this, this);

    // Connect asset browser to actions
    connect(m_assetBrowser, &AssetBrowserWidget::assetOpenRequested,
            this, [this](const QString& relativePath) {
                m_assetActions->Open(relativePath);
            });
    connect(m_assetBrowser, &AssetBrowserWidget::assetOpenInIDERequested,
            this, [this](const QString& relativePath) {
                m_assetActions->OpenInIDE(relativePath);
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

                // Register the portable relative asset path as an alias so that
                // RebuildSceneMeshes can find this mesh after scene reload / play-mode exit.
                {
                    std::string relativeKey = std::filesystem::path(relativePath.toStdString())
                                                  .lexically_normal().generic_string();
                    if (relativeKey != normalizedKey) {
                        frameRenderer->AddMeshAlias(relativeKey, meshHandle);
                    }
                    // Also register the project-root-relative key
                    std::string projectRelKey = relativeKey;
                    if (projectRelKey.rfind("assets/", 0) != 0) {
                        projectRelKey = "assets/" + projectRelKey;
                    }
                    if (projectRelKey != normalizedKey && projectRelKey != relativeKey) {
                        frameRenderer->AddMeshAlias(projectRelKey, meshHandle);
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
                // Store the path as project-root-relative (assets/<relativePath>)
                // so it resolves correctly on any device.
                std::string portablePath = relativePath.toStdString();
                {
                    std::string generic = std::filesystem::path(portablePath).generic_string();
                    if (generic.rfind("assets/", 0) != 0) {
                        portablePath = "assets/" + generic;
                    }
                }
                renderer.SetMesh(meshHandle, bounds, portablePath, true, subCount);
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
                const std::string meshStem = meshPath.stem().string();
                if (meshStem == "Capsule" || meshStem == "Cylinder") {
                    // Built-in procedural meshes are authored along +Z; the
                    // engine world and Jolt primitives use +Y as up.
                    transform.rotation.x = 1.57079632679f;
                }
                transform.scale    = Vec3(1.0f, 1.0f, 1.0f);

                // Select the newly created entity, mark dirty, refresh UI
                ctx->SetSelectionChangedCallback(nullptr);
                ctx->SelectEntity(static_cast<entt::entity>(created));
                ctx->SetSelectionChangedCallback([this](entt::entity) {
                    refreshDetailsPanel();
                });
                ctx->MarkDirty();
                m_engine->RebuildSceneMeshes();
                rebuildOutliner();
                refreshDetailsPanel();

                std::cout << "[MeshSpawn] Added mesh actor: " << entityName << "\n";
                statusBar()->showMessage(tr("Added mesh actor: %1").arg(QString::fromStdString(entityName)), 3000);
            });

    InstallDockTitleBar(m_assetBrowserDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_assetBrowserDock);

    // ─────────────────────────────────────────────────────────────────────────
    // Build Output Dock (Bottom, tabbed with Asset Browser)
    // ─────────────────────────────────────────────────────────────────────────
    m_buildOutputDock = new QDockWidget(tr("Output"), this);
    m_buildOutputDock->setObjectName("BuildOutputDock");
    m_buildOutputDock->setAllowedAreas(Qt::AllDockWidgetAreas);

    auto* buildOutputWidget = new QWidget(m_buildOutputDock);
    auto* buildOutputLayout = new QVBoxLayout(buildOutputWidget);
    buildOutputLayout->setContentsMargins(0, 0, 0, 0);
    buildOutputLayout->setSpacing(2);

    auto* buildTools = new QWidget(buildOutputWidget);
    auto* buildToolsLayout = new QHBoxLayout(buildTools);
    buildToolsLayout->setContentsMargins(4, 2, 4, 2);
    auto* buildSearch = new QLineEdit(buildTools);
    buildSearch->setPlaceholderText(tr("Search build output"));
    auto* findNext = new QPushButton(tr("Find Next"), buildTools);
    auto* copyLog = new QPushButton(tr("Copy All"), buildTools);
    auto* clearLog = new QPushButton(tr("Clear"), buildTools);
    buildToolsLayout->addWidget(buildSearch, 1);
    buildToolsLayout->addWidget(findNext);
    buildToolsLayout->addWidget(copyLog);
    buildToolsLayout->addWidget(clearLog);
    buildOutputLayout->addWidget(buildTools);

    m_buildProblems = new QTreeWidget(buildOutputWidget);
    m_buildProblems->setHeaderLabels({tr("Severity"), tr("Problem"), tr("Asset")});
    m_buildProblems->setRootIsDecorated(false);
    m_buildProblems->setMaximumHeight(170);
    m_buildProblems->hide();
    m_buildProblems->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_buildProblems, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
        const QString path = item ? item->data(0, Qt::UserRole).toString() : QString();
        if (!path.isEmpty() && m_assetBrowser) {
            m_assetBrowser->RevealAsset(path);
            m_assetBrowserDock->show();
            m_assetBrowserDock->raise();
        }
    });
    connect(m_buildProblems, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint& position) {
        QTreeWidgetItem* item = m_buildProblems->itemAt(position);
        const QString path = item ? item->data(0, Qt::UserRole).toString() : QString();
        if (path.isEmpty()) return;
        QMenu menu(this);
        menu.addAction(tr("Open"), [this, path]() {
            if (m_assetActions) m_assetActions->Open(path);
        });
        menu.addAction(tr("Reveal in Content Browser"), [this, path]() {
            if (m_assetBrowser) m_assetBrowser->RevealAsset(path);
        });
        menu.addAction(tr("Find References"), [this, path]() {
            if (m_assetBrowser) m_assetBrowser->FindReferences(path);
        });
        menu.exec(m_buildProblems->viewport()->mapToGlobal(position));
    });
    buildOutputLayout->addWidget(m_buildProblems);

    m_buildOutputText = new QPlainTextEdit(buildOutputWidget);
    m_buildOutputText->setReadOnly(true);
    m_buildOutputText->setFont(QFont("Consolas", 9));
    m_buildOutputText->setMaximumBlockCount(5000);
    buildOutputLayout->addWidget(m_buildOutputText, 1);
    connect(findNext, &QPushButton::clicked, this, [this, buildSearch]() {
        if (!m_buildOutputText->find(buildSearch->text())) {
            m_buildOutputText->moveCursor(QTextCursor::Start);
            m_buildOutputText->find(buildSearch->text());
        }
    });
    connect(copyLog, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_buildOutputText->toPlainText());
    });
    connect(clearLog, &QPushButton::clicked, m_buildOutputText, &QPlainTextEdit::clear);
    m_buildOutputDock->setWidget(buildOutputWidget);

    InstallDockTitleBar(m_buildOutputDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_buildOutputDock);
    tabifyDockWidget(m_assetBrowserDock, m_buildOutputDock);

    // tabifyDockWidget inserts after the first dock's tab, so this keeps Content Browser, Console, Output.
    m_consoleDock = new QDockWidget(tr("Console"), this);
    m_consoleDock->setObjectName("ConsoleDock");
    auto* consolePanel = new EditorConsolePanel(m_consoleDock);
    m_consoleDock->setWidget(consolePanel);
    InstallDockTitleBar(m_consoleDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_consoleDock);
    tabifyDockWidget(m_assetBrowserDock, m_consoleDock);
    m_assetBrowserDock->raise(); // Asset browser stays visible by default

    m_codeEditorDock = new QDockWidget(tr("Code"), this);
    m_codeEditorDock->setObjectName("CodeEditorDock");
    m_codeEditor = new CodeEditorPanel(m_codeEditorDock);
    m_codeEditorDock->setWidget(m_codeEditor);
    InstallDockTitleBar(m_codeEditorDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_codeEditorDock);
    m_codeEditorDock->hide();
    m_searchDock = new QDockWidget(tr("Search"), this);
    m_searchDock->setObjectName("SearchDock");
    m_searchPanel = new ProjectSearchPanel(m_searchDock);
    m_searchDock->setWidget(m_searchPanel);
    InstallDockTitleBar(m_searchDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_searchDock);
    m_searchDock->hide();
    m_searchPanel->onOpen = [this](const QString& path, int line, int) {
        m_codeEditorDock->show();
        m_codeEditor->OpenFileAt(path, line);
    };
    auto* searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        showProgrammerPanel(m_searchDock);
        m_searchPanel->FocusSearch();
    });

    m_debuggerDock = new QDockWidget(tr("Debugger"), this);
    m_debuggerDock->setObjectName("DebuggerDock");
    m_debuggerPanel = new DebuggerPanel(m_debuggerDock);
    m_debuggerDock->setWidget(m_debuggerPanel);
    InstallDockTitleBar(m_debuggerDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_debuggerDock);
    m_debuggerDock->hide();
    setupLuaDebugger();

    consolePanel->onSubmit = [this](const QString& code) { runConsoleLua(code); };
    consolePanel->onErrorLocation = [this](const QString& location, int line, const QString& message) {
        const QString path = CodeEditorPanel::ResolveLocation(location);
        if (!path.isEmpty()) m_codeEditor->MarkError(path, line, message);
    };
    consolePanel->onOpenLocation = [this](const QString& location, int line) {
        const QString path = CodeEditorPanel::ResolveLocation(location);
        if (path.isEmpty()) {
            statusBar()->showMessage(tr("Could not find %1 in the project.").arg(location), 4000);
            return;
        }
        showProgrammerPanel(m_codeEditorDock);
        m_codeEditor->OpenFileAt(path, line);
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Add Actors Dock (Left, tabbed with Outliner)
    // ─────────────────────────────────────────────────────────────────────────
    m_addActorsDock = new QDockWidget(tr("Add Actors"), this);
    m_addActorsDock->setObjectName("AddActorsDock");
    m_addActorsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);

    m_addActorsPanel = new AddActorsPanel(this, m_addActorsDock);
    m_addActorsDock->setWidget(m_addActorsPanel);

    InstallDockTitleBar(m_addActorsDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_addActorsDock);
    m_addActorsDock->hide();

    // ─────────────────────────────────────────────────────────────────────────
    // Add dock visibility toggles to View menu
    // ─────────────────────────────────────────────────────────────────────────
    QMenu* viewMenu = m_menuBar->findChild<QMenu*>();
    for (QMenu* menu : m_menuBar->findChildren<QMenu*>()) {
        if (menu->title() == tr("&View")) {
            viewMenu = menu;
            break;
        }
    }
    
    if (viewMenu) {
        viewMenu->addAction(m_outlinerDock->toggleViewAction());
        viewMenu->addAction(m_addActorsDock->toggleViewAction());
        viewMenu->addAction(m_detailsDock->toggleViewAction());
        viewMenu->addAction(m_assetBrowserDock->toggleViewAction());
        viewMenu->addAction(m_consoleDock->toggleViewAction());
        viewMenu->addAction(m_buildOutputDock->toggleViewAction());
        viewMenu->addAction(m_codeEditorDock->toggleViewAction());
        viewMenu->addAction(m_searchDock->toggleViewAction());
        viewMenu->addAction(m_debuggerDock->toggleViewAction());
        
        // Viewport rendering toggles are added after engine initialization
        // (see setupViewportRenderingToggles called from initializeEngine)
    }
}

void EditorMainWindow::setupCentralWidget()
{
    m_viewportWindow = new QtViewportWindow();

    m_viewportHost = new QWidget(this);
    m_viewportHost->setObjectName("ViewportHost");
    m_viewportContainer = QWidget::createWindowContainer(m_viewportWindow, m_viewportHost);
    m_viewportContainer->setObjectName("ViewportContainer");
    m_viewportContainer->setMinimumSize(200, 200);
    m_viewportContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* hostVLayout = new QVBoxLayout(m_viewportHost);
    hostVLayout->setContentsMargins(0, 0, 0, 0);
    hostVLayout->setSpacing(0);

    // ── Unified viewport toolbar ────────────────────────────────────────────
    m_viewportToolbar = new QWidget(m_viewportHost);
    m_viewportToolbar->setObjectName("ViewportToolbar");
    m_viewportToolbar->setAttribute(Qt::WA_StyledBackground, true);
    m_viewportToolbar->setFixedHeight(44);

    auto* tbLayout = new QHBoxLayout(m_viewportToolbar);
    tbLayout->setContentsMargins(8, 0, 8, 0);
    tbLayout->setSpacing(8);
    m_topToolbar->setParent(m_viewportToolbar);
    m_topToolbar->setContentsMargins(0, 0, 0, 0);
    m_topToolbar->layout()->setContentsMargins(0, 0, 0, 0);
    tbLayout->addWidget(m_topToolbar, 0, Qt::AlignVCenter);
    tbLayout->addStretch(1);

    const QSize tbIconSize(14, 14);

    // Left: Perspective / orthographic standard views
    m_viewPerspectiveCombo = new QComboBox(m_viewportToolbar);
    m_viewPerspectiveCombo->addItems({
        tr("Perspective"),
        tr("Front"),
        tr("Back"),
        tr("Left"),
        tr("Right"),
        tr("Top"),
        tr("Bottom")
    });
    m_viewPerspectiveCombo->setMinimumContentsLength(10);
    m_viewPerspectiveCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_viewPerspectiveCombo->setToolTip(tr("Camera projection and standard orthographic views"));
    connect(m_viewPerspectiveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!m_engine) return;
        auto* controller = m_engine->GetCameraController();
        if (!controller) return;

        auto savePerspectiveCamera = [&]() {
            m_savedPerspPosX = controller->positionX;
            m_savedPerspPosY = controller->positionY;
            m_savedPerspPosZ = controller->positionZ;
            m_savedPerspYaw = controller->yawDeg;
            m_savedPerspPitch = controller->pitchDeg;
            m_savedPerspOrthoHalfExtent = controller->orthoHalfExtent;
            m_hasSavedPerspectiveCamera = true;
        };

        auto applyOrientation = [&](float yaw, float pitch) {
            controller->yawDeg = yaw;
            controller->pitchDeg = pitch;
            if (auto* ctx = m_engine->GetEditorContext()) {
                auto& cam = ctx->GetCamera();
                cam.yawDeg = yaw;
                cam.pitchDeg = pitch;
            }
        };

        if (index == 0) {
            // Restore last free perspective camera
            controller->orthographic = false;
            if (m_hasSavedPerspectiveCamera) {
                controller->positionX = m_savedPerspPosX;
                controller->positionY = m_savedPerspPosY;
                controller->positionZ = m_savedPerspPosZ;
                controller->yawDeg = m_savedPerspYaw;
                controller->pitchDeg = m_savedPerspPitch;
                controller->orthoHalfExtent = m_savedPerspOrthoHalfExtent;
                if (auto* ctx = m_engine->GetEditorContext()) {
                    auto& cam = ctx->GetCamera();
                    cam.positionX = m_savedPerspPosX;
                    cam.positionY = m_savedPerspPosY;
                    cam.positionZ = m_savedPerspPosZ;
                    cam.yawDeg = m_savedPerspYaw;
                    cam.pitchDeg = m_savedPerspPitch;
                }
            }
        } else {
            if (!controller->orthographic) {
                savePerspectiveCamera();
            }
            controller->orthographic = true;
            float yaw = controller->yawDeg;
            float pitch = controller->pitchDeg;
            switch (index) {
                case 1: // Front (-Z)
                    yaw = -90.0f;
                    pitch = 0.0f;
                    break;
                case 2: // Back (+Z)
                    yaw = 90.0f;
                    pitch = 0.0f;
                    break;
                case 3: // Left (-X)
                    yaw = 180.0f;
                    pitch = 0.0f;
                    break;
                case 4: // Right (+X)
                    yaw = 0.0f;
                    pitch = 0.0f;
                    break;
                case 5: // Top
                    yaw = -90.0f;
                    pitch = -89.0f;
                    break;
                case 6: // Bottom
                    yaw = -90.0f;
                    pitch = 89.0f;
                    break;
                default:
                    break;
            }
            applyOrientation(yaw, pitch);
        }
        statusBar()->showMessage(tr("Camera view: %1").arg(m_viewPerspectiveCombo->currentText()), 2000);
    });

    // Left: Lit / Unlit / Wireframe shading modes
    m_viewModeBtn = new QToolButton(m_viewportToolbar);
    m_viewModeBtn->setObjectName(QStringLiteral("StripDropdown"));
    m_viewModeBtn->setIconSize(tbIconSize);
    m_viewModeBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_viewModeBtn->setText(tr("Lit"));
    m_viewModeBtn->setToolTip(tr("Viewport shading mode"));
    m_viewModeBtn->setPopupMode(QToolButton::InstantPopup);
    m_viewModeBtn->setEnabled(true);
    {
        auto* shadingMenu = new QMenu(m_viewModeBtn);
        m_viewModeLitAction = shadingMenu->addAction(tr("Lit"));
        m_viewModeUnlitAction = shadingMenu->addAction(tr("Unlit"));
        m_viewModeWireframeAction = shadingMenu->addAction(tr("Wireframe"));
        m_viewModeLitAction->setCheckable(true);
        m_viewModeUnlitAction->setCheckable(true);
        m_viewModeWireframeAction->setCheckable(true);
        m_viewModeLitAction->setChecked(true);
        auto* shadingGroup = new QActionGroup(shadingMenu);
        shadingGroup->setExclusive(true);
        shadingGroup->addAction(m_viewModeLitAction);
        shadingGroup->addAction(m_viewModeUnlitAction);
        shadingGroup->addAction(m_viewModeWireframeAction);
        m_viewModeBtn->setMenu(shadingMenu);

        auto applyShadingMode = [this](int mode, const QString& label) {
            m_viewModeBtn->setText(label);
            if (!m_engine) return;
            if (auto* ctx = m_engine->GetEditorContext()) {
                ctx->GetViewportSettings().shadingMode = mode;
            }
            statusBar()->showMessage(tr("Shading: %1").arg(label), 2000);
        };
        connect(m_viewModeLitAction, &QAction::triggered, this, [applyShadingMode]() {
            applyShadingMode(0, QObject::tr("Lit"));
        });
        connect(m_viewModeUnlitAction, &QAction::triggered, this, [applyShadingMode]() {
            applyShadingMode(1, QObject::tr("Unlit"));
        });
        connect(m_viewModeWireframeAction, &QAction::triggered, this, [applyShadingMode]() {
            applyShadingMode(2, QObject::tr("Wireframe"));
        });
    }

    auto makeSep = [this]() {
        auto* sep = new QFrame(m_viewportToolbar);
        sep->setObjectName("VPToolbarSep");
        sep->setFrameShape(QFrame::VLine);
        sep->setFixedSize(1, 16);
        return sep;
    };

    // Grid toggle
    m_viewGridBtn = new QToolButton(m_viewportToolbar);
    m_viewGridBtn->setAutoRaise(true);
    m_viewGridBtn->setIcon(Theme::Icon(QStringLiteral("grid")));
    m_viewGridBtn->setIconSize(tbIconSize);
    m_viewGridBtn->setToolTip(tr("Toggle Grid"));
    m_viewGridBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_viewGridBtn->setCheckable(true);
    m_viewGridBtn->setChecked(true);
    m_viewGridBtn->setCursor(Qt::PointingHandCursor);
    m_viewGridBtn->hide();

    // Left: Stats toggle
    m_viewStatsBtn = new QToolButton(m_viewportToolbar);
    m_viewStatsBtn->setAutoRaise(true);
    m_viewStatsBtn->setIcon(Theme::Icon(QStringLiteral("profiler")));
    m_viewStatsBtn->setIconSize(tbIconSize);
    m_viewStatsBtn->setToolTip(tr("Toggle Stats"));
    m_viewStatsBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_viewStatsBtn->setCheckable(true);
    m_viewStatsBtn->setChecked(false);
    m_viewStatsBtn->setCursor(Qt::PointingHandCursor);
    m_viewStatsBtn->hide();


    tbLayout->addWidget(m_viewPerspectiveCombo);
    tbLayout->addWidget(m_viewModeBtn);

    auto* showBtn = new QToolButton(m_viewportToolbar);
    showBtn->setObjectName(QStringLiteral("StripDropdown"));
    showBtn->setText(tr("Show"));
    showBtn->setToolTip(tr("Viewport overlays"));
    showBtn->setPopupMode(QToolButton::InstantPopup);
    auto* showMenu = new QMenu(showBtn);
    for (QToolButton* overlay : { m_viewGridBtn, m_viewStatsBtn }) {
        QAction* action = showMenu->addAction(overlay->toolTip().replace(tr("Toggle "), QString()));
        action->setCheckable(true);
        action->setChecked(overlay->isChecked());
        connect(action, &QAction::toggled, overlay, &QToolButton::setChecked);
        connect(overlay, &QToolButton::toggled, action, &QAction::setChecked);
    }
    showBtn->setMenu(showMenu);
    tbLayout->addWidget(showBtn);

    auto* cameraIcon = new QLabel(m_viewportToolbar);
    cameraIcon->setPixmap(Theme::Icon(QStringLiteral("camera"), Theme::IconTone::Muted).pixmap(QSize(16, 16), devicePixelRatioF()));
    cameraIcon->setToolTip(tr("Camera speed"));
    m_cameraSpeedCombo->setParent(m_viewportToolbar);
    auto* cameraSpeed = new QHBoxLayout();
    cameraSpeed->setSpacing(6);
    cameraSpeed->addWidget(cameraIcon);
    cameraSpeed->addWidget(m_cameraSpeedCombo);
    tbLayout->addLayout(cameraSpeed);

    for (QWidget* dropdown : std::initializer_list<QWidget*>{ m_viewPerspectiveCombo, m_viewModeBtn, showBtn,
                                                             m_cameraSpeedCombo, m_snapValueCombo }) {
        dropdown->setFixedHeight(28);
    }

    auto* maximizeBtn = new QToolButton(m_viewportToolbar);
    maximizeBtn->setIcon(Theme::Icon(QStringLiteral("maximize")));
    maximizeBtn->setToolTip(tr("Maximize viewport (hide panels)"));
    maximizeBtn->setCheckable(true);
    connect(maximizeBtn, &QToolButton::toggled, this, [this](bool maximize) {
        if (maximize) {
            m_stateBeforeMaximize = saveState(kDockLayoutVersion);
            for (QDockWidget* dock : findChildren<QDockWidget*>()) {
                if (!dock->isFloating()) dock->hide();
            }
        } else if (!m_stateBeforeMaximize.isEmpty()) {
            restoreState(m_stateBeforeMaximize, kDockLayoutVersion);
            m_stateBeforeMaximize.clear();
        }
    });
    tbLayout->addWidget(maximizeBtn);

    auto* sceneRow = new QWidget(m_viewportHost);
    sceneRow->setObjectName(QStringLiteral("SceneTabRow"));
    sceneRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* sceneLayout = new QHBoxLayout(sceneRow);
    sceneLayout->setContentsMargins(0, 0, 6, 0);
    sceneLayout->setSpacing(0);
    m_sceneTabs = new QTabBar(sceneRow);
    m_sceneTabs->setObjectName(QStringLiteral("SceneTabs"));
    m_sceneTabs->setDrawBase(false);
    m_sceneTabs->setExpanding(false);
    m_sceneTabs->addTab(Theme::Icon(QStringLiteral("globe")), tr("Untitled"));
    sceneLayout->addWidget(m_sceneTabs);
    sceneLayout->addStretch(1);
    auto* newSceneBtn = new QToolButton(sceneRow);
    newSceneBtn->setIcon(Theme::Icon(QStringLiteral("plus")));
    newSceneBtn->setToolTip(tr("New level"));
    newSceneBtn->setFocusPolicy(Qt::NoFocus);
    connect(newSceneBtn, &QToolButton::clicked, this, &EditorMainWindow::onNewLevel);
    sceneLayout->addWidget(newSceneBtn);
    hostVLayout->addWidget(sceneRow);
    hostVLayout->addWidget(m_viewportToolbar);

    // ── Viewport area (stacked: 3D view + runtime overlay + stats) ──────────
    auto* viewportArea = new QWidget(m_viewportHost);
    viewportArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* areaLayout = new QGridLayout(viewportArea);
    areaLayout->setContentsMargins(0, 0, 0, 0);
    areaLayout->setSpacing(0);
    areaLayout->addWidget(m_viewportContainer, 0, 0);

    // A QWidget sibling cannot reliably stack above createWindowContainer's
    // native Vulkan child on Windows. Use an owned translucent tool window and
    // keep it aligned to the viewport in onFrameRendered().
    m_runtimeUIOverlay = new RuntimeUIOverlayWidget(this);
    m_runtimeUIOverlay->setObjectName("RuntimeUIOverlay");
    m_runtimeUIOverlay->setVisible(false);

    // Bottom-left stats overlay (shown when Stats is toggled on)
    {
        auto* overlay = new QFrame(viewportArea);
        overlay->setObjectName("ViewportOverlayBottomLeft");
        overlay->setFrameShape(QFrame::NoFrame);
        overlay->setAttribute(Qt::WA_TranslucentBackground, true);
        overlay->setStyleSheet("background: transparent; border: none;");
        auto* oLayout = new QHBoxLayout(overlay);
        oLayout->setContentsMargins(6, 6, 6, 6);
        oLayout->setSpacing(6);

        auto* statsFrame = new QFrame(overlay);
        statsFrame->setProperty("overlayGroup", true);
        statsFrame->setAttribute(Qt::WA_StyledBackground, true);
        auto* statsLayout = new QHBoxLayout(statsFrame);
        statsLayout->setContentsMargins(6, 4, 6, 4);
        statsLayout->setSpacing(3);

        m_overlayFpsLabel       = new QLabel(tr("FPS 0.0"), statsFrame);
        m_overlayDrawCallsLabel = new QLabel(tr("Draw 0"),  statsFrame);
        m_overlayRamLabel       = new QLabel(tr("RAM --"),  statsFrame);
        m_overlaySceneLabel     = new QLabel(tr("Entities 0 | Meshes 0 | Lights 0 | Tris 0"), statsFrame);
        statsLayout->addWidget(m_overlayFpsLabel);
        statsLayout->addWidget(m_overlayDrawCallsLabel);
        statsLayout->addWidget(m_overlayRamLabel);
        statsLayout->addWidget(m_overlaySceneLabel);
        oLayout->addWidget(statsFrame);

        m_overlayBottomLeft = overlay;
        areaLayout->addWidget(m_overlayBottomLeft, 0, 0, Qt::AlignBottom | Qt::AlignLeft);
        m_overlayBottomLeft->setVisible(false);
    }

    hostVLayout->addWidget(viewportArea, 1);

    // ── Connections ─────────────────────────────────────────────────────────
    connect(m_viewGridBtn, &QToolButton::toggled, this, [this](bool checked) {
        if (!m_engine) return;
        if (auto* ctx = m_engine->GetEditorContext()) {
            ctx->GetViewportSettings().showGrid = checked;
        }
    });
    connect(m_viewStatsBtn, &QToolButton::toggled, this, [this](bool checked) {
        if (m_overlayBottomLeft) m_overlayBottomLeft->setVisible(checked);
    });

    connect(m_viewportWindow, &QtViewportWindow::viewportReady,
            this, &EditorMainWindow::onViewportReady);

    setCentralWidget(m_viewportHost);

    m_viewportContainer->setAcceptDrops(true);
    m_viewportContainer->installEventFilter(this);

    std::cout << "[EditorMainWindow] Viewport widget created\n";
}

void EditorMainWindow::setupStatusBar()
{
    QStatusBar* status = statusBar();
    status->setObjectName("EditorStatusBar");
    status->setSizeGripEnabled(false);

    m_statusSaved = new QToolButton(status);
    m_statusScene = new QToolButton(status);
    m_statusScene->setIcon(Theme::Icon(QStringLiteral("file"), Theme::IconTone::Muted));
    for (QToolButton* item : { m_statusSaved, m_statusScene }) {
        item->setObjectName(QStringLiteral("StatusItem"));
        item->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        item->setFocusPolicy(Qt::NoFocus);
        item->setAutoRaise(true);
    }
    m_statusSelection = new QLabel(status);
    m_statusFps = new QLabel(status);

    status->addWidget(m_statusSaved);
    status->addWidget(m_statusScene);
    auto* codeStatus = new QLabel(status);
    codeStatus->setProperty("role", "muted");
    codeStatus->hide();
    m_codeEditor->onStatusChanged = [this, codeStatus](const QString& text) {
        codeStatus->setText(text);
        codeStatus->setVisible(!text.isEmpty() && m_codeEditorDock->isVisible());
    };
    connect(m_codeEditorDock, &QDockWidget::visibilityChanged, codeStatus, [codeStatus](bool visible) {
        codeStatus->setVisible(visible && !codeStatus->text().isEmpty());
    });
    status->addPermanentWidget(codeStatus);
    status->addPermanentWidget(m_statusSelection);
    status->addPermanentWidget(m_statusFps);

    m_systemUsage = std::make_unique<EditorSystemUsage>();
    QMenu* statusMenu = nullptr;
    for (QMenu* menu : m_menuBar->findChildren<QMenu*>()) {
        if (menu->title() == tr("&View")) {
            statusMenu = menu->addMenu(tr("Status Bar"));
            break;
        }
    }
    status->setContextMenuPolicy(Qt::ActionsContextMenu);
    QSettings settings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"));
    const struct { const char* key; const char* label; QLabel** target; bool available; } readouts[] = {
        { "ram", QT_TR_NOOP("RAM usage"), &m_statusRam, true },
        { "cpu", QT_TR_NOOP("CPU usage"), &m_statusCpu, true },
        { "gpu", QT_TR_NOOP("GPU usage"), &m_statusGpu, m_systemUsage->GpuSupported() },
    };
    for (const auto& readout : readouts) {
        auto* label = new QLabel(status);
        label->setProperty("role", "muted");
        *readout.target = label;
        status->addPermanentWidget(label);
        auto* toggle = new QAction(tr(readout.label), status);
        toggle->setCheckable(true);
        toggle->setEnabled(readout.available);
        const QString key = QStringLiteral("statusBar/%1").arg(QLatin1String(readout.key));
        toggle->setChecked(readout.available && settings.value(key, true).toBool());
        if (!readout.available) toggle->setToolTip(tr("Not available on this platform"));
        label->setVisible(toggle->isChecked());
        connect(toggle, &QAction::toggled, label, [label, key](bool shown) {
            label->setVisible(shown);
            QSettings(QStringLiteral("Hephaestus"), QStringLiteral("Editor")).setValue(key, shown);
        });
        status->addAction(toggle);
        if (statusMenu) statusMenu->addAction(toggle);
    }
    auto* usageTimer = new QTimer(this);
    connect(usageTimer, &QTimer::timeout, this, &EditorMainWindow::updateSystemUsage);
    usageTimer->start(1000);
    updateSystemUsage();

    status->showMessage(tr("Initializing engine..."));
}

void EditorMainWindow::updateSystemUsage()
{
    m_systemUsage->Sample();
    const double ramMB = getProcessMemoryMB();
    m_statusRam->setText(ramMB >= 0.0 ? tr("RAM %1 MB").arg(ramMB, 0, 'f', 0) : tr("RAM --"));
    const double cpu = m_systemUsage->CpuPercent();
    m_statusCpu->setText(cpu >= 0.0 ? tr("CPU %1%").arg(cpu, 0, 'f', 1) : tr("CPU --"));
    const double gpu = m_systemUsage->GpuPercent();
    m_statusGpu->setText(gpu >= 0.0 ? tr("GPU %1%").arg(gpu, 0, 'f', 0) : tr("GPU --"));
}

bool EditorMainWindow::initializeEngine()
{
    EngineLog::Info("initializeEngine: creating QtEditorEngine");
    m_engine = std::make_unique<QtEditorEngine>(this);
    if (auto* context = m_engine->GetEditorContext()) {
        const bool snapDefault = m_engineSettingsManager.Value(
            QStringLiteral("engine.editor.gridSnappingDefault")).toBool();
        context->GetSnapSettings().enableSnap = snapDefault;
        if (m_snapToggleBtn) m_snapToggleBtn->setChecked(snapDefault);
    }
    if (auto* camera = m_engine->GetCameraController()) {
        const float speed = m_engineSettingsManager.Value(
            QStringLiteral("engine.editor.cameraSpeedDefault")).toFloat();
        if (speed > 0.0f) camera->baseMoveSpeed = speed;
    }
    m_engine->SetGizmoSize(m_engineSettingsManager.Value(QStringLiteral("engine.editor.gizmoSize")).toFloat());
    applyPlayPhysicsSetting();
    
    // Set up viewport rendering toggles now that engine context is available
    setupViewportRenderingToggles();

    // Register all component inspector factories
    registerComponentInspectors();
    
    // Set up mouse event callback for the runtime UI overlay
    if (m_runtimeUIOverlay) {
        m_runtimeUIOverlay->SetMouseEventCallback([this](const UIMouseEventData& event) {
            if (m_engine) {
                int type = 0;
                switch (event.type) {
                case UIMouseEventData::Type::Press:   type = 0; break;
                case UIMouseEventData::Type::Release: type = 1; break;
                case UIMouseEventData::Type::Move:    type = 2; break;
                }
                m_engine->ForwardOverlayMouseEvent(type, event.x, event.y, event.button);
            }
        });
    }
    
    // Connect engine signals
    connect(m_engine.get(), &QtEditorEngine::engineInitialized,
            this, &EditorMainWindow::onEngineInitialized);
    connect(m_engine.get(), &QtEditorEngine::frameRendered,
            this, &EditorMainWindow::onFrameRendered);
    connect(m_engine.get(), &QtEditorEngine::engineError,
            this, &EditorMainWindow::onEngineError);
    connect(m_engine.get(), &QtEditorEngine::presentationTargetChanged,
            this, &EditorMainWindow::onPresentationTargetChanged);
    connect(m_engine.get(), &QtEditorEngine::playStateChanged,
            this, [this](bool playing, bool paused) {
        if (m_debuggerPanel && !LuaDebugger::Instance().IsPaused()) m_debuggerPanel->ShowRunning(playing);
        if (m_playAction)  m_playAction->setChecked(playing);
        if (m_pauseAction) {
            m_pauseAction->setChecked(paused);
            m_pauseAction->setEnabled(playing);
        }
        if (m_stopAction)  m_stopAction->setEnabled(playing);
        if (m_viewportHost) {
            m_viewportHost->setProperty("playMode", playing);
            m_viewportHost->style()->unpolish(m_viewportHost);
            m_viewportHost->style()->polish(m_viewportHost);
        }
        if (!playing && m_playWindow) {
            m_engine->PresentTo(nullptr);
        }
        if (m_runtimeUIOverlay) {
            const auto& runtimeUICommands = m_engine->GetRuntimeUIDrawCommands();
            const bool showUI = (playing || paused) && !runtimeUICommands.empty() &&
                                !m_engine->IsPresentingToPlayWindow();
            m_runtimeUIOverlay->setVisible(showUI);
            m_runtimeUIOverlay->SetMouseEventsEnabled(
                showUI && m_engine->IsRuntimeUICursorMode());
            if (showUI) {
                m_runtimeUIOverlay->SetDrawCommands(runtimeUICommands);
                m_runtimeUIOverlay->raise();
            } else {
                m_runtimeUIOverlay->SetDrawCommands({});
            }
        }
        if (!playing) {
            rebuildOutliner();
            refreshDetailsPanel();
        }
    });
    
    // Start Vulkan initialization
    EngineLog::Info("initializeEngine: InitializeVulkan (this can take a while on some GPUs)");
    if (!m_engine->InitializeVulkan()) {
        EngineLog::Error("initializeEngine: Vulkan initialization failed");
        statusBar()->showMessage(tr("Failed to initialize Vulkan"));
        return false;
    }

    EngineLog::Info("initializeEngine: Vulkan ready, waiting for viewport expose");
    statusBar()->showMessage(tr("Vulkan initialized, waiting for viewport..."));
    return true;
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
        loadEditorViewState();
    });

    // When selection changes, refresh details
    ctx->SetSelectionChangedCallback([this](entt::entity) {
        refreshDetailsPanel();
    });

    // When dirty flag changes, update title
    ctx->SetDirtyChangedCallback([this](bool) {
        updateWindowTitle();
    });

    ctx->SetSceneEditedCallback([this]() {
        if (!m_engine || m_engine->IsPlaying()) {
            return;
        }
        syncPrefabOverridesForSelection();
    });

    connect(m_engine.get(), &QtEditorEngine::sceneHierarchyChanged,
            this, [this]() {
                rebuildOutliner();
                refreshDetailsPanel();
            },
            Qt::UniqueConnection);
    connect(m_engine.get(), &QtEditorEngine::deleteSelectionRequested,
            this, &EditorMainWindow::deleteSelectedEntities,
            Qt::UniqueConnection);
    connect(m_engine.get(), &QtEditorEngine::viewportContextMenuRequested,
            this, &EditorMainWindow::showViewportContextMenu,
            Qt::UniqueConnection);
    connect(m_engine.get(), &QtEditorEngine::cameraSpeedChanged,
            this, [this](float speed) {
                statusBar()->showMessage(
                    tr("Viewport camera speed: %1").arg(speed, 0, 'f', 2),
                    1500);
            },
            Qt::UniqueConnection);
}

void EditorMainWindow::setupViewportRenderingToggles()
{
    if (!m_engine) return;
    
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;
    
    // Find the View menu
    QMenu* viewMenu = nullptr;
    for (QMenu* menu : m_menuBar->findChildren<QMenu*>()) {
        if (menu->title() == tr("&View")) {
            viewMenu = menu;
            break;
        }
    }
    if (!viewMenu) return;
    
    viewMenu->addSeparator();
    QMenu* viewportMenu = viewMenu->addMenu(tr("Overlays"));
    
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
    QAction* statsAction = viewportMenu->addAction(tr("Statistics"));
    statsAction->setCheckable(true);
    statsAction->setChecked(m_viewStatsBtn && m_viewStatsBtn->isChecked());
    connect(statsAction, &QAction::toggled, this, [this](bool checked) {
        if (m_viewStatsBtn) m_viewStatsBtn->setChecked(checked);
        if (m_overlayBottomLeft) m_overlayBottomLeft->setVisible(checked);
    });
}

void EditorMainWindow::onViewportReady()
{
    EngineLog::Info("Viewport ready, attaching engine");
    std::cout << "[EditorMainWindow] Viewport ready, attaching engine\n";
    
    if (!m_engine) {
        EngineLog::Info("Viewport ready before engine init; attach deferred");
        statusBar()->showMessage(tr("Engine not initialized"));
        return;
    }

    if (m_engine->GetState() == EngineState::Running) {
        return;
    }
    
    if (m_engine->AttachToViewport(m_viewportWindow)) {
        m_engine->StartRenderLoop();

        // Gate Wireframe shading on device/pipeline capability
        if (m_viewModeWireframeAction) {
            const bool wireOk = m_engine->GetFrameRenderer()
                && m_engine->GetFrameRenderer()->SupportsWireframe();
            m_viewModeWireframeAction->setEnabled(wireOk);
            if (!wireOk) {
                m_viewModeWireframeAction->setToolTip(tr("Wireframe requires GPU fillModeNonSolid support"));
            }
        }
        
        // Setup callbacks after engine is ready
        setupEditorCallbacks();
        setupProjectCallbacks();
        
        // If a project was already loaded before the viewport was ready
        // (e.g. startup project from the launcher), push the project root
        // to the frame renderer and rebuild scene meshes now that the GPU
        // resources are available.
        if (m_projectContext && m_projectContext->HasProject()) {
            if (auto* fr = m_engine->GetFrameRenderer()) {
                fr->SetProjectRoot(m_projectContext->GetProjectRootDir());
            }
            if (m_engine->GetEditorContext() &&
                m_engine->GetEditorContext()->HasActiveScene()) {
                m_engine->RebuildSceneMeshes();
            }
        }
        
        // Initial UI state
        rebuildOutliner();
        updateWindowTitle();
        
        EngineLog::Info("Render loop started");
        statusBar()->showMessage(tr("Rendering started"));
    } else {
        EngineLog::Error("Failed to attach engine to viewport");
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

    if (m_runtimeUIOverlay && m_engine) {
        if (m_viewportContainer) {
            const QPoint globalTopLeft = m_viewportContainer->mapToGlobal(QPoint(0, 0));
            m_runtimeUIOverlay->setGeometry(
                QRect(globalTopLeft, m_viewportContainer->size()));
        }
        const auto& runtimeUICommands = m_engine->GetRuntimeUIDrawCommands();
        const bool showRuntimeUI = (m_engine->IsPlaying() || m_engine->IsPaused()) &&
                                   !runtimeUICommands.empty() && !m_engine->IsPresentingToPlayWindow();
        m_runtimeUIOverlay->setVisible(showRuntimeUI);
        // Only route mouse to the overlay in UI cursor mode (menus). Game/FPS
        // cursor capture must keep events on the viewport.
        m_runtimeUIOverlay->SetMouseEventsEnabled(
            showRuntimeUI && m_engine->IsRuntimeUICursorMode());
        m_runtimeUIOverlay->SetDrawCommands(runtimeUICommands);
        if (showRuntimeUI) {
            m_runtimeUIOverlay->raise();
        }
    }

    // Keep transform fields synchronized with viewport gizmo edits and other runtime transform updates.
    syncTransformFieldsFromScene();

    // Keep toolbar gizmo mode/space in sync with viewport keyboard shortcuts (W/E/R/Q/X).
    if (m_engine) {
        if (auto* ctx = m_engine->GetEditorContext()) {
            const GizmoMode mode = ctx->GetGizmoMode();
            if (m_selectAction) m_selectAction->setChecked(mode == GizmoMode::None);
            if (m_translateAction) m_translateAction->setChecked(mode == GizmoMode::Translate);
            if (m_rotateAction) m_rotateAction->setChecked(mode == GizmoMode::Rotate);
            if (m_scaleAction) m_scaleAction->setChecked(mode == GizmoMode::Scale);
            if (m_worldLocalBtn) {
                const bool local = ctx->GetGizmoSpace() == GizmoSpace::Local;
                if (m_worldLocalBtn->isChecked() != local) {
                    m_worldLocalBtn->blockSignals(true);
                    m_worldLocalBtn->setChecked(local);
                    syncGizmoSpaceButtons();
                    m_worldLocalBtn->blockSignals(false);
                }
            }
        }
    }
    
    // Update FPS display every 30 frames
    if (m_frameCount % 30 == 0) {
        float fps = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
        if (m_statusFps) {
            m_statusFps->setText(QStringLiteral("%1 fps \u00B7 %2 ms").arg(fps, 0, 'f', 0).arg(dt * 1000.0f, 0, 'f', 1));
        }
        if (m_overlayBottomLeft && m_overlayBottomLeft->isVisible()) {
            if (m_overlayFpsLabel) {
                m_overlayFpsLabel->setText(QString("FPS %1").arg(fps, 0, 'f', 1));
            }
        }

        int drawCalls = 0;
        if (m_engine) {
            if (auto* renderer = m_engine->GetFrameRenderer())
                drawCalls = renderer->GetLastFrameStats().drawCalls;
        }

        const double ramMB = getProcessMemoryMB();
        const QString ramUsage = (ramMB >= 0.0)
            ? QString("RAM %1 MB").arg(ramMB, 0, 'f', 0)
            : QStringLiteral("RAM --");

        if (m_statusSelection && m_engine) {
            const auto* ctx = m_engine->GetEditorContext();
            const size_t selected = ctx ? ctx->GetSelectedEntities().size() : 0;
            QString selection;
            if (selected && ctx->HasActiveScene()) {
                auto& registry = ctx->GetActiveScene()->GetRegistry();
                auto* renderer = m_engine->GetFrameRenderer();
                uint64_t triangles = 0;
                for (entt::entity handle : ctx->GetSelectedEntities()) {
                    if (!renderer || !registry.valid(handle) || !registry.all_of<MeshRendererComponent>(handle)) continue;
                    if (const MeshGPU* mesh = renderer->GetMesh(registry.get<MeshRendererComponent>(handle).meshHandle)) {
                        triangles += mesh->indexCount / 3;
                    }
                }
                selection = tr("%n selected", nullptr, static_cast<int>(selected));
                if (triangles) {
                    selection += QStringLiteral(" \u00B7 ") + tr("%1 tris").arg(QLocale().toString(static_cast<qulonglong>(triangles)));
                }
            }
            m_statusSelection->setText(selection);
            if (m_outlinerFooter && ctx && ctx->HasActiveScene()) {
                auto tags = ctx->GetActiveScene()->GetRegistry().view<TagComponent>();
                const int objects = static_cast<int>(std::distance(tags.begin(), tags.end()));
                QString footer = tr("%n objects", nullptr, objects);
                if (selected) footer += QStringLiteral(" \u00B7 ") + tr("%n selected", nullptr, static_cast<int>(selected));
                m_outlinerFooter->setText(footer);
            }
        }
        if (m_overlayBottomLeft && m_overlayBottomLeft->isVisible()) {
            if (m_overlayDrawCallsLabel) m_overlayDrawCallsLabel->setText(QString("Draw %1").arg(drawCalls));
            if (m_overlayRamLabel) m_overlayRamLabel->setText(ramUsage);
            if (m_overlaySceneLabel && m_engine) {
                auto* ctx = m_engine->GetEditorContext();
                auto* renderer = m_engine->GetFrameRenderer();
                if (ctx && ctx->HasActiveScene() && renderer) {
                    auto& registry = ctx->GetActiveScene()->GetRegistry();
                    auto tags = registry.view<TagComponent>();
                    auto meshes = registry.view<MeshRendererComponent>();
                    auto lights = registry.view<LightComponent>();
                    uint64_t triangles = 0;
                    for (entt::entity entity : meshes) {
                        const auto& meshComponent =
                            registry.get<MeshRendererComponent>(entity);
                        if (const MeshGPU* mesh = renderer->GetMesh(
                                meshComponent.meshHandle)) {
                            triangles += mesh->indexCount / 3;
                        }
                    }
                    const auto entityCount =
                        static_cast<qulonglong>(std::distance(tags.begin(), tags.end()));
                    const auto meshCount =
                        static_cast<qulonglong>(std::distance(meshes.begin(), meshes.end()));
                    const auto lightCount =
                        static_cast<qulonglong>(std::distance(lights.begin(), lights.end()));
                    m_overlaySceneLabel->setText(
                        tr("Entities %1 | Meshes %2 | Lights %3 | Tris %4")
                            .arg(entityCount)
                            .arg(meshCount)
                            .arg(lightCount)
                            .arg(static_cast<qulonglong>(triangles)));
                }
            }
        }
    }
}

void EditorMainWindow::syncTransformFieldsFromScene()
{
    if (!m_engine || m_updatingDetails || !m_transformGroup || !m_transformGroup->isEnabled()) {
        return;
    }

    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) {
        return;
    }

    const InspectorSelectionModel model = resolveInspectorSelection();
    // Multi-selection fields deliberately retain their mixed-axis display.
    // Selection changes rebuild those values; this per-frame path is only
    // needed to follow gizmo edits on a single inspected entity.
    if (model.Count() != 1) {
        return;
    }

    const entt::entity entity = model.primary;
    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    if (!registry.valid(entity) || !registry.all_of<TransformComponent>(entity)) {
        return;
    }

    auto& transform = registry.get<TransformComponent>(entity);
    constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979f;

    const double nextPosX = static_cast<double>(transform.position.x);
    const double nextPosY = static_cast<double>(transform.position.y);
    const double nextPosZ = static_cast<double>(transform.position.z);
    const double nextRotX = static_cast<double>(transform.rotation.x * RAD_TO_DEG);
    const double nextRotY = static_cast<double>(transform.rotation.y * RAD_TO_DEG);
    const double nextRotZ = static_cast<double>(transform.rotation.z * RAD_TO_DEG);
    const double nextScaleX = static_cast<double>(transform.scale.x);
    const double nextScaleY = static_cast<double>(transform.scale.y);
    const double nextScaleZ = static_cast<double>(transform.scale.z);

    constexpr double kEpsilon = 0.0005;
    auto differs = [](double a, double b, double eps) {
        return std::abs(a - b) > eps;
    };

    const bool changed =
        differs(m_posX->value(), nextPosX, kEpsilon) ||
        differs(m_posY->value(), nextPosY, kEpsilon) ||
        differs(m_posZ->value(), nextPosZ, kEpsilon) ||
        differs(m_rotX->value(), nextRotX, kEpsilon) ||
        differs(m_rotY->value(), nextRotY, kEpsilon) ||
        differs(m_rotZ->value(), nextRotZ, kEpsilon) ||
        differs(m_scaleX->value(), nextScaleX, kEpsilon) ||
        differs(m_scaleY->value(), nextScaleY, kEpsilon) ||
        differs(m_scaleZ->value(), nextScaleZ, kEpsilon);

    if (!changed) {
        return;
    }

    m_updatingDetails = true;
    blockDetailSignals(true);

    m_posX->setValue(nextPosX);
    m_posY->setValue(nextPosY);
    m_posZ->setValue(nextPosZ);
    m_rotX->setValue(nextRotX);
    m_rotY->setValue(nextRotY);
    m_rotZ->setValue(nextRotZ);
    m_scaleX->setValue(nextScaleX);
    m_scaleY->setValue(nextScaleY);
    m_scaleZ->setValue(nextScaleZ);

    blockDetailSignals(false);
    m_updatingDetails = false;
}

void EditorMainWindow::onEngineError(const QString& message)
{
    EngineLog::Error(std::string("Engine error: ") + message.toStdString());
    statusBar()->showMessage(QString("Error: %1").arg(message));
    QMessageBox::warning(this, tr("Engine Error"), message);
}

// ─────────────────────────────────────────────────────────────────────────────
// Outliner
// ─────────────────────────────────────────────────────────────────────────────

void EditorMainWindow::rebuildOutliner()
{
    if (m_outlinerTree && m_outlinerTree->topLevelItemCount() > 0) {
        m_expandedOutlinerEntities.clear();
        QTreeWidgetItemIterator existing(m_outlinerTree);
        while (*existing) {
            QTreeWidgetItem* item = *existing;
            const QVariant data = item->data(0, Qt::UserRole);
            if (data.isValid() && item->isExpanded()) {
                m_expandedOutlinerEntities.insert(data.toUInt());
            }
            ++existing;
        }
        m_hasOutlinerExpansionState = true;
    }

    // Keep selection signals blocked for the entire rebuild. Unblocking after
    // clear() can dispatch an empty-selection notification before restoration.
    const QSignalBlocker outlinerSignals(m_outlinerTree);
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

    if (sceneRoot->childCount() == 0) {
        auto* hint = new QTreeWidgetItem(sceneRoot);
        hint->setText(0, tr("Empty scene. Use + or Create to add objects."));
        hint->setFlags(hint->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsDragEnabled);
        hint->setForeground(0, Theme::Color(Theme::Token::TextMuted));
    }

    restoreOutlinerTreeSelection();
    m_outlinerTree->setUpdatesEnabled(true);
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
    if (ctx->IsEditorHidden(entity)) {
        item->setData(0, HierarchyEye::kRole, static_cast<int>(HierarchyEye::State::Hidden));
    } else if (!ctx->IsEditorEntityShown(entity)) {
        item->setData(0, HierarchyEye::kRole, static_cast<int>(HierarchyEye::State::HiddenByParent));
    } else {
        item->setData(0, HierarchyEye::kRole, static_cast<int>(HierarchyEye::State::Shown));
    }
    if (!ctx->IsEditorEntityShown(entity)) {
        item->setForeground(0, Theme::Color(Theme::Token::TextDisabled));
        item->setToolTip(0, tr("Hidden in editor"));
    } else if (ctx->IsEditorLocked(entity)) {
        item->setForeground(0, Theme::Color(Theme::Token::TextMuted));
        item->setData(0, kOutlinerTrailingRole, QStringLiteral("locked"));
        item->setToolTip(0, tr("Locked in editor (selectable from the Hierarchy only)"));
    }

    item->setIcon(0, Theme::Icon(OutlinerIconName(registry, entity)));
    if (registry.all_of<PrefabInstanceComponent>(entity)) {
        const auto& inst = registry.get<PrefabInstanceComponent>(entity);
        if (!item->data(0, kOutlinerTrailingRole).isValid()) {
            item->setData(0, kOutlinerTrailingRole, QStringLiteral("prefab"));
        }
        if (inst.missingPrefab) {
            item->setForeground(0, Theme::Color(Theme::Token::StatusError));
            item->setToolTip(0, QString::fromStdString("Missing prefab: " + inst.prefabPath));
        } else {
            item->setToolTip(0, QString::fromStdString("Prefab: " + inst.prefabPath));
        }
    }
    if (registry.any_of<FolderComponent, EnvironmentComponent>(entity)) {
        item->setFlags(item->flags() | Qt::ItemIsDropEnabled);
    }

    // Add children if this entity has any
    if (registry.all_of<TransformComponent>(entity)) {
        auto& transform = registry.get<TransformComponent>(entity);
        for (auto child : transform.children) {
            populateOutlinerItem(item, child);
        }
    }
    
    item->setExpanded(!m_hasOutlinerExpansionState ||
                      m_expandedOutlinerEntities.contains(
                          static_cast<uint32_t>(entity)));
}

void EditorMainWindow::onOutlinerSelectionChanged()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;

    const QList<QTreeWidgetItem*> selected = m_outlinerTree->selectedItems();
    if (selected.isEmpty()) {
        ctx->ClearSelection();
        return;
    }

    std::unordered_set<uint32_t> selectedIds;
    selectedIds.reserve(static_cast<size_t>(selected.size()));
    for (QTreeWidgetItem* item : selected) {
        const entt::entity e = entityFromOutlinerItem(item);
        if (e != entt::null) {
            selectedIds.insert(static_cast<uint32_t>(e));
        }
    }

    if (selectedIds.empty()) {
        ctx->ClearSelection();
        return;
    }

    std::vector<entt::entity> ordered;
    ordered.reserve(selectedIds.size());
    if (QTreeWidgetItem* sceneRoot = m_outlinerTree->topLevelItem(0)) {
        appendSelectedEntitiesInDisplayOrder(sceneRoot, selectedIds, ordered);
    }

    if (ordered.empty()) {
        ctx->ClearSelection();
        return;
    }

    entt::entity primary = entt::null;
    QTreeWidgetItem* current = m_outlinerTree->currentItem();
    if (current && current->isSelected()) {
        primary = entityFromOutlinerItem(current);
    }
    if (primary == entt::null || selectedIds.count(static_cast<uint32_t>(primary)) == 0) {
        primary = ordered.front();
    }

    ctx->SelectEntities(ordered, primary);
}

void EditorMainWindow::restoreOutlinerTreeSelection()
{
    if (!m_engine || !m_outlinerTree) {
        return;
    }
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) {
        return;
    }

    const std::vector<entt::entity> entities = ctx->GetSelectedEntities();
    if (entities.empty()) {
        return;
    }

    std::unordered_set<uint32_t> ids;
    for (entt::entity e : entities) {
        ids.insert(static_cast<uint32_t>(e));
    }

    const entt::entity primary = ctx->GetSelectedEntity();

    const QSignalBlocker outlinerSignals(m_outlinerTree);
    m_outlinerTree->clearSelection();
    QTreeWidgetItem* primaryItem = nullptr;
    QTreeWidgetItemIterator it(m_outlinerTree);
    while (*it) {
        QTreeWidgetItem* item = *it;
        const QVariant d = item->data(0, Qt::UserRole);
        if (d.isValid()) {
            const uint32_t raw = d.toUInt();
            if (ids.count(raw) != 0) {
                item->setSelected(true);
                if (raw == static_cast<uint32_t>(primary)) {
                    primaryItem = item;
                }
            }
        }
        ++it;
    }
    if (primaryItem != nullptr) {
        m_outlinerTree->setCurrentItem(primaryItem);
    }
}

QTreeWidgetItem* EditorMainWindow::findOutlinerItemForEntity(entt::entity entity)
{
    if (entity == entt::null || !m_outlinerTree) {
        return nullptr;
    }
    const uint32_t id = static_cast<uint32_t>(entity);
    QTreeWidgetItemIterator it(m_outlinerTree);
    while (*it) {
        QTreeWidgetItem* item = *it;
        const QVariant d = item->data(0, Qt::UserRole);
        if (d.isValid() && d.toUInt() == id) {
            return item;
        }
        ++it;
    }
    return nullptr;
}

void EditorMainWindow::onOutlinerContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_outlinerTree->itemAt(pos);
    QMenu menu(this);

    entt::entity entity = entt::null;
    bool isFolder = false;
    bool isEnv = false;

    if (item) {
        QVariant data = item->data(0, Qt::UserRole);
        if (data.isValid()) {
            entity = static_cast<entt::entity>(data.toUInt());
            if (m_engine) {
                auto* ctx = m_engine->GetEditorContext();
                if (ctx && ctx->HasActiveScene()) {
                    auto& reg = ctx->GetActiveScene()->GetRegistry();
                    isFolder = reg.all_of<FolderComponent>(entity);
                    isEnv = reg.all_of<EnvironmentComponent>(entity);
                }
            }
        }
    }
    if (entity != entt::null && m_engine) {
        if (auto* ctx = m_engine->GetEditorContext()) {
            const auto selected = ctx->GetSelectedEntities();
            if (std::find(selected.begin(), selected.end(), entity) == selected.end()) {
                ctx->SelectEntity(entity);
            }
        }
    }

    // "New Folder" - always available
    if (isFolder || isEnv) {
        menu.addAction(tr("New Folder Inside"), [this, entity]() {
            createFolderInOutliner(entity);
        });
    }
    menu.addAction(tr("New Folder"), [this]() {
        createFolderInOutliner(entt::null);
    });

    if (entity != entt::null && !isEnv) {
        menu.addSeparator();
        menu.addAction(tr("Rename"), [this, entity]() {
            renameEntityInOutliner(entity);
        });
        menu.addAction(tr("Move to Root"), [this, entity]() {
            reparentEntityToRoot(entity);
        });
        menu.addAction(tr("Duplicate"), this, &EditorMainWindow::onDuplicateEntity);
        menu.addAction(tr("Focus in Viewport"), [this, entity]() {
            if (!m_engine) return;
            if (auto* ctx = m_engine->GetEditorContext()) {
                ctx->SelectEntity(entity);
                m_engine->FocusOnSelection();
            }
        });
        menu.addAction(tr("Hide in Editor"), this,
                       &EditorMainWindow::hideSelectedInEditor);
        menu.addAction(tr("Isolate"), this,
                       &EditorMainWindow::isolateSelectedInEditor);
        menu.addAction(tr("Lock / Unlock"), this,
                       &EditorMainWindow::toggleLockSelectedInEditor);
        menu.addAction(tr("Select Hierarchy"), [this, entity]() {
            if (!m_engine) return;
            auto* ctx = m_engine->GetEditorContext();
            if (!ctx || !ctx->HasActiveScene()) return;
            Scene* scene = ctx->GetActiveScene();
            std::vector<entt::entity> hierarchy;
            std::function<void(Entity)> collect = [&](Entity current) {
                if (!current) return;
                hierarchy.push_back(static_cast<entt::entity>(current));
                for (Entity child : scene->GetChildren(current)) collect(child);
            };
            collect(Entity(entity, scene));
            if (!hierarchy.empty())
                ctx->SelectEntities(hierarchy, hierarchy.front());
        });

        // Prefab actions
        if (m_engine) {
            auto* ctx = m_engine->GetEditorContext();
            if (ctx && ctx->HasActiveScene()) {
                auto& reg = ctx->GetActiveScene()->GetRegistry();
                bool isPrefabInstance = reg.all_of<PrefabInstanceComponent>(entity);
                bool isPrefabNode = reg.all_of<PrefabNodeComponent>(entity);

                menu.addSeparator();
                if (!isPrefabNode && !isPrefabInstance) {
                    menu.addAction(tr("Create Prefab From Selection"), [this, entity]() {
                        createPrefabFromSelection(entity);
                    });
                }

                if (isPrefabInstance || isPrefabNode) {
                    Entity ent(entity, ctx->GetActiveScene());
                    auto* inst = PrefabOverrideSystem::TryGetInstance(*ctx->GetActiveScene(), ent);
                    const std::string prefabPath = inst ? inst->prefabPath : std::string();
                    menu.addAction(tr("Open Prefab"), [this, prefabPath]() {
                        if (m_prefabEditorManager && !prefabPath.empty()) {
                            m_prefabEditorManager->OpenPrefab(QString::fromStdString(prefabPath));
                        }
                    });
                    menu.addAction(tr("Select Prefab Asset"), [this, prefabPath]() {
                        if (!m_assetBrowser || prefabPath.empty()) return;
                        QFileInfo info(QString::fromStdString(prefabPath));
                        m_assetBrowser->NavigateTo(info.path());
                    });
                    if (isPrefabInstance) {
                        menu.addAction(tr("Refresh From Prefab"), [this, entity]() {
                            if (!m_engine) return;
                            auto* ctx = m_engine->GetEditorContext();
                            if (!ctx || !ctx->HasActiveScene()) return;
                            Scene* scene = ctx->GetActiveScene();
                            Entity ent(entity, scene);
                            EditorUndoManager::PushSnapshot(ctx, "Refresh prefab instance");
                            PrefabOverrideSystem::SyncInstance(*scene, ent);
                            PrefabOverrideSystem::RefreshInstance(*scene, ent);
                            m_engine->RebuildSceneMeshes();
                            ctx->MarkDirty();
                            rebuildOutliner();
                            refreshDetailsPanel();
                        });
                        menu.addAction(tr("Apply Overrides"), [this, entity]() {
                            runPrefabApply(entity, "*", "*", true);
                        });
                        menu.addAction(tr("Revert Overrides"), [this, entity]() {
                            runPrefabRevert(entity, "*", "*");
                        });
                        menu.addAction(tr("Unpack Prefab"), [this, entity]() {
                            if (!m_engine) return;
                            auto* ctx = m_engine->GetEditorContext();
                            if (!ctx || !ctx->HasActiveScene()) return;
                            Scene* scene = ctx->GetActiveScene();
                            Entity ent(entity, scene);
                            EditorUndoManager::PushSnapshot(ctx, "Unpack prefab");
                            PrefabOverrideSystem::UnpackInstance(*scene, ent);
                            ctx->MarkDirty();
                            rebuildOutliner();
                            refreshDetailsPanel();
                            statusBar()->showMessage(tr("Prefab unpacked"), 3000);
                        });
                    } else {
                        menu.addAction(tr("Revert Node Overrides"), [this, entity]() {
                            runPrefabRevert(entity, "", "");
                        });
                        menu.addAction(tr("Apply Node Changes to Prefab"), [this, entity]() {
                            runPrefabApply(entity, "", "", false);
                        });
                    }
                }
            }
        }

        menu.addSeparator();
        menu.addAction(tr("Delete"), [this, entity]() {
            deleteEntityFromOutliner(entity);
        });
    }

    if (m_pluginHost) m_pluginHost->PopulateContextMenu("sceneHierarchy", menu);
    menu.exec(m_outlinerTree->viewport()->mapToGlobal(pos));
}

void EditorMainWindow::syncGizmoSpaceButtons()
{
    const bool local = m_worldLocalBtn->isChecked();
    for (auto [world, localBtn] : { std::pair{ m_worldBtn, m_worldLocalBtn }, std::pair{ m_inspectorWorldBtn, m_inspectorLocalBtn } }) {
        if (!world || !localBtn) continue;
        world->setChecked(!local);
        localBtn->setChecked(local);
    }
}

void EditorMainWindow::setEditorMode(EditorMode mode)
{
    if (mode == m_mode) return;
    QSettings settings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"));
    settings.setValue(ModeStateKey(m_mode), saveState(kDockLayoutVersion));
    m_mode = mode;
    settings.setValue(QStringLiteral("mainWindow/mode"), static_cast<int>(m_mode));
    if (m_projectSettingsManager) {
        m_projectSettingsManager->SetEditorMode(QLatin1String(kEditorModeNames[static_cast<int>(m_mode)]));
    }
    restoreModeLayout(m_mode);
}

void EditorMainWindow::showProgrammerPanel(QDockWidget* dock)
{
    if (m_mode != EditorMode::Programmer) {
        if (QAbstractButton* button = m_modeButtons->button(static_cast<int>(EditorMode::Programmer))) {
            button->setChecked(true);
        }
        setEditorMode(EditorMode::Programmer);
    }
    dock->show();
    dock->raise();
}

void EditorMainWindow::setupLuaDebugger()
{
    LuaDebugger& debugger = LuaDebugger::Instance();
    const struct {
        QKeySequence key;
        LuaDebugger::Action action;
    } keys[] = {
        { QKeySequence(Qt::Key_F5), LuaDebugger::Action::Continue },
        { QKeySequence(Qt::Key_F10), LuaDebugger::Action::StepOver },
        { QKeySequence(Qt::Key_F11), LuaDebugger::Action::StepInto },
        { QKeySequence(Qt::SHIFT | Qt::Key_F11), LuaDebugger::Action::StepOut },
        { QKeySequence(Qt::SHIFT | Qt::Key_F5), LuaDebugger::Action::Stop },
    };
    auto act = [this](LuaDebugger::Action action) {
        if (action == LuaDebugger::Action::Stop) {
            if (m_engine) m_engine->StopPlayMode();
        } else {
            LuaDebugger::Instance().RequestResume(action);
        }
    };
    for (const auto& entry : keys) {
        // Enabled only while paused, so F5/F10/F11 keep their usual meaning otherwise.
        auto* shortcut = new QShortcut(entry.key, this);
        shortcut->setContext(Qt::ApplicationShortcut);
        shortcut->setEnabled(false);
        const LuaDebugger::Action action = entry.action;
        connect(shortcut, &QShortcut::activated, this, [act, action]() { act(action); });
        m_debuggerShortcuts << shortcut;
    }
    m_debuggerPanel->onAction = act;
    m_debuggerPanel->onPauseRequested = []() { LuaDebugger::Instance().RequestPause(); };
    m_debuggerPanel->onFrameSelected = [this](const QString& path, int line) {
        const QString resolved = CodeEditorPanel::ResolveLocation(path);
        if (!resolved.isEmpty()) m_codeEditor->OpenFileAt(resolved, line);
    };

    // Lua waits inside this callback; a nested event loop keeps the editor usable meanwhile.
    debugger.onPause = [this](const LuaDebugger::PauseState& state) {
        QEventLoop loop;
        LuaDebugger::Instance().onResumeRequested = [&loop]() { loop.quit(); };
        for (QShortcut* shortcut : m_debuggerShortcuts) shortcut->setEnabled(true);
        // The engine tick that hands the keyboard to the game does not run while Lua is paused.
        if (QtViewportWindow* gameWindow = m_engine ? m_engine->PresentationTarget() : nullptr) {
            gameWindow->SetGameOwnsKeyboard(false);
        }
        showProgrammerPanel(m_debuggerDock);
        m_codeEditorDock->show();
        m_debuggerPanel->ShowPause(state);
        QString where;
        if (!state.frames.empty()) {
            const LuaDebugger::Frame& top = state.frames.front();
            const QString path = CodeEditorPanel::ResolveLocation(QString::fromStdString(top.source));
            if (!path.isEmpty()) m_codeEditor->ShowExecutionLine(path, top.line);
            where = QStringLiteral(" · %1:%2").arg(QFileInfo(QString::fromStdString(top.source)).fileName()).arg(top.line);
        }
        statusBar()->showMessage(tr("Paused at %1").arg(QString::fromStdString(state.reason).toLower()) + where);
        loop.exec();
        LuaDebugger::Instance().onResumeRequested = nullptr;
        for (QShortcut* shortcut : m_debuggerShortcuts) shortcut->setEnabled(false);
        statusBar()->clearMessage();
        m_codeEditor->ShowExecutionLine({}, 0);
        m_debuggerPanel->ShowRunning(m_engine && m_engine->IsPlaying());
        return LuaDebugger::Instance().TakeRequestedAction();
    };
}

void EditorMainWindow::runConsoleLua(const QString& code)
{
    EngineLog::Write(EngineLogLevel::Info, "lua> " + code.toStdString(), "Lua");
    LuaDebugger& debugger = LuaDebugger::Instance();
    if (debugger.IsPaused()) {
        const LuaDebugger::Variable result = debugger.Evaluate(code.toStdString(), m_debuggerPanel->SelectedFrame());
        EngineLog::Write(result.type == "error" ? EngineLogLevel::Error : EngineLogLevel::Info, result.value, "Lua");
        return;
    }
    LuaScriptEngine* lua = m_engine ? m_engine->GetLuaScriptEngine() : nullptr;
    if (!lua || !(m_engine->IsPlaying() || m_engine->IsPaused())) {
        EngineLog::Write(EngineLogLevel::Warning, "Lua runs while the game is playing. Press Play first.", "Lua");
        return;
    }
    bool ok = false;
    const std::string result = lua->EvaluateConsole(code.toStdString(), ok);
    if (!ok || !result.empty()) {
        EngineLog::Write(ok ? EngineLogLevel::Info : EngineLogLevel::Error, result, "Lua");
    }
}

void EditorMainWindow::restoreModeLayout(EditorMode mode)
{
    QSettings settings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"));
    const QByteArray state = settings.value(ModeStateKey(mode)).toByteArray();
    if (state.isEmpty() || !restoreState(state, kDockLayoutVersion)) {
        arrangeModeLayout(mode);
    }
    m_topToolbar->setVisible(mode != EditorMode::Artist);
    m_artistToolRail->setVisible(mode == EditorMode::Artist);
    if (m_assetBrowser) {
        m_assetBrowser->SetGridView(mode != EditorMode::Programmer);
        m_assetBrowser->SetLargeCards(mode == EditorMode::Artist);
    }
}

void EditorMainWindow::arrangeModeLayout(EditorMode mode)
{
    const QList<QDockWidget*> editorDocks = { m_outlinerDock, m_detailsDock, m_assetBrowserDock,
                                              m_consoleDock, m_buildOutputDock, m_addActorsDock,
                                              m_codeEditorDock, m_searchDock, m_debuggerDock };
    for (QDockWidget* dock : editorDocks) {
        dock->setFloating(false);
        removeDockWidget(dock);
    }
    auto place = [this](QDockWidget* dock, Qt::DockWidgetArea area) {
        addDockWidget(area, dock);
        dock->show();
    };
    // Default and Artist: Content Browser, Console, Output, Search, Debugger as one tab group.
    auto placeBottomTabs = [this, &place]() {
        for (QDockWidget* dock : { m_assetBrowserDock, m_consoleDock, m_buildOutputDock, m_searchDock, m_debuggerDock }) {
            place(dock, Qt::BottomDockWidgetArea);
        }
        for (QDockWidget* dock : { m_consoleDock, m_buildOutputDock, m_searchDock, m_debuggerDock }) {
            tabifyDockWidget(m_assetBrowserDock, dock);
        }
        m_assetBrowserDock->raise();
    };
    // The Code panel stays docked but closed outside Programmer mode; View opens it.
    auto placeHiddenCodeDock = [this](Qt::DockWidgetArea area) {
        addDockWidget(area, m_codeEditorDock);
        m_codeEditorDock->hide();
    };

    // Dock sizes only apply once the new arrangement has been laid out.
    QTimer::singleShot(0, this, [this, mode]() {
        switch (mode) {
        case EditorMode::Default:
            resizeDocks({ m_outlinerDock, m_detailsDock }, { 280, 340 }, Qt::Horizontal);
            resizeDocks({ m_assetBrowserDock }, { 320 }, Qt::Vertical);
            break;
        case EditorMode::Artist:
            resizeDocks({ m_outlinerDock }, { 360 }, Qt::Horizontal);
            resizeDocks({ m_outlinerDock, m_detailsDock }, { 1, 2 }, Qt::Vertical);
            resizeDocks({ m_assetBrowserDock }, { 330 }, Qt::Vertical);
            break;
        case EditorMode::Programmer:
            resizeDocks({ m_assetBrowserDock, m_codeEditorDock, m_detailsDock }, { 280, 760, 340 }, Qt::Horizontal);
            resizeDocks({ m_consoleDock }, { 300 }, Qt::Vertical);
            break;
        }
    });

    switch (mode) {
    case EditorMode::Default:
        place(m_outlinerDock, Qt::LeftDockWidgetArea);
        place(m_detailsDock, Qt::RightDockWidgetArea);
        placeBottomTabs();
        placeHiddenCodeDock(Qt::LeftDockWidgetArea);
        break;
    case EditorMode::Artist:
        place(m_outlinerDock, Qt::RightDockWidgetArea);
        place(m_detailsDock, Qt::RightDockWidgetArea);
        splitDockWidget(m_outlinerDock, m_detailsDock, Qt::Vertical);
        placeBottomTabs();
        placeHiddenCodeDock(Qt::LeftDockWidgetArea);
        break;
    case EditorMode::Programmer:
        // Split before tabifying: splitting a dock that is already in a tab group drops the group off-screen.
        place(m_assetBrowserDock, Qt::LeftDockWidgetArea);
        place(m_codeEditorDock, Qt::LeftDockWidgetArea);
        splitDockWidget(m_assetBrowserDock, m_codeEditorDock, Qt::Horizontal);
        place(m_outlinerDock, Qt::LeftDockWidgetArea);
        tabifyDockWidget(m_assetBrowserDock, m_outlinerDock);
        m_assetBrowserDock->raise();
        place(m_detailsDock, Qt::RightDockWidgetArea);
        place(m_consoleDock, Qt::BottomDockWidgetArea);
        place(m_buildOutputDock, Qt::BottomDockWidgetArea);
        place(m_searchDock, Qt::BottomDockWidgetArea);
        place(m_debuggerDock, Qt::BottomDockWidgetArea);
        tabifyDockWidget(m_consoleDock, m_buildOutputDock);
        tabifyDockWidget(m_consoleDock, m_searchDock);
        tabifyDockWidget(m_consoleDock, m_debuggerDock);
        m_consoleDock->raise();
        break;
    }
}

QWidget* EditorMainWindow::componentCardOf(QWidget* body)
{
    QWidget* parent = body ? body->parentWidget() : nullptr;
    return parent && parent->objectName() == QLatin1String("ComponentCard") ? parent : body;
}

QWidget* EditorMainWindow::wrapComponentCard(QGroupBox* body, const QString& title, const QString& type,
                                             std::function<void()> onReset)
{
    auto* card = new QFrame(m_detailsWidget);
    card->setObjectName(QStringLiteral("ComponentCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(card);
    header->setObjectName(QStringLiteral("ComponentHeader"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto* row = new QHBoxLayout(header);
    row->setContentsMargins(4, 0, 4, 0);
    row->setSpacing(4);

    auto* toggle = new QToolButton(header);
    toggle->setToolTip(tr("Collapse or expand"));
    toggle->setFocusPolicy(Qt::NoFocus);
    row->addWidget(toggle);

    auto* icon = new QLabel(header);
    icon->setPixmap(Theme::Icon(ComponentIconName(type)).pixmap(QSize(16, 16), devicePixelRatioF()));
    row->addWidget(icon);

    auto* titleLabel = new QLabel(title, header);
    titleLabel->setObjectName(QStringLiteral("ComponentTitle"));
    row->addWidget(titleLabel);
    row->addStretch(1);

    auto* reset = new QToolButton(header);
    reset->setIcon(Theme::Icon(QStringLiteral("reset")));
    reset->setToolTip(tr("Reset to default"));
    connect(reset, &QToolButton::clicked, this, [body, onReset = std::move(onReset)]() {
        if (body->isEnabled()) onReset();
    });
    row->addWidget(reset);

    auto* more = new QToolButton(header);
    more->setIcon(Theme::Icon(QStringLiteral("more")));
    more->setToolTip(tr("Component options"));
    connect(more, &QToolButton::clicked, this, [body, more]() {
        if (body->isEnabled() && body->contextMenuPolicy() == Qt::CustomContextMenu) {
            emit body->customContextMenuRequested(body->mapFromGlobal(more->mapToGlobal(QPoint(0, more->height()))));
        }
    });
    row->addWidget(more);

    body->setParent(card);
    body->setObjectName(body->objectName().isEmpty() ? QStringLiteral("ComponentBody") : body->objectName());
    body->setProperty("componentBody", true);
    layout->addWidget(header);
    layout->addWidget(body);

    const bool collapsed = m_collapsedComponents.contains(type);
    body->setVisible(!collapsed);
    toggle->setIcon(Theme::Icon(collapsed ? QStringLiteral("chevron-right") : QStringLiteral("chevron-down"), Theme::IconTone::Muted));
    connect(toggle, &QToolButton::clicked, this, [this, body, toggle, type]() {
        const bool collapse = body->isVisible();
        body->setVisible(!collapse);
        toggle->setIcon(Theme::Icon(collapse ? QStringLiteral("chevron-right") : QStringLiteral("chevron-down"), Theme::IconTone::Muted));
        if (collapse) m_collapsedComponents.insert(type);
        else m_collapsedComponents.remove(type);
    });
    return card;
}

void EditorMainWindow::createEmptyEntity()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;

    EditorUndoManager::PushSnapshot(ctx, "Create entity");
    Entity created = ctx->GetActiveScene()->CreateEntity("Entity");
    ctx->SetSelectionChangedCallback(nullptr);
    ctx->SelectEntity(static_cast<entt::entity>(created));
    ctx->SetSelectionChangedCallback([this](entt::entity) {
        refreshDetailsPanel();
    });
    ctx->MarkDirty();
    rebuildOutliner();
    refreshDetailsPanel();
}

void EditorMainWindow::createFolderInOutliner(entt::entity parentEntity)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;

    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Create Folder"),
                                         tr("Folder name:"), QLineEdit::Normal,
                                         tr("New Folder"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    EditorUndoManager::PushSnapshot(ctx, "Create folder");
    Scene* scene = ctx->GetActiveScene();
    Entity folder = scene->CreateEntity(name.trimmed().toStdString());
    folder.AddComponent<FolderComponent>();

    if (parentEntity != entt::null) {
        Entity parent(parentEntity, scene);
        if (parent) {
            scene->SetParent(folder, parent);
        }
    }

    ctx->MarkDirty();
    rebuildOutliner();
}

void EditorMainWindow::deleteEntityFromOutliner(entt::entity entity)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;

    Scene* scene = ctx->GetActiveScene();
    Entity e(entity, scene);
    if (!e) return;

    // Don't allow deleting the environment root
    if (e.HasComponent<EnvironmentComponent>()) return;

    EditorUndoManager::PushSnapshot(ctx, "Delete entity");

    const auto selectedIds = ctx->GetSelectedEntities();
    const auto it = std::find(selectedIds.begin(), selectedIds.end(), entity);
    if (it != selectedIds.end()) {
        ctx->ClearSelection();
    }

    scene->DestroyEntity(e);
    ctx->MarkDirty();
    rebuildOutliner();
    clearDetailsPanel();
}

void EditorMainWindow::deleteSelectedEntities()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;

    const std::vector<entt::entity> selected = ctx->GetSelectedEntities();
    if (selected.empty()) return;

    Scene* scene = ctx->GetActiveScene();
    EditorUndoManager::PushSnapshot(ctx, "Delete selected entities");
    ctx->ClearSelection();
    for (entt::entity handle : selected) {
        Entity entity(handle, scene);
        if (entity && !entity.HasComponent<EnvironmentComponent>()) {
            if (entity.HasComponent<PrefabInstanceComponent>()) {
                PrefabInstantiator::DestroyInstance(*scene, entity);
            } else {
                scene->DestroyEntity(entity);
            }
        }
    }
    ctx->MarkDirty();
    rebuildOutliner();
    clearDetailsPanel();
}

void EditorMainWindow::reparentEntityToRoot(entt::entity entity)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;

    Scene* scene = ctx->GetActiveScene();
    Entity e(entity, scene);
    if (!e) return;

    EditorUndoManager::PushSnapshot(ctx, "Move to root");
    scene->RemoveParent(e, true);
    ctx->MarkDirty();
    rebuildOutliner();
}

void EditorMainWindow::renameEntityInOutliner(entt::entity entity)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;

    Scene* scene = ctx->GetActiveScene();
    Entity e(entity, scene);
    if (!e || !e.HasComponent<TagComponent>()) return;

    QString current = QString::fromStdString(e.GetComponent<TagComponent>().tag);
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Rename"),
                                         tr("New name:"), QLineEdit::Normal,
                                         current, &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    EditorUndoManager::PushSnapshot(ctx, "Rename entity");
    e.GetComponent<TagComponent>().tag = name.trimmed().toStdString();
    ctx->MarkDirty();
    rebuildOutliner();
    refreshDetailsPanel();
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
    m_nameEdit->setPlaceholderText(tr("Select an entity to inspect."));
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
    if (m_prefabBar) {
        m_prefabBar->Clear();
    }

    m_entityIdLabel->setText(tr("Select an entity to inspect."));
    m_entityIconLabel->clear();
    if (m_detailsSharedHint) m_detailsSharedHint->hide();
    if (m_detailsAdvancedId) m_detailsAdvancedId->hide();
    if (m_detailsEmptyHint) {
        m_detailsEmptyHint->setText(tr("Select an entity to inspect."));
        m_detailsEmptyHint->show();
    }
    if (m_detailsPartialHint) m_detailsPartialHint->hide();
    m_addComponentBtn->setEnabled(false);
    if (m_visibilityBtn) {
        m_visibilityBtn->blockSignals(true);
        m_visibilityBtn->setChecked(true);
        m_visibilityBtn->setEnabled(false);
        m_visibilityBtn->blockSignals(false);
    }
    
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
        QWidget* card = componentCardOf(insp);
        m_detailsLayout->removeWidget(card);
        delete card;
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

    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    InspectorSelectionModel model = resolveInspectorSelection();

    if (model.pinnedMissing) {
        m_updatingDetails = true;
        blockDetailSignals(true);
        m_entityIdLabel->setText(tr("Pinned entity no longer exists."));
        if (m_detailsSharedHint) m_detailsSharedHint->hide();
        if (m_detailsAdvancedId) m_detailsAdvancedId->hide();
        if (m_detailsEmptyHint) {
            m_detailsEmptyHint->setText(tr("Pinned entity no longer exists."));
            m_detailsEmptyHint->show();
        }
        if (m_detailsPartialHint) m_detailsPartialHint->hide();
        m_nameEdit->clear();
        m_nameEdit->setEnabled(false);
        m_transformGroup->setEnabled(false);
        m_addComponentBtn->setEnabled(false);
        if (m_visibilityBtn) m_visibilityBtn->setEnabled(false);
        if (m_prefabBar) m_prefabBar->Clear();
        for (auto* insp : m_componentInspectors) {
            QWidget* card = componentCardOf(insp);
            m_detailsLayout->removeWidget(card);
            delete card;
        }
        m_componentInspectors.clear();
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
        blockDetailSignals(false);
        m_updatingDetails = false;
        return;
    }

    if (model.Empty()) {
        clearDetailsPanel();
        return;
    }

    const entt::entity entity = model.primary;
    if (entity == entt::null || !registry.valid(entity)) {
        clearDetailsPanel();
        return;
    }
    
    m_updatingDetails = true;
    blockDetailSignals(true);

    if (m_detailsEmptyHint) m_detailsEmptyHint->hide();

    const size_t selectionCount = model.Count();
    if (selectionCount > 1) {
        m_entityIdLabel->setText(tr("%1 objects selected").arg(static_cast<int>(selectionCount)));
        m_entityIconLabel->clear();
        if (m_detailsSharedHint) {
            m_detailsSharedHint->setText(tr("Showing shared properties."));
            m_detailsSharedHint->show();
        }
        if (m_detailsAdvancedId) {
            m_detailsAdvancedId->setText(tr("Primary #%1").arg(static_cast<uint32_t>(entity)));
            m_detailsAdvancedId->show();
        }
    } else {
        MixedString name;
        if (registry.all_of<TagComponent>(entity)) {
            AccumulateString(name, registry.get<TagComponent>(entity).tag);
        }
        QString typeLine = OutlinerTypeLabel(OutlinerIconName(registry, entity));
        if (registry.all_of<PrefabNodeComponent>(entity) && !registry.all_of<PrefabInstanceComponent>(entity)) {
            typeLine += QStringLiteral(" \u00B7 ") + tr("in prefab");
        }
        m_entityIdLabel->setText(typeLine);
        m_entityIconLabel->setPixmap(Theme::Icon(OutlinerIconName(registry, entity))
                                         .pixmap(QSize(16, 16), devicePixelRatioF()));
        m_entityIconLabel->setToolTip(QStringLiteral("#%1").arg(static_cast<uint32_t>(entity)));
        if (m_detailsSharedHint) m_detailsSharedHint->hide();
        if (m_detailsAdvancedId) {
            m_detailsAdvancedId->hide();
        }
    }

    m_addComponentBtn->setEnabled(true);

    MixedBool vis;
    int meshCount = 0;
    for (entt::entity handle : model.entities) {
        if (registry.valid(handle) && registry.all_of<MeshRendererComponent>(handle)) {
            ++meshCount;
            AccumulateBool(vis, registry.get<MeshRendererComponent>(handle).visible);
        }
    }
    if (m_visibilityBtn) {
        m_visibilityBtn->setEnabled(meshCount > 0);
        m_visibilityBtn->blockSignals(true);
        if (!vis.present || vis.mixed) {
            m_visibilityBtn->setChecked(false);
        } else {
            m_visibilityBtn->setChecked(vis.value);
        }
        m_visibilityBtn->blockSignals(false);
    }

    MixedString name;
    for (entt::entity handle : model.entities) {
        if (registry.valid(handle) && registry.all_of<TagComponent>(handle)) {
            AccumulateString(name, registry.get<TagComponent>(handle).tag);
        }
    }
    m_nameEdit->setEnabled(true);
    if (!name.present || name.mixed) {
        m_nameEdit->clear();
        m_nameEdit->setPlaceholderText(QString::fromUtf8("\xE2\x80\x94 Mixed \xE2\x80\x94"));
    } else {
        m_nameEdit->setPlaceholderText(tr("Name"));
        m_nameEdit->setText(QString::fromStdString(name.value));
    }
    
    std::vector<Vec3> positions;
    std::vector<Vec3> rotations;
    std::vector<Vec3> scales;
    positions.reserve(model.entities.size());
    rotations.reserve(model.entities.size());
    scales.reserve(model.entities.size());
    int transformCount = 0;
    constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979f;
    for (entt::entity handle : model.entities) {
        if (!registry.valid(handle) || !registry.all_of<TransformComponent>(handle)) continue;
        ++transformCount;
        const auto& transform = registry.get<TransformComponent>(handle);
        positions.push_back(transform.position);
        rotations.push_back(Vec3(transform.rotation.x * RAD_TO_DEG,
                                 transform.rotation.y * RAD_TO_DEG,
                                 transform.rotation.z * RAD_TO_DEG));
        scales.push_back(transform.scale);
    }
    m_transformGroup->setEnabled(transformCount > 0);
    if (transformCount > 0) {
        SetSpinFromMixed(m_posX, MixVec3(positions).x);
        SetSpinFromMixed(m_posY, MixVec3(positions).y);
        SetSpinFromMixed(m_posZ, MixVec3(positions).z);
        const MixedVec3Axes mixedRot = MixVec3(rotations);
        SetSpinFromMixed(m_rotX, mixedRot.x);
        SetSpinFromMixed(m_rotY, mixedRot.y);
        SetSpinFromMixed(m_rotZ, mixedRot.z);
        const MixedVec3Axes mixedScale = MixVec3(scales);
        SetSpinFromMixed(m_scaleX, mixedScale.x);
        SetSpinFromMixed(m_scaleY, mixedScale.y);
        SetSpinFromMixed(m_scaleZ, mixedScale.z);
    }

    const auto partial = model.PartialComponents(registry);
    if (m_detailsPartialHint) {
        if (!partial.empty()) {
            QStringList names;
            for (const auto& type : partial) {
                names.push_back(QString::fromStdString(InspectorDisplayName(type)));
            }
            m_detailsPartialHint->setText(
                tr("Some selected entities also contain: %1").arg(names.join(tr(", "))));
            m_detailsPartialHint->show();
        } else {
            m_detailsPartialHint->hide();
        }
    }
    
    blockDetailSignals(false);
    m_updatingDetails = false;

    rebuildComponentInspectors();
    updatePrefabInspectorUi();
}

void EditorMainWindow::onNameChanged(const QString& text)
{
    if (m_updatingDetails || !m_engine) return;
    
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() && m_nameEdit->placeholderText().contains(QString::fromUtf8("\xE2\x80\x94"))) {
        return;
    }

    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    for (entt::entity entity : model.entities) {
        if (!registry.valid(entity)) continue;
        if (registry.all_of<TagComponent>(entity)) {
            registry.get<TagComponent>(entity).tag = trimmed.toStdString();
        } else {
            registry.emplace<TagComponent>(entity, trimmed.toStdString());
        }
        if (QTreeWidgetItem* row = findOutlinerItemForEntity(entity)) {
            row->setText(0, trimmed);
        }
    }
    ctx->MarkDirty();
}

void EditorMainWindow::onPositionChanged()
{
    if (m_updatingDetails || !m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;

    double x = 0.0, y = 0.0, z = 0.0;
    const bool hasX = TryReadSpin(m_posX, x);
    const bool hasY = TryReadSpin(m_posY, y);
    const bool hasZ = TryReadSpin(m_posZ, z);
    if (!hasX && !hasY && !hasZ) return;

    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    beginCoalescedTransformUndo("Move entity");
    for (entt::entity entity : model.entities) {
        if (!registry.valid(entity) || !registry.all_of<TransformComponent>(entity)) continue;
        auto& transform = registry.get<TransformComponent>(entity);
        if (hasX) transform.position.x = static_cast<float>(x);
        if (hasY) transform.position.y = static_cast<float>(y);
        if (hasZ) transform.position.z = static_cast<float>(z);
    }
    ctx->MarkDirty();
}

void EditorMainWindow::onRotationChanged()
{
    if (m_updatingDetails || !m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;

    double x = 0.0, y = 0.0, z = 0.0;
    const bool hasX = TryReadSpin(m_rotX, x);
    const bool hasY = TryReadSpin(m_rotY, y);
    const bool hasZ = TryReadSpin(m_rotZ, z);
    if (!hasX && !hasY && !hasZ) return;

    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    constexpr float DEG_TO_RAD = 3.14159265358979f / 180.0f;
    beginCoalescedTransformUndo("Rotate entity");
    for (entt::entity entity : model.entities) {
        if (!registry.valid(entity) || !registry.all_of<TransformComponent>(entity)) continue;
        auto& transform = registry.get<TransformComponent>(entity);
        if (hasX) transform.rotation.x = static_cast<float>(x) * DEG_TO_RAD;
        if (hasY) transform.rotation.y = static_cast<float>(y) * DEG_TO_RAD;
        if (hasZ) transform.rotation.z = static_cast<float>(z) * DEG_TO_RAD;
    }
    ctx->MarkDirty();
}

void EditorMainWindow::onScaleChanged()
{
    if (m_updatingDetails || !m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;

    double x = 0.0, y = 0.0, z = 0.0;
    const bool hasX = TryReadSpin(m_scaleX, x);
    const bool hasY = TryReadSpin(m_scaleY, y);
    const bool hasZ = TryReadSpin(m_scaleZ, z);
    if (!hasX && !hasY && !hasZ) return;

    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    beginCoalescedTransformUndo("Scale entity");
    for (entt::entity entity : model.entities) {
        if (!registry.valid(entity) || !registry.all_of<TransformComponent>(entity)) continue;
        auto& transform = registry.get<TransformComponent>(entity);
        if (hasX) transform.scale.x = static_cast<float>(x);
        if (hasY) transform.scale.y = static_cast<float>(y);
        if (hasZ) transform.scale.z = static_cast<float>(z);
    }
    ctx->MarkDirty();
}

void EditorMainWindow::beginCoalescedTransformUndo(const char* label)
{
    if (m_transformUndoActive || !m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;
    EditorUndoManager::PushSnapshot(ctx, label ? label : "Transform");
    m_transformUndoActive = true;
    updateUndoRedoActions();
}

void EditorMainWindow::endCoalescedTransformUndo()
{
    m_transformUndoActive = false;
}

void EditorMainWindow::updateUndoRedoActions()
{
    bool canUndo = false;
    bool canRedo = false;
    if (m_engine) {
        auto* ctx = m_engine->GetEditorContext();
        if (ctx) {
            canUndo = EditorUndoManager::CanUndo(ctx);
            canRedo = EditorUndoManager::CanRedo(ctx);
        }
    }
    if (m_undoAction) m_undoAction->setEnabled(canUndo);
    if (m_redoAction) m_redoAction->setEnabled(canRedo);
}

void EditorMainWindow::onVisibilityToggled(bool visible)
{
    if (m_updatingDetails || !m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;

    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    EditorUndoManager::PushSnapshot(ctx, "Toggle visibility");
    for (entt::entity entity : model.entities) {
        if (registry.valid(entity) && registry.all_of<MeshRendererComponent>(entity)) {
            registry.get<MeshRendererComponent>(entity).visible = visible;
        }
    }
    ctx->MarkDirty();
    updateUndoRedoActions();
}

void EditorMainWindow::loadEditorViewState()
{
    m_editorViewState = {};
    m_editorViewStatePath.clear();
    if (!m_engine || !m_projectContext || !m_projectContext->HasProject()) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;

    const std::string sceneKey = ctx->GetScenePath().empty()
        ? std::string("unsaved:") + ctx->GetActiveScene()->GetName()
        : ctx->GetScenePath();
    m_editorViewStatePath = EditorViewState::StatePath(
        std::filesystem::path(m_projectContext->GetProjectRootDir()), sceneKey);
    m_editorViewState.Load(m_editorViewStatePath);
    ctx->SetEditorStateIds(m_editorViewState.hiddenEntities,
                           m_editorViewState.lockedEntities);
    rebuildOutliner();
}

void EditorMainWindow::saveEditorViewState()
{
    if (!m_engine || m_editorViewStatePath.empty()) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;
    m_editorViewState.hiddenEntities = ctx->GetEditorHiddenIds();
    m_editorViewState.lockedEntities = ctx->GetEditorLockedIds();
    m_editorViewState.Save(m_editorViewStatePath);
}

void EditorMainWindow::hideSelectedInEditor()
{
    QWidget* focus = QApplication::focusWidget();
    if (focus && (qobject_cast<QLineEdit*>(focus) ||
                  focus->inherits("QAbstractSpinBox") ||
                  focus->inherits("QTextEdit") ||
                  focus->inherits("QPlainTextEdit"))) {
        return;
    }
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;
    for (entt::entity entity : ctx->GetSelectedEntities()) {
        ctx->SetEditorHidden(entity, true);
    }
    ctx->ClearSelection();
    saveEditorViewState();
    rebuildOutliner();
    refreshDetailsPanel();
}

void EditorMainWindow::toggleOutlinerVisibility(entt::entity entity, bool isolate)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || entity == entt::null) return;
    if (isolate) {
        const bool alreadyIsolated = ctx->GetIsolatedIds().size() == 1 &&
                                     ctx->GetIsolatedIds().contains(static_cast<uint32_t>(entity));
        if (alreadyIsolated) {
            ctx->ExitIsolation();
        } else {
            ctx->IsolateEntities({ entity });
        }
    } else {
        ctx->SetEditorHidden(entity, !ctx->IsEditorHidden(entity));
        saveEditorViewState();
    }
    rebuildOutliner();
}

void EditorMainWindow::isolateSelectedInEditor()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;
    if (ctx->IsIsolationActive()) {
        ctx->ExitIsolation();
    } else {
        ctx->IsolateEntities(ctx->GetSelectedEntities());
    }
    rebuildOutliner();
}

void EditorMainWindow::toggleLockSelectedInEditor()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;
    const std::vector<entt::entity> selected = ctx->GetSelectedEntities();
    if (selected.empty()) return;
    const bool lock = std::any_of(
        selected.begin(), selected.end(),
        [ctx](entt::entity entity) { return !ctx->IsEditorLocked(entity); });
    for (entt::entity entity : selected) {
        ctx->SetEditorLocked(entity, lock);
    }
    saveEditorViewState();
    rebuildOutliner();
}

void EditorMainWindow::alignSelection(int axis)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    const auto selected = ctx->GetSelectedEntities();
    if (selected.size() < 2) return;
    Scene* scene = ctx->GetActiveScene();
    Entity active(ctx->GetSelectedEntity(), scene);
    if (!active) return;
    const Mat4 activeWorld = scene->GetWorldTransform(active);
    const float target = activeWorld.m[12 + axis];

    EditorUndoManager::PushSnapshot(ctx, "Align selection");
    for (entt::entity handle : selected) {
        Entity entity(handle, scene);
        if (!entity || handle == ctx->GetSelectedEntity()) continue;
        Mat4 world = scene->GetWorldTransform(entity);
        world.m[12 + axis] = target;
        Entity parent = scene->GetParent(entity);
        const Mat4 local = parent
            ? Mat4::Multiply(Mat4::Invert(scene->GetWorldTransform(parent)), world)
            : world;
        entity.GetComponent<TransformComponent>().SetFromMatrix(local);
    }
    ctx->MarkDirty();
}

void EditorMainWindow::distributeSelection(int axis)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    auto selected = ctx->GetSelectedEntities();
    if (selected.size() < 3) return;
    Scene* scene = ctx->GetActiveScene();
    std::sort(selected.begin(), selected.end(), [scene, axis](entt::entity a, entt::entity b) {
        return scene->GetWorldTransform(Entity(a, scene)).m[12 + axis] <
               scene->GetWorldTransform(Entity(b, scene)).m[12 + axis];
    });
    const float first =
        scene->GetWorldTransform(Entity(selected.front(), scene)).m[12 + axis];
    const float last =
        scene->GetWorldTransform(Entity(selected.back(), scene)).m[12 + axis];
    const std::vector<float> targets =
        SceneAuthoring::EvenlySpacedValues(first, last, selected.size());

    EditorUndoManager::PushSnapshot(ctx, "Distribute selection");
    for (size_t i = 1; i + 1 < selected.size(); ++i) {
        Entity entity(selected[i], scene);
        Mat4 world = scene->GetWorldTransform(entity);
        world.m[12 + axis] = targets[i];
        Entity parent = scene->GetParent(entity);
        const Mat4 local = parent
            ? Mat4::Multiply(Mat4::Invert(scene->GetWorldTransform(parent)), world)
            : world;
        entity.GetComponent<TransformComponent>().SetFromMatrix(local);
    }
    ctx->MarkDirty();
}

void EditorMainWindow::dropSelectionToGround()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    const auto selected = ctx->GetSelectedEntities();
    if (selected.empty()) return;
    Scene* scene = ctx->GetActiveScene();
    std::unordered_set<entt::entity> ignored(selected.begin(), selected.end());

    EditorUndoManager::PushSnapshot(ctx, "Drop selection to ground");
    bool changed = false;
    for (entt::entity handle : selected) {
        Entity entity(handle, scene);
        if (!entity || !entity.HasComponent<MeshRendererComponent>()) continue;
        const auto& renderer = entity.GetComponent<MeshRendererComponent>();
        const Mat4 world = scene->GetWorldTransform(entity);
        const AABB bounds = SceneAuthoring::TransformBounds(renderer.localBounds, world);
        const Ray ray(Vec3(bounds.Center().x, bounds.min.y - 0.001f, bounds.Center().z),
                      Vec3(0, -1, 0));
        SceneAuthoring::SceneRaycastHit hit;
        if (!SceneAuthoring::RaycastBounds(*scene, ray, hit, ignored)) continue;
        Mat4 droppedWorld = world;
        droppedWorld.m[13] = hit.point.y + (world.m[13] - bounds.min.y);
        Entity parent = scene->GetParent(entity);
        const Mat4 local = parent
            ? Mat4::Multiply(Mat4::Invert(scene->GetWorldTransform(parent)), droppedWorld)
            : droppedWorld;
        entity.GetComponent<TransformComponent>().SetFromMatrix(local);
        changed = true;
    }
    if (changed) ctx->MarkDirty();
    else EditorUndoManager::DiscardLastSnapshot(ctx);
}

void EditorMainWindow::createEmptyParentForSelection()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    const auto selected = ctx->GetSelectedEntities();
    if (selected.empty()) return;
    Scene* scene = ctx->GetActiveScene();
    Vec3 center(0, 0, 0);
    for (entt::entity handle : selected) {
        const Mat4 world = scene->GetWorldTransform(Entity(handle, scene));
        center = vadd(center, Vec3(world.m[12], world.m[13], world.m[14]));
    }
    center = vscale(center, 1.0f / static_cast<float>(selected.size()));

    EditorUndoManager::PushSnapshot(ctx, "Create empty parent");
    Entity parent = scene->CreateEntity("Group");
    parent.GetComponent<TransformComponent>().position = center;
    for (entt::entity handle : selected) {
        Entity child(handle, scene);
        if (child && child != parent) scene->SetParent(child, parent, true);
    }
    ctx->SelectEntity(static_cast<entt::entity>(parent));
    ctx->MarkDirty();
    rebuildOutliner();
}

void EditorMainWindow::showViewportContextMenu(float x, float y)
{
    if (!m_engine) return;
    QMenu menu(this);
    auto* ctx = m_engine->GetEditorContext();
    const bool hasSelection = ctx && ctx->HasSelection();
    QAction* focus = menu.addAction(tr("Focus Selected"), [this]() {
        if (m_engine) m_engine->FocusOnSelection();
    });
    focus->setEnabled(hasSelection);
    QAction* duplicate = menu.addAction(tr("Duplicate"), this,
                                        &EditorMainWindow::onDuplicateEntity);
    duplicate->setEnabled(hasSelection);
    QAction* remove = menu.addAction(tr("Delete"), this,
                                     &EditorMainWindow::deleteSelectedEntities);
    remove->setEnabled(hasSelection);
    menu.addSeparator();
    QAction* hide = menu.addAction(tr("Hide Selected"), this,
                                   &EditorMainWindow::hideSelectedInEditor);
    hide->setEnabled(hasSelection);
    QAction* isolate = menu.addAction(tr("Isolate Selected"), this,
                                      &EditorMainWindow::isolateSelectedInEditor);
    isolate->setEnabled(hasSelection);
    QAction* drop = menu.addAction(tr("Drop to Ground"), this,
                                   &EditorMainWindow::dropSelectionToGround);
    drop->setEnabled(hasSelection);
    QAction* group = menu.addAction(tr("Create Empty Parent"), this,
                                    &EditorMainWindow::createEmptyParentForSelection);
    group->setEnabled(hasSelection);

    QMenu* create = menu.addMenu(tr("Create"));
    const QPoint dropPoint(static_cast<int>(x), static_cast<int>(y));
    auto addSpawn = [this, create, dropPoint](
                        const QString& label, SpawnableEntry entry) {
        create->addAction(label, [this, entry, dropPoint]() {
            handleViewportSpawnDrop(entry.toMimePayload(), dropPoint);
        });
    };
    for (const QString& primitive : {QStringLiteral("Cube"),
                                     QStringLiteral("Sphere"),
                                     QStringLiteral("Plane")}) {
        SpawnableEntry entry;
        entry.kind = SpawnableEntry::Kind::Mesh;
        entry.displayName = primitive;
        entry.assetPath = QStringLiteral(".engine/meshes/") + primitive +
                          QStringLiteral(".glb");
        addSpawn(primitive, entry);
    }
    {
        SpawnableEntry entry;
        entry.kind = SpawnableEntry::Kind::Mesh;
        entry.displayName = tr("Skinned Rest Fixture");
        entry.assetPath = QStringLiteral("builtin://skinned_rest_fixture");
        addSpawn(entry.displayName, entry);
    }
    for (const auto& light : {
             std::pair<QString, QString>{tr("Directional Light"), "DirectionalLight"},
             std::pair<QString, QString>{tr("Point Light"), "PointLight"},
             std::pair<QString, QString>{tr("Spot Light"), "SpotLight"}}) {
        SpawnableEntry entry;
        entry.kind = SpawnableEntry::Kind::Light;
        entry.displayName = light.first;
        entry.typeId = light.second;
        addSpawn(light.first, entry);
    }
    SpawnableEntry camera;
    camera.kind = SpawnableEntry::Kind::Camera;
    camera.displayName = tr("Camera");
    addSpawn(tr("Camera"), camera);

    create->addAction(tr("Empty Entity"), [this, dropPoint]() {
        if (!m_engine) return;
        auto* context = m_engine->GetEditorContext();
        if (!context || !context->HasActiveScene()) return;
        EditorUndoManager::PushSnapshot(context, "Create empty entity");
        Entity entity = context->GetActiveScene()->CreateEntity("Entity");
        SceneAuthoring::SceneRaycastHit hit;
        if (m_engine->RaycastViewport(dropPoint.x(), dropPoint.y(), hit)) {
            entity.GetComponent<TransformComponent>().position = hit.point;
        }
        context->SelectEntity(static_cast<entt::entity>(entity));
        context->MarkDirty();
        rebuildOutliner();
    });
    create->addAction(tr("Audio Source"), [this, dropPoint]() {
        if (!m_engine) return;
        auto* context = m_engine->GetEditorContext();
        if (!context || !context->HasActiveScene()) return;
        EditorUndoManager::PushSnapshot(context, "Create audio source");
        Entity entity = context->GetActiveScene()->CreateEntity("Audio Source");
        entity.AddComponent<AudioSourceComponent>();
        SceneAuthoring::SceneRaycastHit hit;
        if (m_engine->RaycastViewport(dropPoint.x(), dropPoint.y(), hit)) {
            entity.GetComponent<TransformComponent>().position = hit.point;
        }
        context->SelectEntity(static_cast<entt::entity>(entity));
        context->MarkDirty();
        rebuildOutliner();
    });
    if (m_pluginHost) m_pluginHost->PopulateContextMenu("viewport", menu);
    menu.exec(QCursor::pos());
}

void EditorMainWindow::replaceSelectionWithAsset(const QByteArray& spawnPayload)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    const auto selected = ctx->GetSelectedEntities();
    if (selected.empty()) return;
    const SpawnableEntry entry = SpawnableEntry::fromMimePayload(spawnPayload);
    Scene* scene = ctx->GetActiveScene();

    if (entry.kind == SpawnableEntry::Kind::Mesh) {
        EditorUndoManager::PushSnapshot(ctx, "Replace selected mesh");
        for (entt::entity handle : selected) {
            Entity entity(handle, scene);
            if (!entity) continue;
            auto& renderer = entity.HasComponent<MeshRendererComponent>()
                ? entity.GetComponent<MeshRendererComponent>()
                : entity.AddComponent<MeshRendererComponent>();
            renderer.meshAssetPath = entry.assetPath.toStdString();
            renderer.meshHandle = MeshRendererComponent::InvalidMesh;
            renderer.meshImported = true;
        }
        m_engine->RebuildSceneMeshes();
        ctx->MarkDirty();
        refreshDetailsPanel();
        return;
    }
    if (entry.kind != SpawnableEntry::Kind::Prefab) return;

    bool hasUniqueData = false;
    for (entt::entity handle : selected) {
        Entity entity(handle, scene);
        hasUniqueData = hasUniqueData ||
            (entity && (entity.HasComponent<LuaScriptComponent>() ||
                        entity.HasComponent<LightComponent>() ||
                        entity.HasComponent<AudioSourceComponent>()));
    }
    if (hasUniqueData && QMessageBox::question(
            this, tr("Replace Selected"),
            tr("Some selected entities contain scripts, lights, or audio. "
               "Replacing them with a prefab will remove that unique data. Continue?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    PrefabAsset asset;
    std::string error;
    if (!PrefabOverrideSystem::LoadAsset(entry.assetPath.toStdString(), asset, error)) {
        QMessageBox::warning(this, tr("Replace Selected"),
                             QString::fromStdString(error));
        return;
    }
    EditorUndoManager::PushSnapshot(ctx, "Replace selected with prefab");
    std::vector<entt::entity> replacements;
    for (entt::entity handle : selected) {
        Entity source(handle, scene);
        if (!source) continue;
        const TransformComponent sourceTransform =
            source.GetComponent<TransformComponent>();
        const std::string sourceName = source.GetComponent<TagComponent>().tag;
        Entity parent = scene->GetParent(source);

        PrefabInstantiateOptions options;
        options.prefabPath = entry.assetPath.toStdString();
        options.parent = parent ? static_cast<entt::entity>(parent) : entt::null;
        options.attachInstanceComponent = true;
        Entity replacement = PrefabInstantiator::Instantiate(*scene, asset, options);
        if (!replacement) continue;
        PrefabComponentIO::CopyLocalTransform(
            replacement.GetComponent<TransformComponent>(), sourceTransform);
        if (source.HasComponent<PrefabInstanceComponent>())
            PrefabInstantiator::DestroyInstance(*scene, source);
        else
            scene->DestroyEntity(source);
        replacement.GetComponent<TagComponent>().tag = sourceName;
        if (replacement.HasComponent<PrefabInstanceComponent>()) {
            auto& instance = replacement.GetComponent<PrefabInstanceComponent>();
            PrefabComponentIO::CopyLocalTransform(
                instance.rootTransform,
                replacement.GetComponent<TransformComponent>());
            instance.hasRootTransform = true;
        }
        replacements.push_back(static_cast<entt::entity>(replacement));
    }
    if (!replacements.empty()) {
        ctx->SelectEntities(replacements, replacements.back());
        ctx->MarkDirty();
        m_engine->RebuildSceneMeshes();
        rebuildOutliner();
        refreshDetailsPanel();
    }
}

void EditorMainWindow::saveCameraBookmark(int slot)
{
    if (!m_engine || slot < 1 || slot > 9) return;
    auto* camera = m_engine->GetCameraController();
    if (!camera) return;
    EditorCameraBookmark bookmark;
    bookmark.positionX = camera->positionX;
    bookmark.positionY = camera->positionY;
    bookmark.positionZ = camera->positionZ;
    bookmark.yawDeg = camera->yawDeg;
    bookmark.pitchDeg = camera->pitchDeg;
    bookmark.orthographic = camera->orthographic;
    bookmark.orthoHalfExtent = camera->orthoHalfExtent;
    m_editorViewState.bookmarks[slot] = bookmark;
    saveEditorViewState();
    statusBar()->showMessage(tr("Saved camera bookmark %1").arg(slot), 2000);
}

void EditorMainWindow::recallCameraBookmark(int slot)
{
    if (!m_engine) return;
    const auto it = m_editorViewState.bookmarks.find(slot);
    if (it == m_editorViewState.bookmarks.end()) {
        statusBar()->showMessage(tr("Camera bookmark %1 is empty").arg(slot), 2000);
        return;
    }
    auto* camera = m_engine->GetCameraController();
    const EditorCameraBookmark& bookmark = it->second;
    camera->positionX = bookmark.positionX;
    camera->positionY = bookmark.positionY;
    camera->positionZ = bookmark.positionZ;
    camera->yawDeg = bookmark.yawDeg;
    camera->pitchDeg = bookmark.pitchDeg;
    camera->orthographic = bookmark.orthographic;
    camera->orthoHalfExtent = bookmark.orthoHalfExtent;
    if (m_viewPerspectiveCombo) {
        m_viewPerspectiveCombo->blockSignals(true);
        m_viewPerspectiveCombo->setCurrentIndex(bookmark.orthographic ? 1 : 0);
        m_viewPerspectiveCombo->blockSignals(false);
    }
    statusBar()->showMessage(tr("Recalled camera bookmark %1").arg(slot), 2000);
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

    if (m_projectButton) {
        m_projectButton->setText(m_projectContext && m_projectContext->HasProject()
            ? QString::fromStdString(m_projectContext->GetProjectName())
            : tr("No project"));
    }
    if (m_statusSaved && m_statusScene) {
        auto* ctx = m_engine ? m_engine->GetEditorContext() : nullptr;
        const bool hasScene = ctx && ctx->HasActiveScene();
        const bool dirty = hasScene && ctx->IsDirty();
        m_statusSaved->setText(dirty ? tr("Unsaved changes") : tr("Saved"));
        m_statusSaved->setIcon(dirty ? Theme::Icon(QStringLiteral("dot"), Theme::IconTone::Warning)
                                     : Theme::Icon(QStringLiteral("check"), Theme::IconTone::Success));
        m_statusSaved->setProperty("state", dirty ? "dirty" : "saved");
        m_statusSaved->style()->unpolish(m_statusSaved);
        m_statusSaved->style()->polish(m_statusSaved);
        m_statusSaved->setVisible(hasScene);
        QString scene = hasScene ? QString::fromStdString(ctx->GetActiveScene()->GetName()) : QString();
        if (hasScene && m_projectContext && m_projectContext->HasProject()) {
            scene = QString::fromStdString(m_projectContext->GetProjectName()) + QStringLiteral(" / ") + scene;
        }
        m_statusScene->setText(scene);
    }
    if (m_sceneTabs) {
        auto* ctx = m_engine ? m_engine->GetEditorContext() : nullptr;
        const bool hasScene = ctx && ctx->HasActiveScene();
        QString tab = hasScene ? QString::fromStdString(ctx->GetActiveScene()->GetName()) : tr("No scene");
        if (hasScene && !ctx->GetScenePath().empty()) {
            tab = QFileInfo(QString::fromStdString(ctx->GetScenePath())).fileName();
        }
        if (hasScene && ctx->IsDirty()) {
            tab += QStringLiteral("  \u25CF");
        }
        m_sceneTabs->setTabText(0, tab);
    }
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
    const QString skybox = m_projectSettingsManager
        ? m_projectSettingsManager->Value(QStringLiteral("project.rendering.defaultSkybox")).toString().trimmed()
        : QString();
    if (!skybox.isEmpty()) {
        Entity environment = ctx->GetActiveScene()->CreateEntity("Environment");
        auto& env = environment.AddComponent<EnvironmentComponent>();
        env.type = EnvironmentComponent::BackgroundType::HDRI;
        env.hdriPath = skybox.toStdString();
        rebuildOutliner();
    }
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
    // Play runs on the edited scene, so saving now would write the game's state into the level.
    if (m_engine->IsPlaying() || m_engine->IsPaused()) {
        statusBar()->showMessage(tr("Stop Play to save the level"), 3000);
        return;
    }
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
    if (m_engine->IsPlaying() || m_engine->IsPaused()) {
        statusBar()->showMessage(tr("Stop Play to save the level"), 3000);
        return;
    }
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
    auto* ctx = m_engine ? m_engine->GetEditorContext() : nullptr;
    if (ctx && ctx->IsDirty()) {
        m_recoveryBusy = true;
        auto result = QMessageBox::question(this, tr("Unsaved Changes"),
            tr("The current scene has unsaved changes. Do you want to save before continuing?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        m_recoveryBusy = false;
        if (result == QMessageBox::Save) {
            onSaveLevel();
            if (ctx->IsDirty()) return false;
        } else if (result == QMessageBox::Cancel) {
            return false;
        }
    }

    auto& documents = EditorAssetServices::Instance().Documents();
    for (const auto& document : documents.ListDirty()) {
        if (document.kind != DocumentKind::Plugin) continue;
        const auto result = QMessageBox::question(
            this, tr("Unsaved Plugin Asset"),
            tr("%1 has unsaved changes. Save it before continuing?")
                .arg(QString::fromStdString(document.portablePath)),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (result == QMessageBox::Cancel) return false;
        if (result == QMessageBox::Save) {
            std::string error;
            if (!documents.RequestSave(document.portablePath, &error)) {
                QMessageBox::warning(this, tr("Save Plugin Asset"),
                                     QString::fromStdString(error));
                return false;
            }
        }
    }
    return true;
}

void EditorMainWindow::onNewProject()
{
    if (!promptSaveDirtyScene() || !m_codeEditor->CloseAll()) {
        return;
    }

    QString startDir = QDir::homePath();
    if (m_projectContext && m_projectContext->HasProject()) {
        startDir = QString::fromStdString(m_projectContext->GetProjectRootDir());
        QFileInfo info(startDir);
        startDir = info.absolutePath();
    }

    QString location = QFileDialog::getExistingDirectory(this, tr("Choose Project Location"), startDir);
    if (location.isEmpty()) {
        return;
    }

    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Project"),
        tr("Project name:"), QLineEdit::Normal, tr("MyHephaestusProject"), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    name = name.trimmed();

    Hephaestus::Tools::ProjectCreationService::Result created;
    QString error;
    if (!Hephaestus::Tools::ProjectCreationService::CreateBlankProject(
            location, name, Hephaestus::Tools::VersionInfo::EngineVersion(), true, &created, &error)) {
        QMessageBox::critical(this, tr("New Project"),
            error.isEmpty() ? tr("Failed to create project.") : error);
        return;
    }

    if (!openProjectFile(created.projectFilePath)) {
        QMessageBox::warning(this, tr("New Project"),
            tr("Project was created but failed to open:\n%1").arg(created.projectFilePath));
        return;
    }

    if (m_projectSettingsManager && !created.defaultSceneRelativePath.isEmpty()) {
        Hephaestus::Tools::ProjectCreationService::SetLastSceneRelative(
            *m_projectSettingsManager, created.defaultSceneRelativePath);
    }

    statusBar()->showMessage(tr("Created project: %1").arg(name), 4000);
}

void EditorMainWindow::onOpenProject()
{
    // Prompt to save dirty scene
    if (!promptSaveDirtyScene() || !m_codeEditor->CloseAll()) {
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

    openProjectFile(filepath);
}

void EditorMainWindow::onCloseProject()
{
    if (!m_projectContext || !m_projectContext->HasProject()) {
        return;
    }

    // Prompt to save dirty scene
    if (!promptSaveDirtyScene() || !m_codeEditor->CloseAll()) {
        return;
    }

    stopRecoverySession();

    if (m_characterEditorManager) {
        m_characterEditorManager->CloseAll();
    }
    if (m_prefabEditorManager) {
        m_prefabEditorManager->CloseAll();
    }

    // Close the project
    if (m_assetReimportTimer) {
        m_assetReimportTimer->stop();
    }
    if (m_assetPipeline) {
        m_assetPipeline->Shutdown();
    }
    m_projectContext->CloseProject();
    m_projectSettingsManager.reset();
    applyPlayPhysicsSetting();
    EditorAssetServices::Instance().OnProjectClosed();

    if (m_pluginSystem) {
        m_pluginSystem->Discover(Hephaestus::Tools::PathUtils::ExecutableDir().toStdString(), {});
        m_pluginSystem->LoadEnabled();
    }

    // Clear the scene
    if (m_engine) {
        if (auto* frameRenderer = m_engine->GetFrameRenderer()) {
            frameRenderer->SetProjectRoot(std::string());
        }
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
    if (!promptSaveDirtyScene() || !m_codeEditor->CloseAll()) {
        return;
    }

    openProjectFile(projectPath);
}

bool EditorMainWindow::openProjectFile(const QString& filepath)
{
    if (filepath.isEmpty()) {
        return false;
    }

    if (!std::filesystem::exists(filepath.toStdString())) {
        QMessageBox::warning(this, tr("Project Not Found"),
            tr("The project file no longer exists:\n%1").arg(filepath));
        return false;
    }

    std::string error;
    if (!m_projectContext->LoadProject(filepath.toStdString(), error)) {
        QMessageBox::critical(this, tr("Open Project Failed"),
            tr("Failed to open project:\n%1").arg(QString::fromStdString(error)));
        return false;
    }

    m_projectSettingsManager = std::make_unique<Hephaestus::Tools::ProjectSettingsManager>(
        QString::fromStdString(m_projectContext->GetProjectRootDir()));
    m_projectSettingsManager->Load();
    applyPlayPhysicsSetting();
    const QString newerEngineVersion =
        m_projectSettingsManager->RecordEngineVersion(Hephaestus::Tools::VersionInfo::EngineVersion());
    if (!newerEngineVersion.isEmpty()) {
        QMessageBox::warning(this, tr("Project From a Newer Version"),
            tr("%1 has been opened with Hephaestus %2, which is newer than this editor (%3).\n\n"
               "It may use features this version doesn't support, and saving it here could lose "
               "that work. Update Hephaestus to %2 or later to edit it safely.")
                .arg(QString::fromStdString(m_projectContext->GetProjectName()), newerEngineVersion,
                     Hephaestus::Tools::VersionInfo::EngineVersion()));
    }
    {
        const QString projectMode = m_projectSettingsManager->EditorMode();
        if (projectMode.isEmpty()) {
            m_projectSettingsManager->SetEditorMode(QLatin1String(kEditorModeNames[static_cast<int>(m_mode)]));
        }
        for (int i = 0; i < kEditorModeCount; ++i) {
            if (projectMode == QLatin1String(kEditorModeNames[i]) && i != static_cast<int>(m_mode)) {
                m_modeButtons->button(i)->setChecked(true);
                setEditorMode(static_cast<EditorMode>(i));
            }
        }
    }

    if (m_assetPipeline) {
        m_assetPipeline->Initialize(m_projectContext->GetProjectRootDir());
    }
    if (m_assetReimportTimer) {
        m_assetReimportTimer->start();
    }

    if (m_engine) {
        if (auto* frameRenderer = m_engine->GetFrameRenderer()) {
            frameRenderer->SetProjectRoot(m_projectContext->GetProjectRootDir());
        }
    }

    statusBar()->showMessage(tr("Project opened: %1").arg(
        QString::fromStdString(m_projectContext->GetProjectName())), 5000);

    EditorAssetServices::Instance().OnProjectOpened(m_projectContext->GetProjectRootDir());

    if (m_pluginSystem) {
        m_pluginSystem->Discover(Hephaestus::Tools::PathUtils::ExecutableDir().toStdString(),
                                 m_projectContext->GetProjectRootDir());
        m_pluginSystem->LoadEnabled();
    }

    updateWindowTitle();
    refreshAssetBrowser();
    rebuildRecentProjectsMenu();

    // Refresh the Add Actors panel so it picks up .engine/meshes from this project
    if (m_addActorsPanel && m_projectContext->HasProject()) {
        m_addActorsPanel->SetProjectRoot(m_projectContext->GetProjectRootDir());
        m_addActorsPanel->Refresh();
    }

    if (m_engine) {
        // Set the project root on the engine so RebuildSceneMeshes can
        // resolve relative mesh paths without re-discovering it from the
        // scene file path.  This is critical when opening projects that
        // were created on another device.
        m_engine->SetProjectRoot(m_projectContext->GetProjectRootDir());

        auto* ctx = m_engine->GetEditorContext();
        if (ctx) {
            const std::string projectRoot = m_projectContext->GetProjectRootDir();

            auto tryLoadScene = [&](const QString& configuredPath) {
                const QString trimmed = configuredPath.trimmed();
                if (trimmed.isEmpty()) {
                    return false;
                }

                std::filesystem::path scenePath(trimmed.toStdString());
                if (scenePath.is_relative()) {
                    scenePath = std::filesystem::path(projectRoot) / scenePath;
                }

                if (!std::filesystem::exists(scenePath)) {
                    return false;
                }

                if (!ctx->LoadScene(scenePath.string())) {
                    return false;
                }

                m_engine->RebuildSceneMeshes();
                return true;
            };

            bool loadedScene = false;
            const QString defaultScene = m_projectSettingsManager
                ? m_projectSettingsManager->Value(QStringLiteral("project.info.defaultScene")).toString()
                : QString();

            if (!defaultScene.trimmed().isEmpty()) {
                loadedScene = tryLoadScene(defaultScene);
                if (!loadedScene) {
                    statusBar()->showMessage(tr("Default Scene could not be loaded. Falling back to last scene."), 4000);
                }
            }

            if (!loadedScene) {
                const std::string& lastScene = m_projectContext->GetLastScene();
                if (!lastScene.empty()) {
                    loadedScene = tryLoadScene(QString::fromStdString(lastScene));
                }
            }
        }
    }

    // Begin recovery session after a successful project open.
    if (m_recoveryStore.HasActiveSession()) {
        m_recoveryStore.EndSessionClean();
    }
    if (m_recoveryStore.BeginSession(m_projectContext->GetProjectRootDir())) {
        startRecoverySessionTimers();
        if (m_recoveryStore.HasPendingRecovery()) {
            promptPendingRecovery();
        }
    } else {
        if (m_recoveryHeartbeatTimer) {
            m_recoveryHeartbeatTimer->stop();
        }
        if (m_autosaveTimer) {
            m_autosaveTimer->stop();
        }
    }

    return true;
}

void EditorMainWindow::pollAssetReimports()
{
    if (!m_assetPipeline || !m_assetPipeline->IsInitialized()) {
        return;
    }

    const auto completed = m_assetPipeline->DrainCompleted();
    if (completed.empty()) {
        return;
    }

    bool anyMesh = false;
    bool anySuccess = false;
    for (const auto& item : completed) {
        if (item.result.success) {
            anySuccess = true;
        }
        if (item.assetType == AssetType::StaticMesh) {
            anyMesh = true;
            const QString name = QString::fromStdString(item.sourcePath.filename().string());
            if (item.result.success) {
                statusBar()->showMessage(tr("Mesh reimported: %1").arg(name), 4000);
            } else {
                statusBar()->showMessage(
                    tr("Mesh reimport failed: %1 — %2")
                        .arg(name, QString::fromStdString(item.result.message)),
                    5000);
            }
        } else if (!item.result.success) {
            statusBar()->showMessage(
                tr("Reimport failed: %1")
                    .arg(QString::fromStdString(item.result.message)),
                4000);
        }
    }

    if (anySuccess) {
        AssetManager::GetInstance().Refresh();
        if (m_assetBrowser) {
            m_assetBrowser->Refresh();
        }
    }

    // Full GPU mesh reload is best-effort; refresh + status covers the common case.
    if (anyMesh && anySuccess && m_engine) {
        m_engine->RebuildSceneMeshes();
    }
}

void EditorMainWindow::setupRecoveryTimers()
{
    if (!m_recoveryHeartbeatTimer) {
        m_recoveryHeartbeatTimer = new QTimer(this);
        connect(m_recoveryHeartbeatTimer, &QTimer::timeout,
                this, &EditorMainWindow::onRecoveryHeartbeat);
    }
    if (!m_autosaveTimer) {
        m_autosaveTimer = new QTimer(this);
        connect(m_autosaveTimer, &QTimer::timeout,
                this, &EditorMainWindow::onAutosaveTick);
    }
}

void EditorMainWindow::startRecoverySessionTimers()
{
    setupRecoveryTimers();
    if (m_recoveryHeartbeatTimer) {
        m_recoveryHeartbeatTimer->start(30000);
    }
    if (m_autosaveTimer) {
        m_autosaveTimer->start(autosaveIntervalSeconds() * 1000);
    }
}

void EditorMainWindow::stopRecoverySession()
{
    if (m_recoveryHeartbeatTimer) {
        m_recoveryHeartbeatTimer->stop();
    }
    if (m_autosaveTimer) {
        m_autosaveTimer->stop();
    }
    if (m_recoveryStore.HasActiveSession()) {
        m_recoveryStore.EndSessionClean();
    }
}

void EditorMainWindow::applyPlayPhysicsSetting()
{
    if (!m_engine) return;
    m_engine->SetPlayPhysicsEnabled(!m_projectSettingsManager ||
        m_projectSettingsManager->Value(QStringLiteral("project.rendering.physicsEnabled")).toBool());
}

int EditorMainWindow::autosaveIntervalSeconds() const
{
    int interval = m_engineSettingsManager.Value(
        QStringLiteral("engine.general.autosaveInterval")).toInt();
    if (interval <= 0) {
        interval = 120;
    }
    return interval;
}

void EditorMainWindow::onRecoveryHeartbeat()
{
    if (!m_recoveryStore.HasActiveSession()) {
        return;
    }
    m_recoveryStore.Heartbeat();
}

void EditorMainWindow::onAutosaveTick()
{
    writeRecoverySnapshot();

    if (m_autosaveTimer) {
        const int ms = autosaveIntervalSeconds() * 1000;
        if (m_autosaveTimer->interval() != ms) {
            m_autosaveTimer->setInterval(ms);
        }
    }
}

void EditorMainWindow::writeRecoverySnapshot()
{
    if (m_recoveryBusy || !m_recoveryStore.HasActiveSession()) {
        return;
    }
    if (!m_engine || m_engine->IsPlaying()) {
        return;
    }

    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->IsDirty() || !ctx->HasActiveScene()) {
        return;
    }

    Scene* scene = ctx->GetActiveScene();
    if (!scene) {
        return;
    }

    SceneSerializer serializer(*scene);
    const std::string content = serializer.SerializeToString();
    if (content.empty()) {
        return;
    }

    const std::string& scenePath = ctx->GetScenePath();
    const std::string documentId = scenePath.empty() ? "unsaved_scene" : scenePath;
    const std::string id = m_recoveryStore.WriteSceneSnapshot(documentId, scenePath, content);
    if (id.empty()) {
        return;
    }

    m_recoveryStore.Prune(5);
    if (m_notifications) {
        m_notifications->ShowInfo(tr("Recovery snapshot saved"));
    }
}

void EditorMainWindow::promptPendingRecovery()
{
    const auto candidates = m_recoveryStore.ListRecoveryCandidates();
    if (candidates.empty()) {
        m_recoveryStore.DiscardAll();
        return;
    }

    QStringList lines;
    lines.reserve(static_cast<int>(candidates.size()));
    for (const auto& candidate : candidates) {
        const QString source = candidate.sourcePath.empty()
            ? QString::fromStdString(candidate.documentId)
            : QString::fromStdString(candidate.sourcePath);
        lines << tr("%1 — %2")
                    .arg(source, QString::fromStdString(candidate.timestamp));
    }

    m_recoveryBusy = true;
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Recover Unsaved Work"));
    box.setText(tr("The previous editor session ended unexpectedly. "
                   "Reopen the newest snapshot's scene with the recovered changes "
                   "(nothing is written to disk until you save)?"));
    box.setInformativeText(lines.join(QLatin1Char('\n')));
    auto* restoreBtn = box.addButton(tr("Restore"), QMessageBox::AcceptRole);
    auto* discardBtn = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.setDefaultButton(restoreBtn);
    box.exec();
    m_recoveryBusy = false;

    if (box.clickedButton() == discardBtn) {
        m_recoveryStore.DiscardAll();
        if (m_notifications) {
            m_notifications->ShowInfo(tr("Recovery snapshots discarded"));
        }
        return;
    }

    if (box.clickedButton() != restoreBtn) {
        return;
    }

    // Prefer newest snapshot (WriteSceneSnapshot appends).
    const RecoveryCandidate& chosen = candidates.back();
    const auto content = m_recoveryStore.LoadSnapshotContent(chosen.id);
    if (!content) {
        if (m_notifications) {
            m_notifications->ShowError(tr("Failed to load recovery snapshot"));
        }
        return;
    }

    if (!m_engine) {
        return;
    }
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) {
        return;
    }
    bool openedSource = false;
    std::string restoreError;
    if (!ctx->RestoreSceneSnapshot(chosen.sourcePath, *content, openedSource, restoreError)) {
        if (m_notifications) {
            m_notifications->ShowError(tr("Failed to restore recovery snapshot: %1")
                                           .arg(QString::fromStdString(restoreError)));
        }
        return;
    }

    m_engine->RebuildSceneMeshes();
    m_recoveryStore.DiscardAll();
    updateWindowTitle();
    rebuildOutliner();
    if (m_notifications) {
        if (openedSource) {
            m_notifications->ShowInfo(tr("Recovery snapshot restored into %1 (unsaved)")
                                          .arg(QString::fromStdString(chosen.sourcePath)));
        } else {
            m_notifications->ShowWarning(tr("The snapshot's scene file is missing; restored into a new unsaved scene"));
        }
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

    // Drop missing entries so the menu stays accurate
    m_projectContext->RemoveMissingRecentProjects();

    const auto& recentProjects = m_projectContext->GetRecentProjects();
    
    if (recentProjects.empty()) {
        QAction* emptyAction = m_recentProjectsMenu->addAction(tr("(No Recent Projects)"));
        emptyAction->setEnabled(false);
    } else {
        for (const auto& projectPath : recentProjects) {
            std::filesystem::path path(projectPath);
            QString displayName = QString::fromStdString(path.parent_path().filename().string());
            if (displayName.isEmpty()) {
                displayName = QString::fromStdString(path.filename().string());
            }
            
            QMenu* projectMenu = m_recentProjectsMenu->addMenu(displayName);
            projectMenu->setToolTip(QString::fromStdString(projectPath));

            QAction* openAction = projectMenu->addAction(tr("Open"));
            openAction->setData(QString::fromStdString(projectPath));
            connect(openAction, &QAction::triggered, this, &EditorMainWindow::onOpenRecentProject);

            QAction* revealAction = projectMenu->addAction(tr("Show in Explorer"));
            connect(revealAction, &QAction::triggered, this, [projectPath]() {
                const QString native = QDir::toNativeSeparators(QString::fromStdString(projectPath));
#ifdef _WIN32
                QProcess::startDetached("explorer", {"/select,", native});
#else
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(native).absolutePath()));
#endif
            });

            QAction* removeAction = projectMenu->addAction(tr("Remove from List"));
            connect(removeAction, &QAction::triggered, this, [this, projectPath]() {
                m_projectContext->RemoveRecentProject(projectPath);
                rebuildRecentProjectsMenu();
                statusBar()->showMessage(tr("Removed from recent projects"), 2000);
            });
        }
        
        m_recentProjectsMenu->addSeparator();

        QAction* pruneAction = m_recentProjectsMenu->addAction(tr("Remove Missing Projects"));
        connect(pruneAction, &QAction::triggered, this, [this]() {
            const size_t removed = m_projectContext->RemoveMissingRecentProjects();
            rebuildRecentProjectsMenu();
            statusBar()->showMessage(tr("Removed %1 missing project(s)").arg(static_cast<qulonglong>(removed)), 2500);
        });
        
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
        if (m_codeEditor) m_codeEditor->RestoreSession();

        if (m_settingsWindow) {
            const bool hasProject = m_projectContext && m_projectContext->HasProject();
            const QString root = hasProject
                ? QString::fromStdString(m_projectContext->GetProjectRootDir())
                : QString();
            m_settingsWindow->ConfigureContext(Hephaestus::EngineContext::Editor, hasProject, root);
        }
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

        m_assetActions->SetOpenPrefabCallback([this](const QString& absolutePath) -> bool {
            if (!m_prefabEditorManager) return false;
            auto* window = m_prefabEditorManager->OpenPrefab(absolutePath);
            return window != nullptr;
        });

        m_assetActions->SetOpenScriptCallback([this](const QString& absolutePath) -> bool {
            if (m_mode != EditorMode::Programmer) return false;
            m_codeEditorDock->show();
            m_codeEditorDock->raise();
            m_codeEditor->OpenFile(absolutePath);
            return true;
        });

        m_assetActions->SetOpenCharacterCallback([this](const QString& absolutePath) -> bool {
            if (!m_characterEditorManager) return false;
            auto* window = m_characterEditorManager->OpenCharacter(absolutePath);
            return window != nullptr;
        });
    }

    // Wire up prefab-saved notifications to refresh scene instances
    if (m_prefabEditorManager) {
        m_prefabEditorManager->SetPrefabSavedNotification([this](const std::filesystem::path& prefabPath) {
            if (!m_engine) return;
            auto* ctx = m_engine->GetEditorContext();
            if (!ctx) return;
            Scene* scene = ctx->GetActiveScene();
            if (!scene) return;

            PrefabOverrideSystem::InvalidateAssetCache(prefabPath.generic_string());
            const int refreshed = PrefabOverrideSystem::RefreshInstancesOfPrefab(*scene, prefabPath.generic_string());
            if (refreshed > 0) {
                m_engine->RebuildSceneMeshes();
                rebuildOutliner();
                refreshDetailsPanel();
                statusBar()->showMessage(
                    tr("Refreshed %1 prefab instance(s)").arg(refreshed), 3000);
            }
        });
    }

    if (m_characterEditorManager) {
        m_characterEditorManager->SetCharacterSavedNotification(
            [this](const std::filesystem::path& characterAbs) {
                if (!m_engine) return;
                auto* ctx = m_engine->GetEditorContext();
                if (!ctx) return;
                Scene* scene = ctx->GetActiveScene();
                if (!scene) return;

                std::filesystem::path projectRoot;
                if (m_projectContext && m_projectContext->HasProject()) {
                    projectRoot = m_projectContext->GetProjectRootDir();
                }
                const std::string portable = projectRoot.empty()
                    ? characterAbs.generic_string()
                    : Hephaestus::AssetPathPolicy::ToPortableKey(projectRoot,
                                                                 characterAbs.generic_string());
                Hephaestus::CharacterRuntime::Invalidate(portable);
                if (auto* renderer = m_engine->GetFrameRenderer()) {
                    Hephaestus::CharacterRuntime::InvalidateGpuMeshes(renderer, portable);
                }
                rebuildSceneVisuals();
                refreshDetailsPanel();
                statusBar()->showMessage(tr("Character asset saved. Live instances refreshed."), 3000);
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
        m_engine->RebuildSceneMeshes();
        rebuildOutliner();
        refreshDetailsPanel();
        statusBar()->showMessage(tr("Undo"), 2000);
    } else {
        QMessageBox::warning(this, tr("Undo"), QString::fromStdString(error));
    }
    updateUndoRedoActions();
}

void EditorMainWindow::onRedo()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx) return;

    std::string error;
    if (EditorUndoManager::Redo(ctx, error)) {
        m_engine->RebuildSceneMeshes();
        rebuildOutliner();
        refreshDetailsPanel();
        statusBar()->showMessage(tr("Redo"), 2000);
    } else {
        statusBar()->showMessage(QString::fromStdString("Redo: " + error), 2000);
    }
    updateUndoRedoActions();
}

void EditorMainWindow::onCopyEntity()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene() || !ctx->HasSelection()) return;

    Scene* scene = ctx->GetActiveScene();
    Entity selected(ctx->GetSelectedEntity(), scene);
    if (selected) {
        scene->CopyEntityToClipboard(selected);
        statusBar()->showMessage(tr("Entity copied"), 2000);
    }
}

void EditorMainWindow::onPasteEntity()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;

    Scene* scene = ctx->GetActiveScene();
    if (!scene->HasClipboardEntity()) {
        statusBar()->showMessage(tr("Nothing to paste"), 2000);
        return;
    }

    EditorUndoManager::PushSnapshot(ctx, "Paste entity");
    Entity pasted = scene->PasteClipboard();
    if (pasted) {
        ctx->SelectEntity(pasted);
        ctx->MarkDirty();
        rebuildOutliner();
        refreshDetailsPanel();
        statusBar()->showMessage(tr("Entity pasted"), 2000);
    }
}

void EditorMainWindow::onDuplicateEntity()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene() || !ctx->HasSelection()) return;

    Scene* scene = ctx->GetActiveScene();
    EditorUndoManager::PushSnapshot(ctx, "Duplicate entity");
    const std::vector<Entity> duplicates =
        scene->DuplicateEntities(ctx->GetSelectedEntities(), true);
    if (!duplicates.empty()) {
        std::vector<entt::entity> handles;
        handles.reserve(duplicates.size());
        for (const Entity& duplicate : duplicates) {
            handles.push_back(static_cast<entt::entity>(duplicate));
        }
        ctx->SelectEntities(handles, handles.back());
        ctx->MarkDirty();
        rebuildOutliner();
        refreshDetailsPanel();
        statusBar()->showMessage(
            tr("%1 entit%2 duplicated")
                .arg(static_cast<int>(duplicates.size()))
                .arg(duplicates.size() == 1 ? tr("y") : tr("ies")),
            2000);
    }
}

void EditorMainWindow::onOpenSettings()
{
    const bool hasProject = m_projectContext && m_projectContext->HasProject();
    const QString projectRoot = hasProject
        ? QString::fromStdString(m_projectContext->GetProjectRootDir())
        : QString();

    if (!m_settingsWindow) {
        m_settingsWindow = new Hephaestus::UI::SettingsWindow(this);
        connect(m_settingsWindow, &QObject::destroyed, this, [this]() {
            m_settingsWindow = nullptr;
        });
        connect(m_settingsWindow, &Hephaestus::UI::SettingsWindow::settingsApplied,
                this, [this]() {
            m_engineSettingsManager.Load();
            if (m_engine) {
                m_engine->SetGizmoSize(
                    m_engineSettingsManager.Value(QStringLiteral("engine.editor.gizmoSize")).toFloat());
            }
            if (m_projectSettingsManager) {
                m_projectSettingsManager->Load();
            }
            applyPlayPhysicsSetting();
        });
    }

    m_settingsWindow->ConfigureContext(Hephaestus::EngineContext::Editor, hasProject, projectRoot);
    m_settingsWindow->show();
    m_settingsWindow->raise();
    m_settingsWindow->activateWindow();
}

void EditorMainWindow::onOpenPlugins()
{
    if (!m_pluginSystem) return;
    PluginManagerDialog dialog(*m_pluginSystem, this);
    dialog.exec();
}

// ─────────────────────────────────────────────────────────────────────────────
// Help Menu Slots
// ─────────────────────────────────────────────────────────────────────────────

void EditorMainWindow::onAbout()
{
    AboutDialog dlg(this);
    dlg.exec();
}

// ─────────────────────────────────────────────────────────────────────────────
// Build
// ─────────────────────────────────────────────────────────────────────────────

void EditorMainWindow::onBuildProject()
{
    startBuild(Build::BuildAction::Package);
#if 0 // Replaced by the shared Stage 6 BuildService; retained temporarily for migration reference.
    // Don't start another build while one is running
    if (m_buildProcess && m_buildProcess->state() != QProcess::NotRunning) {
        statusBar()->showMessage(tr("Build already in progress..."), 3000);
        m_buildOutputDock->raise();
        return;
    }

    // Determine the engine root directory (the folder containing the build/ directory).
    // The running executable lives in build/Release/ (or build/Debug/), so go up two levels.
    QString appDir = Hephaestus::Tools::PathUtils::ExecutableDir();
    QDir engineRoot(appDir);
    engineRoot.cdUp(); // build/Release -> build/
    engineRoot.cdUp(); // build/ -> engine root

    QString buildDir = engineRoot.absoluteFilePath("build");
    if (!QDir(buildDir).exists()) {
        QMessageBox::warning(this, tr("Build Error"),
            tr("Cannot find build directory at:\n%1\n\n"
               "Make sure CMake has been configured first.").arg(buildDir));
        return;
    }

    // Resolve project name and path
    QString projectName;
    QString projectPath;
    if (m_projectContext && m_projectContext->HasProject()) {
        projectName = QString::fromStdString(m_projectContext->GetProjectName());
        projectPath = QString::fromStdString(m_projectContext->GetProjectRootDir());
    }
    if (projectName.isEmpty()) {
        projectName = QStringLiteral("HephaestusGame");
    }
    if (projectPath.isEmpty()) {
        projectPath = engineRoot.absolutePath();
    }

    // Scan for .hpscene files in project and engine asset directories
    QStringList sceneFiles;
    auto scanDir = [&](const QString& rootDir) {
        QDir scenesDir(rootDir + "/assets/scenes");
        if (!scenesDir.exists()) return;
        for (const QFileInfo& fi : scenesDir.entryInfoList({"*.hpscene"}, QDir::Files)) {
            // Store as relative path: assets/scenes/Name.hpscene
            QString rel = "assets/scenes/" + fi.fileName();
            if (!sceneFiles.contains(rel))
                sceneFiles.append(rel);
        }
    };
    scanDir(projectPath);
    if (projectPath != engineRoot.absolutePath())
        scanDir(engineRoot.absolutePath());

    // Show Build Settings dialog
    BuildSettingsDialog dlg(projectName, sceneFiles, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_lastBuildSettings = dlg.GetSettings();

    // Show the build output dock
    m_buildOutputDock->show();
    m_buildOutputDock->raise();
    m_buildOutputText->clear();

    m_buildBtn->setEnabled(false);
    m_buildExportPhase = false; // Start with compile phase

    m_buildOutputText->appendPlainText(tr("=== Building game: %1 ===").arg(projectName));
    if (!m_lastBuildSettings.startScene.isEmpty()) {
        m_buildOutputText->appendPlainText(tr("Start scene: %1").arg(m_lastBuildSettings.startScene));
    }
    m_buildOutputText->appendPlainText(QString());
    statusBar()->showMessage(tr("Compiling %1...").arg(projectName));

    // Create the process (once, reused for both compile and export phases)
    if (!m_buildProcess) {
        m_buildProcess = new QProcess(this);

        connect(m_buildProcess, &QProcess::readyReadStandardOutput, this, [this]() {
            QByteArray data = m_buildProcess->readAllStandardOutput();
            m_buildOutputText->appendPlainText(QString::fromLocal8Bit(data));
        });

        connect(m_buildProcess, &QProcess::readyReadStandardError, this, [this]() {
            QByteArray data = m_buildProcess->readAllStandardError();
            m_buildOutputText->appendPlainText(QString::fromLocal8Bit(data));
        });

        connect(m_buildProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (exitStatus == QProcess::CrashExit || exitCode != 0) {
                    // Failure in either phase
                    m_buildBtn->setEnabled(true);
                    QString phase = m_buildExportPhase ? tr("Export") : tr("Compile");
                    if (exitStatus == QProcess::CrashExit) {
                        m_buildOutputText->appendPlainText(tr("\n=== %1 process crashed ===").arg(phase));
                        statusBar()->showMessage(tr("%1 crashed").arg(phase), 5000);
                    } else {
                        m_buildOutputText->appendPlainText(tr("\n=== %1 FAILED (exit code %2) ===").arg(phase).arg(exitCode));
                        statusBar()->showMessage(tr("%1 failed (exit code %2)").arg(phase).arg(exitCode), 5000);
                    }
                    return;
                }

                if (!m_buildExportPhase) {
                    // Compile succeeded — now run the export script
                    m_buildExportPhase = true;

                    QString appDir = Hephaestus::Tools::PathUtils::ExecutableDir();
                    QDir engineRoot(appDir);
                    engineRoot.cdUp();
                    engineRoot.cdUp();

                    QString exportScript = engineRoot.absoluteFilePath("tools/export_game.ps1");
                    if (!QFile::exists(exportScript)) {
                        m_buildOutputText->appendPlainText(tr("\n=== Compile succeeded, but export script not found ==="));
                        m_buildOutputText->appendPlainText(tr("Expected: %1").arg(exportScript));
                        m_buildBtn->setEnabled(true);
                        statusBar()->showMessage(tr("Compile succeeded (export script missing)"), 5000);
                        return;
                    }

                    // Determine names
                    QString projectName;
                    QString projectPath;
                    if (m_projectContext && m_projectContext->HasProject()) {
                        projectName = QString::fromStdString(m_projectContext->GetProjectName());
                        projectPath = QString::fromStdString(m_projectContext->GetProjectRootDir());
                    }
                    if (projectName.isEmpty()) projectName = QStringLiteral("HephaestusGame");
                    if (projectPath.isEmpty()) projectPath = engineRoot.absolutePath();

                    m_buildOutputText->appendPlainText(tr("\n=== Compile succeeded — exporting to dist/%1 ===\n").arg(projectName));
                    statusBar()->showMessage(tr("Exporting %1...").arg(projectName));

                    m_buildProcess->setWorkingDirectory(engineRoot.absolutePath());

                    QStringList exportArgs = {
                        "-ExecutionPolicy", "Bypass",
                        "-File", exportScript,
                        "-ProjectPath", projectPath,
                        "-ProjectName", projectName,
                        "-EngineRoot", engineRoot.absolutePath(),
                        "-Configuration", "Release"
                    };
                    if (!m_lastBuildSettings.startScene.isEmpty()) {
                        exportArgs << "-StartScene" << m_lastBuildSettings.startScene;
                    }
                    if (!m_lastBuildSettings.windowTitle.isEmpty()) {
                        exportArgs << "-WindowTitle" << m_lastBuildSettings.windowTitle;
                    }
                    exportArgs << "-WindowWidth" << QString::number(m_lastBuildSettings.windowWidth);
                    exportArgs << "-WindowHeight" << QString::number(m_lastBuildSettings.windowHeight);
                    if (m_lastBuildSettings.fullscreen) {
                        exportArgs << "-Fullscreen";
                    }
                    if (!m_lastBuildSettings.vsync) {
                        exportArgs << "-NoVsync";
                    }

                    m_buildProcess->start("powershell", exportArgs);
                } else {
                    // Export succeeded — done!
                    m_buildBtn->setEnabled(true);

                    QString projectName;
                    if (m_projectContext && m_projectContext->HasProject()) {
                        projectName = QString::fromStdString(m_projectContext->GetProjectName());
                    }
                    if (projectName.isEmpty()) projectName = QStringLiteral("HephaestusGame");

                    m_buildOutputText->appendPlainText(tr("\n=== Build & export complete — output in dist/%1/ ===").arg(projectName));
                    statusBar()->showMessage(tr("Build complete — dist/%1/").arg(projectName), 8000);
                }
            });

        connect(m_buildProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            m_buildBtn->setEnabled(true);
            QString phase = m_buildExportPhase ? tr("Export") : tr("Build");
            QString msg;
            switch (error) {
            case QProcess::FailedToStart:
                msg = m_buildExportPhase
                    ? tr("Failed to start powershell for export.")
                    : tr("Failed to start cmake. Make sure it is installed and in PATH.");
                break;
            case QProcess::Timedout:
                msg = tr("%1 process timed out.").arg(phase);
                break;
            default:
                msg = tr("%1 process error occurred.").arg(phase);
                break;
            }
            m_buildOutputText->appendPlainText(tr("\n=== ERROR: %1 ===").arg(msg));
            statusBar()->showMessage(msg, 5000);
        });
    }

    // Phase 1: Compile the HephaestusGame target
    m_buildProcess->setWorkingDirectory(engineRoot.absolutePath());
    m_buildProcess->setProcessChannelMode(QProcess::MergedChannels);
    m_buildProcess->start("cmake", {"--build", "build", "--config", "Release", "--target", "HephaestusGame"});
#endif
}

void EditorMainWindow::onValidateProject()
{
    startBuild(Build::BuildAction::Validate);
}

void EditorMainWindow::onPackageAndRun()
{
    startBuild(Build::BuildAction::PackageAndRun);
}

void EditorMainWindow::onBuildAndRun()
{
    if (m_buildThread && m_buildThread->isRunning()) {
        startBuild(Build::BuildAction::PackageAndRun);
        return;
    }
    if (!m_engine) return;
    if (m_engine->IsPlaying() || m_engine->IsPaused()) {
        statusBar()->showMessage(tr("Stop Play to build and run"), 3000);
        return;
    }
    // The packager reads scenes from disk, so unsaved edits would silently be left out.
    auto* ctx = m_engine->GetEditorContext();
    if (ctx && ctx->HasActiveScene() && (ctx->IsDirty() || ctx->GetScenePath().empty())) {
        QMessageBox box(QMessageBox::Question, tr("Build and Run"),
                        tr("The open scene has unsaved changes, and the build uses the saved files."),
                        QMessageBox::NoButton, this);
        QPushButton* save = box.addButton(tr("Save and Build"), QMessageBox::AcceptRole);
        QPushButton* lastSaved = ctx->GetScenePath().empty()
            ? nullptr : box.addButton(tr("Build Last Saved Version"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(save);
        box.exec();
        if (box.clickedButton() == save) {
            onSaveLevel();
            if (ctx->IsDirty() || ctx->GetScenePath().empty()) return;
        } else if (!lastSaved || box.clickedButton() != lastSaved) {
            return;
        }
    }
    startBuild(Build::BuildAction::PackageAndRun,
               m_buildRunReleaseAction && m_buildRunReleaseAction->isChecked()
                   ? Build::BuildConfiguration::Release
                   : Build::BuildConfiguration::Development);
}

void EditorMainWindow::openPlayWindow()
{
    if (!m_engine || m_playWindow) return;
    m_playWindow = new QtViewportWindow();
    QString title = m_projectContext && m_projectContext->HasProject()
        ? QString::fromStdString(m_projectContext->GetProjectName()) : QString();
    if (title.trimmed().isEmpty()) title = QStringLiteral("Hephaestus");
    m_playWindow->setTitle(tr("%1 (Play)").arg(title));
    m_playWindow->setIcon(windowIcon());
    m_playWindow->setMinimumSize(QSize(320, 180));
    const QRect saved = QSettings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"))
                            .value(QStringLiteral("play/windowGeometry")).toRect();
    if (saved.isValid() && QGuiApplication::screenAt(saved.center())) {
        m_playWindow->setGeometry(saved);
    } else {
        QScreen* screen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
        const QRect available = screen->availableGeometry();
        const double scale = std::min({1.0, available.width() * 0.8 / 1920.0, available.height() * 0.8 / 1080.0});
        const QSize size(static_cast<int>(1920 * scale), static_cast<int>(1080 * scale));
        m_playWindow->resize(size);
        m_playWindow->setPosition(available.center() - QPoint(size.width() / 2, size.height() / 2));
    }
    connect(m_playWindow, &QtViewportWindow::viewportReady, this, [this]() {
        if (m_engine && m_playWindow) m_engine->PresentTo(m_playWindow);
    });
    connect(m_playWindow, &QtViewportWindow::closeRequested, this, [this]() {
        if (!m_engine) return;
        if (m_engine->IsPlaying() || m_engine->IsPaused()) m_stopAction->trigger();
        else m_engine->PresentTo(nullptr);
    });
    m_playWindow->show();
    statusBar()->showMessage(tr("Opening the Play window"), 2000);
}

void EditorMainWindow::onPresentationTargetChanged(QtViewportWindow* target)
{
    if (!m_engine || !m_playWindow) return;
    if (target == m_playWindow) {
        if (!m_engine->IsPlaying()) m_engine->StartPlayMode();
        m_playWindow->requestActivate();
        return;
    }
    QSettings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"))
        .setValue(QStringLiteral("play/windowGeometry"), m_playWindow->geometry());
    QtViewportWindow* window = std::exchange(m_playWindow, nullptr);
    window->hide();
    window->deleteLater();
    if (m_playAction) m_playAction->setChecked(m_engine->IsPlaying());
    activateWindow();
}

void EditorMainWindow::onRunLastPackage()
{
    QSettings settings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"));
    if (!m_projectContext || !m_projectContext->HasProject()) {
        QMessageBox::information(this, tr("Run Last Package"),
                                 tr("Open a project first."));
        return;
    }
    const QString currentProject =
        QString::fromStdString(m_projectContext->GetProjectRootDir());
    const QString projectKey = QString::number(qHash(currentProject));
    const QString executable =
        settings.value(QStringLiteral("build/lastExecutable/") + projectKey).toString();
    if (executable.isEmpty() || !QFileInfo::exists(executable)) {
        QMessageBox::information(this, tr("Run Last Package"),
                                 tr("No previous package was found."));
        return;
    }
    const QDateTime packagedAt =
        settings.value(QStringLiteral("build/lastTimestamp/") + projectKey).toDateTime();
    bool changedSincePackage = false;
    if (m_projectContext && m_projectContext->HasProject() && packagedAt.isValid()) {
        const QString root = QString::fromStdString(m_projectContext->GetProjectRootDir());
        QDirIterator it(QDir(root).filePath(QStringLiteral("assets")), QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            if (it.fileInfo().lastModified().toUTC() > packagedAt) {
                changedSincePackage = true;
                break;
            }
        }
    }
    if (changedSincePackage &&
        QMessageBox::question(this, tr("Run Last Package"),
                              tr("Project assets changed after the last successful package. "
                                 "Run the older package anyway?"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(executable));
}

void EditorMainWindow::onCleanPackage()
{
    startBuild(Build::BuildAction::Clean);
}

void EditorMainWindow::startBuild(Build::BuildAction requestedAction,
                                  std::optional<Build::BuildConfiguration> quickRun)
{
    if (m_buildThread && m_buildThread->isRunning()) {
        if (m_buildCancel) {
            m_buildCancel->store(true);
            m_buildOutputText->appendPlainText(tr("[Info] Cancelling package operation..."));
            m_buildBtn->setEnabled(false);
        }
        return;
    }
    if (!m_projectContext || !m_projectContext->HasProject()) {
        QMessageBox::information(this, tr("Build Project"),
                                 tr("Open a project before validating or packaging."));
        return;
    }

    const QString projectRoot =
        QString::fromStdString(m_projectContext->GetProjectRootDir());
    QString projectName = QString::fromStdString(m_projectContext->GetProjectName());
    if (projectName.trimmed().isEmpty()) {
        projectName = QStringLiteral("HephaestusGame");
    }

    QStringList sceneFiles;
    QDirIterator sceneIterator(QDir(projectRoot).filePath(QStringLiteral("assets/scenes")),
                               {QStringLiteral("*.hpscene")}, QDir::Files,
                               QDirIterator::Subdirectories);
    while (sceneIterator.hasNext()) {
        sceneIterator.next();
        sceneFiles.push_back(
            QDir(projectRoot).relativeFilePath(sceneIterator.filePath()).replace('\\', '/'));
    }
    sceneFiles.sort(Qt::CaseInsensitive);

    BuildSettings defaults;
    QString startupScene;
    QString version = QStringLiteral("0.1.0");
    QString company;
    QString applicationIcon;
    QString inputProfile = QStringLiteral("assets/input/Default.hpinput");
    if (m_projectSettingsManager) {
        startupScene =
            m_projectSettingsManager->Value(QStringLiteral("project.info.defaultScene")).toString();
        version = m_projectSettingsManager->Value(QStringLiteral("project.info.version")).toString();
        company =
            m_projectSettingsManager->Value(QStringLiteral("project.info.companyName")).toString();
        inputProfile =
            m_projectSettingsManager->Value(QStringLiteral("project.input.defaultInputProfile")).toString();
        defaults.windowTitle =
            m_projectSettingsManager->Value(QStringLiteral("project.build.windowTitle")).toString();
        defaults.fullscreen =
            m_projectSettingsManager->Value(QStringLiteral("project.build.fullscreenDefault")).toBool();
        defaults.vsync =
            m_projectSettingsManager->Value(QStringLiteral("project.build.vsyncDefault")).toBool();
        applicationIcon =
            m_projectSettingsManager->Value(QStringLiteral("project.build.applicationIcon")).toString();
        const QString configuration =
            m_projectSettingsManager->Value(QStringLiteral("project.build.defaultConfiguration")).toString();
        defaults.configuration = configuration == QStringLiteral("Release")
            ? Build::BuildConfiguration::Release
            : Build::BuildConfiguration::Development;
        const QStringList resolution =
            m_projectSettingsManager->Value(QStringLiteral("project.build.defaultResolution"))
                .toString().split('x');
        if (resolution.size() == 2) {
            defaults.windowWidth = resolution[0].toInt();
            defaults.windowHeight = resolution[1].toInt();
        }
    }
    defaults.validateFirst = defaults.configuration == Build::BuildConfiguration::Release;
    defaults.action = requestedAction;

    QSettings userSettings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"));
    const QString outputRoot = userSettings.value(
        QStringLiteral("build/outputRoot/") + QString::number(qHash(projectRoot)),
        QDir(projectRoot).filePath(QStringLiteral("Builds"))).toString();

    BuildSettings selected;
    if (quickRun) {
        selected = defaults;
        selected.configuration = *quickRun;
        selected.action = Build::BuildAction::PackageAndRun;
        const auto* ctx = m_engine ? m_engine->GetEditorContext() : nullptr;
        const QString scenePath = ctx ? QString::fromStdString(ctx->GetScenePath()) : QString();
        selected.startScene = scenePath.isEmpty()
            ? startupScene : QDir(projectRoot).relativeFilePath(scenePath).replace('\\', '/');
        selected.outputDirectory = QDir(projectRoot).filePath(
            *quickRun == Build::BuildConfiguration::Release ? QStringLiteral("Saved/BuildAndRun/Release")
                                                            : QStringLiteral("Saved/BuildAndRun/Development"));
        selected.validateFirst = *quickRun == Build::BuildConfiguration::Release;
        selected.openOutputFolder = false;
        selected.createZip = false;
        selected.reportUnusedAssets = false;
    } else {
        BuildSettingsDialog dialog(projectName, sceneFiles, startupScene, outputRoot, defaults, this);
        connect(&dialog, &BuildSettingsDialog::openStartupSceneRequested, this,
                [this](const QString& path) {
            if (!path.isEmpty() && m_assetActions) {
                m_assetActions->Open(path);
            }
        });
        connect(&dialog, &BuildSettingsDialog::revealStartupSceneRequested, this,
                [this](const QString& path) {
            if (!path.isEmpty() && m_assetBrowser) {
                m_assetBrowser->RevealAsset(path);
                m_assetBrowserDock->show();
                m_assetBrowserDock->raise();
            }
        });
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        selected = dialog.GetSettings();
        if (requestedAction != Build::BuildAction::Package) {
            selected.action = requestedAction;
        }
        if (selected.action == Build::BuildAction::Clean) {
            const QString product = QString::fromStdString(
                Build::PackageStager::sanitizeProductName(projectName.toStdString()));
            const QString packagePath =
                QDir(selected.outputDirectory).filePath(product);
            if (QMessageBox::warning(
                    this, tr("Clean Package"),
                    tr("This permanently removes the last successful package and any "
                       "temporary staging data at:\n\n%1\n\nContinue?")
                        .arg(QDir::toNativeSeparators(packagePath)),
                    QMessageBox::Yes | QMessageBox::Cancel,
                    QMessageBox::Cancel) != QMessageBox::Yes) {
                return;
            }
        }

        if (m_projectSettingsManager) {
            m_projectSettingsManager->SetValue(
                QStringLiteral("project.info.defaultScene"), selected.startScene);
            m_projectSettingsManager->SetValue(
                QStringLiteral("project.build.defaultConfiguration"),
                selected.configuration == Build::BuildConfiguration::Release
                    ? QStringLiteral("Release") : QStringLiteral("Development"));
            m_projectSettingsManager->SetValue(
                QStringLiteral("project.build.windowTitle"), selected.windowTitle);
            m_projectSettingsManager->SetValue(
                QStringLiteral("project.build.defaultResolution"),
                QStringLiteral("%1x%2").arg(selected.windowWidth).arg(selected.windowHeight));
            m_projectSettingsManager->SetValue(
                QStringLiteral("project.build.fullscreenDefault"), selected.fullscreen);
            m_projectSettingsManager->SetValue(
                QStringLiteral("project.build.vsyncDefault"), selected.vsync);
            m_projectSettingsManager->Save();
        }
        userSettings.setValue(
            QStringLiteral("build/outputRoot/") + QString::number(qHash(projectRoot)),
            selected.outputDirectory);
    }

    const std::filesystem::path executableDirectory(
        Hephaestus::Tools::PathUtils::ExecutableDir().toStdWString());
    const std::filesystem::path engineRoot =
        Build::RuntimeLocator::discoverEngineRoot(executableDirectory);

    Build::BuildRequest request;
    request.projectRoot = std::filesystem::path(projectRoot.toStdWString());
    request.engineRoot = engineRoot;
    request.runtimeSearchRoot = executableDirectory;
    request.outputRoot = std::filesystem::path(selected.outputDirectory.toStdWString());
    request.productName = projectName.toStdString();
    request.version = version.toStdString();
    request.company = company.toStdString();
    request.engineVersion = Tools::VersionInfo::EngineVersion().toStdString();
    request.startupScene = selected.startScene.toStdString();
    request.inputProfile = inputProfile.toStdString();
    request.configuration = selected.configuration;
    request.action = selected.action;
    request.validateFirst = selected.validateFirst;
    request.createZip = selected.createZip;
    request.reportUnusedAssets = selected.reportUnusedAssets;
    request.clean = false;
    request.incremental = true;
    request.window.title = selected.windowTitle.toStdString();
    request.window.width = selected.windowWidth;
    request.window.height = selected.windowHeight;
    request.window.fullscreen = selected.fullscreen;
    request.window.vsync = selected.vsync;
    if (m_projectSettingsManager) {
        request.physicsEnabled =
            m_projectSettingsManager->Value(QStringLiteral("project.rendering.physicsEnabled")).toBool();
    }
    if (!applicationIcon.trimmed().isEmpty()) {
        const std::filesystem::path icon(applicationIcon.toStdWString());
        request.applicationIcon = icon.is_absolute()
            ? icon : request.projectRoot / icon;
    }

    m_buildOutputDock->show();
    m_buildOutputDock->raise();
    m_buildOutputText->clear();
    m_buildProblems->clear();
    m_buildProblems->hide();
    m_buildCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelFlag = m_buildCancel;
    request.isCancelled = [cancelFlag]() { return cancelFlag->load(); };
    request.onLog = [this](const std::string& message) {
        QMetaObject::invokeMethod(this, [this, message]() {
            m_buildOutputText->appendPlainText(
                QStringLiteral("[%1] %2")
                    .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                         QString::fromStdString(message)));
        }, Qt::QueuedConnection);
    };
    request.onProgress = [this](float, const std::string& stage) {
        QMetaObject::invokeMethod(this, [this, stage]() {
            statusBar()->showMessage(QString::fromStdString(stage));
        }, Qt::QueuedConnection);
    };

    m_buildBtn->setText(tr("Cancel"));
    m_buildBtn->setEnabled(true);
    if (m_notifications) {
        m_notifications->ShowProgress(tr("Preparing package..."));
    }
    const bool openOutput = selected.openOutputFolder;
    m_buildThread = QThread::create([this, request, openOutput, quickRun]() {
        Build::BuildService service;
        Build::BuildResult result = service.execute(request);
        QMetaObject::invokeMethod(this, [this, result = std::move(result), openOutput, quickRun,
                                         projectRoot = request.projectRoot,
                                         buildAction = request.action]() mutable {
            m_buildBtn->setText(tr("Build && Run"));
            m_buildBtn->setEnabled(true);
            m_buildProblems->clear();
            for (const auto& problem : result.problems) {
                const QString severity =
                    problem.severity == Build::ProblemSeverity::Error ? tr("Error") :
                    problem.severity == Build::ProblemSeverity::Warning ? tr("Warning") :
                                                                          tr("Info");
                auto* item = new QTreeWidgetItem(
                    {severity, QString::fromStdString(problem.message),
                     QString::fromStdString(problem.assetPath)});
                item->setToolTip(1, QString::fromStdString(problem.detail));
                item->setData(0, Qt::UserRole, QString::fromStdString(problem.assetPath));
                m_buildProblems->addTopLevelItem(item);
                m_buildOutputText->appendPlainText(
                    QStringLiteral("[%1] [%2] %3%4")
                        .arg(severity,
                             QString::fromStdString(problem.code),
                             QString::fromStdString(problem.message),
                             problem.detail.empty()
                                 ? QString()
                                 : QStringLiteral(": ") +
                                       QString::fromStdString(problem.detail)));
            }
            m_buildProblems->setVisible(m_buildProblems->topLevelItemCount() > 0);
            const QString projectKey = QString::number(
                qHash(QString::fromStdWString(projectRoot.wstring())));
            if (result.success) {
                QSettings settings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"));
                if (buildAction == Build::BuildAction::Clean) {
                    settings.remove(QStringLiteral("build/lastOutput/") + projectKey);
                    settings.remove(QStringLiteral("build/lastExecutable/") + projectKey);
                    settings.remove(QStringLiteral("build/lastTimestamp/") + projectKey);
                } else if (!result.outputPath.empty()) {
                    settings.setValue(QStringLiteral("build/lastOutput/") + projectKey,
                                      QString::fromStdWString(result.outputPath.wstring()));
                    settings.setValue(QStringLiteral("build/lastExecutable/") + projectKey,
                                      QString::fromStdWString(result.executablePath.wstring()));
                    settings.setValue(QStringLiteral("build/lastTimestamp/") + projectKey,
                                      QDateTime::currentDateTimeUtc());
                }
                const QString message =
                    buildAction == Build::BuildAction::Clean
                        ? tr("Package and staging data removed")
                        : result.outputPath.empty()
                            ? tr("Project validation completed")
                            : tr("Package completed: %1")
                                  .arg(QString::fromStdWString(result.outputPath.wstring()));
                if (result.packageBytes > 0) {
                    m_buildOutputText->appendPlainText(
                        tr("Package size: %1 MB")
                            .arg(static_cast<double>(result.packageBytes) /
                                 (1024.0 * 1024.0), 0, 'f', 2));
                    if (!result.largestFiles.empty()) {
                        m_buildOutputText->appendPlainText(tr("Largest packaged files:"));
                        for (const auto& file : result.largestFiles) {
                            m_buildOutputText->appendPlainText(
                                tr("  %1 — %2 MB")
                                    .arg(QString::fromStdString(file.path))
                                    .arg(static_cast<double>(file.bytes) /
                                         (1024.0 * 1024.0), 0, 'f', 2));
                        }
                    }
                }
                if (m_notifications) m_notifications->CompleteProgress(message);
                if (openOutput && buildAction != Build::BuildAction::Clean &&
                    !result.outputPath.empty()) {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(
                        QString::fromStdWString(result.outputPath.wstring())));
                }
            } else if (result.cancelled) {
                if (m_notifications) m_notifications->CompleteProgress(tr("Packaging cancelled"));
            } else {
                if (m_notifications) m_notifications->ShowError(tr("Packaging failed"));
                const bool outputLocked = std::any_of(
                    result.problems.begin(), result.problems.end(),
                    [](const Build::EditorProblem& problem) {
                        return problem.code == "PACKAGE_OUTPUT_LOCKED";
                    });
                if (outputLocked) {
                    QMessageBox lockMessage(this);
                    lockMessage.setWindowTitle(tr("Package is running"));
                    lockMessage.setIcon(QMessageBox::Warning);
                    lockMessage.setText(
                        tr("The current package cannot be replaced because its executable "
                           "or Lua runtime is still in use."));
                    lockMessage.setInformativeText(
                        tr("Stop the game and retry, choose another output directory, or cancel."));
                    auto* retryButton = lockMessage.addButton(
                        tr("Retry After Stopping Game"), QMessageBox::AcceptRole);
                    auto* chooseButton = quickRun ? nullptr : lockMessage.addButton(
                        tr("Choose Different Output"), QMessageBox::ActionRole);
                    lockMessage.addButton(QMessageBox::Cancel);
                    lockMessage.exec();

                    bool retry = lockMessage.clickedButton() == retryButton;
                    if (chooseButton && lockMessage.clickedButton() == chooseButton) {
                        const QString chosen = QFileDialog::getExistingDirectory(
                            this, tr("Choose Package Output"),
                            result.outputPath.empty()
                                ? QString()
                                : QString::fromStdWString(
                                      result.outputPath.parent_path().wstring()));
                        if (!chosen.isEmpty()) {
                            QSettings outputSettings(
                                QStringLiteral("Hephaestus"), QStringLiteral("Editor"));
                            outputSettings.setValue(
                                QStringLiteral("build/outputRoot/") + projectKey, chosen);
                            retry = true;
                        }
                    }
                    if (retry) {
                        QTimer::singleShot(250, this, [this, buildAction, quickRun]() {
                            startBuild(buildAction, quickRun);
                        });
                    }
                }
            }
            std::error_code logError;
            const auto logDirectory = projectRoot / ".hephaestus" / "build";
            std::filesystem::create_directories(logDirectory, logError);
            if (!logError) {
                std::ofstream latestLog(logDirectory / "latest.log",
                                        std::ios::binary | std::ios::trunc);
                latestLog << m_buildOutputText->toPlainText().toStdString();
            }
        }, Qt::QueuedConnection);
    });
    connect(m_buildThread, &QThread::finished, this, [this]() {
        m_buildThread = nullptr;
    });
    connect(m_buildThread, &QThread::finished, m_buildThread, &QObject::deleteLater);
    m_buildThread->start();
}

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
            if (!r.all_of<MeshRendererComponent>(e)) {
                return false;
            }
            return !IsRuntimeGeneratedVisual(r.get<MeshRendererComponent>(e));
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new MeshRendererInspector(ctx, e, parent);
        });

    // ── Character (order 12) ────────────────────────────────────────────
    reg.Register("Character", 12,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<CharacterComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new CharacterInspector(ctx, e, parent);
        });

    reg.Register("CharacterController", 16,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<CharacterControllerComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new CharacterControllerInspector(ctx, e, parent);
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
    reg.Register("ScriptComponent", 40,
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

    // ── Light (order 8) ─────────────────────────────────────────────────
    reg.Register("Light", 8,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<LightComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new LightInspector(ctx, e, parent);
        });

    // ── Environment (order 9) ───────────────────────────────────────────
    reg.Register("Environment", 9,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<EnvironmentComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new EnvironmentInspector(ctx, e, parent);
        });

    reg.Register("ProbeVolume", 11,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<ProbeVolumeComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new ProbeVolumeInspector(ctx, e, parent);
        });

    // ── AudioSource (order 42) ──────────────────────────────────────────
    reg.Register("AudioSource", 42,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<AudioSourceComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new AudioSourceInspector(ctx, e, parent);
        });

    reg.Register("ParticleEmitter", 44,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<ParticleEmitterComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new ParticleEmitterInspector(ctx, e, parent);
        });

    // ── Camera (order 50) ───────────────────────────────────────────────
    reg.Register("Camera", 50,
        [](const entt::registry& r, entt::entity e) {
            return r.all_of<CameraComponent>(e);
        },
        [](EditorContext* ctx, entt::entity e, QWidget* parent) -> ComponentInspector* {
            return new CameraInspector(ctx, e, parent);
        });
}

/**
 * Rebuild all component inspectors for the currently viewed selection.
 * One widget tree for the shared-component intersection.
 */
void EditorMainWindow::rebuildComponentInspectors()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;

    Scene* scene = ctx->GetActiveScene();
    auto& registry = scene->GetRegistry();
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;

    if (m_lightInspector) {
        m_detailsLayout->removeWidget(m_lightInspector);
        m_lightInspector->blockSignals(true);
        m_lightInspector->disconnect(this);
        delete m_lightInspector;
        m_lightInspector = nullptr;
    }
    if (m_environmentInspector) {
        m_detailsLayout->removeWidget(m_environmentInspector);
        m_environmentInspector->blockSignals(true);
        m_environmentInspector->disconnect(this);
        delete m_environmentInspector;
        m_environmentInspector = nullptr;
    }
    for (auto* insp : m_componentInspectors) {
        QWidget* card = componentCardOf(insp);
        m_detailsLayout->removeWidget(card);
        insp->blockSignals(true);
        insp->disconnect(this);
        delete card;
    }
    m_componentInspectors.clear();

    int stretchIndex = m_detailsLayout->count();
    for (int i = 0; i < m_detailsLayout->count(); ++i) {
        QLayoutItem* item = m_detailsLayout->itemAt(i);
        if (item && item->spacerItem()) {
            stretchIndex = i;
            break;
        }
    }

    std::vector<entt::entity> targets = model.entities;
    const auto primaryIt = std::find(targets.begin(), targets.end(), model.primary);
    if (primaryIt != targets.end() && primaryIt != targets.begin()) {
        std::rotate(targets.begin(), primaryIt, std::next(primaryIt));
    }
    auto inspectors = ComponentInspectorRegistry::Instance().CreateInspectors(
        ctx, targets, registry, m_detailsWidget);

    for (auto* insp : inspectors) {
        connect(insp, &ComponentInspector::componentRemoved,
                this, &EditorMainWindow::onComponentRemoved);
        connect(insp, &ComponentInspector::copyComponentRequested, this,
                [this](const QString& typeName) { copyInspectorComponent(typeName.toStdString()); });
        connect(insp, &ComponentInspector::pasteValuesRequested, this,
                [this](const QString& typeName) { pasteInspectorComponentValues(typeName.toStdString()); });
        connect(insp, &ComponentInspector::resetComponentRequested, this,
                [this](const QString& typeName) { resetInspectorComponent(typeName.toStdString()); });
        if (auto* probeVolume = qobject_cast<ProbeVolumeInspector*>(insp)) {
            connect(probeVolume, &ProbeVolumeInspector::bakeRequested, this, &EditorMainWindow::bakeSelectedProbeVolumes);
            if (m_engine && m_engine->GetFrameRenderer()) {
                probeVolume->SetHardwareRayTracingAvailable(
                    m_engine->GetFrameRenderer()->ProbeHardwareRayTracingAvailable());
            }
            probeVolume->SetGeometrySignatureProvider([this]() -> uint64_t {
                auto* ctx = m_engine ? m_engine->GetEditorContext() : nullptr;
                if (!ctx || !ctx->HasActiveScene() || !m_engine->GetFrameRenderer()) return 0;
                return ProbeBakeGeometrySignature(
                    m_engine->GetFrameRenderer()->CollectProbeBakeGeometry(*ctx->GetActiveScene()));
            });
        }

        const std::string type = insp->TypeName();
        const QString title = insp->title();
        insp->setTitle(QString());
        m_detailsLayout->insertWidget(stretchIndex, wrapComponentCard(insp, title, QString::fromStdString(type),
            [this, type]() { resetInspectorComponent(type); }));
        decoratePrefabComponentInspector(insp, insp->TypeName());
        for (auto* field : insp->findChildren<AssetReferenceEditor*>()) {
            connect(field, &AssetReferenceEditor::openRequested, this, [this](const QString& path) {
                if (m_assetActions) m_assetActions->Open(path);
            });
            connect(field, &AssetReferenceEditor::revealRequested, this, [this](const QString& path) {
                if (m_assetBrowser) m_assetBrowser->RevealAsset(path);
            });
            connect(field, &AssetReferenceEditor::findReferencesRequested, this, [this](const QString& path) {
                if (m_assetBrowser) m_assetBrowser->FindReferences(path);
            });
            connect(field, &AssetReferenceEditor::copyPathRequested, this, [this](const QString& path) {
                QApplication::clipboard()->setText(path);
                statusBar()->showMessage(tr("Copied %1").arg(path), 2000);
            });
        }
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
    if (!ctx || !ctx->HasActiveScene()) return;
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;

    Scene* scene = ctx->GetActiveScene();
    auto& reg = scene->GetRegistry();
    std::vector<AddComponentItem> items;
    for (const auto& desc : InspectorAddableComponents()) {
        AddComponentItem item;
        item.id = QString::fromUtf8(desc.id);
        item.displayName = QString::fromUtf8(desc.displayName);
        item.category = QString::fromUtf8(desc.category);
        if (item.id == QLatin1String("RigidBody")) {
            item.displayName = tr("Rigid Body (also adds Physics Shape)");
        }
        int present = 0;
        for (entt::entity entity : model.entities) {
            if (InspectorEntityHasComponent(reg, entity, desc.id)) {
                ++present;
            }
        }
        item.alreadyPresent = present == static_cast<int>(model.Count());
        if (desc.sceneUnique && CountComponentsInScene(*scene, desc.id) > 0) {
            item.disabled = true;
            item.disabledReason = tr("This scene already has an Environment.");
        }
        items.push_back(item);
    }

    auto* popup = new AddComponentPopup(this);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    connect(popup, &AddComponentPopup::componentChosen, this, &EditorMainWindow::applyBatchAddComponent);
    popup->Present(m_addComponentBtn->mapToGlobal(QPoint(0, m_addComponentBtn->height())), items);
}

InspectorSelectionModel EditorMainWindow::resolveInspectorSelection()
{
    InspectorSelectionModel empty;
    if (!m_engine) return empty;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return empty;
    return InspectorSelectionModel::Resolve(
        ctx->GetSelectedEntities(),
        ctx->GetSelectedEntity(),
        m_detailsLocked,
        m_lockedEntity,
        &ctx->GetActiveScene()->GetRegistry());
}

ComponentInspector* EditorMainWindow::focusedComponentInspector() const
{
    for (QWidget* widget = QApplication::focusWidget(); widget; widget = widget->parentWidget()) {
        if (auto* inspector = qobject_cast<ComponentInspector*>(widget)) {
            return inspector;
        }
    }
    return nullptr;
}

void EditorMainWindow::applyBatchAddComponent(const QString& typeId)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;

    const std::string type = typeId.toStdString();
    EditorUndoManager::PushSnapshot(ctx, std::string("Add ") + type);
    const BatchComponentResult result = BatchAddComponent(*ctx->GetActiveScene(), model.entities, type);
    ctx->MarkDirty();
    refreshDetailsPanel();
    rebuildOutliner();

    const QString display = QString::fromStdString(InspectorDisplayName(type));
    QString message = tr("Added %1 to %2 of %3")
        .arg(display)
        .arg(result.added)
        .arg(result.Requested());
    if (result.skippedExisting > 0) {
        message += tr(" (%1 already had it)").arg(result.skippedExisting);
    }
    if (result.skippedUnique > 0) {
        message += tr(" (%1 skipped: unique component)").arg(result.skippedUnique);
    }
    statusBar()->showMessage(message, 4000);
    if (m_notifications) {
        m_notifications->ShowInfo(message);
    }
}

void EditorMainWindow::copyInspectorComponent(const std::string& type)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;
    Entity entity(model.primary, ctx->GetActiveScene());
    if (!ComponentClipboard::Instance().CopyFrom(entity, type)) {
        statusBar()->showMessage(tr("Could not copy component"), 2500);
        return;
    }
    statusBar()->showMessage(tr("Copied %1").arg(QString::fromStdString(InspectorDisplayName(type))), 2000);
}

void EditorMainWindow::pasteInspectorComponentValues(const std::string& type)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    auto& clip = ComponentClipboard::Instance();
    if (!clip.HasComponent()) {
        statusBar()->showMessage(tr("Component clipboard is empty"), 2500);
        return;
    }
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;
    if (!type.empty() && clip.Type() != type &&
        InspectorDisplayName(clip.Type()) != InspectorDisplayName(type)) {
        statusBar()->showMessage(tr("Clipboard holds %1, not %2")
            .arg(QString::fromStdString(InspectorDisplayName(clip.Type())))
            .arg(QString::fromStdString(InspectorDisplayName(type))), 3000);
        return;
    }
    EditorUndoManager::PushSnapshot(ctx, "Paste component values");
    const int applied = clip.PasteValues(*ctx->GetActiveScene(), model.entities);
    ctx->MarkDirty();
    refreshDetailsPanel();
    statusBar()->showMessage(tr("Pasted values on %1 entities").arg(applied), 2500);
}

void EditorMainWindow::resetInspectorComponent(const std::string& type)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;
    EditorUndoManager::PushSnapshot(ctx, std::string("Reset ") + type);
    for (entt::entity handle : model.entities) {
        Entity entity(handle, ctx->GetActiveScene());
        if (!entity) continue;
        PrefabComponentIO::RemoveComponent(entity, type);
        if (type == "AudioSource" && entity.HasComponent<AudioSourceComponent>()) {
            entity.RemoveComponent<AudioSourceComponent>();
        }
        if (type == "Environment" && entity.HasComponent<EnvironmentComponent>()) {
            entity.RemoveComponent<EnvironmentComponent>();
        }
        if (type == "ProbeVolume" && entity.HasComponent<ProbeVolumeComponent>()) {
            entity.RemoveComponent<ProbeVolumeComponent>();
        }
        BatchAddComponent(*ctx->GetActiveScene(), { handle }, type);
    }
    ctx->MarkDirty();
    refreshDetailsPanel();
    statusBar()->showMessage(tr("Reset %1 to defaults").arg(QString::fromStdString(InspectorDisplayName(type))), 2500);
}

void EditorMainWindow::bakeAllProbeVolumes()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    auto view = ctx->GetActiveScene()->GetRegistry().view<ProbeVolumeComponent>();
    const std::vector<entt::entity> volumes(view.begin(), view.end());
    if (volumes.empty()) {
        statusBar()->showMessage(tr("This scene has no probe volumes to bake. Add one from Add Component, under Lighting."), 4000);
        return;
    }
    bakeProbeVolumes(volumes);
}

void EditorMainWindow::bakeSelectedProbeVolumes()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    auto& reg = ctx->GetActiveScene()->GetRegistry();
    std::vector<entt::entity> volumes;
    for (entt::entity handle : resolveInspectorSelection().entities) {
        if (reg.valid(handle) && reg.all_of<ProbeVolumeComponent>(handle)) {
            volumes.push_back(handle);
        }
    }
    if (!volumes.empty()) {
        bakeProbeVolumes(volumes);
    }
}

void EditorMainWindow::bakeProbeVolumes(const std::vector<entt::entity>& volumes)
{
    if (!m_engine || !m_engine->GetFrameRenderer()) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    if (ctx->GetScenePath().empty() || !m_projectContext || !m_projectContext->HasProject()) {
        QMessageBox::information(this, tr("Bake Probes"),
                                 tr("Save the scene in a project first. The bake is stored next to the scene file."));
        return;
    }
    Scene* scene = ctx->GetActiveScene();
    auto& reg = scene->GetRegistry();

    namespace fs = std::filesystem;
    const fs::path projectRoot(m_projectContext->GetProjectRootDir());
    const fs::path scenePath(ctx->GetScenePath());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto started = std::chrono::steady_clock::now();
    const ProbeBakeGeometry geometry = m_engine->GetFrameRenderer()->CollectProbeBakeGeometry(*scene);
    EditorUndoManager::PushSnapshot(ctx, "Bake Probes");
    size_t probes = 0, moved = 0;
    QString failure;
    for (entt::entity handle : volumes) {
        Entity entity(handle, scene);
        auto& volume = reg.get<ProbeVolumeComponent>(handle);
        const Mat4 world = scene->GetWorldTransform(entity);
        const ProbeBake bake = BakeProbeVolume(geometry, Vec3(world.m[12], world.m[13], world.m[14]), volume);

        std::string name = entity.HasComponent<TagComponent>() ? entity.GetComponent<TagComponent>().tag : "";
        for (char& c : name) {
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        }
        name += "_" + std::to_string(static_cast<uint32_t>(handle));
        const fs::path file = scenePath.parent_path() / (scenePath.stem().string() + "." + name + ".hpprobes");
        std::string error;
        if (!WriteProbeBake(file, bake, error)) {
            failure = QString::fromStdString(error);
            break;
        }
        volume.bakePath = file.lexically_relative(projectRoot).generic_string();
        probes += bake.ProbeCount();
        for (const Vec3& offset : bake.probeOffsets) {
            if (vlen(offset) > 0.0f) ++moved;
        }
    }
    QApplication::restoreOverrideCursor();
    ctx->MarkDirty();
    refreshDetailsPanel();
    if (!failure.isEmpty()) {
        QMessageBox::warning(this, tr("Bake Probes"), failure);
        return;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    statusBar()->showMessage(tr("Baked %1 probes against %2 triangles in %3 ms (%4 moved out of geometry)")
                                 .arg(probes).arg(geometry.triangles.size()).arg(ms).arg(moved), 6000);
}

void EditorMainWindow::removeInspectorComponent(const std::string& type)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;
    Scene* scene = ctx->GetActiveScene();
    auto& reg = scene->GetRegistry();

    if (type == "PhysicsShape") {
        bool stillHasBody = false;
        for (entt::entity handle : model.entities) {
            if (reg.valid(handle) && reg.all_of<RigidBodyComponent>(handle) &&
                InspectorEntityHasComponent(reg, handle, "PhysicsShape")) {
                stillHasBody = true;
                break;
            }
        }
        if (stillHasBody) {
            auto choice = QMessageBox::question(this, tr("Remove Physics Shape"),
                tr("Some selected entities still have a Rigid Body. Remove both Rigid Body and Physics Shape?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (choice == QMessageBox::Cancel) return;
            EditorUndoManager::PushSnapshot(ctx, "Remove PhysicsShape");
            if (choice == QMessageBox::Yes) {
                BatchRemoveComponent(*scene, model.entities, "RigidBody");
            }
            BatchRemoveComponent(*scene, model.entities, "PhysicsShape");
            ctx->MarkDirty();
            refreshDetailsPanel();
            rebuildOutliner();
            return;
        }
    }

    EditorUndoManager::PushSnapshot(ctx, std::string("Remove ") + type);
    const BatchComponentResult result = BatchRemoveComponent(*scene, model.entities, type);
    ctx->MarkDirty();
    refreshDetailsPanel();
    rebuildOutliner();
    statusBar()->showMessage(tr("Removed %1 from %2 of %3")
        .arg(QString::fromStdString(InspectorDisplayName(type)))
        .arg(result.added)
        .arg(result.Requested()), 3000);
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

    if (targetField == "lua_script" &&
        ScriptLanguageRegistry::Instance().IsScriptPath(assetPath.toStdString())) {
        EditorUndoManager::PushSnapshot(ctx, "Drag-drop script");
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
        // Normalise the mesh path to be project-root-relative.
        // If the path is relative to the asset root (from the asset browser),
        // prefix it with "assets/". If it's an absolute path external to the
        // project, copy the file into <projectRoot>/assets/meshes/ first.
        namespace fs = std::filesystem;
        std::string portablePath = assetPath.toStdString();
        fs::path assetP(portablePath);
        if (assetP.is_absolute()) {
            // Try to make project-relative
            std::string projRoot;
            if (m_projectContext && m_projectContext->HasProject()) {
                projRoot = m_projectContext->GetProjectRootDir();
            }
            if (!projRoot.empty()) {
                fs::path normalized = fs::weakly_canonical(assetP);
                fs::path rel = normalized.lexically_relative(fs::path(projRoot));
                std::string relStr = rel.generic_string();
                if (!rel.empty() && relStr.find("..") == std::string::npos) {
                    portablePath = relStr;
                } else {
                    // External file: copy into project
                    fs::path destDir = fs::path(projRoot) / "assets" / "meshes";
                    std::error_code ec;
                    fs::create_directories(destDir, ec);
                    fs::path destFile = destDir / assetP.filename();
                    fs::copy_file(assetP, destFile, fs::copy_options::skip_existing, ec);
                    if (fs::exists(destFile)) {
                        portablePath = fs::path("assets/meshes" / assetP.filename()).generic_string();
                        std::cout << "[DragDrop] Copied external mesh to project: " << destFile << "\n";
                    }
                }
            }
        } else {
            // Asset-root-relative path (e.g. "meshes/hero.obj") from the
            // asset browser – prefix with "assets/" for portability.
            std::string generic = fs::path(portablePath).generic_string();
            if (generic.rfind("assets/", 0) != 0) {
                portablePath = "assets/" + generic;
            }
        }
        reg.get<MeshRendererComponent>(entity).meshAssetPath = portablePath;
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

// ═════════════════════════════════════════════════════════════════════════════
// Viewport drag-drop event filter (Add Actors / Asset spawning)
// ═════════════════════════════════════════════════════════════════════════════

bool EditorMainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_viewportContainer) {
        if (event->type() == QEvent::DragEnter) {
            auto* de = static_cast<QDragEnterEvent*>(event);
            if (de->mimeData()->hasFormat(kSpawnableMimeType)) {
                de->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragMove) {
            auto* de = static_cast<QDragMoveEvent*>(event);
            if (de->mimeData()->hasFormat(kSpawnableMimeType)) {
                de->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::Drop) {
            auto* de = static_cast<QDropEvent*>(event);
            if (de->mimeData()->hasFormat(kSpawnableMimeType)) {
                QByteArray payload = de->mimeData()->data(kSpawnableMimeType);
                QPoint dropPos = de->position().toPoint();
                entt::entity previousSelection = entt::null;
                if (m_engine) {
                    if (auto* ctx = m_engine->GetEditorContext()) {
                        previousSelection = ctx->GetSelectedEntity();
                    }
                }
                de->acceptProposedAction();
                handleViewportSpawnDrop(payload, dropPos);
                if (m_engine) {
                    if (auto* ctx = m_engine->GetEditorContext();
                        ctx && ctx->HasSelection() &&
                        ctx->GetSelectedEntity() != previousSelection) {
                        m_engine->BeginPlacement(ctx->GetSelectedEntity());
                        statusBar()->showMessage(
                            tr("Placement preview: click to place, Shift+click to repeat, Esc to cancel"),
                            5000);
                    }
                }
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ─────────────────────────────────────────────────────────────────────────────
// Spawn entity from an Add Actors drop onto the viewport
// ─────────────────────────────────────────────────────────────────────────────

void EditorMainWindow::rebuildSceneVisuals()
{
    if (m_engine) {
        m_engine->RebuildSceneMeshes();
    }
}

bool EditorMainWindow::requestCloseOpenCharacter(const QString& absolutePath)
{
    if (!m_characterEditorManager) {
        return true;
    }
    return m_characterEditorManager->RequestCloseWindow(absolutePath);
}

void EditorMainWindow::spawnFromAssetBrowser(const QByteArray& spawnPayload)
{
    QPoint dropPos(0, 0);
    if (m_viewportContainer) {
        dropPos = m_viewportContainer->rect().center();
    }
    handleViewportSpawnDrop(spawnPayload, dropPos);
}

void EditorMainWindow::handleViewportSpawnDrop(const QByteArray& spawnPayload, const QPoint& dropPos)
{
    std::cout << "[SpawnDrop] === handleViewportSpawnDrop START ===\n";
    std::cout.flush();

    try {

    if (!m_engine) { std::cout << "[SpawnDrop] ABORT: m_engine null\n"; return; }
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) {
        statusBar()->showMessage(tr("No active scene to spawn into."), 3000);
        std::cout << "[SpawnDrop] ABORT: no active scene\n";
        return;
    }

    auto* frameRenderer = m_engine->GetFrameRenderer();
    SpawnableEntry entry = SpawnableEntry::fromMimePayload(spawnPayload);
    Scene* scene = ctx->GetActiveScene();

    std::cout << "[SpawnDrop] kind=" << static_cast<int>(entry.kind)
              << " display=" << entry.displayName.toStdString()
              << " asset=" << entry.assetPath.toStdString() << "\n";
    std::cout.flush();

    // ── Determine spawn position ────────────────────────────────────────────
    // Try to hit-test against the scene at the drop pixel position.
    // If no hit, place a fixed distance in front of the editor camera.
    const auto& cam = ctx->GetCamera();
    constexpr float kSpawnDistance = 6.0f;
    const float yawR   = cam.yawDeg   * 3.1415926535f / 180.0f;
    const float pitchR = cam.pitchDeg * 3.1415926535f / 180.0f;
    float fx = std::cos(yawR) * std::cos(pitchR);
    float fy = std::sin(pitchR);
    float fz = std::sin(yawR) * std::cos(pitchR);
    float len = std::sqrt(fx*fx + fy*fy + fz*fz);
    if (len > 0.0001f) { fx /= len; fy /= len; fz /= len; }

    Vec3 spawnPos(cam.positionX + fx * kSpawnDistance,
                  cam.positionY + fy * kSpawnDistance,
                  cam.positionZ + fz * kSpawnDistance);
    SceneAuthoring::SceneRaycastHit placementHit;
    const bool hasPlacementHit =
        m_engine->RaycastViewport(static_cast<float>(dropPos.x()),
                                  static_cast<float>(dropPos.y()),
                                  placementHit);
    if (hasPlacementHit) {
        spawnPos = placementHit.point;
    }
    const auto& snapSettings = ctx->GetSnapSettings();
    if (snapSettings.enableSnap) {
        spawnPos = SceneAuthoring::SnapVector(spawnPos,
                                              snapSettings.translateSnap);
    }

    // ── Push undo ───────────────────────────────────────────────────────────
    EditorUndoManager::PushSnapshot(ctx, "Add Actor: " + entry.displayName.toStdString());

    // ── Generate a unique entity name ───────────────────────────────────────
    std::string baseName = entry.displayName.toStdString();
    if (baseName.empty()) baseName = "Actor";
    std::string entityName = baseName;
    if (scene->FindEntityByName(entityName)) {
        int suffix = 1;
        do {
            entityName = baseName + " (" + std::to_string(suffix++) + ")";
        } while (scene->FindEntityByName(entityName));
    }

    // ── Spawn based on kind ─────────────────────────────────────────────────
    switch (entry.kind) {

    case SpawnableEntry::Kind::Mesh: {
        std::cout << "[SpawnDrop] Kind::Mesh\n"; std::cout.flush();
        if (!frameRenderer) {
            statusBar()->showMessage(tr("Renderer not ready."), 3000);
            return;
        }
        namespace fs = std::filesystem;
        const std::string assetPath = entry.assetPath.toStdString();
        if (assetPath.rfind("builtin://", 0) == 0) {
            size_t meshHandle = frameRenderer->GetMeshHandle(assetPath);
            if (meshHandle == SIZE_MAX) {
                statusBar()->showMessage(tr("Builtin mesh not loaded: %1").arg(entry.assetPath), 3000);
                return;
            }
            MeshGPU* meshGPU = frameRenderer->GetMesh(meshHandle);
            int subCount = (meshGPU && !meshGPU->submeshes.empty())
                ? static_cast<int>(meshGPU->submeshes.size()) : 1;
            AABB bounds = meshGPU ? meshGPU->bounds : AABB{};
            Entity created = scene->CreateEntity(entityName);
            if (!created) {
                statusBar()->showMessage(tr("Failed to create entity."), 3000);
                return;
            }
            auto& renderer = created.AddComponent<MeshRendererComponent>();
            renderer.SetMesh(meshHandle, bounds, assetPath, false, subCount);
            renderer.materialIndices.assign(static_cast<size_t>(subCount), -1);
            renderer.appliedMaterialPaths.resize(static_cast<size_t>(subCount));
            created.AddComponent<MaterialComponent>(Vec3(0.75f, 0.55f, 0.35f));
            auto& tc = created.GetComponent<TransformComponent>();
            tc.position = spawnPos;
            if (hasPlacementHit && placementHit.normal.y > 0.5f) {
                tc.position.y += -bounds.min.y;
            }
            ctx->SetSelectionChangedCallback(nullptr);
            ctx->SelectEntity(static_cast<entt::entity>(created));
            ctx->SetSelectionChangedCallback([this](entt::entity) {
                refreshDetailsPanel();
            });
            ctx->MarkDirty();
            break;
        }
        // Resolve the mesh file against the project root.
        // The asset path stored by AddActorsPanel is e.g. ".engine/meshes/Cube.glb"
        // which is relative to the project root (copied there by SyncEngineAssets).
        namespace fs = std::filesystem;
        fs::path meshPath;
        if (m_projectContext && m_projectContext->HasProject()) {
            meshPath = fs::path(m_projectContext->GetProjectRootDir()) / entry.assetPath.toStdString();
            std::cout << "[SpawnDrop] projectRoot path: " << meshPath << "\n"; std::cout.flush();
        }
        // Fallback to exe dir for legacy paths like "assets/meshes/Cube.glb"
        if (meshPath.empty() || !fs::exists(meshPath)) {
            fs::path exeDir = fs::path(Hephaestus::Tools::PathUtils::ExecutableDir().toStdWString());
            meshPath = exeDir / entry.assetPath.toStdString();
            std::cout << "[SpawnDrop] fallback exe path: " << meshPath << "\n"; std::cout.flush();
        }
        if (!fs::exists(meshPath)) {
            statusBar()->showMessage(tr("Mesh file not found: %1").arg(entry.assetPath), 3000);
            std::cout << "[SpawnDrop] ABORT: mesh not found: " << meshPath << "\n";
            return;
        }
        std::cout << "[SpawnDrop] resolved meshPath: " << meshPath << "\n"; std::cout.flush();

        std::string normalizedKey = meshPath.lexically_normal().generic_string();
        size_t meshHandle = frameRenderer->GetMeshHandle(normalizedKey);
        std::cout << "[SpawnDrop] lookup normalizedKey='" << normalizedKey << "' -> handle=" << meshHandle << "\n"; std::cout.flush();

        // Also try the relative asset path as a lookup key (alias)
        if (meshHandle == SIZE_MAX) {
            std::string relativeKey = fs::path(entry.assetPath.toStdString())
                                         .lexically_normal().generic_string();
            meshHandle = frameRenderer->GetMeshHandle(relativeKey);
            std::cout << "[SpawnDrop] lookup relativeKey='" << relativeKey << "' -> handle=" << meshHandle << "\n"; std::cout.flush();
        }

        if (meshHandle == SIZE_MAX) {
            std::cout << "[SpawnDrop] Loading mesh from disk...\n"; std::cout.flush();
            MeshData meshData;
            std::string loadError;
            if (!ModelImporter::LoadModel(meshPath, meshData, loadError) ||
                meshData.vertices.empty() || meshData.indices.empty()) {
                statusBar()->showMessage(tr("Failed to load mesh: %1").arg(QString::fromStdString(loadError)), 3000);
                std::cout << "[SpawnDrop] ABORT: load failed: " << loadError << "\n";
                return;
            }
            std::cout << "[SpawnDrop] Loaded: verts=" << meshData.vertices.size()
                      << " indices=" << meshData.indices.size() << "\n"; std::cout.flush();
            std::string meshName = meshPath.stem().string();
            meshHandle = m_engine->uploadMeshToGPU(meshName, meshData, meshPath, true);
            std::cout << "[SpawnDrop] Uploaded -> handle=" << meshHandle << "\n"; std::cout.flush();
            if (meshHandle == MeshRendererComponent::InvalidMesh) {
                statusBar()->showMessage(tr("Failed to upload mesh to GPU."), 3000);
                return;
            }
        }

        std::cout << "[SpawnDrop] Final meshHandle=" << meshHandle
                  << " librarySize=" << frameRenderer->GetMeshCount() << "\n"; std::cout.flush();

        // Register the portable relative asset path as an alias so that
        // RebuildSceneMeshes can find this mesh after scene reload / play-mode exit.
        {
            std::string relativeKey = std::filesystem::path(entry.assetPath.toStdString())
                                          .lexically_normal().generic_string();
            if (relativeKey != normalizedKey) {
                frameRenderer->AddMeshAlias(relativeKey, meshHandle);
            }
        }

        std::cout << "[SpawnDrop] Creating entity '" << entityName << "'...\n"; std::cout.flush();
        Entity created = scene->CreateEntity(entityName);
        if (!created) { std::cout << "[SpawnDrop] ABORT: CreateEntity failed\n"; break; }

        std::cout << "[SpawnDrop] Getting MeshGPU for handle " << meshHandle << "...\n"; std::cout.flush();
        MeshGPU* meshGPU = frameRenderer->GetMesh(meshHandle);
        std::cout << "[SpawnDrop] meshGPU=" << (void*)meshGPU << "\n"; std::cout.flush();
        int subCount = (meshGPU && !meshGPU->submeshes.empty())
            ? static_cast<int>(meshGPU->submeshes.size()) : 1;
        AABB bounds = meshGPU ? meshGPU->bounds : AABB{};

        std::cout << "[SpawnDrop] Adding MeshRendererComponent...\n"; std::cout.flush();
        auto& renderer = created.AddComponent<MeshRendererComponent>();
        renderer.SetMesh(meshHandle, bounds, entry.assetPath.toStdString(), true, subCount);
        renderer.materialIndices.assign(static_cast<size_t>(subCount), -1);
        if (meshGPU && !meshGPU->submeshes.empty()) {
            for (size_t i = 0; i < meshGPU->submeshes.size(); ++i)
                renderer.materialIndices[i] = meshGPU->submeshes[i].materialSlot;
        }
        renderer.appliedMaterialPaths.resize(static_cast<size_t>(subCount));
        created.AddComponent<MaterialComponent>(Vec3(1.0f, 1.0f, 1.0f));

        auto& tc = created.GetComponent<TransformComponent>();
        tc.position = spawnPos;
        if (hasPlacementHit && placementHit.normal.y > 0.5f) {
            tc.position.y += -bounds.min.y;
        }
        tc.rotation = Vec3(0.0f, 0.0f, 0.0f);
        const std::string meshStem = meshPath.stem().string();
        if (meshStem == "Capsule" || meshStem == "Cylinder") {
            tc.rotation.x = 1.57079632679f;
        }
        tc.scale    = Vec3(1.0f, 1.0f, 1.0f);

        std::cout << "[SpawnDrop] Selecting entity...\n"; std::cout.flush();
        // Suppress the selection-changed callback to avoid a premature
        // refreshDetailsPanel() that creates inspectors and scans the
        // filesystem while the spawn handler is still running.  We'll
        // call refreshDetailsPanel() once at the end of this function.
        ctx->SetSelectionChangedCallback(nullptr);
        ctx->SelectEntity(static_cast<entt::entity>(created));
        ctx->SetSelectionChangedCallback([this](entt::entity) {
            refreshDetailsPanel();
        });
        std::cout << "[SpawnDrop] Mesh spawn complete.\n"; std::cout.flush();
        break;
    }

    case SpawnableEntry::Kind::Light: {
        Entity created = scene->CreateEntity(entityName);
        if (!created) break;

        LightComponent light;
        if (entry.typeId == "DirectionalLight") {
            light = LightComponent::CreateDirectional(Vec3(0.0f, -1.0f, 0.0f));
        } else if (entry.typeId == "PointLight") {
            light = LightComponent::CreatePoint(Vec3(1,1,1), 1.0f, 10.0f);
        } else if (entry.typeId == "SpotLight") {
            light = LightComponent::CreateSpot(Vec3(1,1,1), 1.0f, 10.0f);
        } else {
            light = LightComponent::CreatePoint();
        }
        created.AddComponent<LightComponent>(light);

        auto& tc = created.GetComponent<TransformComponent>();
        tc.position = spawnPos;
        tc.rotation = Vec3(0.0f, 0.0f, 0.0f);
        tc.scale    = Vec3(1.0f, 1.0f, 1.0f);

        ctx->SetSelectionChangedCallback(nullptr);
        ctx->SelectEntity(static_cast<entt::entity>(created));
        ctx->SetSelectionChangedCallback([this](entt::entity) {
            refreshDetailsPanel();
        });
        break;
    }

    case SpawnableEntry::Kind::Camera: {
        Entity created = scene->CreateEntity(entityName);
        if (!created) break;

        // CameraComponent — inactive by default so it won't override editor camera
        CameraComponent camComp;
        camComp.fov      = 60.0f;
        camComp.nearPlane = 0.1f;
        camComp.farPlane  = 1000.0f;
        camComp.isActive  = false;
        camComp.showEditorMesh = true;
        created.AddComponent<CameraComponent>(camComp);

        auto& tc = created.GetComponent<TransformComponent>();
        tc.position = spawnPos;
        tc.rotation = Vec3(0.0f, 0.0f, 0.0f);
        tc.scale    = Vec3(1.0f, 1.0f, 1.0f);

        ctx->SetSelectionChangedCallback(nullptr);
        ctx->SelectEntity(static_cast<entt::entity>(created));
        ctx->SetSelectionChangedCallback([this](entt::entity) {
            refreshDetailsPanel();
        });
        break;
    }

    case SpawnableEntry::Kind::Character: {
        std::string portablePath = entry.assetPath.toStdString();
        if (portablePath.rfind("assets/", 0) != 0 && portablePath.rfind(".engine/", 0) != 0) {
            portablePath = "assets/" + portablePath;
        }
        Entity created = scene->CreateEntity(entityName);
        if (!created) {
            break;
        }
        created.GetComponent<TransformComponent>().position = spawnPos;
        auto& character = created.AddComponent<CharacterComponent>();
        character.characterPath = portablePath;
        std::filesystem::path root;
        if (m_projectContext && m_projectContext->HasProject()) {
            root = m_projectContext->GetProjectRootDir();
        } else {
            root = AssetManager::GetInstance().GetProjectRoot();
        }
        std::string bindError;
        if (!Hephaestus::CharacterRuntime::BindEntity(created, root, true, bindError)) {
            if (m_notifications) {
                m_notifications->ShowError(tr("Character spawn: %1")
                                               .arg(QString::fromStdString(bindError)));
            }
        }
        if (m_engine) {
            m_engine->RebuildSceneMeshes();
        }
        ctx->SetSelectionChangedCallback(nullptr);
        ctx->SelectEntity(static_cast<entt::entity>(created));
        ctx->SetSelectionChangedCallback([this](entt::entity) {
            refreshDetailsPanel();
        });
        break;
    }

    case SpawnableEntry::Kind::Audio: {
        Entity created = scene->CreateEntity(entityName);
        if (!created) break;

        AudioSourceComponent source;
        source.clipPath = entry.assetPath.toStdString();
        created.AddComponent<AudioSourceComponent>(source);
        created.GetComponent<TransformComponent>().position = spawnPos;

        ctx->SetSelectionChangedCallback(nullptr);
        ctx->SelectEntity(static_cast<entt::entity>(created));
        ctx->SetSelectionChangedCallback([this](entt::entity) {
            refreshDetailsPanel();
        });
        break;
    }

    case SpawnableEntry::Kind::Prefab: {
        namespace fs = std::filesystem;
        std::string portablePath = entry.assetPath.toStdString();
        if (portablePath.rfind("assets/", 0) != 0 && portablePath.rfind(".engine/", 0) != 0) {
            portablePath = "assets/" + portablePath;
        }

        fs::path prefabAbs;
        if (m_projectContext && m_projectContext->HasProject()) {
            prefabAbs = fs::path(m_projectContext->GetProjectRootDir()) / portablePath;
        }
        if (prefabAbs.empty() || !fs::exists(prefabAbs)) {
            auto& assetMgr = AssetManager::GetInstance();
            if (assetMgr.HasValidAssetRoot()) {
                std::string rel = entry.assetPath.toStdString();
                if (rel.rfind("assets/", 0) == 0) {
                    rel = rel.substr(7);
                }
                prefabAbs = fs::path(assetMgr.ToAbsolutePath(rel));
            }
        }
        if (prefabAbs.empty() || !fs::exists(prefabAbs)) {
            statusBar()->showMessage(tr("Prefab not found: %1").arg(entry.assetPath), 3000);
            std::cout << "[SpawnDrop] ABORT: prefab not found: " << portablePath << "\n";
            return;
        }

        PrefabAsset asset;
        std::string loadError;
        if (!PrefabOverrideSystem::LoadAsset(prefabAbs.generic_string(), asset, loadError) &&
            !PrefabOverrideSystem::LoadAsset(portablePath, asset, loadError)) {
            statusBar()->showMessage(
                tr("Failed to load prefab: %1").arg(QString::fromStdString(loadError)), 3000);
            return;
        }

        PrefabInstantiateOptions options;
        options.prefabPath = portablePath;
        options.attachInstanceComponent = true;
        Entity created = PrefabInstantiator::Instantiate(*scene, asset, options);
        if (!created) {
            statusBar()->showMessage(tr("Failed to instantiate prefab."), 3000);
            return;
        }

        // Keep a unique root name when the scene already has an entity with this tag.
        if (created.HasComponent<TagComponent>()) {
            auto& tag = created.GetComponent<TagComponent>();
            std::string rootName = tag.tag.empty() ? baseName : tag.tag;
            std::string unique = rootName;
            int suffix = 1;
            while (true) {
                Entity existing = scene->FindEntityByName(unique);
                if (!existing || static_cast<entt::entity>(existing) == static_cast<entt::entity>(created)) {
                    break;
                }
                unique = rootName + " (" + std::to_string(suffix++) + ")";
            }
            tag.tag = unique;
            entityName = unique;
        }

        if (created.HasComponent<TransformComponent>()) {
            auto& tc = created.GetComponent<TransformComponent>();
            tc.position = spawnPos;
            if (created.HasComponent<PrefabInstanceComponent>()) {
                auto& inst = created.GetComponent<PrefabInstanceComponent>();
                PrefabComponentIO::CopyLocalTransform(inst.rootTransform, tc);
                inst.hasRootTransform = true;
                inst.prefabPath = portablePath;
            }
        }

        m_engine->RebuildSceneMeshes();

        ctx->SetSelectionChangedCallback(nullptr);
        ctx->SelectEntity(static_cast<entt::entity>(created));
        ctx->SetSelectionChangedCallback([this](entt::entity) {
            refreshDetailsPanel();
        });
        break;
    }

    } // switch

    // ── Finalise ────────────────────────────────────────────────────────────
    std::cout << "[SpawnDrop] Finalising: MarkDirty...\n"; std::cout.flush();
    ctx->MarkDirty();
    std::cout << "[SpawnDrop] rebuildOutliner...\n"; std::cout.flush();
    rebuildOutliner();
    std::cout << "[SpawnDrop] refreshDetailsPanel...\n"; std::cout.flush();
    refreshDetailsPanel();

    std::cout << "[SpawnDrop] === handleViewportSpawnDrop DONE: " << entityName << " ===\n";
    std::cout.flush();
    statusBar()->showMessage(tr("Added actor: %1").arg(QString::fromStdString(entityName)), 3000);

    } catch (const std::exception& e) {
        std::cerr << "[SpawnDrop] EXCEPTION: " << e.what() << "\n";
        std::cerr.flush();
    } catch (...) {
        std::cerr << "[SpawnDrop] UNKNOWN EXCEPTION\n";
        std::cerr.flush();
    }
}

void EditorMainWindow::syncPrefabOverridesForSelection()
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    Scene* scene = ctx->GetActiveScene();

    std::vector<entt::entity> entities = ctx->GetSelectedEntities();
    InspectorSelectionModel model = resolveInspectorSelection();
    for (entt::entity handle : model.entities) {
        if (std::find(entities.begin(), entities.end(), handle) == entities.end()) {
            entities.push_back(handle);
        }
    }
    if (entities.empty() && ctx->HasSelection()) {
        entities.push_back(ctx->GetSelectedEntity());
    }
    for (entt::entity handle : entities) {
        Entity entity(handle, scene);
        if (entity) {
            PrefabOverrideSystem::SyncEntityOverrides(*scene, entity);
        }
    }
    updatePrefabInspectorUi();
}

void EditorMainWindow::updatePrefabInspectorUi()
{
    if (!m_prefabBar || !m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) {
        m_prefabBar->Clear();
        return;
    }

    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) {
        m_prefabBar->Clear();
        return;
    }

    const entt::entity entity = model.primary;
    m_prefabBar->Bind(ctx, entity);

    Scene* scene = ctx->GetActiveScene();
    std::string sharedPath;
    bool mixedSources = false;
    int instanceCount = 0;
    for (entt::entity handle : model.entities) {
        Entity ent(handle, scene);
        if (!ent || !PrefabOverrideSystem::IsPrefabInstanceEntity(ent)) continue;
        ++instanceCount;
        auto* inst = PrefabOverrideSystem::TryGetInstance(*scene, ent);
        if (!inst) continue;
        if (sharedPath.empty()) {
            sharedPath = inst->prefabPath;
        } else if (sharedPath != inst->prefabPath) {
            mixedSources = true;
        }
    }
    m_prefabBar->SetApplyEnabled(!mixedSources && instanceCount <= 1);

    auto styleSpin = [](QDoubleSpinBox* box, bool overridden) {
        if (!box) return;
        box->setStyleSheet(overridden ? QStringLiteral("QDoubleSpinBox { border-left: 2px solid #c9a227; }") : QString());
    };

    bool pos = false, rot = false, scl = false;
    for (entt::entity handle : model.entities) {
        Entity ent(handle, scene);
        if (ent && PrefabOverrideSystem::IsPrefabInstanceEntity(ent) && !PrefabOverrideSystem::IsInstanceRoot(ent)) {
            if (auto* inst = PrefabOverrideSystem::TryGetInstance(*scene, ent)) {
                const auto* node = PrefabOverrideSystem::TryGetNode(ent);
                if (node) {
                    pos = pos || PrefabOverrideSystem::IsPropertyOverridden(*inst, node->nodeId, "Transform", "position");
                    rot = rot || PrefabOverrideSystem::IsPropertyOverridden(*inst, node->nodeId, "Transform", "rotation");
                    scl = scl || PrefabOverrideSystem::IsPropertyOverridden(*inst, node->nodeId, "Transform", "scale");
                }
            }
        }
    }
    styleSpin(m_posX, pos); styleSpin(m_posY, pos); styleSpin(m_posZ, pos);
    styleSpin(m_rotX, rot); styleSpin(m_rotY, rot); styleSpin(m_rotZ, rot);
    styleSpin(m_scaleX, scl); styleSpin(m_scaleY, scl); styleSpin(m_scaleZ, scl);
}

void EditorMainWindow::decoratePrefabComponentInspector(QWidget* widget, const std::string& componentName)
{
    if (!widget || !m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    InspectorSelectionModel model = resolveInspectorSelection();
    if (model.Empty()) return;
    Scene* scene = ctx->GetActiveScene();

    std::string type = componentName;
    if (type == "Mesh Renderer") type = "MeshRenderer";
    else if (type == "Mesh Collider") type = "MeshCollider";
    else if (type == "Rigid Body") type = "RigidBody";
    else if (type == "Physics Shape") type = "PhysicsShape";
    else if (type == "Scripts") type = "ScriptComponent";
    else if (type == "Lua Scripts" || type == "Lua Script") type = "LuaScript";
    else if (type == "Audio Source") type = "AudioSource";

    bool overridden = false;
    bool anyInstance = false;
    std::string sharedPath;
    bool mixedSources = false;
    for (entt::entity handle : model.entities) {
        Entity entity(handle, scene);
        if (!entity || !PrefabOverrideSystem::IsPrefabInstanceEntity(entity)) continue;
        anyInstance = true;
        auto* inst = PrefabOverrideSystem::TryGetInstance(*scene, entity);
        const auto* node = PrefabOverrideSystem::TryGetNode(entity);
        if (inst && node && PrefabOverrideSystem::IsComponentOverridden(*inst, node->nodeId, type)) {
            overridden = true;
        }
        if (inst) {
            if (sharedPath.empty()) sharedPath = inst->prefabPath;
            else if (sharedPath != inst->prefabPath) mixedSources = true;
        }
    }
    if (overridden) {
        if (QWidget* card = componentCardOf(widget)) {
            card->setProperty("overridden", true);
        }
    }

    const bool allowApply = anyInstance && model.Count() == 1 && !mixedSources;
    const bool allowRevert = anyInstance;

    widget->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(widget, &QWidget::customContextMenuRequested, widget,
        [this, widget, type, allowApply, allowRevert](const QPoint& pos) {
            QMenu menu(widget);
            menu.addAction(tr("Copy Component"), this, [this, type]() { copyInspectorComponent(type); });
            auto* pasteAct = menu.addAction(tr("Paste Values"), this, [this, type]() {
                pasteInspectorComponentValues(type);
            });
            pasteAct->setEnabled(ComponentClipboard::Instance().HasComponent() &&
                (ComponentClipboard::Instance().Type() == type ||
                 InspectorDisplayName(ComponentClipboard::Instance().Type()) == InspectorDisplayName(type)));
            menu.addAction(tr("Reset to Default"), this, [this, type]() { resetInspectorComponent(type); });
            menu.addSeparator();
            if (allowRevert) {
                menu.addAction(tr("Revert Component"), this, [this, type]() {
                    InspectorSelectionModel model = resolveInspectorSelection();
                    if (!model.Empty()) runPrefabRevert(model.primary, type, "");
                });
            }
            if (allowApply) {
                menu.addAction(tr("Apply Component to Prefab"), this, [this, type]() {
                    InspectorSelectionModel model = resolveInspectorSelection();
                    if (!model.Empty()) runPrefabApply(model.primary, type, "", false);
                });
            }
            menu.addSeparator();
            menu.addAction(tr("Remove Component"), this, [this, type]() { removeInspectorComponent(type); });
            menu.exec(widget->mapToGlobal(pos));
        });
}

void EditorMainWindow::createPrefabFromSelection(entt::entity entity)
{
    if (!m_engine) return;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return;
    Scene* scene = ctx->GetActiveScene();
    Entity ent(entity, scene);
    if (!ent) return;

    QString defaultName = QString::fromStdString(
        ent.HasComponent<TagComponent>() ? ent.GetComponent<TagComponent>().tag : "NewPrefab");
    QString filePath = QFileDialog::getSaveFileName(this,
        tr("Save Prefab As"), defaultName + ".hpprefab",
        tr("Prefab Files (*.hpprefab)"));
    if (filePath.isEmpty()) return;
    if (!filePath.endsWith(".hpprefab", Qt::CaseInsensitive)) {
        filePath += ".hpprefab";
    }

    EditorUndoManager::PushSnapshot(ctx, "Create prefab from selection");
    Entity instance;
    std::string error;
    if (!PrefabOverrideSystem::CreateFromSelection(*scene, ent, filePath.toStdString(), &instance, error)) {
        QMessageBox::warning(this, tr("Error"),
            tr("Failed to create prefab: %1").arg(QString::fromStdString(error)));
        return;
    }

    if (m_assetBrowser) {
        m_assetBrowser->NavigateTo(QFileInfo(filePath).path());
    }
    m_engine->RebuildSceneMeshes();
    ctx->MarkDirty();
    rebuildOutliner();
    if (instance) {
        ctx->SelectEntity(static_cast<entt::entity>(instance));
    }
    statusBar()->showMessage(tr("Prefab created: %1").arg(filePath), 3000);
}

bool EditorMainWindow::runPrefabRevert(entt::entity entity, const std::string& component, const std::string& property)
{
    if (!m_engine) return false;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return false;
    Scene* scene = ctx->GetActiveScene();
    Entity ent(entity, scene);
    if (!ent) return false;

    EditorUndoManager::PushSnapshot(ctx, "Revert prefab override");
    bool ok = false;
    if (component == "*" && property == "*") {
        ok = PrefabOverrideSystem::RevertAll(*scene, ent);
    } else if (component.empty() && property.empty()) {
        ok = PrefabOverrideSystem::RevertNode(*scene, ent);
    } else if (property.empty()) {
        ok = PrefabOverrideSystem::RevertComponent(*scene, ent, component);
    } else {
        ok = PrefabOverrideSystem::RevertProperty(*scene, ent, component, property);
    }
    if (ok) {
        m_engine->RebuildSceneMeshes();
        ctx->MarkDirty();
        refreshDetailsPanel();
        statusBar()->showMessage(tr("Prefab override reverted"), 2000);
    }
    return ok;
}

bool EditorMainWindow::runPrefabApply(entt::entity entity, const std::string& component, const std::string& property, bool confirmAll)
{
    if (!m_engine) return false;
    auto* ctx = m_engine->GetEditorContext();
    if (!ctx || !ctx->HasActiveScene()) return false;
    Scene* scene = ctx->GetActiveScene();
    Entity ent(entity, scene);
    if (!ent) return false;
    auto* inst = PrefabOverrideSystem::TryGetInstance(*scene, ent);
    if (!inst) return false;

    if (confirmAll) {
        auto result = QMessageBox::question(this, tr("Apply All Overrides"),
            tr("Apply all overrides on this instance to the source prefab?\nThis updates every instance of that prefab."),
            QMessageBox::Yes | QMessageBox::No);
        if (result != QMessageBox::Yes) return false;
    }

    std::string backup;
    {
        std::ifstream in(inst->prefabPath);
        if (in.is_open()) {
            backup.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
    }
    EditorUndoManager::PushSnapshotWithPrefabBackup(ctx, "Apply prefab override", inst->prefabPath, backup);

    std::string error;
    bool ok = false;
    if (component == "*" && property == "*") {
        ok = PrefabOverrideSystem::ApplyAllToPrefab(*scene, ent, error);
    } else if (component.empty() && property.empty()) {
        ok = PrefabOverrideSystem::ApplyNodeToPrefab(*scene, ent, error);
    } else if (property.empty()) {
        ok = PrefabOverrideSystem::ApplyComponentToPrefab(*scene, ent, component, error);
    } else {
        ok = PrefabOverrideSystem::ApplyPropertyToPrefab(*scene, ent, component, property, error);
    }

    if (!ok) {
        QMessageBox::warning(this, tr("Apply Failed"),
            tr("Could not apply to prefab: %1").arg(QString::fromStdString(error)));
        return false;
    }

    m_engine->RebuildSceneMeshes();
    ctx->MarkDirty();
    rebuildOutliner();
    refreshDetailsPanel();
    statusBar()->showMessage(tr("Applied changes to prefab"), 3000);
    return true;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
