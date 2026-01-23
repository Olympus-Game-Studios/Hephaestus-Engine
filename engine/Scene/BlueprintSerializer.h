#pragma once

#include "BlueprintAsset.h"

#include <filesystem>
#include <string>

class BlueprintSerializer {
public:
    static bool Deserialize(BlueprintAsset& asset, const std::filesystem::path& filepath, std::string& outError);
    static bool Serialize(const BlueprintAsset& asset, const std::filesystem::path& filepath, std::string& outError);
};
