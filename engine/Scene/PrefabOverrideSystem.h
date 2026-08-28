#pragma once

#include "Components.h"
#include "Entity.h"
#include "PrefabAsset.h"
#include "Scene.h"

#include <filesystem>
#include <string>
#include <vector>

struct PrefabOverrideRef {
    std::string nodeId;
    std::string component;
    std::string property;
};

class PrefabOverrideSystem {
public:
    static Entity FindInstanceRoot(Scene& scene, Entity entity);
    static bool IsPrefabInstanceEntity(Entity entity);
    static bool IsInstanceRoot(Entity entity);

    static PrefabInstanceComponent* TryGetInstance(Scene& scene, Entity entity);
    static const PrefabNodeComponent* TryGetNode(Entity entity);

    static bool LoadAsset(const std::string& path, PrefabAsset& out, std::string& error);
    static void InvalidateAssetCache(const std::string& path = {});
    static const PrefabNode* FindAssetNode(const PrefabAsset& asset, const std::string& nodeId);
    static const PrefabComponentEntry* FindAssetComponent(const PrefabNode& node, const std::string& type);

    static void SyncEntityOverrides(Scene& scene, Entity entity);
    static void SyncInstance(Scene& scene, Entity root);
    static void SyncAllInstances(Scene& scene);

    static bool IsPropertyOverridden(const PrefabInstanceComponent& inst, const std::string& nodeId,
                                     const std::string& component, const std::string& property);
    static bool IsTransformOverridden(const PrefabInstanceComponent& inst, const std::string& nodeId);
    static bool IsComponentOverridden(const PrefabInstanceComponent& inst, const std::string& nodeId,
                                      const std::string& component);
    static bool HasAnyOverride(const PrefabInstanceComponent& inst);
    static std::vector<PrefabOverrideRef> ListOverrides(const PrefabInstanceComponent& inst);

    static bool RevertProperty(Scene& scene, Entity entity, const std::string& component, const std::string& property);
    static bool RevertComponent(Scene& scene, Entity entity, const std::string& component);
    static bool RevertNode(Scene& scene, Entity entity);
    static bool RevertAll(Scene& scene, Entity root);

    static bool ApplyPropertyToPrefab(Scene& scene, Entity entity, const std::string& component,
                                      const std::string& property, std::string& error);
    static bool ApplyComponentToPrefab(Scene& scene, Entity entity, const std::string& component, std::string& error);
    static bool ApplyNodeToPrefab(Scene& scene, Entity entity, std::string& error);
    static bool ApplyAllToPrefab(Scene& scene, Entity root, std::string& error);

    static Entity RefreshInstance(Scene& scene, Entity root, std::string* error = nullptr);
    static int RefreshInstancesOfPrefab(Scene& scene, const std::string& prefabPath);

    static bool UnpackInstance(Scene& scene, Entity root);

    static bool CaptureHierarchy(Scene& scene, Entity root, PrefabAsset& outAsset);
    static bool CreateFromSelection(Scene& scene, Entity selectionRoot,
                                    const std::filesystem::path& destPath,
                                    Entity* outInstance, std::string& error);

    static void CleanupStaleOverrides(PrefabInstanceComponent& inst, const PrefabAsset& asset);
    static bool PathsReferToSamePrefab(const std::string& a, const std::string& b);

    static Entity CreateMissingStub(Scene& scene, const PrefabInstanceComponent& instanceData,
                                    const std::string& reason);

    static std::string GenerateId();
};
