#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorPluginHost.h"
#include "EditorDockTitleBar.h"
#include "EditorAssetServices.h"
#include "EditorUndoCommand.h"
#include "../../Assets/AssetIntake.h"
#include "../../Assets/AssetPathPolicy.h"
#include "../../Automation/PluginToolBridge.h"
#include "AssetBrowserWidget.h"
#include "../../Core/EditorContext.h"
#include "../../Core/ProjectContext.h"
#include "../../Plugins/PluginSystem.h"
#include "../../Scene/Components.h"
#include "../../Scene/Scene.h"
#include <QAction>
#include <QDockWidget>
#include <QFileInfo>
#include <QFileDialog>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QToolBar>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <ranges>

namespace Hephaestus::QtEditor {

struct EditorPluginHost::Command { std::string owner, id, name; QAction* action = nullptr; };
struct EditorPluginHost::AssetType { std::string owner, id, extension, name, icon; Plugins::AssetCreateCallback create=nullptr; Plugins::AssetPathCallback open=nullptr; Plugins::AssetPathCallback dirty=nullptr,save=nullptr,close=nullptr; Plugins::AssetRetargetCallback retarget=nullptr; void* userData=nullptr; };
struct EditorPluginHost::Importer { std::string owner, id, extensions, result; Plugins::ImportCallback callback=nullptr; void* userData=nullptr; };
struct EditorPluginHost::ContextAction { std::string owner, location, command; };
struct EditorPluginHost::Setting { std::string owner,id,name,description,defaultValue; Plugins::SettingScope scope; mutable std::string cache; };

class EditorPluginHost::Context final : public Plugins::IPluginContext,
 public Plugins::ICommandService, public Plugins::IUIService, public Plugins::IAssetService,
 public Plugins::ISceneService, public Plugins::IUndoService, public Plugins::IProjectService,
 public Plugins::ISettingsService, public Plugins::IAutomationToolService {
public:
 Context(EditorPluginHost& h, Plugins::PluginManifest m):host(h),manifest(std::move(m)){}
 ~Context() override { host.RemoveOwner(manifest.id); }
 const char* PluginId() const override{return manifest.id.c_str();}
 Plugins::ICommandService& Commands()override{return *this;} Plugins::IUIService& UI()override{return *this;}
 Plugins::IAssetService& Assets()override{return *this;} Plugins::ISceneService& Scene()override{return *this;}
 Plugins::IUndoService& Undo()override{return *this;} Plugins::IProjectService& Project()override{return *this;}
 Plugins::ISettingsService& Settings()override{return *this;}
 Plugins::IAutomationToolService* AutomationTools()override{return host.toolBridge_?this:nullptr;}
 bool RegisterTool(const Plugins::AutomationToolDescriptor&d)override{std::string e;return host.toolBridge_&&host.toolBridge_->Register(manifest.id,d,e);}
 bool CompleteCall(std::uint64_t id,bool ok,const char*json)override{return host.toolBridge_&&host.toolBridge_->Complete(id,ok,json);}
 bool Register(const Plugins::CommandDescriptor&d)override{return host.RegisterCommand(manifest.id,d);}
 bool Execute(const char*id)override{if(auto*a=host.FindCommand(id?id:"")){a->trigger();return true;}return false;}
 bool RegisterPanel(const Plugins::PanelDescriptor&d)override{return host.RegisterPanel(manifest.id,d);}
 bool RegisterMenuAction(const Plugins::MenuActionDescriptor&d)override{return host.RegisterMenu(manifest.id,d);}
 bool RegisterToolbarAction(const Plugins::ToolbarActionDescriptor&d)override{return host.RegisterToolbar(manifest.id,d);}
 bool RegisterContextAction(const Plugins::ContextActionDescriptor&d)override{return host.RegisterContext(manifest.id,d);}
 bool ShowPanel(const char*id)override{return host.ShowPanel(id?id:"");}
 bool RegisterAssetType(const Plugins::AssetTypeDescriptor&d)override{return host.RegisterAssetType(manifest.id,d);}
 bool RegisterImporter(const Plugins::ImporterDescriptor&d)override{return host.RegisterImporter(manifest.id,d);}
 bool ImportAssetPack(const Plugins::AssetPackImportRequest&r,Plugins::AssetPackImportResult&out)override{return host.ImportAssetPack(r,out);}
 bool HasScene()const override{auto*c=host.editorContext_();return c&&c->HasActiveScene();}
 std::uint64_t CreatePrimitive(Plugins::PrimitiveKind k,const char*n)override{return host.CreatePrimitive(k,n);}
 bool DeleteEntity(std::uint64_t id)override{auto*c=host.editorContext_();if(!c||!c->HasActiveScene()||id==0)return false;auto&e=c->GetActiveScene()->GetRegistry();auto h=static_cast<entt::entity>(id-1);if(!e.valid(h))return false;c->GetActiveScene()->DestroyEntity(Entity(h,c->GetActiveScene()));c->MarkDirty();host.sceneChanged_();return true;}
 bool SelectEntity(std::uint64_t id)override{auto*c=host.editorContext_();if(!c||id==0)return false;c->SelectEntity(static_cast<entt::entity>(id-1));return true;}
 std::uint64_t Begin(const char*l)override{auto*c=host.editorContext_();if(!c||!c->HasActiveScene())return 0;EditorUndoManager::PushSnapshot(c,l?l:"Plugin edit");auto t=host.nextTransaction_++;host.transactions_.push_back({t,c});return t;}
 bool Commit(std::uint64_t t)override{auto i=std::ranges::find_if(host.transactions_,[t](auto&x){return x.first==t;});if(i==host.transactions_.end())return false;i->second->MarkDirty();host.transactions_.erase(i);host.sceneChanged_();return true;}
 bool Rollback(std::uint64_t t)override{auto i=std::ranges::find_if(host.transactions_,[t](auto&x){return x.first==t;});if(i==host.transactions_.end())return false;std::string e;bool ok=EditorUndoManager::RestoreAndDiscardLastSnapshot(i->second,e);host.transactions_.erase(i);host.sceneChanged_();return ok;}
 const char* ProjectRoot()const override{projectCache=host.projectContext_&&host.projectContext_->HasProject()?host.projectContext_->GetProjectRootDir():"";return projectCache.c_str();}
 const char* PluginRoot()const override{pluginCache=manifest.root.string();return pluginCache.c_str();}
 bool ResolveAssetPath(const char*p,char*out,std::size_t size)const override{if(!p||!out||!size)return false;auto r=host.system_->ResolvePluginAsset(p);if(r.empty()&&host.projectContext_&&host.projectContext_->HasProject())r=AssetPathPolicy::ResolvePortableKey(host.projectContext_->GetProjectRootDir(),p);auto v=r.string();if(v.size()+1>size)return false;std::memcpy(out,v.c_str(),v.size()+1);return true;}
 bool Register(const Plugins::SettingDescriptor&d)override{return host.RegisterSetting(manifest.id,d);}
 const char* Get(const char*id)const override{for(auto&s:host.settings_)if(s->id==(id?id:"")){if(s->scope==Plugins::SettingScope::Project&&host.projectContext_&&host.projectContext_->HasProject()){try{std::ifstream in(std::filesystem::path(host.projectContext_->GetProjectRootDir())/".hephaestus"/"plugin-settings.json");if(in){auto j=nlohmann::json::parse(in);s->cache=j.value(s->owner,nlohmann::json::object()).value(s->id,s->defaultValue);return s->cache.c_str();}}catch(...){}s->cache=s->defaultValue;return s->cache.c_str();}QSettings q("Hephaestus","Editor");s->cache=q.value(QString("plugins/%1/%2").arg(QString::fromStdString(s->owner),QString::fromStdString(s->id)),QString::fromStdString(s->defaultValue)).toString().toStdString();return s->cache.c_str();}return "";}
 bool Set(const char*id,const char*v)override{for(auto&s:host.settings_)if(s->id==(id?id:"")){if(s->scope==Plugins::SettingScope::Project){if(!host.projectContext_||!host.projectContext_->HasProject())return false;try{auto dir=std::filesystem::path(host.projectContext_->GetProjectRootDir())/".hephaestus";std::filesystem::create_directories(dir);auto path=dir/"plugin-settings.json";nlohmann::json j=nlohmann::json::object();std::ifstream in(path);if(in)j=nlohmann::json::parse(in,nullptr,true,true);j[s->owner][s->id]=v?v:"";std::ofstream out(path,std::ios::trunc);out<<j.dump(2)<<'\n';return static_cast<bool>(out);}catch(...){return false;}}QSettings q("Hephaestus","Editor");q.setValue(QString("plugins/%1/%2").arg(QString::fromStdString(s->owner),QString::fromStdString(s->id)),QString::fromUtf8(v?v:""));return true;}return false;}
private: EditorPluginHost&host;Plugins::PluginManifest manifest;mutable std::string projectCache,pluginCache;
};

EditorPluginHost::EditorPluginHost(QMainWindow*w,Plugins::PluginSystem*s,std::function<EditorContext*()>e,ProjectContext*p,std::function<void()>c,Automation::ToolRegistry*tools):window_(w),system_(s),editorContext_(std::move(e)),projectContext_(p),sceneChanged_(std::move(c)),toolBridge_(tools?std::make_shared<Automation::PluginToolBridge>(*tools):nullptr){
 auto&docs=EditorAssetServices::Instance().Documents();
 saveListener_=docs.AddSaveListener([this](const OpenDocumentInfo&doc,std::string*){if(doc.kind!=DocumentKind::Plugin)return false;for(auto&a:assetTypes_)if(QString::fromStdString(a->extension).mid(1)==QFileInfo(QString::fromStdString(doc.portablePath)).suffix().toLower()&&a->save){bool ok=a->save(doc.portablePath.c_str(),a->userData);if(ok)EditorAssetServices::Instance().Documents().SetDirty(doc.portablePath,false);return ok;}return false;});
 retargetListener_=docs.AddRetargetListener([this](const OpenDocumentInfo&before,const OpenDocumentInfo&after){if(before.kind!=DocumentKind::Plugin)return;for(auto&a:assetTypes_)if(QString::fromStdString(a->extension).mid(1)==QFileInfo(QString::fromStdString(after.portablePath)).suffix().toLower()&&a->retarget){a->retarget(before.portablePath.c_str(),after.portablePath.c_str(),a->userData);return;}});
}
EditorPluginHost::~EditorPluginHost(){auto&docs=EditorAssetServices::Instance().Documents();docs.RemoveSaveListener(saveListener_);docs.RemoveRetargetListener(retargetListener_);}
std::unique_ptr<Plugins::IPluginContext> EditorPluginHost::CreateContext(const Plugins::PluginManifest&m){return std::make_unique<Context>(*this,m);}
QAction*EditorPluginHost::FindCommand(const std::string&id)const{for(auto&c:commands_)if(c->id==id)return c->action;return nullptr;}
bool EditorPluginHost::RegisterCommand(const std::string&o,const Plugins::CommandDescriptor&d){if(!d.id||!*d.id||!d.displayName||!d.execute||FindCommand(d.id))return false;auto c=std::make_unique<Command>();c->owner=o;c->id=d.id;c->name=d.displayName;c->action=new QAction(QString::fromUtf8(d.displayName),window_);if(d.description)c->action->setToolTip(QString::fromUtf8(d.description));if(d.defaultShortcut&&*d.defaultShortcut)c->action->setShortcut(QKeySequence(QString::fromUtf8(d.defaultShortcut)));QObject::connect(c->action,&QAction::triggered,window_,[cb=d.execute,u=d.userData]{cb(u);});commands_.push_back(std::move(c));return true;}
bool EditorPluginHost::RegisterPanel(const std::string&o,const Plugins::PanelDescriptor&d){if(!d.id||!*d.id||!d.title||!d.factory||window_->findChild<QDockWidget*>(QString("PluginPanel.%1").arg(d.id)))return false;auto*dock=new QDockWidget(QString::fromUtf8(d.title),window_);InstallDockTitleBar(dock);dock->setObjectName(QString("PluginPanel.%1").arg(d.id));dock->setProperty("hpPluginOwner",QString::fromStdString(o));auto*w=d.factory(dock,d.userData);if(!w){delete dock;return false;}dock->setWidget(w);window_->addDockWidget(static_cast<Qt::DockWidgetArea>(d.defaultDockArea),dock);dock->hide();panels_.push_back(dock);return true;}
bool EditorPluginHost::RegisterMenu(const std::string&,const Plugins::MenuActionDescriptor&d){auto*a=FindCommand(d.commandId?d.commandId:"");if(!a||!d.menuPath)return false;auto parts=QString::fromUtf8(d.menuPath).split('/',Qt::SkipEmptyParts);auto*bar=window_->findChild<QMenuBar*>(QStringLiteral("EditorMenuBar"));if(!bar)return false;QMenu*m=nullptr;for(auto part:parts){QString wanted=part;wanted.remove('&');QMenu*f=nullptr;auto actions=m?m->actions():bar->actions();for(auto*x:actions)if(x->menu()){QString t=x->text();t.remove('&');if(t==wanted){f=x->menu();break;}}if(!f)f=m?m->addMenu(part):bar->addMenu(part);m=f;}if(!m)return false;m->addAction(a);return true;}
bool EditorPluginHost::RegisterToolbar(const std::string&,const Plugins::ToolbarActionDescriptor&d){auto*a=FindCommand(d.commandId?d.commandId:"");if(!a)return false;if(auto*row=window_->findChild<QToolBar*>(QStringLiteral("EditorTopToolbar"))){if(auto*slot=row->findChild<QAction*>(QStringLiteral("PluginToolbarSlot"))){row->insertAction(slot,a);return true;}}QString id=QString::fromUtf8(d.toolbarId?d.toolbarId:"Plugins");auto*b=window_->findChild<QToolBar*>(QString("PluginToolbar.%1").arg(id));if(!b){b=window_->addToolBar(id);b->setObjectName(QString("PluginToolbar.%1").arg(id));}b->addAction(a);return true;}
bool EditorPluginHost::RegisterContext(const std::string&o,const Plugins::ContextActionDescriptor&d){if(!d.location||!d.commandId||!FindCommand(d.commandId))return false;auto x=std::make_unique<ContextAction>();x->owner=o;x->location=d.location;x->command=d.commandId;contextActions_.push_back(std::move(x));return true;}
bool EditorPluginHost::ShowPanel(const std::string&id){auto*d=window_->findChild<QDockWidget*>(QString("PluginPanel.%1").arg(QString::fromStdString(id)));if(!d)return false;d->show();d->raise();return true;}
bool EditorPluginHost::RegisterAssetType(const std::string&o,const Plugins::AssetTypeDescriptor&d){if(!d.typeId||!d.extension||!d.displayName)return false;std::string ext=d.extension;if(!ext.starts_with('.'))ext.insert(ext.begin(),'.');std::ranges::transform(ext,ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});for(auto&a:assetTypes_)if(a->id==d.typeId||a->extension==ext)return false;auto a=std::make_unique<AssetType>();a->owner=o;a->id=d.typeId;a->extension=ext;a->name=d.displayName;a->icon=d.icon?d.icon:"";a->create=d.create;a->open=d.openEditor;a->dirty=d.isDirty;a->save=d.saveEditor;a->close=d.closeEditor;a->retarget=d.retargetEditor;a->userData=d.userData;assetTypes_.push_back(std::move(a));return true;}
bool EditorPluginHost::RegisterImporter(const std::string&o,const Plugins::ImporterDescriptor&d){if(!d.id||!d.sourceExtensions||!d.resultAssetTypeId||!d.import)return false;auto x=std::make_unique<Importer>();x->owner=o;x->id=d.id;x->extensions=d.sourceExtensions;x->result=d.resultAssetTypeId;x->callback=d.import;x->userData=d.userData;importers_.push_back(std::move(x));return true;}
bool EditorPluginHost::RegisterSetting(const std::string&o,const Plugins::SettingDescriptor&d){if(!d.id||!d.displayName)return false;auto s=std::make_unique<Setting>();s->owner=o;s->id=d.id;s->name=d.displayName;s->description=d.description?d.description:"";s->defaultValue=d.defaultValue?d.defaultValue:"";s->scope=d.scope;settings_.push_back(std::move(s));return true;}
bool EditorPluginHost::ImportAssetPack(const Plugins::AssetPackImportRequest& r, Plugins::AssetPackImportResult& out) {
    out = Plugins::AssetPackImportResult{};
    auto text = [](const char* value) { return std::string(value ? value : ""); };
    auto copy = [](char* target, std::size_t size, const std::string& value) {
        const std::size_t length = std::min(value.size(), size - 1);
        std::memcpy(target, value.data(), length);
        target[length] = '\0';
    };
    if (r.structSize < sizeof(Plugins::AssetPackImportRequest) || !r.sourcePath || !r.targetFolder) {
        copy(out.message, sizeof(out.message), "Invalid asset pack import request.");
        return false;
    }
    if (!projectContext_ || !projectContext_->HasProject()) {
        copy(out.message, sizeof(out.message), "Open a project before importing assets.");
        return false;
    }
    AssetIntakeRequest request;
    request.source = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(r.sourcePath)));
    request.projectRoot = projectContext_->GetProjectRootDir();
    request.targetFolder = r.targetFolder;
    request.credit = {text(r.title), text(r.source), text(r.sourceUrl), text(r.author), text(r.authorUrl),
                      text(r.license), text(r.licenseUrl)};
    AssetIntakeResult result;
    std::string error;
    const bool ok = AssetIntake::Import(request, result, error);
    std::string message = error;
    for (const std::string& warning : result.warnings) {
        message += (message.empty() ? "" : "\n") + warning;
    }
    copy(out.message, sizeof(out.message), message);
    copy(out.folder, sizeof(out.folder), result.folder);
    out.prefabCount = static_cast<std::uint32_t>(result.prefabs.size());
    out.materialCount = static_cast<std::uint32_t>(result.materials.size());
    out.warningCount = static_cast<std::uint32_t>(result.warnings.size());
    if (ok) {
        if (auto* browser = window_->findChild<AssetBrowserWidget*>()) {
            const std::string& shown = !result.prefabs.empty() ? result.prefabs.front() : result.materials.front();
            browser->RevealAsset(QString::fromStdString(shown));
        }
    }
    return ok;
}
void EditorPluginHost::PopulateContextMenu(const char*l,QMenu&m,const QString&){bool sep=false;for(auto&x:contextActions_)if(x->location==(l?l:""))if(auto*a=FindCommand(x->command)){if(!sep){m.addSeparator();sep=true;}m.addAction(a);}}
void EditorPluginHost::PopulateAssetCreationMenu(QMenu&m,const QString&dir){for(auto&a:assetTypes_)if(a->create){auto*x=m.addAction(QString::fromStdString(a->name));QObject::connect(x,&QAction::triggered,&m,[p=a.get(),dir]{p->create(dir.toUtf8().constData(),p->userData);});}}
void EditorPluginHost::PopulateImporterMenu(QMenu&m,const QString&dir){for(auto&i:importers_){auto*x=m.addAction(QString::fromStdString(i->id));QObject::connect(x,&QAction::triggered,&m,[p=i.get(),dir]{QString filter=QStringLiteral("Supported files (%1)").arg(QString::fromStdString(p->extensions).replace(';',' '));QString source=QFileDialog::getOpenFileName(nullptr,QObject::tr("Import with %1").arg(QString::fromStdString(p->id)),{},filter);if(!source.isEmpty())p->callback(source.toUtf8().constData(),dir.toUtf8().constData(),p->userData);});}}
QString EditorPluginHost::CustomAssetTypeId(const QString&p)const{auto e=QFileInfo(p).suffix().toLower();for(auto&a:assetTypes_)if(QString::fromStdString(a->extension).mid(1)==e)return QString::fromStdString(a->id);return{};}
QString EditorPluginHost::CustomAssetDisplayName(const QString&p)const{auto id=CustomAssetTypeId(p);for(auto&a:assetTypes_)if(QString::fromStdString(a->id)==id)return QString::fromStdString(a->name);return{};}
QIcon EditorPluginHost::CustomAssetIcon(const QString&p)const{auto id=CustomAssetTypeId(p);for(auto&a:assetTypes_)if(QString::fromStdString(a->id)==id&&!a->icon.empty()){auto*m=system_->FindManifest(a->owner);if(m)return QIcon(QString::fromStdString((m->root/a->icon).string()));}return{};}
bool EditorPluginHost::OpenCustomAsset(const QString&p){auto id=CustomAssetTypeId(p);for(auto&a:assetTypes_)if(QString::fromStdString(a->id)==id&&a->open){bool ok=a->open(p.toUtf8().constData(),a->userData);if(ok)EditorAssetServices::Instance().Documents().Register(DocumentKind::Plugin,p.toStdString(),a->dirty&&a->dirty(p.toUtf8().constData(),a->userData));return ok;}return false;}
std::uint64_t EditorPluginHost::CreatePrimitive(Plugins::PrimitiveKind k,const char*n){auto*c=editorContext_();if(!c||!c->HasActiveScene())return 0;Entity e=c->GetActiveScene()->CreateEntity(n&&*n?n:"Plugin Entity");if(k==Plugins::PrimitiveKind::Cube||k==Plugins::PrimitiveKind::Sphere||k==Plugins::PrimitiveKind::Plane){auto&m=e.AddComponent<MeshRendererComponent>();m.meshAssetPath=k==Plugins::PrimitiveKind::Cube?"builtin://Cube":k==Plugins::PrimitiveKind::Sphere?"builtin://Sphere":"builtin://Plane";}else if(k==Plugins::PrimitiveKind::PointLight)e.AddComponent<LightComponent>();c->SelectEntity(static_cast<entt::entity>(e));c->MarkDirty();sceneChanged_();return static_cast<std::uint64_t>(static_cast<std::uint32_t>(e))+1;}
void EditorPluginHost::RemoveOwner(const std::string&o){if(toolBridge_)toolBridge_->RemoveOwner(o);auto&docs=EditorAssetServices::Instance().Documents();for(const auto&doc:docs.ListAll())if(doc.kind==DocumentKind::Plugin)for(auto&a:assetTypes_)if(a->owner==o&&QString::fromStdString(a->extension).mid(1)==QFileInfo(QString::fromStdString(doc.portablePath)).suffix().toLower()){if(a->close)a->close(doc.portablePath.c_str(),a->userData);docs.Unregister(doc.portablePath);break;}for(auto i=panels_.begin();i!=panels_.end();)if(*i&&(*i)->property("hpPluginOwner").toString()==QString::fromStdString(o)){delete *i;i=panels_.erase(i);}else++i;for(auto i=commands_.begin();i!=commands_.end();)if((*i)->owner==o){delete(*i)->action;i=commands_.erase(i);}else++i;std::erase_if(assetTypes_,[&](auto&x){return x->owner==o;});std::erase_if(importers_,[&](auto&x){return x->owner==o;});std::erase_if(contextActions_,[&](auto&x){return x->owner==o;});std::erase_if(settings_,[&](auto&x){return x->owner==o;});}

} // namespace Hephaestus::QtEditor
#endif
