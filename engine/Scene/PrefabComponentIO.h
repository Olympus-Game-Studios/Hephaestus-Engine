#pragma once

#include "Components.h"
#include "Entity.h"
#include "Json.h"
#include "PrefabAsset.h"
#include "../Core/PropertyValue.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace PrefabComponentIO {

constexpr float kEpsilon = 0.0001f;

JsonValue MakeNull();
JsonValue MakeBool(bool value);
JsonValue MakeNumber(double value);
JsonValue MakeString(const std::string& value);
JsonValue MakeObject();
JsonValue MakeArray();
JsonValue MakeVec3(const Vec3& value);

PropertyValue JsonToProperty(const JsonValue& value);
JsonValue PropertyToJson(const PropertyValue& value);
bool JsonEquals(const JsonValue& a, const JsonValue& b, float epsilon = kEpsilon);

const JsonValue* GetPath(const JsonValue& root, const std::string& path);
bool SetPath(JsonValue& root, const std::string& path, const JsonValue& value);

void DiffJson(const JsonValue& current, const JsonValue& baseline,
              std::unordered_map<std::string, PropertyValue>& outDiff,
              const std::string& prefix = {});

void MergeOverrides(JsonValue& data, const std::unordered_map<std::string, PropertyValue>& properties);

JsonValue MeshRendererToJson(const MeshRendererComponent& mesh);
JsonValue MeshColliderToJson(const MeshColliderComponent& collider);
bool CaptureFromEntity(Entity entity, std::vector<PrefabComponentEntry>& out);
bool CaptureComponent(Entity entity, const std::string& type, PrefabComponentEntry& out);
bool ApplyEntry(Entity entity, const PrefabComponentEntry& entry);
bool ApplyComponentJson(Entity entity, const std::string& type, const JsonValue& data, bool enabled);

bool HasComponent(Entity entity, const std::string& type);
bool AddComponent(Entity entity, const std::string& type);
bool RemoveComponent(Entity entity, const std::string& type);

const std::vector<std::string>& SupportedTypes();

// MeshRenderer/Light/Camera/AudioSource get independent transforms in the Prefab
// Editor hierarchy (Unreal SceneComponent-style child nodes).
bool IsHierarchySceneComponent(const std::string& type);

bool TransformsEqual(const TransformComponent& a, const TransformComponent& b, float epsilon = kEpsilon);
void CopyLocalTransform(TransformComponent& dst, const TransformComponent& src);

std::string LightTypeToString(LightType type);
LightType ParseLightType(const std::string& str);
std::string PhysicsShapeTypeToString(PhysicsShapeType type);
PhysicsShapeType ParsePhysicsShapeType(const std::string& str);
std::string CameraProjectionToString(CameraProjection projection);
CameraProjection ParseCameraProjection(const std::string& str);

bool IsNodeOverrideEmpty(const PrefabNodeOverride& nodeOverride);
// {nodeId: {"transform": {...}, "components": {type: {path: value}}, "scriptVars": {...}}}, as scenes store overrides.
JsonValue NodeOverridesToJson(const std::unordered_map<std::string, PrefabNodeOverride>& overrides);
void NodeOverridesFromJson(const JsonValue& value, std::unordered_map<std::string, PrefabNodeOverride>& out);
void PruneEmptyOverrides(PrefabInstanceComponent& instance);

} // namespace PrefabComponentIO
