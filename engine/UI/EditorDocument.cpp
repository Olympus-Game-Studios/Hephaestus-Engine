#include "EditorDocument.h"

#include "../Scene/PrefabSerializer.h"
#include "../Scene/PrefabInstantiator.h"
#include "../Scene/PrefabComponentIO.h"
#include "../Scene/PrefabOverrideSystem.h"
#include "../Scene/SceneSerializer.h"
#include "../Scene/Components.h"
#include "../Scripting/ScriptComponent.h"
#include "../Scripting/LuaScriptComponent.h"

#include <algorithm>
#include <ctime>
#include <sstream>
#include <unordered_set>

namespace {
constexpr size_t kUndoLimit = 32;

std::string HierarchyComponentNodeName(const std::string& type) {
    if (type == "MeshRenderer") return "Mesh Renderer";
    if (type == "AudioSource") return "Audio Source";
    return type;
}

JsonValue MakeNull() {
    JsonValue v;
    v.type = JsonValue::Type::Null;
    return v;
}

JsonValue MakeBool(bool value) {
    JsonValue v;
    v.type = JsonValue::Type::Bool;
    v.boolValue = value;
    return v;
}

JsonValue MakeNumber(double value) {
    JsonValue v;
    v.type = JsonValue::Type::Number;
    v.numberValue = value;
    return v;
}

JsonValue MakeString(const std::string& value) {
    JsonValue v;
    v.type = JsonValue::Type::String;
    v.stringValue = value;
    return v;
}

JsonValue MakeObject() {
    JsonValue v;
    v.type = JsonValue::Type::Object;
    return v;
}

JsonValue MakeArray() {
    JsonValue v;
    v.type = JsonValue::Type::Array;
    return v;
}

JsonValue MakeVec3(const Vec3& v) {
    JsonValue arr = MakeArray();
    arr.arrayValue.push_back(MakeNumber(v.x));
    arr.arrayValue.push_back(MakeNumber(v.y));
    arr.arrayValue.push_back(MakeNumber(v.z));
    return arr;
}

std::string LightTypeToString(LightType type) {
    switch (type) {
        case LightType::Directional: return "Directional";
        case LightType::Spot: return "Spot";
        default: return "Point";
    }
}

std::string PhysicsShapeTypeToString(PhysicsShapeType type) {
    switch (type) {
        case PhysicsShapeType::Sphere: return "Sphere";
        case PhysicsShapeType::Capsule: return "Capsule";
        case PhysicsShapeType::Cylinder: return "Cylinder";
        case PhysicsShapeType::Mesh: return "Mesh";
        default: return "Box";
    }
}

JsonValue PropertyValueToJson(const PropertyValue& value) {
    if (value.IsNull()) {
        return MakeNull();
    }
    if (value.IsBool()) {
        return MakeBool(*value.AsBool());
    }
    if (value.IsNumber()) {
        return MakeNumber(*value.AsNumber());
    }
    if (value.IsString()) {
        return MakeString(*value.AsString());
    }
    if (value.IsVec2()) {
        const Vec2& v = *value.AsVec2();
        JsonValue arr = MakeArray();
        arr.arrayValue.push_back(MakeNumber(v.x));
        arr.arrayValue.push_back(MakeNumber(v.y));
        return arr;
    }
    if (value.IsVec3()) {
        const Vec3& v = *value.AsVec3();
        return MakeVec3(v);
    }
    if (value.IsVec4()) {
        const Vec4& v = *value.AsVec4();
        JsonValue arr = MakeArray();
        arr.arrayValue.push_back(MakeNumber(v.x));
        arr.arrayValue.push_back(MakeNumber(v.y));
        arr.arrayValue.push_back(MakeNumber(v.z));
        arr.arrayValue.push_back(MakeNumber(v.w));
        return arr;
    }
    if (value.IsArray()) {
        JsonValue arr = MakeArray();
        for (const auto& entry : *value.AsArray()) {
            arr.arrayValue.push_back(PropertyValueToJson(entry));
        }
        return arr;
    }
    if (value.IsObject()) {
        JsonValue obj = MakeObject();
        for (const auto& [key, entry] : *value.AsObject()) {
            obj.objectValue.emplace(key, PropertyValueToJson(entry));
        }
        return obj;
    }
    return MakeNull();
}
}

void EditorDocument::PushUndoSnapshot() {
    std::string snapshot = SerializeForUndo();
    if (snapshot.empty()) {
        return;
    }
    if (undoStack_.size() >= kUndoLimit) {
        undoStack_.erase(undoStack_.begin());
    }
    undoStack_.push_back(std::move(snapshot));
    redoStack_.clear();
}

bool EditorDocument::Undo(std::string& outError) {
    if (undoStack_.empty()) {
        outError = "Nothing to undo.";
        return false;
    }
    std::string current = SerializeForUndo();
    std::string snapshot = std::move(undoStack_.back());
    undoStack_.pop_back();
    if (!DeserializeFromUndo(snapshot, outError)) {
        return false;
    }
    if (!current.empty()) {
        if (redoStack_.size() >= kUndoLimit) {
            redoStack_.erase(redoStack_.begin());
        }
        redoStack_.push_back(std::move(current));
    }
    return true;
}

bool EditorDocument::Redo(std::string& outError) {
    if (redoStack_.empty()) {
        outError = "Nothing to redo.";
        return false;
    }
    std::string current = SerializeForUndo();
    std::string snapshot = std::move(redoStack_.back());
    redoStack_.pop_back();
    if (!DeserializeFromUndo(snapshot, outError)) {
        return false;
    }
    if (!current.empty()) {
        if (undoStack_.size() >= kUndoLimit) {
            undoStack_.erase(undoStack_.begin());
        }
        undoStack_.push_back(std::move(current));
    }
    return true;
}

bool EditorDocument::CanUndo() const {
    return !undoStack_.empty();
}

bool EditorDocument::CanRedo() const {
    return !redoStack_.empty();
}

void EditorDocument::ClearUndo() {
    undoStack_.clear();
    redoStack_.clear();
}

SceneDocument::SceneDocument(Scene* scene) : scene_(scene) {
    displayName_ = "Scene";
}

bool SceneDocument::OnOpen(const std::filesystem::path& path, std::string& outError) {
    if (!scene_) {
        outError = "Scene not available.";
        return false;
    }
    path_ = path;
    displayName_ = "Scene";

    if (!path_.empty()) {
        if (!SceneSerializer::Deserialize(*scene_, path_, outError)) {
            return false;
        }
        if (postLoadCallback_) {
            postLoadCallback_(*scene_);
        }
    }

    dirty_ = false;
    ClearUndo();
    return true;
}

bool SceneDocument::OnSave(std::string& outError) {
    if (!scene_) {
        outError = "Scene not available.";
        return false;
    }
    if (path_.empty()) {
        outError = "Scene save failed: no path set.";
        return false;
    }
    PrefabOverrideSystem::SyncAllInstances(*scene_);
    SceneSerializer serializer(*scene_);
    if (!serializer.Serialize(path_, outError)) {
        return false;
    }
    dirty_ = false;
    return true;
}

std::string SceneDocument::SerializeForUndo() {
    if (!scene_) {
        return {};
    }
    SceneSerializer serializer(*scene_);
    return serializer.SerializeToString();
}

bool SceneDocument::DeserializeFromUndo(const std::string& data, std::string& outError) {
    if (!scene_) {
        outError = "Scene not available.";
        return false;
    }
    if (!SceneSerializer::DeserializeFromString(*scene_, data)) {
        outError = "Failed to restore scene snapshot.";
        return false;
    }
    if (postUndoCallback_) {
        postUndoCallback_(*scene_);
    }
    dirty_ = true;
    return true;
}

PrefabDocument::PrefabDocument(Scene* previewScene) : scene_(previewScene) {
    displayName_ = "Prefab";
}

bool PrefabDocument::OnOpen(const std::filesystem::path& path, std::string& outError) {
    if (!scene_) {
        outError = "Preview scene not available.";
        return false;
    }

    path_ = path;
    displayName_ = path_.stem().string();

    if (!PrefabSerializer::Deserialize(asset_, path_, outError)) {
        return false;
    }

    if (asset_.guid.empty()) {
        asset_.guid = GenerateNodeId();
        dirty_ = true;
    }

    if (sceneSnapshotBeforeOpen_.empty()) {
        SceneSerializer serializer(*scene_);
        sceneSnapshotBeforeOpen_ = serializer.SerializeToString();
    }

    RebuildPreviewWorld();
    ClearUndo();
    return true;
}

bool PrefabDocument::OnSave(std::string& outError) {
    if (path_.empty()) {
        outError = "Prefab save failed: no path set.";
        return false;
    }
    if (!PrefabSerializer::Serialize(asset_, path_, outError)) {
        return false;
    }
    PrefabOverrideSystem::InvalidateAssetCache(path_.generic_string());
    dirty_ = false;
    if (postSaveCallback_) {
        postSaveCallback_(path_);
    }
    return true;
}

void PrefabDocument::OnClose() {
    // Restore the preview scene from snapshot if we have one
    if (scene_ && !sceneSnapshotBeforeOpen_.empty()) {
        scene_->Clear();
        SceneSerializer::DeserializeFromString(*scene_, sceneSnapshotBeforeOpen_);
        sceneSnapshotBeforeOpen_.clear();
    }
    
    // Always invoke callback to rebuild mesh handles and other resources for the main scene
    // This is needed because rebuildSceneMeshesFromComponents may have removed meshes
    // used by the main scene when it was called for the blueprint preview scene
    if (postCloseCallback_ && scene_) {
        postCloseCallback_(*scene_);
    }
}

std::string PrefabDocument::SerializeForUndo() {
    return PrefabSerializer::SerializeToString(asset_);
}

bool PrefabDocument::DeserializeFromUndo(const std::string& data, std::string& outError) {
    if (!PrefabSerializer::DeserializeFromString(asset_, data, outError)) {
        return false;
    }
    RebuildPreviewWorld();
    dirty_ = true;
    return true;
}

std::vector<std::string> PrefabDocument::GetNodeIds() const {
    std::vector<std::string> ids;
    ids.reserve(asset_.nodes.size());
    for (const auto& node : asset_.nodes) {
        ids.push_back(node.id);
    }
    return ids;
}

PrefabNode* PrefabDocument::FindNodeById(const std::string& nodeId) {
    for (auto& node : asset_.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

Entity PrefabDocument::FindEntityByNodeId(const std::string& nodeId) {
    if (!scene_ || nodeId.empty()) {
        return {};
    }
    auto view = scene_->GetEntitiesWith<PrefabNodeComponent>();
    for (auto entityHandle : view) {
        Entity entity(entityHandle, scene_);
        const auto& nodeComp = entity.GetComponent<PrefabNodeComponent>();
        if (nodeComp.nodeId == nodeId) {
            return entity;
        }
    }
    return {};
}

std::string PrefabDocument::GenerateNodeId() const {
    static unsigned int counter = 0;
    std::stringstream idStream;
    idStream << std::hex << std::time(nullptr) << "-" << counter++;
    return idStream.str();
}

void PrefabDocument::RebuildPreview() {
    RebuildPreviewWorld();
}

void PrefabDocument::RebuildPreviewWorld() {
    if (!scene_) {
        return;
    }

    scene_->Clear();
    scene_->ClearSelection();

    Entity env = scene_->CreateEntity("Prefab Preview Environment");
    env.AddComponent<EnvironmentComponent>();
    env.AddComponent<EditorOnlyComponent>();

    Entity light = scene_->CreateEntity("Prefab Preview Light");
    light.AddComponent<LightComponent>(LightComponent::CreateDirectional(Vec3(0.0f, -1.0f, -0.2f)));
    light.AddComponent<EditorOnlyComponent>();

    PrefabInstantiateOptions options;
    options.prefabPath = path_.generic_string();
    options.attachInstanceComponent = false;
    Entity root = PrefabInstantiator::Instantiate(*scene_, asset_, options);
    rootEntity_ = root ? static_cast<entt::entity>(root) : entt::null;

    if (postRebuildCallback_) {
        postRebuildCallback_(*scene_);
    }
}

bool PrefabDocument::SyncNodeFromEntity(Entity entity) {
    if (!entity || !scene_) {
        return false;
    }
    if (!entity.HasComponent<PrefabNodeComponent>()) {
        return false;
    }

    const auto& nodeComp = entity.GetComponent<PrefabNodeComponent>();
    PrefabNode* node = FindNodeById(nodeComp.nodeId);
    if (!node) {
        return false;
    }

    if (entity.HasComponent<TagComponent>()) {
        node->name = entity.GetComponent<TagComponent>().tag;
    }

    auto& transform = entity.GetComponent<TransformComponent>();
    node->localTransform.position = transform.position;
    node->localTransform.rotation = transform.rotation;
    node->localTransform.scale = transform.scale;

    if (transform.parent != entt::null) {
        Entity parent(transform.parent, scene_);
        if (parent && parent.HasComponent<PrefabNodeComponent>()) {
            node->parentId = parent.GetComponent<PrefabNodeComponent>().nodeId;
        } else {
            node->parentId.clear();
        }
    } else {
        node->parentId.clear();
    }

    if (node->parentId.empty()) {
        asset_.rootNodeId = node->id;
    }

    std::vector<PrefabComponentEntry> updatedComponents;
    PrefabComponentIO::CaptureFromEntity(entity, updatedComponents);

    node->components = std::move(updatedComponents);
    dirty_ = true;
    return true;
}

bool PrefabDocument::AddChildNode(const std::string& parentId, const std::string& nameHint, Entity* outEntity,
                                  std::string* outNodeId) {
    PrefabNode* parentNode = FindNodeById(parentId);
    if (!parentNode) {
        return false;
    }

    PrefabNode node;
    node.id = GenerateNodeId();
    node.name = nameHint.empty() ? "Prefab Node" : nameHint;
    node.parentId = parentNode->id;
    node.localTransform = TransformComponent();

    if (outNodeId) {
        *outNodeId = node.id;
    }

    asset_.nodes.push_back(node);

    Entity parentEntity = FindEntityByNodeId(parentNode->id);
    Entity created = scene_ ? scene_->CreateEntity(node.name) : Entity{};
    if (created) {
        auto& nodeComp = created.AddComponent<PrefabNodeComponent>();
        nodeComp.nodeId = node.id;
        nodeComp.instanceId = "preview";
        if (parentEntity) {
            scene_->SetParent(created, parentEntity);
        }
        if (outEntity) {
            *outEntity = created;
        }
    }

    dirty_ = true;
    return true;
}

std::string PrefabDocument::MakeUniqueChildName(const std::string& parentId, const std::string& baseName) const {
    std::unordered_set<std::string> used;
    for (const auto& node : asset_.nodes) {
        const bool isChild = (node.parentId == parentId) ||
            (parentId == asset_.rootNodeId && node.parentId.empty() && node.id != parentId);
        if (isChild) {
            used.insert(node.name);
        }
    }
    if (!used.count(baseName)) {
        return baseName;
    }
    for (int i = 1; i < 1000; ++i) {
        const std::string candidate = baseName + std::to_string(i);
        if (!used.count(candidate)) {
            return candidate;
        }
    }
    return baseName + "Copy";
}

bool PrefabDocument::NodeHasComponent(const PrefabNode& node, Entity entity, const std::string& type) const {
    if (entity && PrefabComponentIO::HasComponent(entity, type)) {
        return true;
    }
    for (const auto& entry : node.components) {
        if (entry.type == type) {
            return true;
        }
    }
    return false;
}

bool PrefabDocument::AttachComponentToNode(PrefabNode& node, Entity entity, const std::string& type) {
    auto addCompanion = [&](const std::string& companionType) {
        if (entity) {
            if (!PrefabComponentIO::HasComponent(entity, companionType)) {
                PrefabComponentIO::AddComponent(entity, companionType);
            }
        } else {
            bool exists = false;
            for (const auto& entry : node.components) {
                if (entry.type == companionType) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                PrefabComponentEntry companion;
                companion.type = companionType;
                companion.enabled = true;
                companion.data = PrefabComponentIO::MakeObject();
                node.components.push_back(std::move(companion));
            }
        }
    };

    if (entity) {
        if (!PrefabComponentIO::AddComponent(entity, type)) {
            return false;
        }
        if (type == "MeshRenderer") {
            addCompanion("Material");
        }
        if (type == "RigidBody") {
            addCompanion("PhysicsShape");
        }
        return SyncNodeFromEntity(entity);
    }

    if (NodeHasComponent(node, {}, type)) {
        return false;
    }

    PrefabComponentEntry entry;
    entry.type = type;
    entry.enabled = true;
    entry.data = PrefabComponentIO::MakeObject();
    node.components.push_back(std::move(entry));
    if (type == "MeshRenderer") {
        addCompanion("Material");
    }
    if (type == "RigidBody") {
        addCompanion("PhysicsShape");
    }
    dirty_ = true;
    RebuildPreview();
    return true;
}

bool PrefabDocument::AddHierarchyComponent(const std::string& nodeId, const std::string& type,
                                           std::string* outTargetNodeId) {
    PrefabNode* node = FindNodeById(nodeId);
    if (!node) {
        return false;
    }

    bool supported = false;
    for (const auto& supportedType : PrefabComponentIO::SupportedTypes()) {
        if (supportedType == type) {
            supported = true;
            break;
        }
    }
    if (!supported) {
        return false;
    }

    Entity entity = FindEntityByNodeId(nodeId);
    const bool existsOnNode = NodeHasComponent(*node, entity, type);
    const bool sceneComponent = PrefabComponentIO::IsHierarchySceneComponent(type);

    std::string targetId = nodeId;
    if (sceneComponent && (existsOnNode || !node->components.empty())) {
        const std::string childName = MakeUniqueChildName(nodeId, HierarchyComponentNodeName(type));
        Entity childEntity;
        std::string childId;
        if (!AddChildNode(nodeId, childName, &childEntity, &childId)) {
            return false;
        }
        PrefabNode* childNode = FindNodeById(childId);
        if (!childNode) {
            return false;
        }
        if (!AttachComponentToNode(*childNode, childEntity, type)) {
            return false;
        }
        // SyncNodeFromEntity infers parent from the preview entity. If that parent
        // was missing, restore the authored parent so the new node is not promoted
        // to root.
        childNode = FindNodeById(childId);
        if (childNode && childNode->parentId.empty()) {
            childNode->parentId = nodeId;
            if (asset_.rootNodeId == childId) {
                asset_.rootNodeId = nodeId;
            }
        }
        if (outTargetNodeId) {
            *outTargetNodeId = childId;
        }
        return true;
    }

    if (existsOnNode) {
        return false;
    }

    if (!AttachComponentToNode(*node, entity, type)) {
        return false;
    }
    if (outTargetNodeId) {
        *outTargetNodeId = targetId;
    }
    return true;
}

bool PrefabDocument::DeleteNode(const std::string& nodeId) {
    if (nodeId.empty() || nodeId == asset_.rootNodeId) {
        return false;
    }

    std::unordered_set<std::string> toDelete;
    std::vector<std::string> stack;
    stack.push_back(nodeId);

    while (!stack.empty()) {
        std::string current = stack.back();
        stack.pop_back();
        if (toDelete.insert(current).second) {
            for (const auto& node : asset_.nodes) {
                if (node.parentId == current) {
                    stack.push_back(node.id);
                }
            }
        }
    }

    asset_.nodes.erase(std::remove_if(asset_.nodes.begin(), asset_.nodes.end(), [&](const PrefabNode& node) {
        return toDelete.count(node.id) > 0;
    }), asset_.nodes.end());

    if (scene_) {
        for (const auto& id : toDelete) {
            Entity entity = FindEntityByNodeId(id);
            if (entity) {
                scene_->DestroyEntity(entity);
            }
        }
        scene_->ClearSelection();
    }

    dirty_ = true;
    return true;
}

bool PrefabDocument::ReparentNode(const std::string& nodeId, const std::string& newParentId) {
    if (nodeId.empty() || nodeId == asset_.rootNodeId) {
        return false;
    }
    PrefabNode* node = FindNodeById(nodeId);
    if (!node) {
        return false;
    }
    if (newParentId.empty()) {
        node->parentId = asset_.rootNodeId;
    } else {
        node->parentId = newParentId;
    }

    if (scene_) {
        Entity child = FindEntityByNodeId(nodeId);
        Entity parent = FindEntityByNodeId(newParentId);
        if (child) {
            if (parent) {
                scene_->SetParent(child, parent);
            } else {
                scene_->RemoveParent(child);
            }
        }
    }

    dirty_ = true;
    return true;
}
