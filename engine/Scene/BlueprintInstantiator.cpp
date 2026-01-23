#include "BlueprintInstantiator.h"

#include "Components.h"
#include "../Scripting/LuaScriptComponent.h"

#include <atomic>
#include <unordered_map>

namespace {
bool ParseVec3(const JsonValue& value, Vec3& out) {
    if (!value.IsArray()) {
        return false;
    }
    const auto& arr = value.AsArray();
    if (arr.size() < 3) {
        return false;
    }
    if (!arr[0].IsNumber() || !arr[1].IsNumber() || !arr[2].IsNumber()) {
        return false;
    }
    out.x = static_cast<float>(arr[0].AsNumber());
    out.y = static_cast<float>(arr[1].AsNumber());
    out.z = static_cast<float>(arr[2].AsNumber());
    return true;
}

bool ParseBoolLike(const JsonValue& value, bool fallback) {
    if (value.IsBool()) {
        return value.AsBool();
    }
    if (value.IsNumber()) {
        return value.AsNumber() != 0.0;
    }
    return fallback;
}

LightType ParseLightType(const JsonValue& value) {
    if (!value.IsString()) {
        return LightType::Point;
    }
    const std::string& str = value.AsString();
    if (str == "Directional") return LightType::Directional;
    if (str == "Spot") return LightType::Spot;
    return LightType::Point;
}

PhysicsShapeType ParsePhysicsShapeType(const JsonValue& value) {
    if (!value.IsString()) {
        return PhysicsShapeType::Box;
    }
    const std::string& str = value.AsString();
    if (str == "Sphere") return PhysicsShapeType::Sphere;
    if (str == "Capsule") return PhysicsShapeType::Capsule;
    if (str == "Cylinder") return PhysicsShapeType::Cylinder;
    if (str == "Mesh") return PhysicsShapeType::Mesh;
    return PhysicsShapeType::Box;
}

void ApplyPropertyOverridesMeshRenderer(MeshRendererComponent& mesh, const BlueprintComponentOverride& overrideData) {
    for (const auto& [key, value] : overrideData.properties) {
        if (key == "visible" && value.IsBool()) {
            mesh.visible = *value.AsBool();
        } else if (key == "hideInGame" && value.IsBool()) {
            mesh.hideInGame = *value.AsBool();
        } else if (key == "meshSource" && value.IsString()) {
            mesh.meshAssetPath = *value.AsString();
        } else if (key == "meshImported" && value.IsBool()) {
            mesh.meshImported = *value.AsBool();
        } else if (key == "backfaceCulling" && value.IsBool()) {
            mesh.backfaceCulling = *value.AsBool();
        }
    }
}

void ApplyPropertyOverridesMeshCollider(MeshColliderComponent& collider, const BlueprintComponentOverride& overrideData) {
    for (const auto& [key, value] : overrideData.properties) {
        if (key == "enabled" && value.IsBool()) {
            collider.enabled = *value.AsBool();
        } else if (key == "isStatic" && value.IsBool()) {
            collider.isStatic = *value.AsBool();
        } else if (key == "isTrigger" && value.IsBool()) {
            collider.isTrigger = *value.AsBool();
        } else if (key == "debugDraw" && value.IsBool()) {
            collider.debugDraw = *value.AsBool();
        } else if (key == "useComplexCollision" && value.IsBool()) {
            collider.useComplexCollision = *value.AsBool();
        }
    }
}

void ApplyPropertyOverridesPhysicsShape(PhysicsShapeComponent& shape, const BlueprintComponentOverride& overrideData) {
    for (const auto& [key, value] : overrideData.properties) {
        if (key == "type" && value.IsString()) {
            std::string typeStr = *value.AsString();
            JsonValue temp;
            temp.type = JsonValue::Type::String;
            temp.stringValue = typeStr;
            shape.type = ParsePhysicsShapeType(temp);
        } else if (key == "extents" && value.IsVec3()) {
            shape.extents = *value.AsVec3();
        } else if (key == "radius" && value.IsNumber()) {
            shape.radius = static_cast<float>(*value.AsNumber());
        } else if (key == "height" && value.IsNumber()) {
            shape.height = static_cast<float>(*value.AsNumber());
        } else if (key == "offset" && value.IsVec3()) {
            shape.offset = *value.AsVec3();
        }
    }
}

void ApplyPropertyOverridesRigidBody(RigidBodyComponent& body, const BlueprintComponentOverride& overrideData) {
    for (const auto& [key, value] : overrideData.properties) {
        if (key == "mass" && value.IsNumber()) {
            body.mass = static_cast<float>(*value.AsNumber());
        } else if (key == "isStatic" && value.IsBool()) {
            body.isStatic = *value.AsBool();
        } else if (key == "isTrigger" && value.IsBool()) {
            body.isTrigger = *value.AsBool();
        } else if (key == "useGravity" && value.IsBool()) {
            body.useGravity = *value.AsBool();
        } else if (key == "freezeRotation" && value.IsBool()) {
            body.freezeRotation = *value.AsBool();
        } else if (key == "linearDamping" && value.IsNumber()) {
            body.linearDamping = static_cast<float>(*value.AsNumber());
        } else if (key == "angularDamping" && value.IsNumber()) {
            body.angularDamping = static_cast<float>(*value.AsNumber());
        } else if (key == "friction" && value.IsNumber()) {
            body.friction = static_cast<float>(*value.AsNumber());
        } else if (key == "restitution" && value.IsNumber()) {
            body.restitution = static_cast<float>(*value.AsNumber());
        } else if (key == "linearVelocity" && value.IsVec3()) {
            body.linearVelocity = *value.AsVec3();
        } else if (key == "angularVelocity" && value.IsVec3()) {
            body.angularVelocity = *value.AsVec3();
        } else if (key == "debugDraw" && value.IsBool()) {
            body.debugDraw = *value.AsBool();
        }
    }
}

void ApplyPropertyOverridesMaterial(MaterialComponent& material, const BlueprintComponentOverride& overrideData) {
    for (const auto& [key, value] : overrideData.properties) {
        if (key == "albedo" && value.IsVec3()) {
            material.albedo = *value.AsVec3();
        } else if (key == "metallic" && value.IsNumber()) {
            material.metallic = static_cast<float>(*value.AsNumber());
        } else if (key == "roughness" && value.IsNumber()) {
            material.roughness = static_cast<float>(*value.AsNumber());
        } else if (key == "ao" && value.IsNumber()) {
            material.ao = static_cast<float>(*value.AsNumber());
        } else if (key == "emissive" && value.IsVec3()) {
            material.emissive = *value.AsVec3();
        } else if (key == "emissiveStrength" && value.IsNumber()) {
            material.emissiveStrength = static_cast<float>(*value.AsNumber());
        } else if (key == "albedoTexturePath" && value.IsString()) {
            material.albedoTexturePath = *value.AsString();
        } else if (key == "useAlbedoTexture" && value.IsBool()) {
            material.useAlbedoTexture = *value.AsBool();
        } else if (key == "normalTexturePath" && value.IsString()) {
            material.normalTexturePath = *value.AsString();
        } else if (key == "useNormalTexture" && value.IsBool()) {
            material.useNormalTexture = *value.AsBool();
        } else if (key == "roughnessTexturePath" && value.IsString()) {
            material.roughnessTexturePath = *value.AsString();
        } else if (key == "useRoughnessTexture" && value.IsBool()) {
            material.useRoughnessTexture = *value.AsBool();
        } else if (key == "aoTexturePath" && value.IsString()) {
            material.aoTexturePath = *value.AsString();
        } else if (key == "useAoTexture" && value.IsBool()) {
            material.useAoTexture = *value.AsBool();
        } else if (key == "useWorldAlignedTexture" && value.IsBool()) {
            material.useWorldAlignedTexture = *value.AsBool();
        } else if (key == "worldTextureScale" && value.IsNumber()) {
            material.worldTextureScale = static_cast<float>(*value.AsNumber());
        }
    }
}

void ApplyPropertyOverridesLight(LightComponent& light, const BlueprintComponentOverride& overrideData) {
    for (const auto& [key, value] : overrideData.properties) {
        if (key == "type" && value.IsString()) {
            JsonValue temp;
            temp.type = JsonValue::Type::String;
            temp.stringValue = *value.AsString();
            light.type = ParseLightType(temp);
        } else if (key == "color" && value.IsVec3()) {
            light.color = *value.AsVec3();
        } else if (key == "intensity" && value.IsNumber()) {
            light.intensity = static_cast<float>(*value.AsNumber());
        } else if (key == "range" && value.IsNumber()) {
            light.range = static_cast<float>(*value.AsNumber());
        } else if (key == "innerConeAngle" && value.IsNumber()) {
            light.innerConeAngle = static_cast<float>(*value.AsNumber());
        } else if (key == "outerConeAngle" && value.IsNumber()) {
            light.outerConeAngle = static_cast<float>(*value.AsNumber());
        } else if (key == "direction" && value.IsVec3()) {
            light.direction = *value.AsVec3();
        } else if (key == "castShadows" && value.IsBool()) {
            light.castShadows = *value.AsBool();
        } else if (key == "shadowBias" && value.IsNumber()) {
            light.shadowBias = static_cast<float>(*value.AsNumber());
        } else if (key == "shadowStrength" && value.IsNumber()) {
            light.shadowStrength = static_cast<float>(*value.AsNumber());
        } else if (key == "shadowDistance" && value.IsNumber()) {
            light.shadowDistance = static_cast<float>(*value.AsNumber());
        } else if (key == "shadowExtent" && value.IsNumber()) {
            light.shadowExtent = static_cast<float>(*value.AsNumber());
        }
    }
}

void ApplyPropertyOverridesCamera(CameraComponent& camera, const BlueprintComponentOverride& overrideData) {
    for (const auto& [key, value] : overrideData.properties) {
        if (key == "fov" && value.IsNumber()) {
            camera.fov = static_cast<float>(*value.AsNumber());
        } else if (key == "nearPlane" && value.IsNumber()) {
            camera.nearPlane = static_cast<float>(*value.AsNumber());
        } else if (key == "farPlane" && value.IsNumber()) {
            camera.farPlane = static_cast<float>(*value.AsNumber());
        } else if (key == "isPrimary" && value.IsBool()) {
            camera.isPrimary = *value.AsBool();
        }
    }
}
}

Entity BlueprintInstantiator::Instantiate(Scene& scene, const BlueprintAsset& asset, const BlueprintInstantiateOptions& options) {
    std::unordered_map<std::string, Entity> nodeEntities;
    nodeEntities.reserve(asset.nodes.size());

    static std::atomic<uint64_t> s_InstanceCounter{ 1 };
    std::string instanceId = options.instanceId.empty() ? std::to_string(s_InstanceCounter.fetch_add(1)) : options.instanceId;
    std::string rootNodeId;

    for (const BlueprintNode& node : asset.nodes) {
        Entity entity = scene.CreateEntity(node.name);
        nodeEntities[node.id] = entity;

        auto& nodeComp = entity.AddComponent<BlueprintNodeComponent>();
        nodeComp.instanceId = instanceId;
        nodeComp.nodeId = node.id;
    }

    Entity rootEntity;
    for (const BlueprintNode& node : asset.nodes) {
        auto it = nodeEntities.find(node.id);
        if (it == nodeEntities.end()) {
            continue;
        }

        Entity entity = it->second;
        auto& transform = entity.GetComponent<TransformComponent>();
        transform.position = node.localTransform.position;
        transform.rotation = node.localTransform.rotation;
        transform.scale = node.localTransform.scale;

        if (node.parentId.empty()) {
            rootEntity = entity;
            rootNodeId = node.id;
            if (entity.HasComponent<BlueprintNodeComponent>()) {
                entity.GetComponent<BlueprintNodeComponent>().isRoot = true;
            }
        }

        for (const auto& [componentName, componentData] : node.components) {
            if (!componentData.IsObject()) {
                continue;
            }

            if (componentName == "MeshRenderer") {
                auto& mesh = entity.AddComponent<MeshRendererComponent>();
                if (const JsonValue* visibleValue = componentData.Find("visible")) {
                    mesh.visible = visibleValue->IsBool() ? visibleValue->AsBool() : mesh.visible;
                }
                if (const JsonValue* hideInGameValue = componentData.Find("hideInGame")) {
                    mesh.hideInGame = ParseBoolLike(*hideInGameValue, mesh.hideInGame);
                }
                if (const JsonValue* meshSourceValue = componentData.Find("meshSource")) {
                    if (meshSourceValue->IsString()) {
                        mesh.meshAssetPath = meshSourceValue->AsString();
                    }
                }
                if (const JsonValue* meshImportedValue = componentData.Find("meshImported")) {
                    mesh.meshImported = ParseBoolLike(*meshImportedValue, mesh.meshImported);
                }
                if (const JsonValue* backfaceValue = componentData.Find("backfaceCulling")) {
                    mesh.backfaceCulling = ParseBoolLike(*backfaceValue, mesh.backfaceCulling);
                }
                if (const JsonValue* boundsObj = componentData.Find("localBounds")) {
                    if (boundsObj->IsObject()) {
                        if (const JsonValue* minValue = boundsObj->Find("min")) {
                            ParseVec3(*minValue, mesh.localBounds.min);
                        }
                        if (const JsonValue* maxValue = boundsObj->Find("max")) {
                            ParseVec3(*maxValue, mesh.localBounds.max);
                        }
                    }
                }
            } else if (componentName == "MeshCollider") {
                auto& collider = entity.AddComponent<MeshColliderComponent>();
                if (const JsonValue* enabledValue = componentData.Find("enabled")) {
                    collider.enabled = ParseBoolLike(*enabledValue, collider.enabled);
                }
                if (const JsonValue* staticValue = componentData.Find("isStatic")) {
                    collider.isStatic = ParseBoolLike(*staticValue, collider.isStatic);
                }
                if (const JsonValue* triggerValue = componentData.Find("isTrigger")) {
                    collider.isTrigger = ParseBoolLike(*triggerValue, collider.isTrigger);
                }
                if (const JsonValue* debugValue = componentData.Find("debugDraw")) {
                    collider.debugDraw = ParseBoolLike(*debugValue, collider.debugDraw);
                }
                if (const JsonValue* complexValue = componentData.Find("useComplexCollision")) {
                    collider.useComplexCollision = ParseBoolLike(*complexValue, collider.useComplexCollision);
                }
            } else if (componentName == "PhysicsShape") {
                auto& shape = entity.AddComponent<PhysicsShapeComponent>();
                if (const JsonValue* typeValue = componentData.Find("type")) {
                    shape.type = ParsePhysicsShapeType(*typeValue);
                }
                if (const JsonValue* extentsValue = componentData.Find("extents")) {
                    ParseVec3(*extentsValue, shape.extents);
                }
                if (const JsonValue* radiusValue = componentData.Find("radius")) {
                    if (radiusValue->IsNumber()) {
                        shape.radius = static_cast<float>(radiusValue->AsNumber());
                    }
                }
                if (const JsonValue* heightValue = componentData.Find("height")) {
                    if (heightValue->IsNumber()) {
                        shape.height = static_cast<float>(heightValue->AsNumber());
                    }
                }
                if (const JsonValue* offsetValue = componentData.Find("offset")) {
                    ParseVec3(*offsetValue, shape.offset);
                }
            } else if (componentName == "RigidBody") {
                auto& body = entity.AddComponent<RigidBodyComponent>();
                if (const JsonValue* massValue = componentData.Find("mass")) {
                    if (massValue->IsNumber()) {
                        body.mass = static_cast<float>(massValue->AsNumber());
                    }
                }
                if (const JsonValue* staticValue = componentData.Find("isStatic")) {
                    body.isStatic = ParseBoolLike(*staticValue, body.isStatic);
                }
                if (const JsonValue* triggerValue = componentData.Find("isTrigger")) {
                    body.isTrigger = ParseBoolLike(*triggerValue, body.isTrigger);
                }
                if (const JsonValue* gravityValue = componentData.Find("useGravity")) {
                    body.useGravity = ParseBoolLike(*gravityValue, body.useGravity);
                }
                if (const JsonValue* freezeValue = componentData.Find("freezeRotation")) {
                    body.freezeRotation = ParseBoolLike(*freezeValue, body.freezeRotation);
                }
                if (const JsonValue* linearDampingValue = componentData.Find("linearDamping")) {
                    if (linearDampingValue->IsNumber()) {
                        body.linearDamping = static_cast<float>(linearDampingValue->AsNumber());
                    }
                }
                if (const JsonValue* angularDampingValue = componentData.Find("angularDamping")) {
                    if (angularDampingValue->IsNumber()) {
                        body.angularDamping = static_cast<float>(angularDampingValue->AsNumber());
                    }
                }
                if (const JsonValue* frictionValue = componentData.Find("friction")) {
                    if (frictionValue->IsNumber()) {
                        body.friction = static_cast<float>(frictionValue->AsNumber());
                    }
                }
                if (const JsonValue* restitutionValue = componentData.Find("restitution")) {
                    if (restitutionValue->IsNumber()) {
                        body.restitution = static_cast<float>(restitutionValue->AsNumber());
                    }
                }
                if (const JsonValue* linearVelocityValue = componentData.Find("linearVelocity")) {
                    ParseVec3(*linearVelocityValue, body.linearVelocity);
                }
                if (const JsonValue* angularVelocityValue = componentData.Find("angularVelocity")) {
                    ParseVec3(*angularVelocityValue, body.angularVelocity);
                }
                if (const JsonValue* debugValue = componentData.Find("debugDraw")) {
                    body.debugDraw = ParseBoolLike(*debugValue, body.debugDraw);
                }
            } else if (componentName == "Material") {
                auto& material = entity.AddComponent<MaterialComponent>();
                if (const JsonValue* albedoValue = componentData.Find("albedo")) {
                    ParseVec3(*albedoValue, material.albedo);
                }
                if (const JsonValue* metallicValue = componentData.Find("metallic")) {
                    if (metallicValue->IsNumber()) {
                        material.metallic = static_cast<float>(metallicValue->AsNumber());
                    }
                }
                if (const JsonValue* roughnessValue = componentData.Find("roughness")) {
                    if (roughnessValue->IsNumber()) {
                        material.roughness = static_cast<float>(roughnessValue->AsNumber());
                    }
                }
                if (const JsonValue* aoValue = componentData.Find("ao")) {
                    if (aoValue->IsNumber()) {
                        material.ao = static_cast<float>(aoValue->AsNumber());
                    }
                }
                if (const JsonValue* emissiveValue = componentData.Find("emissive")) {
                    ParseVec3(*emissiveValue, material.emissive);
                }
                if (const JsonValue* emissiveStrengthValue = componentData.Find("emissiveStrength")) {
                    if (emissiveStrengthValue->IsNumber()) {
                        material.emissiveStrength = static_cast<float>(emissiveStrengthValue->AsNumber());
                    }
                }
                if (const JsonValue* albedoTextureValue = componentData.Find("albedoTexturePath")) {
                    if (albedoTextureValue->IsString()) {
                        material.albedoTexturePath = albedoTextureValue->AsString();
                    }
                }
                if (const JsonValue* useAlbedoValue = componentData.Find("useAlbedoTexture")) {
                    material.useAlbedoTexture = ParseBoolLike(*useAlbedoValue, material.useAlbedoTexture);
                }
                if (const JsonValue* normalTextureValue = componentData.Find("normalTexturePath")) {
                    if (normalTextureValue->IsString()) {
                        material.normalTexturePath = normalTextureValue->AsString();
                    }
                }
                if (const JsonValue* useNormalValue = componentData.Find("useNormalTexture")) {
                    material.useNormalTexture = ParseBoolLike(*useNormalValue, material.useNormalTexture);
                }
                if (const JsonValue* roughnessTextureValue = componentData.Find("roughnessTexturePath")) {
                    if (roughnessTextureValue->IsString()) {
                        material.roughnessTexturePath = roughnessTextureValue->AsString();
                    }
                }
                if (const JsonValue* useRoughnessValue = componentData.Find("useRoughnessTexture")) {
                    material.useRoughnessTexture = ParseBoolLike(*useRoughnessValue, material.useRoughnessTexture);
                }
                if (const JsonValue* aoTextureValue = componentData.Find("aoTexturePath")) {
                    if (aoTextureValue->IsString()) {
                        material.aoTexturePath = aoTextureValue->AsString();
                    }
                }
                if (const JsonValue* useAoValue = componentData.Find("useAoTexture")) {
                    material.useAoTexture = ParseBoolLike(*useAoValue, material.useAoTexture);
                }
                if (const JsonValue* worldAlignedValue = componentData.Find("useWorldAlignedTexture")) {
                    material.useWorldAlignedTexture = ParseBoolLike(*worldAlignedValue, material.useWorldAlignedTexture);
                }
                if (const JsonValue* worldScaleValue = componentData.Find("worldTextureScale")) {
                    if (worldScaleValue->IsNumber()) {
                        material.worldTextureScale = static_cast<float>(worldScaleValue->AsNumber());
                    }
                }
            } else if (componentName == "Light") {
                auto& light = entity.AddComponent<LightComponent>();
                if (const JsonValue* typeValue = componentData.Find("type")) {
                    light.type = ParseLightType(*typeValue);
                }
                if (const JsonValue* colorValue = componentData.Find("color")) {
                    ParseVec3(*colorValue, light.color);
                }
                if (const JsonValue* intensityValue = componentData.Find("intensity")) {
                    if (intensityValue->IsNumber()) {
                        light.intensity = static_cast<float>(intensityValue->AsNumber());
                    }
                }
                if (const JsonValue* rangeValue = componentData.Find("range")) {
                    if (rangeValue->IsNumber()) {
                        light.range = static_cast<float>(rangeValue->AsNumber());
                    }
                }
                if (const JsonValue* innerValue = componentData.Find("innerConeAngle")) {
                    if (innerValue->IsNumber()) {
                        light.innerConeAngle = static_cast<float>(innerValue->AsNumber());
                    }
                }
                if (const JsonValue* outerValue = componentData.Find("outerConeAngle")) {
                    if (outerValue->IsNumber()) {
                        light.outerConeAngle = static_cast<float>(outerValue->AsNumber());
                    }
                }
                if (const JsonValue* directionValue = componentData.Find("direction")) {
                    ParseVec3(*directionValue, light.direction);
                }
                if (const JsonValue* shadowsValue = componentData.Find("castShadows")) {
                    light.castShadows = ParseBoolLike(*shadowsValue, light.castShadows);
                }
                if (const JsonValue* biasValue = componentData.Find("shadowBias")) {
                    if (biasValue->IsNumber()) {
                        light.shadowBias = static_cast<float>(biasValue->AsNumber());
                    }
                }
                if (const JsonValue* strengthValue = componentData.Find("shadowStrength")) {
                    if (strengthValue->IsNumber()) {
                        light.shadowStrength = static_cast<float>(strengthValue->AsNumber());
                    }
                }
                if (const JsonValue* distanceValue = componentData.Find("shadowDistance")) {
                    if (distanceValue->IsNumber()) {
                        light.shadowDistance = static_cast<float>(distanceValue->AsNumber());
                    }
                }
                if (const JsonValue* extentValue = componentData.Find("shadowExtent")) {
                    if (extentValue->IsNumber()) {
                        light.shadowExtent = static_cast<float>(extentValue->AsNumber());
                    }
                }
            } else if (componentName == "Camera") {
                auto& camera = entity.AddComponent<CameraComponent>();
                if (const JsonValue* fovValue = componentData.Find("fov")) {
                    if (fovValue->IsNumber()) {
                        camera.fov = static_cast<float>(fovValue->AsNumber());
                    }
                }
                if (const JsonValue* nearValue = componentData.Find("nearPlane")) {
                    if (nearValue->IsNumber()) {
                        camera.nearPlane = static_cast<float>(nearValue->AsNumber());
                    }
                }
                if (const JsonValue* farValue = componentData.Find("farPlane")) {
                    if (farValue->IsNumber()) {
                        camera.farPlane = static_cast<float>(farValue->AsNumber());
                    }
                }
                if (const JsonValue* primaryValue = componentData.Find("isPrimary")) {
                    camera.isPrimary = ParseBoolLike(*primaryValue, camera.isPrimary);
                }
            }
        }

        if (node.script.has_value()) {
            auto& lua = entity.AddComponent<LuaScriptComponent>();
            lua.scriptSource = node.script->source;
            lua.variables = node.script->defaultVars;
        }
    }

    for (const BlueprintNode& node : asset.nodes) {
        if (node.parentId.empty()) {
            continue;
        }
        auto itChild = nodeEntities.find(node.id);
        auto itParent = nodeEntities.find(node.parentId);
        if (itChild != nodeEntities.end() && itParent != nodeEntities.end()) {
            scene.SetParent(itChild->second, itParent->second);
        }
    }

    if (rootEntity && options.instanceData) {
        auto& instanceComp = rootEntity.AddComponent<BlueprintInstanceComponent>();
        instanceComp.blueprintPath = options.instanceData->blueprintPath;
        instanceComp.instanceId = options.instanceData->instanceId.empty() ? instanceId : options.instanceData->instanceId;
        instanceComp.rootNodeId = options.instanceData->rootNodeId.empty() ? rootNodeId : options.instanceData->rootNodeId;
        instanceComp.parent = options.instanceData->parent;
        instanceComp.overrides = options.instanceData->overrides;
    } else if (rootEntity) {
        auto& instanceComp = rootEntity.AddComponent<BlueprintInstanceComponent>();
        instanceComp.blueprintPath = options.blueprintPath;
        instanceComp.instanceId = instanceId;
        instanceComp.rootNodeId = rootNodeId;
        instanceComp.parent = options.parent;
    }

    if (rootEntity && options.parent != entt::null) {
        scene.SetParent(rootEntity, Entity(options.parent, &scene));
    }

    if (options.instanceData) {
        for (const auto& [nodeId, overrideData] : options.instanceData->overrides) {
            auto it = nodeEntities.find(nodeId);
            if (it == nodeEntities.end()) {
                continue;
            }
            Entity entity = it->second;
            if (overrideData.hasTransform) {
                auto& transform = entity.GetComponent<TransformComponent>();
                transform.position = overrideData.transform.position;
                transform.rotation = overrideData.transform.rotation;
                transform.scale = overrideData.transform.scale;
            }
            for (const auto& [componentType, componentOverride] : overrideData.componentOverrides) {
                if (componentType == "MeshRenderer" && entity.HasComponent<MeshRendererComponent>()) {
                    ApplyPropertyOverridesMeshRenderer(entity.GetComponent<MeshRendererComponent>(), componentOverride);
                } else if (componentType == "MeshCollider" && entity.HasComponent<MeshColliderComponent>()) {
                    ApplyPropertyOverridesMeshCollider(entity.GetComponent<MeshColliderComponent>(), componentOverride);
                } else if (componentType == "PhysicsShape" && entity.HasComponent<PhysicsShapeComponent>()) {
                    ApplyPropertyOverridesPhysicsShape(entity.GetComponent<PhysicsShapeComponent>(), componentOverride);
                } else if (componentType == "RigidBody" && entity.HasComponent<RigidBodyComponent>()) {
                    ApplyPropertyOverridesRigidBody(entity.GetComponent<RigidBodyComponent>(), componentOverride);
                } else if (componentType == "Material" && entity.HasComponent<MaterialComponent>()) {
                    ApplyPropertyOverridesMaterial(entity.GetComponent<MaterialComponent>(), componentOverride);
                } else if (componentType == "Light" && entity.HasComponent<LightComponent>()) {
                    ApplyPropertyOverridesLight(entity.GetComponent<LightComponent>(), componentOverride);
                } else if (componentType == "Camera" && entity.HasComponent<CameraComponent>()) {
                    ApplyPropertyOverridesCamera(entity.GetComponent<CameraComponent>(), componentOverride);
                }
            }
            if (!overrideData.scriptVars.empty() && entity.HasComponent<LuaScriptComponent>()) {
                auto& lua = entity.GetComponent<LuaScriptComponent>();
                for (const auto& [key, value] : overrideData.scriptVars) {
                    lua.variables[key] = value;
                }
            }
        }
    }

    return rootEntity;
}
