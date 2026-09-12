#include "PluginAPI/PluginAPI.h"
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <filesystem>

using namespace Hephaestus::Plugins;

class PrimitiveToolsPlugin final : public IHephaestusPlugin {
public:
 bool OnLoad(IPluginContext& c) override {
  context=&c;
  bool ok=true;
  ok&=Command("primitive.open","Open Primitive Tools",&OpenPanel);
  ok&=Command("primitive.cube","Create Cube",&Cube);
  ok&=Command("primitive.sphere","Create Sphere",&Sphere);
  ok&=Command("primitive.plane","Create Plane",&Plane);
  ok&=Command("primitive.light","Create Light",&Light);
  PanelDescriptor panel{"primitive.tools","Primitive Tools","",2,&MakePanel,this};
  ok&=c.UI().RegisterPanel(panel);
  ok&=c.UI().RegisterMenuAction({"Tools/Primitive Tools","primitive.open","open"});
  ok&=c.UI().RegisterToolbarAction({"Primitive Tools","primitive.open"});
  ok&=c.UI().RegisterContextAction({"viewport","primitive.cube"});
  AssetTypeDescriptor asset{"primitive.testAsset",".hptestasset","Primitive Test Asset","",false,&CreateAsset,&OpenAsset,this};
  ok&=c.Assets().RegisterAssetType(asset);
  ok&=c.Assets().RegisterImporter({"Primitive Test Source (*.hptestsrc)","*.hptestsrc","primitive.testAsset",&ImportAsset,this});
  ok&=c.Settings().Register({"defaultPrimitive","Default Primitive","Used by future tool presets.","Cube",SettingScope::Project});
  return ok;
 }
 void OnUnload() override { context=nullptr; }
private:
 bool Command(const char*id,const char*name,PluginCallback cb){return context->Commands().Register({id,name,"Primitive Tools example command","",cb,this});}
 static void OpenPanel(void*p){auto*self=static_cast<PrimitiveToolsPlugin*>(p);self->context->UI().ShowPanel("primitive.tools");}
 static void Add(void*p,PrimitiveKind kind,const char*name){auto*self=static_cast<PrimitiveToolsPlugin*>(p);auto tx=self->context->Undo().Begin(name);if(!tx)return;if(self->context->Scene().CreatePrimitive(kind,name))self->context->Undo().Commit(tx);else self->context->Undo().Rollback(tx);}
 static void Cube(void*p){Add(p,PrimitiveKind::Cube,"Cube");} static void Sphere(void*p){Add(p,PrimitiveKind::Sphere,"Sphere");}
 static void Plane(void*p){Add(p,PrimitiveKind::Plane,"Plane");} static void Light(void*p){Add(p,PrimitiveKind::PointLight,"Point Light");}
 static QWidget* MakePanel(QWidget*parent,void*data){auto*self=static_cast<PrimitiveToolsPlugin*>(data);auto*w=new QWidget(parent);auto*l=new QVBoxLayout(w);l->addWidget(new QLabel("Create scene entities through the public scene and undo services.",w));for(auto item:{std::pair{"Cube",&Cube},std::pair{"Sphere",&Sphere},std::pair{"Plane",&Plane},std::pair{"Light",&Light}}){auto*b=new QPushButton(item.first,w);QObject::connect(b,&QPushButton::clicked,w,[self,fn=item.second]{fn(self);});l->addWidget(b);}l->addStretch();return w;}
 static bool CreateAsset(const char*dir,void*data){auto*self=static_cast<PrimitiveToolsPlugin*>(data);bool ok=false;QString name=QInputDialog::getText(nullptr,"Create Primitive Test Asset","Name:",QLineEdit::Normal,"PrimitivePreset",&ok);if(!ok||name.trimmed().isEmpty())return false;std::filesystem::path root(self->context->Project().ProjectRoot());std::filesystem::path target=root/"assets"/(dir?dir:"")/(name.trimmed().toStdString()+".hptestasset");QFile file(QString::fromStdString(target.string()));if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))return false;file.write("{\n  \"version\": 1,\n  \"primitive\": \"Cube\"\n}\n");return true;}
 static bool OpenAsset(const char*path,void*){QMessageBox::information(nullptr,"Primitive Test Asset",QString("Opened through the registered custom asset editor:\n%1").arg(QString::fromUtf8(path)));return true;}
 static bool ImportAsset(const char*source,const char*dir,void*data){auto*self=static_cast<PrimitiveToolsPlugin*>(data);std::filesystem::path root(self->context->Project().ProjectRoot());QFileInfo info(QString::fromUtf8(source));std::filesystem::path target=root/"assets"/(dir?dir:"")/(info.completeBaseName().toStdString()+".hptestasset");QFile::remove(QString::fromStdString(target.string()));return QFile::copy(QString::fromUtf8(source),QString::fromStdString(target.string()));}
 IPluginContext*context=nullptr;
};

HP_DECLARE_PLUGIN(PrimitiveToolsPlugin)
