#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <ranges>
namespace Hephaestus {
struct AssetMount { std::string scheme; std::filesystem::path root; bool readOnly = true; };
class AssetMountRegistry {
public:
 static AssetMountRegistry& Instance(){static AssetMountRegistry v;return v;}
 bool Mount(std::string s,std::filesystem::path r,bool ro=true){if(s.empty()||r.empty())return false;std::replace(s.begin(),s.end(),'\\','/');while(s.ends_with('/'))s.pop_back();mounts_[s]={s,std::move(r),ro};return true;}
 void Unmount(const std::string&s){mounts_.erase(s);}
 void UnmountPrefix(const std::string&p){for(auto i=mounts_.begin();i!=mounts_.end();)if(i->first.starts_with(p))i=mounts_.erase(i);else++i;}
 std::optional<std::filesystem::path> Resolve(const std::string&p)const{std::string v=p;std::replace(v.begin(),v.end(),'\\','/');for(const auto&[k,m]:mounts_)if(v==k||v.starts_with(k+"/")){auto r=std::filesystem::path(v.size()==k.size()?"":v.substr(k.size()+1));for(const auto&part:r)if(part=="..")return std::nullopt;return m.root/r;}return std::nullopt;}
 std::vector<AssetMount> Mounts()const{std::vector<AssetMount>o;for(const auto&[_,m]:mounts_)o.push_back(m);std::ranges::sort(o,{},&AssetMount::scheme);return o;}
private: std::unordered_map<std::string, AssetMount> mounts_;
};
}
