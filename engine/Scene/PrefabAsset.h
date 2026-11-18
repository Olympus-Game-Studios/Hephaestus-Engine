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

    // A nested prefab: this node is an instance of prefabPath placed at localTransform, with these overrides keyed
    // by the nested prefab's own node ids. Expansion (PrefabOverrideSystem::ExpandNested) replaces it with that
    // prefab's nodes.
    std::string prefabPath;
    std::unordered_map<std::string, PrefabNodeOverride> nestedOverrides;

    // Set by expansion: the nested node this node was expanded from and its id inside that prefab, so saving can
    // fold edits back into nestedOverrides. Written only for in-memory round trips such as undo.
    std::string nestedFrom;
    std::string nestedNodeId;
};

struct PrefabAsset {
    int assetVersion = 1;
    std::string guid;
    std::string rootNodeId;
    std::string name;
    std::vector<PrefabNode> nodes;
    // Every prefab this one includes, directly or deeper, filled by expansion.
    std::vector<std::string> nestedPrefabs;
};

// Legacy aliases for backward compatibility during migration
using BlueprintComponentEntry = PrefabComponentEntry;
using BlueprintNode = PrefabNode;
using BlueprintAsset = PrefabAsset;
