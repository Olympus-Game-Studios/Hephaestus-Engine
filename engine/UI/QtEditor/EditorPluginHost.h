#pragma once
#if HEPHAESTUS_EDITOR_QT_UI

#include "../../PluginAPI/PluginAPI.h"
#include "../../Plugins/PluginManifest.h"
#include <QIcon>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class QAction;
class QDockWidget;
class QMainWindow;
class QMenu;

namespace Hephaestus {
class EditorContext;
class ProjectContext;
namespace Plugins { class PluginSystem; }
namespace Automation { class ToolRegistry; class PluginToolBridge; }
namespace QtEditor {

class EditorPluginHost {
public:
    EditorPluginHost(QMainWindow*, Plugins::PluginSystem*, std::function<EditorContext*()>,
                     ProjectContext*, std::function<void()> sceneChanged, Automation::ToolRegistry* tools);
    ~EditorPluginHost();
    std::unique_ptr<Plugins::IPluginContext> CreateContext(const Plugins::PluginManifest&);
    void PopulateContextMenu(const char* location, QMenu&, const QString& portablePath = {});
    void PopulateAssetCreationMenu(QMenu&, const QString& targetDirectory);
    void PopulateImporterMenu(QMenu&, const QString& targetDirectory);
    bool OpenCustomAsset(const QString& portablePath);
    QString CustomAssetTypeId(const QString& path) const;
    QString CustomAssetDisplayName(const QString& path) const;
    QIcon CustomAssetIcon(const QString& path) const;
    /// Title bar buttons a plugin registered, as (command id, tool tip).
    std::vector<std::pair<std::string, QString>> HeaderButtons(const std::string& pluginId) const;
    bool IsHeaderButtonShown(const std::string& pluginId, const std::string& commandId) const;
    void SetHeaderButtonShown(const std::string& pluginId, const std::string& commandId, bool shown);

private:
    class Context;
    friend class Context;
    struct Command;
    struct AssetType;
    struct Importer;
    struct ContextAction;
    struct Setting;
    struct HeaderButton;
    bool RegisterHeaderButton(const Plugins::PluginManifest&, const Plugins::HeaderButtonDescriptor&);
    void RemoveOwner(const std::string&);
    QAction* FindCommand(const std::string&) const;
    bool RegisterCommand(const std::string&, const Plugins::CommandDescriptor&);
    bool RegisterPanel(const std::string&, const Plugins::PanelDescriptor&);
    bool RegisterMenu(const std::string&, const Plugins::MenuActionDescriptor&);
    bool RegisterToolbar(const std::string&, const Plugins::ToolbarActionDescriptor&);
    bool RegisterContext(const std::string&, const Plugins::ContextActionDescriptor&);
    bool ShowPanel(const std::string&);
    bool RegisterAssetType(const std::string&, const Plugins::AssetTypeDescriptor&);
    bool RegisterImporter(const std::string&, const Plugins::ImporterDescriptor&);
    bool ImportAssetPack(const Plugins::AssetPackImportRequest&, Plugins::AssetPackImportResult&);
    bool RegisterSetting(const std::string&, const Plugins::SettingDescriptor&);
    bool RegisterMaterialNode(const std::string& owner, const Plugins::MaterialNodeDescriptor& descriptor);
    std::uint64_t CreatePrimitive(Plugins::PrimitiveKind, const char*);
    std::uint64_t CreatePrimitiveEx(const Plugins::PrimitiveRequest&);
    bool RegisterToolbarButton(const Plugins::PluginManifest&, const Plugins::ToolbarButtonDescriptor&);

    QMainWindow* window_ = nullptr;
    Plugins::PluginSystem* system_ = nullptr;
    std::function<EditorContext*()> editorContext_;
    ProjectContext* projectContext_ = nullptr;
    std::function<void()> sceneChanged_;
    std::vector<std::unique_ptr<Command>> commands_;
    std::vector<std::unique_ptr<AssetType>> assetTypes_;
    std::vector<std::unique_ptr<Importer>> importers_;
    std::vector<std::unique_ptr<ContextAction>> contextActions_;
    std::vector<std::unique_ptr<Setting>> settings_;
    std::vector<QPointer<QDockWidget>> panels_;
    std::vector<std::unique_ptr<HeaderButton>> headerButtons_;
    std::uint64_t nextTransaction_ = 1;
    std::vector<std::pair<std::uint64_t, EditorContext*>> transactions_;
    std::shared_ptr<Automation::PluginToolBridge> toolBridge_;
    std::uint64_t saveListener_ = 0;
    std::uint64_t retargetListener_ = 0;
};

} // namespace QtEditor
} // namespace Hephaestus
#endif
