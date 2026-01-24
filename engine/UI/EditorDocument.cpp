#include "EditorDocument.h"

#include "../Scene/BlueprintSerializer.h"
#include "../Scene/BlueprintInstantiator.h"
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
}

bool EditorDocument::Undo(std::string& outError) {
    if (undoStack_.empty()) {
        outError = "Nothing to undo.";
        return false;
    }
    std::string snapshot = std::move(undoStack_.back());
    undoStack_.pop_back();
    return DeserializeFromUndo(snapshot, outError);
}

void EditorDocument::ClearUndo() {
    undoStack_.clear();
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

BlueprintDocument::BlueprintDocument(Scene* previewScene) : scene_(previewScene) {
    displayName_ = "Blueprint Mode";
}

bool BlueprintDocument::OnOpen(const std::filesystem::path& path, std::string& outError) {
    if (!scene_) {
        outError = "Preview scene not available.";
        return false;
    }

    path_ = path;
    displayName_ = "Blueprint Mode";

    if (!BlueprintSerializer::Deserialize(asset_, path_, outError)) {
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

bool BlueprintDocument::OnSave(std::string& outError) {
    if (path_.empty()) {
        outError = "Blueprint save failed: no path set.";
        return false;
    }
    if (!BlueprintSerializer::Serialize(asset_, path_, outError)) {
        return false;
    }
    dirty_ = false;
    return true;
}

void BlueprintDocument::OnClose() {
    if (!scene_ || sceneSnapshotBeforeOpen_.empty()) {
        return;
    }
    scene_->Clear();
    SceneSerializer::DeserializeFromString(*scene_, sceneSnapshotBeforeOpen_);
    sceneSnapshotBeforeOpen_.clear();
}

std::string BlueprintDocument::SerializeForUndo() {
    return BlueprintSerializer::SerializeToString(asset_);
}

bool BlueprintDocument::DeserializeFromUndo(const std::string& data, std::string& outError) {
    if (!BlueprintSerializer::DeserializeFromString(asset_, data, outError)) {
        return false;
    }
    RebuildPreviewWorld();
    dirty_ = true;
    return true;
}

std::vector<std::string> BlueprintDocument::GetNodeIds() const {
    std::vector<std::string> ids;
    ids.reserve(asset_.nodes.size());
    for (const auto& node : asset_.nodes) {
        ids.push_back(node.id);
    }
    return ids;
}

BlueprintNode* BlueprintDocument::FindNodeById(const std::string& nodeId) {
    for (auto& node : asset_.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

Entity BlueprintDocument::FindEntityByNodeId(const std::string& nodeId) {
    if (!scene_ || nodeId.empty()) {
        return {};
    }
    auto view = scene_->GetEntitiesWith<BlueprintNodeComponent>();
    for (auto entityHandle : view) {
        Entity entity(entityHandle, scene_);
        const auto& nodeComp = entity.GetComponent<BlueprintNodeComponent>();
        if (nodeComp.nodeId == nodeId) {
            return entity;
        }
    }
    return {};
}

std::string BlueprintDocument::GenerateNodeId() const {
    static unsigned int counter = 0;
    std::stringstream idStream;
    idStream << std::hex << std::time(nullptr) << "-" << counter++;
    return idStream.str();
}

void BlueprintDocument::RebuildPreviewWorld() {
    if (!scene_) {
        return;
    }

    scene_->Clear();
    scene_->ClearSelection();

    Entity env = scene_->CreateEntity("Blueprint Preview Environment");
    env.AddComponent<EnvironmentComponent>();
    env.AddComponent<EditorOnlyComponent>();

    Entity light = scene_->CreateEntity("Blueprint Preview Light");
    light.AddComponent<LightComponent>(LightComponent::CreateDirectional(Vec3(0.0f, -1.0f, -0.2f)));
    light.AddComponent<EditorOnlyComponent>();

    BlueprintInstantiateOptions options;
    options.blueprintPath = path_.generic_string();
    Entity root = BlueprintInstantiator::Instantiate(*scene_, asset_, options);
    rootEntity_ = root ? static_cast<entt::entity>(root) : entt::null;

    if (postRebuildCallback_) {
        postRebuildCallback_(*scene_);
    }
}

bool BlueprintDocument::SyncNodeFromEntity(Entity entity) {
    if (!entity || !scene_) {
        return false;
    }
    if (!entity.HasComponent<BlueprintNodeComponent>()) {
        return false;
    }

    const auto& nodeComp = entity.GetComponent<BlueprintNodeComponent>();
    BlueprintNode* node = FindNodeById(nodeComp.nodeId);
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
        if (parent && parent.HasComponent<BlueprintNodeComponent>()) {
            node->parentId = parent.GetComponent<BlueprintNodeComponent>().nodeId;
        } else {
            node->parentId.clear();
        }
    } else {
        node->parentId.clear();
    }

    if (node->parentId.empty()) {
        asset_.rootNodeId = node->id;
    }

    std::vector<BlueprintComponentEntry> updatedComponents;
    updatedComponents.reserve(12);
    std::unordered_set<std::string> knownTypes;

    auto addComponentEntry = [&](const std::string& type, bool enabled, JsonValue data) {
        BlueprintComponentEntry entry;
        entry.type = type;
        entry.enabled = enabled;
        entry.data = std::move(data);
        updatedComponents.push_back(std::move(entry));
        knownTypes.insert(type);
    };

    if (entity.HasComponent<MeshRendererComponent>()) {
        const auto& mesh = entity.GetComponent<MeshRendererComponent>();
        JsonValue data = MakeObject();
        data.objectValue["visible"] = MakeBool(mesh.visible);
        data.objectValue["hideInGame"] = MakeBool(mesh.hideInGame);
        if (!mesh.meshAssetPath.empty()) {
            data.objectValue["meshSource"] = MakeString(mesh.meshAssetPath);
        }
        data.objectValue["meshImported"] = MakeBool(mesh.meshImported);
        data.objectValue["backfaceCulling"] = MakeBool(mesh.backfaceCulling);
        JsonValue boundsObj = MakeObject();
        boundsObj.objectValue["min"] = MakeVec3(mesh.localBounds.min);
        boundsObj.objectValue["max"] = MakeVec3(mesh.localBounds.max);
        data.objectValue["localBounds"] = std::move(boundsObj);
        addComponentEntry("MeshRenderer", true, std::move(data));
    }

    if (entity.HasComponent<MeshColliderComponent>()) {
        const auto& collider = entity.GetComponent<MeshColliderComponent>();
        JsonValue data = MakeObject();
        data.objectValue["enabled"] = MakeBool(collider.enabled);
        data.objectValue["isStatic"] = MakeBool(collider.isStatic);
        data.objectValue["isTrigger"] = MakeBool(collider.isTrigger);
        data.objectValue["debugDraw"] = MakeBool(collider.debugDraw);
        data.objectValue["useComplexCollision"] = MakeBool(collider.useComplexCollision);
        addComponentEntry("MeshCollider", collider.enabled, std::move(data));
    }

    if (entity.HasComponent<PhysicsShapeComponent>()) {
        const auto& shape = entity.GetComponent<PhysicsShapeComponent>();
        JsonValue data = MakeObject();
        data.objectValue["type"] = MakeString(PhysicsShapeTypeToString(shape.type));
        data.objectValue["extents"] = MakeVec3(shape.extents);
        data.objectValue["radius"] = MakeNumber(shape.radius);
        data.objectValue["height"] = MakeNumber(shape.height);
        data.objectValue["offset"] = MakeVec3(shape.offset);
        addComponentEntry("PhysicsShape", true, std::move(data));
    }

    if (entity.HasComponent<RigidBodyComponent>()) {
        const auto& body = entity.GetComponent<RigidBodyComponent>();
        JsonValue data = MakeObject();
        data.objectValue["mass"] = MakeNumber(body.mass);
        data.objectValue["isStatic"] = MakeBool(body.isStatic);
        data.objectValue["isTrigger"] = MakeBool(body.isTrigger);
        data.objectValue["useGravity"] = MakeBool(body.useGravity);
        data.objectValue["freezeRotation"] = MakeBool(body.freezeRotation);
        data.objectValue["linearDamping"] = MakeNumber(body.linearDamping);
        data.objectValue["angularDamping"] = MakeNumber(body.angularDamping);
        data.objectValue["friction"] = MakeNumber(body.friction);
        data.objectValue["restitution"] = MakeNumber(body.restitution);
        data.objectValue["linearVelocity"] = MakeVec3(body.linearVelocity);
        data.objectValue["angularVelocity"] = MakeVec3(body.angularVelocity);
        data.objectValue["debugDraw"] = MakeBool(body.debugDraw);
        addComponentEntry("RigidBody", true, std::move(data));
    }

    if (entity.HasComponent<MaterialComponent>()) {
        const auto& material = entity.GetComponent<MaterialComponent>();
        JsonValue data = MakeObject();
        data.objectValue["albedo"] = MakeVec3(material.albedo);
        data.objectValue["metallic"] = MakeNumber(material.metallic);
        data.objectValue["roughness"] = MakeNumber(material.roughness);
        data.objectValue["ao"] = MakeNumber(material.ao);
        data.objectValue["emissive"] = MakeVec3(material.emissive);
        data.objectValue["emissiveStrength"] = MakeNumber(material.emissiveStrength);
        data.objectValue["albedoTexturePath"] = MakeString(material.albedoTexturePath);
        data.objectValue["useAlbedoTexture"] = MakeBool(material.useAlbedoTexture);
        data.objectValue["normalTexturePath"] = MakeString(material.normalTexturePath);
        data.objectValue["useNormalTexture"] = MakeBool(material.useNormalTexture);
        data.objectValue["roughnessTexturePath"] = MakeString(material.roughnessTexturePath);
        data.objectValue["useRoughnessTexture"] = MakeBool(material.useRoughnessTexture);
        data.objectValue["aoTexturePath"] = MakeString(material.aoTexturePath);
        data.objectValue["useAoTexture"] = MakeBool(material.useAoTexture);
        data.objectValue["useWorldAlignedTexture"] = MakeBool(material.useWorldAlignedTexture);
        data.objectValue["worldTextureScale"] = MakeNumber(material.worldTextureScale);
        addComponentEntry("Material", true, std::move(data));
    }

    if (entity.HasComponent<LightComponent>()) {
        const auto& light = entity.GetComponent<LightComponent>();
        JsonValue data = MakeObject();
        data.objectValue["type"] = MakeString(LightTypeToString(light.type));
        data.objectValue["color"] = MakeVec3(light.color);
        data.objectValue["intensity"] = MakeNumber(light.intensity);
        data.objectValue["range"] = MakeNumber(light.range);
        data.objectValue["innerConeAngle"] = MakeNumber(light.innerConeAngle);
        data.objectValue["outerConeAngle"] = MakeNumber(light.outerConeAngle);
        data.objectValue["direction"] = MakeVec3(light.direction);
        data.objectValue["castShadows"] = MakeBool(light.castShadows);
        data.objectValue["shadowBias"] = MakeNumber(light.shadowBias);
        data.objectValue["shadowStrength"] = MakeNumber(light.shadowStrength);
        data.objectValue["shadowDistance"] = MakeNumber(light.shadowDistance);
        data.objectValue["shadowExtent"] = MakeNumber(light.shadowExtent);
        addComponentEntry("Light", true, std::move(data));
    }

    if (entity.HasComponent<CameraComponent>()) {
        const auto& camera = entity.GetComponent<CameraComponent>();
        JsonValue data = MakeObject();
        data.objectValue["fov"] = MakeNumber(camera.fov);
        data.objectValue["nearPlane"] = MakeNumber(camera.nearPlane);
        data.objectValue["farPlane"] = MakeNumber(camera.farPlane);
        data.objectValue["isPrimary"] = MakeBool(camera.isPrimary);
        addComponentEntry("Camera", true, std::move(data));
    }

    if (entity.HasComponent<ScriptComponent>()) {
        const auto& script = entity.GetComponent<ScriptComponent>();
        JsonValue data = MakeObject();
        JsonValue names = MakeArray();
        for (const auto& name : script.scriptNames) {
            names.arrayValue.push_back(MakeString(name));
        }
        data.objectValue["scriptNames"] = std::move(names);
        addComponentEntry("ScriptComponent", true, std::move(data));
    }

    if (entity.HasComponent<LuaScriptComponent>()) {
        const auto& lua = entity.GetComponent<LuaScriptComponent>();
        JsonValue data = MakeObject();
        if (!lua.scriptPath.empty()) {
            data.objectValue["path"] = MakeString(lua.scriptPath);
        }
        if (!lua.scriptSource.empty()) {
            data.objectValue["source"] = MakeString(lua.scriptSource);
        }
        data.objectValue["enabled"] = MakeBool(lua.enabled);
        JsonValue vars = MakeObject();
        for (const auto& [key, value] : lua.variables) {
            vars.objectValue.emplace(key, PropertyValueToJson(value));
        }
        data.objectValue["variables"] = std::move(vars);
        addComponentEntry("LuaScript", lua.enabled, std::move(data));
    }

    for (const auto& existing : node->components) {
        if (!knownTypes.count(existing.type)) {
            updatedComponents.push_back(existing);
        }
    }

    node->components = std::move(updatedComponents);
    dirty_ = true;
    return true;
}

bool BlueprintDocument::AddChildNode(const std::string& parentId, const std::string& nameHint, Entity* outEntity) {
    BlueprintNode* parentNode = FindNodeById(parentId);
    if (!parentNode) {
        return false;
    }

    BlueprintNode node;
    node.id = GenerateNodeId();
    node.name = nameHint.empty() ? "Blueprint Node" : nameHint;
    node.parentId = parentNode->id;
    node.localTransform = TransformComponent();

    asset_.nodes.push_back(node);

    Entity parentEntity = FindEntityByNodeId(parentNode->id);
    Entity created = scene_ ? scene_->CreateEntity(node.name) : Entity{};
    if (created) {
        auto& nodeComp = created.AddComponent<BlueprintNodeComponent>();
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

bool BlueprintDocument::DeleteNode(const std::string& nodeId) {
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

    asset_.nodes.erase(std::remove_if(asset_.nodes.begin(), asset_.nodes.end(), [&](const BlueprintNode& node) {
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

bool BlueprintDocument::ReparentNode(const std::string& nodeId, const std::string& newParentId) {
    if (nodeId.empty() || nodeId == asset_.rootNodeId) {
        return false;
    }
    BlueprintNode* node = FindNodeById(nodeId);
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
