#pragma once

#include "Components.h"
#include "Json.h"
#include "../Core/PropertyValue.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct BlueprintScriptData {
    std::string language;
    std::string source;
    PropertyValue::Object defaultVars;
};

struct BlueprintNode {
    std::string id;
    std::string name;
    std::string parentId;
    TransformComponent localTransform;
    std::unordered_map<std::string, JsonValue> components;
    std::optional<BlueprintScriptData> script;
    std::vector<std::string> exposed;
};

struct BlueprintAsset {
    int version = 1;
    std::string assetId;
    std::string name;
    std::vector<BlueprintNode> nodes;
};
