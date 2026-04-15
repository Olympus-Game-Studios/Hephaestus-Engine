#pragma once

#include "Components.h"
#include "Json.h"
#include "../Core/PropertyValue.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct PrefabComponentEntry {
    std::string type;
    bool enabled = true;
    JsonValue data;
};

struct PrefabNode {
    std::string id;
    std::string name;
    std::string parentId;
    TransformComponent localTransform;
    std::vector<PrefabComponentEntry> components;
    std::vector<std::string> exposed;
};

struct PrefabAsset {
    int assetVersion = 1;
    std::string guid;
    std::string rootNodeId;
    std::string name;
    std::vector<PrefabNode> nodes;
};

// Legacy aliases for backward compatibility during migration
using BlueprintComponentEntry = PrefabComponentEntry;
using BlueprintNode = PrefabNode;
using BlueprintAsset = PrefabAsset;
