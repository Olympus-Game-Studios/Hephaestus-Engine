#include "PrefabInstantiator.h"

#include "PrefabComponentIO.h"
#include "../Scripting/LuaScriptComponent.h"

#include <atomic>
#include <unordered_map>
#include <vector>

namespace {
void CollectInstanceEntities(Scene& scene, Entity root, std::vector<entt::entity>& out) {
    if (!root) {
        return;
    }
    out.push_back(static_cast<entt::entity>(root));
    if (!root.HasComponent<TransformComponent>()) {
        return;
    }
    for (entt::entity child : root.GetComponent<TransformComponent>().children) {
        Entity childEntity(child, &scene);
        if (childEntity && childEntity.HasComponent<PrefabNodeComponent>()) {
            CollectInstanceEntities(scene, childEntity, out);
        }
    }
}

void ApplyNodeOverrides(Entity entity, const PrefabNodeOverride& overrideData) {
    auto transformProps = overrideData.componentOverrides.find("Transform");
    const bool hasTransformPaths = transformProps != overrideData.componentOverrides.end() &&
                                   !transformProps->second.properties.empty();

    if (overrideData.hasTransform && !hasTransformPaths && entity.HasComponent<TransformComponent>()) {
        auto& transform = entity.GetComponent<TransformComponent>();
        PrefabComponentIO::CopyLocalTransform(transform, overrideData.transform);
    }

    if (hasTransformPaths && entity.HasComponent<TransformComponent>()) {
        auto& transform = entity.GetComponent<TransformComponent>();
        for (const auto& [property, value] : transformProps->second.properties) {
            if (property == "position" && value.IsVec3()) {
                transform.position = *value.AsVec3();
            } else if (property == "rotation" && value.IsVec3()) {
                transform.rotation = *value.AsVec3();
            } else if (property == "scale" && value.IsVec3()) {
                transform.scale = *value.AsVec3();
            } else if (property == "position.x" && value.IsNumber()) {
                transform.position.x = static_cast<float>(*value.AsNumber());
            } else if (property == "position.y" && value.IsNumber()) {
                transform.position.y = static_cast<float>(*value.AsNumber());
            } else if (property == "position.z" && value.IsNumber()) {
                transform.position.z = static_cast<float>(*value.AsNumber());
            } else if (property == "rotation.x" && value.IsNumber()) {
                transform.rotation.x = static_cast<float>(*value.AsNumber());
            } else if (property == "rotation.y" && value.IsNumber()) {
                transform.rotation.y = static_cast<float>(*value.AsNumber());
            } else if (property == "rotation.z" && value.IsNumber()) {
                transform.rotation.z = static_cast<float>(*value.AsNumber());
            } else if (property == "scale.x" && value.IsNumber()) {
                transform.scale.x = static_cast<float>(*value.AsNumber());
            } else if (property == "scale.y" && value.IsNumber()) {
                transform.scale.y = static_cast<float>(*value.AsNumber());
            } else if (property == "scale.z" && value.IsNumber()) {
                transform.scale.z = static_cast<float>(*value.AsNumber());
            }
        }
    }

    for (const auto& [componentType, componentOverride] : overrideData.componentOverrides) {
        if (componentType == "Transform") {
            continue;
        }
        PrefabComponentEntry live;
        JsonValue data = PrefabComponentIO::MakeObject();
        if (PrefabComponentIO::CaptureComponent(entity, componentType, live)) {
            data = live.data;
        }
        PrefabComponentIO::MergeOverrides(data, componentOverride.properties);
        PrefabComponentIO::ApplyComponentJson(entity, componentType, data, live.enabled);
    }

    if (!overrideData.scriptVars.empty() && entity.HasComponent<LuaScriptComponent>()) {
        auto& lua = entity.GetComponent<LuaScriptComponent>();
        for (const auto& [key, value] : overrideData.scriptVars) {
            lua.variables[key] = value;
        }
    }
}
}

Entity PrefabInstantiator::Instantiate(Scene& scene, const PrefabAsset& asset, const PrefabInstantiateOptions& options) {
    std::unordered_map<std::string, Entity> nodeEntities;
    nodeEntities.reserve(asset.nodes.size());

    static std::atomic<uint64_t> s_InstanceCounter{ 1 };
    std::string instanceId = options.instanceId.empty() ? std::to_string(s_InstanceCounter.fetch_add(1)) : options.instanceId;
    std::string rootNodeId;

    for (const PrefabNode& node : asset.nodes) {
        Entity entity = scene.CreateEntity(node.name);
        nodeEntities[node.id] = entity;

        auto& nodeComp = entity.AddComponent<PrefabNodeComponent>();
        nodeComp.instanceId = instanceId;
        nodeComp.nodeId = node.id;
    }

    Entity rootEntity;
    for (const PrefabNode& node : asset.nodes) {
        auto it = nodeEntities.find(node.id);
        if (it == nodeEntities.end()) {
            continue;
        }

        Entity entity = it->second;
        auto& transform = entity.GetComponent<TransformComponent>();
        PrefabComponentIO::CopyLocalTransform(transform, node.localTransform);

        if (!asset.rootNodeId.empty() && node.id == asset.rootNodeId) {
            rootEntity = entity;
            rootNodeId = node.id;
            entity.GetComponent<PrefabNodeComponent>().isRoot = true;
        } else if (asset.rootNodeId.empty() && node.parentId.empty()) {
            rootEntity = entity;
            rootNodeId = node.id;
            entity.GetComponent<PrefabNodeComponent>().isRoot = true;
        }

        for (const auto& componentEntry : node.components) {
            PrefabComponentIO::ApplyEntry(entity, componentEntry);
        }
    }

    for (const PrefabNode& node : asset.nodes) {
        if (node.id == rootNodeId) {
            continue;
        }

        std::string effectiveParent = node.parentId;
        if (effectiveParent.empty()) {
            effectiveParent = rootNodeId;
        }

        auto itChild = nodeEntities.find(node.id);
        auto itParent = nodeEntities.find(effectiveParent);
        if (itChild != nodeEntities.end() && itParent != nodeEntities.end()) {
            scene.SetParent(itChild->second, itParent->second);
        }
    }

    const bool shouldAttachInstance = options.instanceData || options.attachInstanceComponent;
    if (rootEntity && shouldAttachInstance) {
        PrefabInstanceComponent instanceComp;
        if (options.instanceData) {
            instanceComp = *options.instanceData;
        }
        if (instanceComp.prefabPath.empty()) {
            instanceComp.prefabPath = options.prefabPath;
        }
        instanceComp.prefabGuid = asset.guid.empty() ? instanceComp.prefabGuid : asset.guid;
        if (instanceComp.instanceId.empty()) {
            instanceComp.instanceId = instanceId;
        }
        if (instanceComp.rootNodeId.empty()) {
            instanceComp.rootNodeId = rootNodeId;
        }
        if (options.instanceData && options.instanceData->parent != entt::null) {
            instanceComp.parent = options.instanceData->parent;
        } else if (instanceComp.parent == entt::null) {
            instanceComp.parent = options.parent;
        }
        instanceId = instanceComp.instanceId;
        rootEntity.AddComponent<PrefabInstanceComponent>(instanceComp);

        for (auto& [_, entity] : nodeEntities) {
            if (entity && entity.HasComponent<PrefabNodeComponent>()) {
                entity.GetComponent<PrefabNodeComponent>().instanceId = instanceId;
            }
        }
    }

    if (rootEntity && options.parent != entt::null) {
        scene.SetParent(rootEntity, Entity(options.parent, &scene));
    } else if (rootEntity && options.instanceData && options.instanceData->parent != entt::null) {
        scene.SetParent(rootEntity, Entity(options.instanceData->parent, &scene));
    }

    if (options.instanceData) {
        for (const auto& [nodeId, overrideData] : options.instanceData->overrides) {
            auto it = nodeEntities.find(nodeId);
            if (it == nodeEntities.end()) {
                continue;
            }
            ApplyNodeOverrides(it->second, overrideData);
        }

        if (rootEntity && rootEntity.HasComponent<PrefabInstanceComponent>()) {
            auto& inst = rootEntity.GetComponent<PrefabInstanceComponent>();
            if (inst.hasRootTransform && rootEntity.HasComponent<TransformComponent>()) {
                PrefabComponentIO::CopyLocalTransform(rootEntity.GetComponent<TransformComponent>(), inst.rootTransform);
            } else if (rootEntity.HasComponent<TransformComponent>()) {
                inst.hasRootTransform = true;
                PrefabComponentIO::CopyLocalTransform(inst.rootTransform, rootEntity.GetComponent<TransformComponent>());
                auto rootOverride = inst.overrides.find(inst.rootNodeId);
                if (rootOverride != inst.overrides.end() && rootOverride->second.hasTransform) {
                    rootOverride->second.hasTransform = false;
                    rootOverride->second.componentOverrides.erase("Transform");
                    PrefabComponentIO::PruneEmptyOverrides(inst);
                }
            }
        }
    }

    return rootEntity;
}

void PrefabInstantiator::DestroyInstance(Scene& scene, Entity rootEntity) {
    if (!rootEntity) {
        return;
    }

    std::vector<entt::entity> toDestroy;
    CollectInstanceEntities(scene, rootEntity, toDestroy);

    for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
        Entity e(*it, &scene);
        if (e) {
            scene.DestroyEntity(e);
        }
    }
}

Entity PrefabInstantiator::ReinstantiateInstance(Scene& scene, const PrefabAsset& asset,
                                                const PrefabInstanceComponent& instanceData) {
    PrefabInstantiateOptions options;
    options.prefabPath = instanceData.prefabPath;
    options.instanceId = instanceData.instanceId;
    options.parent = instanceData.parent;
    options.instanceData = &instanceData;
    return Instantiate(scene, asset, options);
}
