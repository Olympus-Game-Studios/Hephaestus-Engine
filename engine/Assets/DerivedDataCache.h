#pragma once
#include <filesystem>
#include <string>
#include <vector>

class DerivedDataCache {
public:
    void SetRoot(const std::filesystem::path& projectRoot);

    bool Store(const std::string& key, const std::vector<uint8_t>& payload);
    bool Has(const std::string& key) const;
    std::filesystem::path GetPathForKey(const std::string& key) const;

private:
    std::filesystem::path root_;
};
