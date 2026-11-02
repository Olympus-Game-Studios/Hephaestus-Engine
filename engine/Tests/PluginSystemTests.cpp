#include "Plugins/PluginManifest.h"
#include "Plugins/PluginSystem.h"
#include "Assets/AssetMountRegistry.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs=std::filesystem;
using namespace Hephaestus::Plugins;
static bool Check(bool v,const char*m){if(!v)std::cerr<<"FAIL: "<<m<<'\n';return v;}
static void Write(const fs::path&p,const std::string&s){fs::create_directories(p.parent_path());std::ofstream(p)<<s;}
static std::string Manifest(const char*id="com.test.plugin",int api=1,const char*extra=""){return std::string("{\"version\":1,\"apiVersion\":")+std::to_string(api)+",\"id\":\""+id+"\",\"name\":\"Test\",\"author\":\"Tests\",\"pluginVersion\":\"1.2.3\",\"engineVersion\":\">=2026.9.0\",\"type\":\"editor\",\"entry\":\"bin/test.dll\",\"content\":\"Content\""+extra+"}";}
int main(){bool ok=true;auto root=fs::temp_directory_path()/"hephaestus-plugin-tests";std::error_code ec;fs::remove_all(root,ec);
 auto valid=root/"valid"/"Test.hpplugin";Write(valid,Manifest());auto p=ParsePluginManifest(valid);ok&=Check(p.success,"valid manifest parses");ok&=Check(p.manifest.id=="com.test.plugin","manifest id");
#ifdef _WIN32
 ok&=Check(p.manifest.entry=="bin/test.dll","Windows entry kept");
#elif defined(__APPLE__)
 ok&=Check(p.manifest.entry=="bin/libtest.dylib","entry mapped to native dylib");
#else
 ok&=Check(p.manifest.entry=="bin/libtest.so","entry mapped to native so");
#endif
 Write(root/"invalid.hpplugin","{\"version\":2}");ok&=Check(!ParsePluginManifest(root/"invalid.hpplugin").success,"invalid manifest rejected");
 ok&=Check(IsEngineVersionCompatible(">=2026.9.0","2026.9.12"),"minimum version accepted");ok&=Check(!IsEngineVersionCompatible(">=2027.1.0","2026.9.12"),"future version rejected");
 Write(root/"engine"/"Plugins"/"A"/"A.hpplugin",Manifest("com.test.duplicate"));Write(root/"project"/"Plugins"/"B"/"B.hpplugin",Manifest("com.test.duplicate"));
 Write(root/"project"/"Plugins"/"Mount"/"Mount.hpplugin",Manifest("com.test.mount"));fs::create_directories(root/"project"/"Plugins"/"Mount"/"Content");
 PluginSystem s;s.SetEngineVersion("2026.9.12");s.Discover(root/"engine",root/"project");int duplicates=0;for(auto&r:s.Records())if(r.status==PluginStatus::Duplicate)++duplicates;ok&=Check(duplicates==2,"all duplicate ids rejected");
 auto mounted=Hephaestus::AssetMountRegistry::Instance().Resolve("plugin://com.test.mount/file.bin");ok&=Check(mounted.has_value(),"plugin content mounted");
 std::string error;ok&=Check(s.SetEnabled("com.test.mount",false,&error),"enabled state persists");PluginSystem s2;s2.SetEngineVersion("2026.9.12");s2.Discover(root/"engine",root/"project");for(auto&r:s2.Records())if(r.manifest.id=="com.test.mount")ok&=Check(!r.enabled&&r.status==PluginStatus::Disabled,"disabled state restored");
 Write(root/"project"/"Plugins"/"BadApi"/"Bad.hpplugin",Manifest("com.test.badapi",99));PluginSystem s3;s3.SetEngineVersion("2026.9.12");s3.Discover(root/"engine",root/"project");for(auto&r:s3.Records())if(r.manifest.id=="com.test.badapi")ok&=Check(r.status==PluginStatus::Incompatible,"API mismatch rejected");
 Write(root/"project"/"Plugins"/"Missing"/"Missing.hpplugin",Manifest("com.test.missing"));PluginSystem s3b;s3b.SetEngineVersion("2026.9.12");s3b.Discover(root/"engine",root/"project");s3b.SetContextFactory([](const PluginManifest&){return std::unique_ptr<IPluginContext>{};});s3b.LoadEnabled();bool failed=false;for(auto&r:s3b.Records())if(r.manifest.id=="com.test.missing")failed=r.status==PluginStatus::Failed;ok&=Check(failed,"missing library failure is isolated");
 Write(root/"engine"/"Plugins"/"Off"/"Off.hpplugin",Manifest("com.test.off",1,",\"defaultEnabled\":false"));PluginSystem s4;s4.SetEngineVersion("2026.9.12");s4.Discover(root/"engine",root/"project");bool off=false;for(auto&r:s4.Records())if(r.manifest.id=="com.test.off")off=!r.enabled&&r.status==PluginStatus::Disabled&&!r.manifest.defaultEnabled;ok&=Check(off,"defaultEnabled false leaves the plugin disabled without a plugins.json key");
 fs::remove_all(root,ec);if(ok)std::cout<<"Plugin system tests passed.\n";return ok?0:1;}
