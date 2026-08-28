#pragma once

#include "PrefabAsset.h"
#include "Scene.h"

#include <string>

struct PrefabInstanceComponent;

struct PrefabInstantiateOptions {
    std::string prefabPath;
    std::string instanceId;
    entt::entity parent = entt::null;
    const PrefabInstanceComponent* instanceData = nullptr;
    bool attachInstanceComponent = true;
};

class PrefabInstantiator {
public:
    static Entity Instantiate(Scene& scene, const PrefabAsset& asset, const PrefabInstantiateOptions& options);

    static void DestroyInstance(Scene& scene, Entity rootEntity);

    static Entity ReinstantiateInstance(Scene& scene, const PrefabAsset& asset,
                                         const PrefabInstanceComponent& instanceData);
};

// Legacy aliases
using BlueprintInstantiateOptions = PrefabInstantiateOptions;
using BlueprintInstantiator = PrefabInstantiator;
