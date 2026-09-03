#include "PrefabOverrideSystem.h"

#include "PrefabComponentIO.h"
#include "PrefabInstantiator.h"
#include "PrefabSerializer.h"
#include "../Scripting/LuaScriptComponent.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
std::unordered_map<std::string, PrefabAsset>& AssetCache() {
    static std::unordered_map<std::string, PrefabAsset> cache;
    return cache;
}

std::filesystem::path& ProjectRootStorage() {
    static std::filesystem::path root;
    return root;
}

std::string NormalizePathKey(const std::string& path) {
    std::string key = fs::path(path).generic_string();
    std::replace(key.begin(), key.end(), '\\', '/');
    return key;
}

void CollectSubtree(Scene& scene, Entity root, std::vector<entt::entity>& out) {
    if (!root) {
        return;
    }
    out.push_back(static_cast<entt::entity>(root));
    if (!root.HasComponent<TransformComponent>()) {
        return;
    }
    const auto children = root.GetComponent<TransformComponent>().children;
    for (entt::entity child : children) {
        CollectSubtree(scene, Entity(child, &scene), out);
    }
}

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

Entity FindEntityByNodeId(Scene& scene, const std::string& instanceId, const std::string& nodeId) {
    auto view = scene.GetEntitiesWith<PrefabNodeComponent>();
    for (auto handle : view) {
        Entity entity(handle, &scene);
        const auto& node = entity.GetComponent<PrefabNodeComponent>();
        if (node.instanceId == instanceId && node.nodeId == nodeId) {
            return entity;
        }
    }
    return {};
}

void StripRuntimeKeys(const std::string& type, JsonValue& data) {
    if (!data.IsObject()) {
        return;
    }
    if (type == "RigidBody") {
        data.objectValue.erase("linearVelocity");
        data.objectValue.erase("angularVelocity");
    }
    if (type == "LuaScript") {
        data.objectValue.erase("variables");
        data.objectValue.erase("authoredVariables");
    }
}

void MarkScriptVariableAuthored(JsonValue& data, const std::string& property) {
    if (!data.IsObject()) {
        return;
    }
    JsonValue* authored = nullptr;
    auto existing = data.objectValue.find("authoredVariables");
    if (existing == data.objectValue.end() || !existing->second.IsArray()) {
        data.objectValue["authoredVariables"] = PrefabComponentIO::MakeArray();
        authored = &data.objectValue["authoredVariables"];
    } else {
        authored = &existing->second;
    }
    for (const JsonValue& value : authored->arrayValue) {
        if (value.IsString() && value.AsString() == property) {
            return;
        }
    }
    authored->arrayValue.push_back(PrefabComponentIO::MakeString(property));
}

PropertyValue TransformProperty(const TransformComponent& transform, const std::string& property) {
    if (property == "position") return PropertyValue(transform.position);
    if (property == "rotation") return PropertyValue(transform.rotation);
    if (property == "scale") return PropertyValue(transform.scale);
    if (property == "position.x") return PropertyValue(static_cast<double>(transform.position.x));
    if (property == "position.y") return PropertyValue(static_cast<double>(transform.position.y));
    if (property == "position.z") return PropertyValue(static_cast<double>(transform.position.z));
    if (property == "rotation.x") return PropertyValue(static_cast<double>(transform.rotation.x));
    if (property == "rotation.y") return PropertyValue(static_cast<double>(transform.rotation.y));
    if (property == "rotation.z") return PropertyValue(static_cast<double>(transform.rotation.z));
    if (property == "scale.x") return PropertyValue(static_cast<double>(transform.scale.x));
    if (property == "scale.y") return PropertyValue(static_cast<double>(transform.scale.y));
    if (property == "scale.z") return PropertyValue(static_cast<double>(transform.scale.z));
    return PropertyValue();
}

void ApplyTransformProperty(TransformComponent& transform, const std::string& property, const PropertyValue& value) {
    auto applyVec = [&](Vec3& target) {
        if (value.IsVec3()) {
            target = *value.AsVec3();
        }
    };
    auto applyAxis = [&](float& axis) {
        if (value.IsNumber()) {
            axis = static_cast<float>(*value.AsNumber());
        }
    };
    if (property == "position") applyVec(transform.position);
    else if (property == "rotation") applyVec(transform.rotation);
    else if (property == "scale") applyVec(transform.scale);
    else if (property == "position.x") applyAxis(transform.position.x);
    else if (property == "position.y") applyAxis(transform.position.y);
    else if (property == "position.z") applyAxis(transform.position.z);
    else if (property == "rotation.x") applyAxis(transform.rotation.x);
    else if (property == "rotation.y") applyAxis(transform.rotation.y);
    else if (property == "rotation.z") applyAxis(transform.rotation.z);
    else if (property == "scale.x") applyAxis(transform.scale.x);
    else if (property == "scale.y") applyAxis(transform.scale.y);
    else if (property == "scale.z") applyAxis(transform.scale.z);
}

void ApplyPrefabTransform(TransformComponent& dest, const TransformComponent& src) {
    PrefabComponentIO::CopyLocalTransform(dest, src);
}

void WriteTransformToNode(PrefabNode& node, const std::string& property, const TransformComponent& live) {
    if (property == "position" || property.rfind("position.", 0) == 0) {
        node.localTransform.position = live.position;
    } else if (property == "rotation" || property.rfind("rotation.", 0) == 0) {
        node.localTransform.rotation = live.rotation;
    } else if (property == "scale" || property.rfind("scale.", 0) == 0) {
        node.localTransform.scale = live.scale;
    } else {
        PrefabComponentIO::CopyLocalTransform(node.localTransform, live);
    }
}

PrefabNode* MutableFindNode(PrefabAsset& asset, const std::string& nodeId) {
    for (auto& node : asset.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

PrefabComponentEntry* MutableFindComponent(PrefabNode& node, const std::string& type) {
    for (auto& entry : node.components) {
        if (entry.type == type) {
            return &entry;
        }
    }
    return nullptr;
}

std::unordered_map<std::string, PropertyValue> ParseScriptVars(const JsonValue& data) {
    std::unordered_map<std::string, PropertyValue> vars;
    if (!data.IsObject()) {
        return vars;
    }
    const JsonValue* variables = data.Find("variables");
    if (!variables || !variables->IsObject()) {
        return vars;
    }
    for (const auto& [key, value] : variables->AsObject()) {
        vars.emplace(key, PrefabComponentIO::JsonToProperty(value));
    }
    return vars;
}

void CaptureLiveParentAndRoot(Scene& scene, Entity root, PrefabInstanceComponent& inst) {
    if (!root) {
        return;
    }
    if (root.HasComponent<TransformComponent>()) {
        const auto& transform = root.GetComponent<TransformComponent>();
        inst.parent = transform.parent;
        inst.hasRootTransform = true;
        PrefabComponentIO::CopyLocalTransform(inst.rootTransform, transform);
    }
}

bool SaveAsset(const PrefabAsset& asset, const std::string& path, std::string& error) {
    const fs::path resolved = PrefabOverrideSystem::ResolveAssetFilePath(path, true);
    if (!PrefabSerializer::Serialize(asset, resolved, error)) {
        return false;
    }
    PrefabOverrideSystem::InvalidateAssetCache(path);
    PrefabOverrideSystem::InvalidateAssetCache(resolved.generic_string());
    return true;
}

} // namespace

void PrefabOverrideSystem::SetProjectRoot(const std::filesystem::path& projectRoot) {
    ProjectRootStorage() = projectRoot.empty() ? fs::path{} : projectRoot.lexically_normal();
}

const std::filesystem::path& PrefabOverrideSystem::GetProjectRoot() {
    return ProjectRootStorage();
}

std::filesystem::path PrefabOverrideSystem::ResolveAssetFilePath(const std::string& path, bool forWrite) {
    if (path.empty()) {
        return {};
    }

    fs::path input(path);
    std::error_code ec;

    if (input.is_absolute()) {
        return input.lexically_normal();
    }

    auto tryExisting = [&](const fs::path& candidate) -> fs::path {
        if (!candidate.empty() && fs::exists(candidate, ec)) {
            return candidate.lexically_normal();
        }
        return {};
    };

    const fs::path& root = ProjectRootStorage();
    if (!root.empty()) {
        fs::path fromRoot = tryExisting(root / input);
        if (!fromRoot.empty()) {
            return fromRoot;
        }

        // Accept keys that omit the assets/ prefix (Asset Browser relative paths).
        const std::string generic = NormalizePathKey(path);
        if (generic.rfind("assets/", 0) != 0 && generic.rfind(".engine/", 0) != 0) {
            fromRoot = tryExisting(root / "assets" / input);
            if (!fromRoot.empty()) {
                return fromRoot;
            }
        }

        if (forWrite) {
            if (generic.rfind("assets/", 0) == 0 || generic.rfind(".engine/", 0) == 0) {
                return (root / input).lexically_normal();
            }
            return (root / "assets" / input).lexically_normal();
        }
    }

    fs::path fromCwd = tryExisting(fs::current_path(ec) / input);
    if (!fromCwd.empty()) {
        return fromCwd;
    }

    // Last resort: return as-is (absolute if possible) so callers get a clear open error.
    if (forWrite && !root.empty()) {
        return (root / input).lexically_normal();
    }
    return input.lexically_normal();
}

Entity PrefabOverrideSystem::FindInstanceRoot(Scene& scene, Entity entity) {
    if (!entity) {
        return {};
    }
    Entity current = entity;
    while (current) {
        if (current.HasComponent<PrefabInstanceComponent>()) {
            return current;
        }
        if (!current.HasComponent<TransformComponent>()) {
            break;
        }
        const entt::entity parent = current.GetComponent<TransformComponent>().parent;
        if (parent == entt::null) {
            break;
        }
        current = Entity(parent, &scene);
    }
    if (entity.HasComponent<PrefabNodeComponent>()) {
        const std::string instanceId = entity.GetComponent<PrefabNodeComponent>().instanceId;
        auto view = scene.GetEntitiesWith<PrefabInstanceComponent>();
        for (auto handle : view) {
            Entity candidate(handle, &scene);
            if (candidate.GetComponent<PrefabInstanceComponent>().instanceId == instanceId) {
                return candidate;
            }
        }
    }
    return {};
}

bool PrefabOverrideSystem::IsPrefabInstanceEntity(Entity entity) {
    return entity && entity.HasComponent<PrefabNodeComponent>();
}

bool PrefabOverrideSystem::IsInstanceRoot(Entity entity) {
    return entity && entity.HasComponent<PrefabInstanceComponent>();
}

PrefabInstanceComponent* PrefabOverrideSystem::TryGetInstance(Scene& scene, Entity entity) {
    Entity root = FindInstanceRoot(scene, entity);
    if (!root || !root.HasComponent<PrefabInstanceComponent>()) {
        return nullptr;
    }
    return &root.GetComponent<PrefabInstanceComponent>();
}

const PrefabNodeComponent* PrefabOverrideSystem::TryGetNode(Entity entity) {
    if (!entity || !entity.HasComponent<PrefabNodeComponent>()) {
        return nullptr;
    }
    return &entity.GetComponent<PrefabNodeComponent>();
}

bool PrefabOverrideSystem::LoadAsset(const std::string& path, PrefabAsset& out, std::string& error) {
    const std::string key = NormalizePathKey(path);
    auto& cache = AssetCache();
    auto it = cache.find(key);
    if (it != cache.end()) {
        out = it->second;
        error.clear();
        return true;
    }

    const fs::path resolved = ResolveAssetFilePath(path, false);
    const std::string resolvedKey = NormalizePathKey(resolved.generic_string());
    if (!resolvedKey.empty() && resolvedKey != key) {
        auto resolvedIt = cache.find(resolvedKey);
        if (resolvedIt != cache.end()) {
            cache[key] = resolvedIt->second;
            out = resolvedIt->second;
            error.clear();
            return true;
        }
    }

    PrefabAsset loaded;
    std::string loadError;
    bool ok = PrefabSerializer::Deserialize(loaded, resolved, loadError);
    if (!ok && fs::path(path) != resolved) {
        ok = PrefabSerializer::Deserialize(loaded, fs::path(path), loadError);
    }
    if (!ok) {
        error = loadError.empty() ? "Unable to open prefab file." : loadError;
        return false;
    }

    cache[key] = loaded;
    if (!resolvedKey.empty()) {
        cache[resolvedKey] = loaded;
    }
    out = loaded;
    error.clear();
    return true;
}

void PrefabOverrideSystem::InvalidateAssetCache(const std::string& path) {
    if (path.empty()) {
        AssetCache().clear();
        return;
    }
    const std::string key = NormalizePathKey(path);
    auto& cache = AssetCache();
    cache.erase(key);
    cache.erase(path);
}

const PrefabNode* PrefabOverrideSystem::FindAssetNode(const PrefabAsset& asset, const std::string& nodeId) {
    for (const auto& node : asset.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

const PrefabComponentEntry* PrefabOverrideSystem::FindAssetComponent(const PrefabNode& node, const std::string& type) {
    for (const auto& entry : node.components) {
        if (entry.type == type) {
            return &entry;
        }
    }
    return nullptr;
}

void PrefabOverrideSystem::SyncEntityOverrides(Scene& scene, Entity entity) {
    if (!entity || !entity.HasComponent<PrefabNodeComponent>()) {
        return;
    }
    Entity root = FindInstanceRoot(scene, entity);
    if (!root || !root.HasComponent<PrefabInstanceComponent>()) {
        return;
    }

    auto& inst = root.GetComponent<PrefabInstanceComponent>();
    if (root.HasComponent<TransformComponent>()) {
        const auto& transform = root.GetComponent<TransformComponent>();
        inst.parent = transform.parent;
        inst.hasRootTransform = true;
        PrefabComponentIO::CopyLocalTransform(inst.rootTransform, transform);
    }

    if (inst.missingPrefab) {
        return;
    }

    PrefabAsset asset;
    std::string error;
    if (!LoadAsset(inst.prefabPath, asset, error)) {
        return;
    }

    const auto& nodeComp = entity.GetComponent<PrefabNodeComponent>();
    const PrefabNode* assetNode = FindAssetNode(asset, nodeComp.nodeId);
    if (!assetNode) {
        inst.overrides.erase(nodeComp.nodeId);
        PrefabComponentIO::PruneEmptyOverrides(inst);
        return;
    }

    PrefabNodeOverride& nodeOverride = inst.overrides[nodeComp.nodeId];
    const bool isRoot = nodeComp.isRoot || nodeComp.nodeId == inst.rootNodeId || nodeComp.nodeId == asset.rootNodeId;

    if (entity.HasComponent<TransformComponent>()) {
        const auto& live = entity.GetComponent<TransformComponent>();
        if (isRoot) {
            nodeOverride.hasTransform = false;
            nodeOverride.componentOverrides.erase("Transform");
        } else {
            auto& transformProps = nodeOverride.componentOverrides["Transform"].properties;
            auto syncVec = [&](const std::string& name, const Vec3& liveVec, const Vec3& prefabVec) {
                if (std::abs(liveVec.x - prefabVec.x) > PrefabComponentIO::kEpsilon ||
                    std::abs(liveVec.y - prefabVec.y) > PrefabComponentIO::kEpsilon ||
                    std::abs(liveVec.z - prefabVec.z) > PrefabComponentIO::kEpsilon) {
                    transformProps[name] = PropertyValue(liveVec);
                } else {
                    transformProps.erase(name);
                }
            };
            syncVec("position", live.position, assetNode->localTransform.position);
            syncVec("rotation", live.rotation, assetNode->localTransform.rotation);
            syncVec("scale", live.scale, assetNode->localTransform.scale);
            if (transformProps.empty()) {
                nodeOverride.componentOverrides.erase("Transform");
                nodeOverride.hasTransform = false;
            } else {
                nodeOverride.hasTransform = true;
                PrefabComponentIO::CopyLocalTransform(nodeOverride.transform, live);
            }
        }
    }

    std::unordered_set<std::string> seenComponents;
    for (const auto& type : PrefabComponentIO::SupportedTypes()) {
        if (!PrefabComponentIO::HasComponent(entity, type)) {
            continue;
        }
        const PrefabComponentEntry* assetComp = FindAssetComponent(*assetNode, type);
        if (!assetComp) {
            continue;
        }
        seenComponents.insert(type);
        PrefabComponentEntry live;
        if (!PrefabComponentIO::CaptureComponent(entity, type, live)) {
            continue;
        }
        JsonValue liveData = live.data;
        JsonValue baseData = assetComp->data;
        StripRuntimeKeys(type, liveData);
        StripRuntimeKeys(type, baseData);
        std::unordered_map<std::string, PropertyValue> diff;
        PrefabComponentIO::DiffJson(liveData, baseData, diff);
        if (diff.empty()) {
            nodeOverride.componentOverrides.erase(type);
        } else {
            nodeOverride.componentOverrides[type].properties = std::move(diff);
        }

        if (type == "LuaScript") {
            auto liveVars = entity.HasComponent<LuaScriptComponent>()
                ? entity.GetComponent<LuaScriptComponent>().variables
                : std::unordered_map<std::string, PropertyValue>{};
            auto prefabVars = ParseScriptVars(assetComp->data);
            nodeOverride.scriptVars.clear();
            for (const auto& [key, value] : liveVars) {
                auto it = prefabVars.find(key);
                if (it == prefabVars.end() || !value.Equals(it->second)) {
                    nodeOverride.scriptVars[key] = value;
                }
            }
        }
    }

    for (auto it = nodeOverride.componentOverrides.begin(); it != nodeOverride.componentOverrides.end();) {
        if (it->first != "Transform" && seenComponents.count(it->first) == 0) {
            it = nodeOverride.componentOverrides.erase(it);
        } else {
            ++it;
        }
    }

    CleanupStaleOverrides(inst, asset);
    PrefabComponentIO::PruneEmptyOverrides(inst);
}

void PrefabOverrideSystem::SyncInstance(Scene& scene, Entity root) {
    root = FindInstanceRoot(scene, root);
    if (!root) {
        return;
    }
    std::vector<entt::entity> members;
    CollectInstanceEntities(scene, root, members);
    for (auto member : members) {
        Entity entity(member, &scene);
        if (entity) {
            SyncEntityOverrides(scene, entity);
        }
    }
}

void PrefabOverrideSystem::SyncAllInstances(Scene& scene) {
    std::vector<entt::entity> roots;
    auto view = scene.GetEntitiesWith<PrefabInstanceComponent>();
    for (auto handle : view) {
        roots.push_back(handle);
    }
    for (auto handle : roots) {
        Entity root(handle, &scene);
        if (!root) {
            continue;
        }
        std::vector<entt::entity> members;
        CollectInstanceEntities(scene, root, members);
        for (auto member : members) {
            Entity entity(member, &scene);
            if (entity) {
                SyncEntityOverrides(scene, entity);
            }
        }
    }
}

bool PrefabOverrideSystem::IsPropertyOverridden(const PrefabInstanceComponent& inst, const std::string& nodeId,
                                                const std::string& component, const std::string& property) {
    auto nodeIt = inst.overrides.find(nodeId);
    if (nodeIt == inst.overrides.end()) {
        return false;
    }
    if (component == "LuaScript") {
        return nodeIt->second.scriptVars.count(property) > 0;
    }
    auto compIt = nodeIt->second.componentOverrides.find(component);
    if (compIt == nodeIt->second.componentOverrides.end()) {
        return false;
    }
    return compIt->second.properties.count(property) > 0;
}

bool PrefabOverrideSystem::IsTransformOverridden(const PrefabInstanceComponent& inst, const std::string& nodeId) {
    auto nodeIt = inst.overrides.find(nodeId);
    if (nodeIt == inst.overrides.end()) {
        return false;
    }
    if (nodeIt->second.hasTransform) {
        return true;
    }
    return IsComponentOverridden(inst, nodeId, "Transform");
}

bool PrefabOverrideSystem::IsComponentOverridden(const PrefabInstanceComponent& inst, const std::string& nodeId,
                                                 const std::string& component) {
    auto nodeIt = inst.overrides.find(nodeId);
    if (nodeIt == inst.overrides.end()) {
        return false;
    }
    if (component == "LuaScript" && !nodeIt->second.scriptVars.empty()) {
        return true;
    }
    auto compIt = nodeIt->second.componentOverrides.find(component);
    return compIt != nodeIt->second.componentOverrides.end() && !compIt->second.properties.empty();
}

bool PrefabOverrideSystem::HasAnyOverride(const PrefabInstanceComponent& inst) {
    for (const auto& [_, nodeOverride] : inst.overrides) {
        if (!PrefabComponentIO::IsNodeOverrideEmpty(nodeOverride)) {
            return true;
        }
    }
    return false;
}

std::vector<PrefabOverrideRef> PrefabOverrideSystem::ListOverrides(const PrefabInstanceComponent& inst) {
    std::vector<PrefabOverrideRef> refs;
    for (const auto& [nodeId, nodeOverride] : inst.overrides) {
        if (nodeOverride.hasTransform || nodeOverride.componentOverrides.count("Transform")) {
            const auto transformIt = nodeOverride.componentOverrides.find("Transform");
            if (transformIt != nodeOverride.componentOverrides.end()) {
                for (const auto& [property, _] : transformIt->second.properties) {
                    refs.push_back({ nodeId, "Transform", property });
                }
            } else if (nodeOverride.hasTransform) {
                refs.push_back({ nodeId, "Transform", "position" });
                refs.push_back({ nodeId, "Transform", "rotation" });
                refs.push_back({ nodeId, "Transform", "scale" });
            }
        }
        for (const auto& [component, componentOverride] : nodeOverride.componentOverrides) {
            if (component == "Transform") {
                continue;
            }
            for (const auto& [property, _] : componentOverride.properties) {
                refs.push_back({ nodeId, component, property });
            }
        }
        for (const auto& [property, _] : nodeOverride.scriptVars) {
            refs.push_back({ nodeId, "LuaScript", property });
        }
    }
    return refs;
}

bool PrefabOverrideSystem::RevertProperty(Scene& scene, Entity entity, const std::string& component,
                                          const std::string& property) {
    Entity root = FindInstanceRoot(scene, entity);
    if (!root || !entity.HasComponent<PrefabNodeComponent>()) {
        return false;
    }
    auto& inst = root.GetComponent<PrefabInstanceComponent>();
    const std::string nodeId = entity.GetComponent<PrefabNodeComponent>().nodeId;

    PrefabAsset asset;
    std::string error;
    if (!LoadAsset(inst.prefabPath, asset, error)) {
        return false;
    }
    const PrefabNode* assetNode = FindAssetNode(asset, nodeId);
    if (!assetNode) {
        return false;
    }

    auto nodeIt = inst.overrides.find(nodeId);
    if (nodeIt == inst.overrides.end()) {
        return false;
    }

    if (component == "Transform" && entity.HasComponent<TransformComponent>()) {
        auto& live = entity.GetComponent<TransformComponent>();
        if (property == "position" || property.rfind("position.", 0) == 0) {
            live.position = assetNode->localTransform.position;
        } else if (property == "rotation" || property.rfind("rotation.", 0) == 0) {
            live.rotation = assetNode->localTransform.rotation;
        } else if (property == "scale" || property.rfind("scale.", 0) == 0) {
            live.scale = assetNode->localTransform.scale;
        } else {
            ApplyPrefabTransform(live, assetNode->localTransform);
        }
        auto& props = nodeIt->second.componentOverrides["Transform"].properties;
        props.erase(property);
        if (property == "position" || property == "rotation" || property == "scale") {
            props.erase(property + ".x");
            props.erase(property + ".y");
            props.erase(property + ".z");
        }
        if (props.empty()) {
            nodeIt->second.componentOverrides.erase("Transform");
            nodeIt->second.hasTransform = false;
        } else {
            PrefabComponentIO::CopyLocalTransform(nodeIt->second.transform, live);
        }
        PrefabComponentIO::PruneEmptyOverrides(inst);
        return true;
    }

    if (component == "LuaScript" && entity.HasComponent<LuaScriptComponent>()) {
        auto prefabVars = ParseScriptVars(
            FindAssetComponent(*assetNode, "LuaScript") ? FindAssetComponent(*assetNode, "LuaScript")->data : JsonValue{});
        auto& lua = entity.GetComponent<LuaScriptComponent>();
        auto prefabIt = prefabVars.find(property);
        if (prefabIt != prefabVars.end()) {
            lua.variables[property] = prefabIt->second;
        } else {
            lua.variables.erase(property);
        }
        nodeIt->second.scriptVars.erase(property);
        PrefabComponentIO::PruneEmptyOverrides(inst);
        return true;
    }

    const PrefabComponentEntry* assetComp = FindAssetComponent(*assetNode, component);
    if (!assetComp) {
        return false;
    }
    PrefabComponentIO::ApplyEntry(entity, *assetComp);
    auto& props = nodeIt->second.componentOverrides[component].properties;
    props.erase(property);
    if (!props.empty()) {
        JsonValue data = assetComp->data;
        PrefabComponentIO::MergeOverrides(data, props);
        PrefabComponentIO::ApplyComponentJson(entity, component, data, assetComp->enabled);
    } else {
        nodeIt->second.componentOverrides.erase(component);
    }
    PrefabComponentIO::PruneEmptyOverrides(inst);
    return true;
}

bool PrefabOverrideSystem::RevertComponent(Scene& scene, Entity entity, const std::string& component) {
    Entity root = FindInstanceRoot(scene, entity);
    if (!root || !entity.HasComponent<PrefabNodeComponent>()) {
        return false;
    }
    auto& inst = root.GetComponent<PrefabInstanceComponent>();
    const std::string nodeId = entity.GetComponent<PrefabNodeComponent>().nodeId;

    PrefabAsset asset;
    std::string error;
    if (!LoadAsset(inst.prefabPath, asset, error)) {
        return false;
    }
    const PrefabNode* assetNode = FindAssetNode(asset, nodeId);
    if (!assetNode) {
        return false;
    }

    if (component == "Transform" && entity.HasComponent<TransformComponent>()) {
        ApplyPrefabTransform(entity.GetComponent<TransformComponent>(), assetNode->localTransform);
        auto nodeIt = inst.overrides.find(nodeId);
        if (nodeIt != inst.overrides.end()) {
            nodeIt->second.hasTransform = false;
            nodeIt->second.componentOverrides.erase("Transform");
        }
        PrefabComponentIO::PruneEmptyOverrides(inst);
        return true;
    }

    const PrefabComponentEntry* assetComp = FindAssetComponent(*assetNode, component);
    if (assetComp) {
        PrefabComponentIO::ApplyEntry(entity, *assetComp);
    }
    auto nodeIt = inst.overrides.find(nodeId);
    if (nodeIt != inst.overrides.end()) {
        nodeIt->second.componentOverrides.erase(component);
        if (component == "LuaScript") {
            nodeIt->second.scriptVars.clear();
        }
    }
    PrefabComponentIO::PruneEmptyOverrides(inst);
    return true;
}

bool PrefabOverrideSystem::RevertNode(Scene& scene, Entity entity) {
    Entity root = FindInstanceRoot(scene, entity);
    if (!root || !entity.HasComponent<PrefabNodeComponent>()) {
        return false;
    }
    auto& inst = root.GetComponent<PrefabInstanceComponent>();
    const std::string nodeId = entity.GetComponent<PrefabNodeComponent>().nodeId;
    inst.overrides.erase(nodeId);

    PrefabAsset asset;
    std::string error;
    if (!LoadAsset(inst.prefabPath, asset, error)) {
        return false;
    }
    const PrefabNode* assetNode = FindAssetNode(asset, nodeId);
    if (!assetNode) {
        return true;
    }
    const bool isRoot = entity.GetComponent<PrefabNodeComponent>().isRoot || nodeId == inst.rootNodeId;
    if (entity.HasComponent<TransformComponent>() && !isRoot) {
        ApplyPrefabTransform(entity.GetComponent<TransformComponent>(), assetNode->localTransform);
    }
    for (const auto& entry : assetNode->components) {
        PrefabComponentIO::ApplyEntry(entity, entry);
    }
    PrefabComponentIO::PruneEmptyOverrides(inst);
    return true;
}

bool PrefabOverrideSystem::RevertAll(Scene& scene, Entity root) {
    root = FindInstanceRoot(scene, root);
    if (!root || !root.HasComponent<PrefabInstanceComponent>()) {
        return false;
    }
    auto& inst = root.GetComponent<PrefabInstanceComponent>();
    inst.overrides.clear();
    CaptureLiveParentAndRoot(scene, root, inst);
    std::string error;
    Entity refreshed = RefreshInstance(scene, root, &error);
    return static_cast<bool>(refreshed);
}

bool PrefabOverrideSystem::ApplyPropertyToPrefab(Scene& scene, Entity entity, const std::string& component,
                                                 const std::string& property, std::string& error) {
    Entity root = FindInstanceRoot(scene, entity);
    if (!root || !entity.HasComponent<PrefabNodeComponent>()) {
        error = "Entity is not part of a prefab instance.";
        return false;
    }
    SyncAllInstances(scene);
    auto& inst = root.GetComponent<PrefabInstanceComponent>();
    const std::string nodeId = entity.GetComponent<PrefabNodeComponent>().nodeId;

    PrefabAsset asset;
    if (!LoadAsset(inst.prefabPath, asset, error)) {
        return false;
    }
    PrefabNode* assetNode = MutableFindNode(asset, nodeId);
    if (!assetNode) {
        error = "Prefab node not found.";
        return false;
    }

    if (component == "Transform" && entity.HasComponent<TransformComponent>()) {
        WriteTransformToNode(*assetNode, property, entity.GetComponent<TransformComponent>());
    } else if (component == "LuaScript" && entity.HasComponent<LuaScriptComponent>()) {
        PrefabComponentEntry* luaEntry = MutableFindComponent(*assetNode, "LuaScript");
        if (!luaEntry) {
            PrefabComponentEntry created;
            created.type = "LuaScript";
            created.enabled = true;
            created.data = PrefabComponentIO::MakeObject();
            assetNode->components.push_back(std::move(created));
            luaEntry = MutableFindComponent(*assetNode, "LuaScript");
        }
        const auto& vars = entity.GetComponent<LuaScriptComponent>().variables;
        auto it = vars.find(property);
        JsonValue variables = PrefabComponentIO::MakeObject();
        if (const JsonValue* existing = luaEntry->data.Find("variables")) {
            if (existing->IsObject()) {
                variables = *existing;
            }
        }
        if (it != vars.end()) {
            variables.objectValue[property] = PrefabComponentIO::PropertyToJson(it->second);
            MarkScriptVariableAuthored(luaEntry->data, property);
        }
        luaEntry->data.objectValue["variables"] = std::move(variables);
    } else {
        PrefabComponentEntry live;
        if (!PrefabComponentIO::CaptureComponent(entity, component, live)) {
            error = "Component is not present on the entity.";
            return false;
        }
        PrefabComponentEntry* assetComp = MutableFindComponent(*assetNode, component);
        if (!assetComp) {
            assetNode->components.push_back(live);
            assetComp = MutableFindComponent(*assetNode, component);
        }
        const JsonValue* liveValue = PrefabComponentIO::GetPath(live.data, property);
        if (!liveValue) {
            error = "Property not found on live component.";
            return false;
        }
        PrefabComponentIO::SetPath(assetComp->data, property, *liveValue);
    }

    if (!SaveAsset(asset, inst.prefabPath, error)) {
        return false;
    }

    auto nodeIt = inst.overrides.find(nodeId);
    if (nodeIt != inst.overrides.end()) {
        if (component == "LuaScript") {
            nodeIt->second.scriptVars.erase(property);
        } else {
            auto compIt = nodeIt->second.componentOverrides.find(component);
            if (compIt != nodeIt->second.componentOverrides.end()) {
                compIt->second.properties.erase(property);
            }
            if (component == "Transform") {
                nodeIt->second.hasTransform = !nodeIt->second.componentOverrides["Transform"].properties.empty();
            }
        }
    }
    PrefabComponentIO::PruneEmptyOverrides(inst);
    RefreshInstancesOfPrefab(scene, inst.prefabPath);
    return true;
}

bool PrefabOverrideSystem::ApplyComponentToPrefab(Scene& scene, Entity entity, const std::string& component,
                                                  std::string& error) {
    Entity root = FindInstanceRoot(scene, entity);
    if (!root || !entity.HasComponent<PrefabNodeComponent>()) {
        error = "Entity is not part of a prefab instance.";
        return false;
    }
    SyncAllInstances(scene);
    auto& inst = root.GetComponent<PrefabInstanceComponent>();
    const std::string nodeId = entity.GetComponent<PrefabNodeComponent>().nodeId;

    PrefabAsset asset;
    if (!LoadAsset(inst.prefabPath, asset, error)) {
        return false;
    }
    PrefabNode* assetNode = MutableFindNode(asset, nodeId);
    if (!assetNode) {
        error = "Prefab node not found.";
        return false;
    }

    if (component == "Transform" && entity.HasComponent<TransformComponent>()) {
        PrefabComponentIO::CopyLocalTransform(assetNode->localTransform, entity.GetComponent<TransformComponent>());
    } else {
        PrefabComponentEntry live;
        if (!PrefabComponentIO::CaptureComponent(entity, component, live)) {
            error = "Component is not present on the entity.";
            return false;
        }
        if (component == "LuaScript" && entity.HasComponent<LuaScriptComponent>()) {
            for (const auto& [name, _] : entity.GetComponent<LuaScriptComponent>().variables) {
                MarkScriptVariableAuthored(live.data, name);
            }
        }
        if (PrefabComponentEntry* existing = MutableFindComponent(*assetNode, component)) {
            *existing = live;
        } else {
            assetNode->components.push_back(std::move(live));
        }
    }

    if (!SaveAsset(asset, inst.prefabPath, error)) {
        return false;
    }

    auto nodeIt = inst.overrides.find(nodeId);
    if (nodeIt != inst.overrides.end()) {
        nodeIt->second.componentOverrides.erase(component);
        if (component == "Transform") {
            nodeIt->second.hasTransform = false;
        }
        if (component == "LuaScript") {
            nodeIt->second.scriptVars.clear();
        }
    }
    PrefabComponentIO::PruneEmptyOverrides(inst);
    RefreshInstancesOfPrefab(scene, inst.prefabPath);
    return true;
}

bool PrefabOverrideSystem::ApplyNodeToPrefab(Scene& scene, Entity entity, std::string& error) {
    Entity root = FindInstanceRoot(scene, entity);
    if (!root || !entity.HasComponent<PrefabNodeComponent>()) {
        error = "Entity is not part of a prefab instance.";
        return false;
    }
    const auto overrides = ListOverrides(root.GetComponent<PrefabInstanceComponent>());
    const std::string nodeId = entity.GetComponent<PrefabNodeComponent>().nodeId;
    std::unordered_set<std::string> components;
    for (const auto& ref : overrides) {
        if (ref.nodeId == nodeId) {
            components.insert(ref.component);
        }
    }
    for (const auto& component : components) {
        if (!ApplyComponentToPrefab(scene, entity, component, error)) {
            return false;
        }
    }
    return true;
}

bool PrefabOverrideSystem::ApplyAllToPrefab(Scene& scene, Entity root, std::string& error) {
    root = FindInstanceRoot(scene, root);
    if (!root || !root.HasComponent<PrefabInstanceComponent>()) {
        error = "Entity is not a prefab instance.";
        return false;
    }
    const std::string instanceId = root.GetComponent<PrefabInstanceComponent>().instanceId;
    auto refs = ListOverrides(root.GetComponent<PrefabInstanceComponent>());
    std::unordered_map<std::string, std::unordered_set<std::string>> nodeComponents;
    for (const auto& ref : refs) {
        nodeComponents[ref.nodeId].insert(ref.component);
    }
    for (const auto& [nodeId, components] : nodeComponents) {
        Entity nodeEntity = FindEntityByNodeId(scene, instanceId, nodeId);
        if (!nodeEntity) {
            continue;
        }
        for (const auto& component : components) {
            if (!ApplyComponentToPrefab(scene, nodeEntity, component, error)) {
                return false;
            }
        }
    }
    return true;
}

Entity PrefabOverrideSystem::RefreshInstance(Scene& scene, Entity root, std::string* error) {
    root = FindInstanceRoot(scene, root);
    if (!root || !root.HasComponent<PrefabInstanceComponent>()) {
        if (error) {
            *error = "Entity is not a prefab instance.";
        }
        return {};
    }

    std::vector<entt::entity> members;
    CollectInstanceEntities(scene, root, members);

    PrefabInstanceComponent instData = root.GetComponent<PrefabInstanceComponent>();
    CaptureLiveParentAndRoot(scene, root, instData);
    PrefabInstantiator::DestroyInstance(scene, root);

    PrefabAsset asset;
    std::string loadError;
    if (!LoadAsset(instData.prefabPath, asset, loadError)) {
        std::cerr << "[Prefab] Failed to refresh missing prefab '" << instData.prefabPath
                  << "': " << loadError << "\n";
        if (error) {
            *error = loadError;
        }
        return CreateMissingStub(scene, instData, loadError);
    }

    instData.missingPrefab = false;
    instData.missingReason.clear();
    instData.prefabGuid = asset.guid;
    CleanupStaleOverrides(instData, asset);
    return PrefabInstantiator::ReinstantiateInstance(scene, asset, instData);
}

int PrefabOverrideSystem::RefreshInstancesOfPrefab(Scene& scene, const std::string& prefabPath) {
    std::vector<PrefabInstanceComponent> jobs;
    std::vector<Entity> roots;
    auto view = scene.GetEntitiesWith<PrefabInstanceComponent>();
    for (auto handle : view) {
        Entity entity(handle, &scene);
        if (!entity) {
            continue;
        }
        const auto& inst = entity.GetComponent<PrefabInstanceComponent>();
        if (!PathsReferToSamePrefab(inst.prefabPath, prefabPath)) {
            continue;
        }
        roots.push_back(entity);
    }

    for (Entity root : roots) {
        if (!root) {
            continue;
        }
        PrefabInstanceComponent data = root.GetComponent<PrefabInstanceComponent>();
        CaptureLiveParentAndRoot(scene, root, data);
        jobs.push_back(std::move(data));
        PrefabInstantiator::DestroyInstance(scene, root);
    }

    InvalidateAssetCache(prefabPath);
    int refreshed = 0;
    for (auto& data : jobs) {
        PrefabAsset asset;
        std::string error;
        if (!LoadAsset(data.prefabPath, asset, error)) {
            CreateMissingStub(scene, data, error);
            continue;
        }
        data.missingPrefab = false;
        data.missingReason.clear();
        data.prefabGuid = asset.guid;
        CleanupStaleOverrides(data, asset);
        PrefabInstantiator::ReinstantiateInstance(scene, asset, data);
        ++refreshed;
    }
    return refreshed;
}

bool PrefabOverrideSystem::UnpackInstance(Scene& scene, Entity root) {
    root = FindInstanceRoot(scene, root);
    if (!root || !root.HasComponent<PrefabInstanceComponent>()) {
        return false;
    }

    std::vector<entt::entity> members;
    CollectInstanceEntities(scene, root, members);
    for (auto member : members) {
        Entity entity(member, &scene);
        if (entity) {
            SyncEntityOverrides(scene, entity);
        }
    }

    auto& registry = scene.GetRegistry();
    if (registry.all_of<PrefabInstanceComponent>(static_cast<entt::entity>(root))) {
        registry.remove<PrefabInstanceComponent>(static_cast<entt::entity>(root));
    }
    for (auto member : members) {
        if (registry.valid(member) && registry.all_of<PrefabNodeComponent>(member)) {
            registry.remove<PrefabNodeComponent>(member);
        }
    }
    return true;
}

bool PrefabOverrideSystem::CaptureHierarchy(Scene& scene, Entity root, PrefabAsset& outAsset) {
    outAsset = PrefabAsset{};
    outAsset.assetVersion = 1;
    outAsset.guid = GenerateId();
    if (root && root.HasComponent<TagComponent>()) {
        outAsset.name = root.GetComponent<TagComponent>().tag;
    } else {
        outAsset.name = "Prefab";
    }

    std::vector<entt::entity> entities;
    CollectSubtree(scene, root, entities);
    if (entities.empty()) {
        return false;
    }

    std::unordered_map<uint32_t, std::string> idMap;
    std::vector<entt::entity> captured;
    int counter = 0;
    for (auto handle : entities) {
        Entity entity(handle, &scene);
        if (!entity || entity.HasComponent<EditorOnlyComponent>()) {
            continue;
        }
        PrefabNode node;
        node.id = "node_" + std::to_string(counter++);
        node.name = entity.HasComponent<TagComponent>() ? entity.GetComponent<TagComponent>().tag : "Node";
        idMap[static_cast<uint32_t>(handle)] = node.id;
        if (entity.HasComponent<TransformComponent>()) {
            const auto& transform = entity.GetComponent<TransformComponent>();
            PrefabComponentIO::CopyLocalTransform(node.localTransform, transform);
        }
        PrefabComponentIO::CaptureFromEntity(entity, node.components);
        if (handle == static_cast<entt::entity>(root)) {
            outAsset.rootNodeId = node.id;
        }
        captured.push_back(handle);
        outAsset.nodes.push_back(std::move(node));
    }

    for (size_t i = 0; i < captured.size() && i < outAsset.nodes.size(); ++i) {
        Entity entity(captured[i], &scene);
        if (!entity || !entity.HasComponent<TransformComponent>()) {
            continue;
        }
        const auto& transform = entity.GetComponent<TransformComponent>();
        if (transform.parent == entt::null) {
            outAsset.nodes[i].parentId.clear();
            continue;
        }
        auto parentIt = idMap.find(static_cast<uint32_t>(transform.parent));
        if (parentIt != idMap.end()) {
            outAsset.nodes[i].parentId = parentIt->second;
        } else {
            outAsset.nodes[i].parentId.clear();
        }
    }

    for (auto& node : outAsset.nodes) {
        if (node.id == outAsset.rootNodeId) {
            node.parentId.clear();
            node.localTransform.position = Vec3(0, 0, 0);
            node.localTransform.rotation = Vec3(0, 0, 0);
            node.localTransform.scale = Vec3(1, 1, 1);
        }
    }

    return !outAsset.nodes.empty() && !outAsset.rootNodeId.empty();
}

bool PrefabOverrideSystem::CreateFromSelection(Scene& scene, Entity selectionRoot,
                                               const std::filesystem::path& destPath,
                                               Entity* outInstance, std::string& error) {
    if (!selectionRoot) {
        error = "No entity selected.";
        return false;
    }

    PrefabAsset asset;
    if (!CaptureHierarchy(scene, selectionRoot, asset)) {
        error = "Failed to capture entity hierarchy.";
        return false;
    }
    asset.name = destPath.stem().string();

    if (!PrefabSerializer::Serialize(asset, destPath, error)) {
        return false;
    }
    InvalidateAssetCache(destPath.generic_string());

    TransformComponent placement;
    entt::entity parent = entt::null;
    if (selectionRoot.HasComponent<TransformComponent>()) {
        const auto& transform = selectionRoot.GetComponent<TransformComponent>();
        PrefabComponentIO::CopyLocalTransform(placement, transform);
        parent = transform.parent;
    }

    std::vector<entt::entity> toDestroy;
    CollectSubtree(scene, selectionRoot, toDestroy);
    for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
        Entity entity(*it, &scene);
        if (entity) {
            scene.DestroyEntity(entity);
        }
    }

    PrefabInstanceComponent instanceData;
    instanceData.prefabPath = destPath.generic_string();
    instanceData.prefabGuid = asset.guid;
    instanceData.rootNodeId = asset.rootNodeId;
    instanceData.parent = parent;
    instanceData.hasRootTransform = true;
    PrefabComponentIO::CopyLocalTransform(instanceData.rootTransform, placement);

    PrefabInstantiateOptions options;
    options.prefabPath = instanceData.prefabPath;
    options.parent = parent;
    options.instanceData = &instanceData;

    Entity instance = PrefabInstantiator::Instantiate(scene, asset, options);
    if (!instance) {
        error = "Failed to instantiate the new prefab.";
        return false;
    }
    if (outInstance) {
        *outInstance = instance;
    }
    return true;
}

void PrefabOverrideSystem::CleanupStaleOverrides(PrefabInstanceComponent& inst, const PrefabAsset& asset) {
    std::unordered_set<std::string> validNodes;
    std::unordered_map<std::string, std::unordered_set<std::string>> validComponents;
    for (const auto& node : asset.nodes) {
        validNodes.insert(node.id);
        for (const auto& entry : node.components) {
            validComponents[node.id].insert(entry.type);
        }
        validComponents[node.id].insert("Transform");
    }

    for (auto nodeIt = inst.overrides.begin(); nodeIt != inst.overrides.end();) {
        if (!validNodes.count(nodeIt->first)) {
            nodeIt = inst.overrides.erase(nodeIt);
            continue;
        }
        auto& nodeOverride = nodeIt->second;
        for (auto compIt = nodeOverride.componentOverrides.begin();
             compIt != nodeOverride.componentOverrides.end();) {
            if (!validComponents[nodeIt->first].count(compIt->first)) {
                compIt = nodeOverride.componentOverrides.erase(compIt);
            } else {
                const PrefabNode* assetNode = FindAssetNode(asset, nodeIt->first);
                if (assetNode && compIt->first != "Transform") {
                    const PrefabComponentEntry* assetComp = FindAssetComponent(*assetNode, compIt->first);
                    if (assetComp) {
                        for (auto propIt = compIt->second.properties.begin();
                             propIt != compIt->second.properties.end();) {
                            if (!PrefabComponentIO::GetPath(assetComp->data, propIt->first)) {
                                propIt = compIt->second.properties.erase(propIt);
                            } else {
                                ++propIt;
                            }
                        }
                    }
                }
                ++compIt;
            }
        }
        if (nodeOverride.componentOverrides.find("LuaScript") == nodeOverride.componentOverrides.end() &&
            validComponents[nodeIt->first].count("LuaScript") == 0) {
            nodeOverride.scriptVars.clear();
        }
        ++nodeIt;
    }
    PrefabComponentIO::PruneEmptyOverrides(inst);
}

bool PrefabOverrideSystem::PathsReferToSamePrefab(const std::string& a, const std::string& b) {
    if (a == b) {
        return true;
    }
    const std::string na = NormalizePathKey(a);
    const std::string nb = NormalizePathKey(b);
    if (na == nb) {
        return true;
    }
    std::error_code ec;
    if (fs::exists(a, ec) && fs::exists(b, ec) && fs::equivalent(a, b, ec) && !ec) {
        return true;
    }
    return fs::path(na).filename() == fs::path(nb).filename() &&
           fs::path(na).stem() == fs::path(nb).stem() &&
           na.size() > 0 && nb.size() > 0 &&
           (na.find(nb) != std::string::npos || nb.find(na) != std::string::npos);
}

Entity PrefabOverrideSystem::CreateMissingStub(Scene& scene, const PrefabInstanceComponent& instanceData,
                                               const std::string& reason) {
    std::string name = "Missing Prefab";
    fs::path path(instanceData.prefabPath);
    if (!path.empty()) {
        name += " (" + path.filename().string() + ")";
    }
    Entity stub = scene.CreateEntity(name);
    auto& inst = stub.AddComponent<PrefabInstanceComponent>(instanceData);
    inst.missingPrefab = true;
    inst.missingReason = reason;
    inst.hasRootTransform = instanceData.hasRootTransform;
    if (inst.hasRootTransform && stub.HasComponent<TransformComponent>()) {
        PrefabComponentIO::CopyLocalTransform(stub.GetComponent<TransformComponent>(), inst.rootTransform);
    }
    if (inst.parent != entt::null) {
        Entity parent(inst.parent, &scene);
        if (parent) {
            scene.SetParent(stub, parent);
        }
    }
    auto& node = stub.AddComponent<PrefabNodeComponent>();
    node.instanceId = inst.instanceId;
    node.nodeId = inst.rootNodeId.empty() ? "root" : inst.rootNodeId;
    node.isRoot = true;
    std::cerr << "[Prefab] Missing prefab asset '" << inst.prefabPath << "'";
    if (!reason.empty()) {
        std::cerr << ": " << reason;
    }
    std::cerr << "\n";
    return stub;
}

std::string PrefabOverrideSystem::GenerateId() {
    static unsigned int counter = 0;
    std::stringstream idStream;
    idStream << std::hex << std::time(nullptr) << "-" << counter++;
    return idStream.str();
}
