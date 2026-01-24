#pragma once

#include "Components.h"
#include "Json.h"
#include "../Core/PropertyValue.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct BlueprintComponentEntry {
    std::string type;
    bool enabled = true;
    JsonValue data;
};

struct BlueprintNode {
    std::string id;
    std::string name;
    std::string parentId; // Empty for root
    TransformComponent localTransform;
    std::vector<BlueprintComponentEntry> components;
    std::vector<std::string> exposed;
};

struct BlueprintAsset {
    int assetVersion = 1;
    std::string guid;
    std::string rootNodeId;
    std::string name;
    std::vector<BlueprintNode> nodes;
};
