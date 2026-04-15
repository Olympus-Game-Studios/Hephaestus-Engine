#pragma once

#include "PrefabAsset.h"

#include <filesystem>
#include <string>

class PrefabSerializer {
public:
    static bool Deserialize(PrefabAsset& asset, const std::filesystem::path& filepath, std::string& outError);
    static bool DeserializeFromString(PrefabAsset& asset, const std::string& data, std::string& outError);
    static bool Serialize(const PrefabAsset& asset, const std::filesystem::path& filepath, std::string& outError);
    static std::string SerializeToString(const PrefabAsset& asset);
};

// Legacy alias
using BlueprintSerializer = PrefabSerializer;
