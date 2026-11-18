#include "DerivedDataCache.h"

#include <fstream>

void DerivedDataCache::SetRoot(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) {
        root_.clear();
        return;
    }
    root_ = projectRoot / ".hephaestus" / "ddc";
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
}

std::filesystem::path DerivedDataCache::GetPathForKey(const std::string& key) const {
    if (root_.empty()) {
        return {};
    }
    return (root_ / key).lexically_normal();
}

bool DerivedDataCache::Store(const std::string& key, const std::vector<uint8_t>& payload) {
    if (root_.empty() || key.empty()) {
        return false;
    }

    std::filesystem::path target = GetPathForKey(key);
    std::filesystem::path tmp = target;
    tmp += ".tmp";

    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);

    std::ofstream out(tmp, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    if (!payload.empty()) {
        out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    }
    out.close();

    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool DerivedDataCache::Load(const std::string& key, std::vector<uint8_t>& payload) const {
    if (root_.empty() || key.empty()) {
        return false;
    }
    std::ifstream in(GetPathForKey(key), std::ios::binary);
    if (!in) {
        return false;
    }
    payload.assign(std::istreambuf_iterator<char>(in), {});
    return in.good() || in.eof();
}

bool DerivedDataCache::Has(const std::string& key) const {
    if (root_.empty() || key.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(GetPathForKey(key), ec);
}
