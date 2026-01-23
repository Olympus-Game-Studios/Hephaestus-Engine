#pragma once

#include "BlueprintAsset.h"
#include "Scene.h"

#include <string>

struct BlueprintInstantiateOptions {
    std::string blueprintPath;
    std::string instanceId;
    entt::entity parent = entt::null;
    const struct BlueprintInstanceComponent* instanceData = nullptr;
};

class BlueprintInstantiator {
public:
    static Entity Instantiate(Scene& scene, const BlueprintAsset& asset, const BlueprintInstantiateOptions& options);
};
