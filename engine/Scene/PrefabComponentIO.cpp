#include "PrefabComponentIO.h"

#include "Scene.h"
#include "../Scripting/LuaScriptComponent.h"
#include "../Scripting/ScriptComponent.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace PrefabComponentIO {
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

std::vector<std::string> SplitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string current;
    current.reserve(path.size());
    for (char ch : path) {
        if (ch == '.') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

void CaptureMeshRenderer(const MeshRendererComponent& mesh, JsonValue& data) {
    data = MakeObject();
    data.objectValue["visible"] = MakeBool(mesh.visible);
    data.objectValue["hideInGame"] = MakeBool(mesh.hideInGame);
    data.objectValue["showHiddenOutline"] = MakeBool(mesh.showHiddenOutline);
    if (!mesh.meshAssetPath.empty()) {
        data.objectValue["meshSource"] = MakeString(mesh.meshAssetPath);
    }
    data.objectValue["meshImported"] = MakeBool(mesh.meshImported);
    data.objectValue["backfaceCulling"] = MakeBool(mesh.backfaceCulling);
    data.objectValue["submeshCount"] = MakeNumber(mesh.submeshCount);
    JsonValue materials = MakeArray();
    for (const auto& path : mesh.appliedMaterialPaths) {
        materials.arrayValue.push_back(MakeString(path));
    }
    data.objectValue["appliedMaterialPaths"] = std::move(materials);
    JsonValue boundsObj = MakeObject();
    boundsObj.objectValue["min"] = MakeVec3(mesh.localBounds.min);
    boundsObj.objectValue["max"] = MakeVec3(mesh.localBounds.max);
    data.objectValue["localBounds"] = std::move(boundsObj);
}

void ApplyMeshRenderer(MeshRendererComponent& mesh, const JsonValue& data) {
    if (!data.IsObject()) {
        return;
    }
    if (const JsonValue* visibleValue = data.Find("visible")) {
        mesh.visible = ParseBoolLike(*visibleValue, mesh.visible);
    }
    if (const JsonValue* hideInGameValue = data.Find("hideInGame")) {
        mesh.hideInGame = ParseBoolLike(*hideInGameValue, mesh.hideInGame);
    }
    if (const JsonValue* showHiddenOutlineValue = data.Find("showHiddenOutline")) {
        mesh.showHiddenOutline = ParseBoolLike(*showHiddenOutlineValue, mesh.showHiddenOutline);
    }
    if (const JsonValue* meshSourceValue = data.Find("meshSource")) {
        if (meshSourceValue->IsString()) {
            mesh.meshAssetPath = meshSourceValue->AsString();
        }
    }
    if (const JsonValue* meshImportedValue = data.Find("meshImported")) {
        mesh.meshImported = ParseBoolLike(*meshImportedValue, mesh.meshImported);
    }
    if (const JsonValue* backfaceValue = data.Find("backfaceCulling")) {
        mesh.backfaceCulling = ParseBoolLike(*backfaceValue, mesh.backfaceCulling);
    }
    if (const JsonValue* submeshValue = data.Find("submeshCount")) {
        if (submeshValue->IsNumber()) {
            mesh.submeshCount = static_cast<int>(submeshValue->AsNumber());
        }
    }
    if (const JsonValue* materialsValue = data.Find("appliedMaterialPaths")) {
        if (materialsValue->IsArray()) {
            mesh.appliedMaterialPaths.clear();
            for (const auto& entry : materialsValue->AsArray()) {
                if (entry.IsString()) {
                    mesh.appliedMaterialPaths.push_back(entry.AsString());
                }
            }
        }
    }
    if (const JsonValue* boundsObj = data.Find("localBounds")) {
        if (boundsObj->IsObject()) {
            if (const JsonValue* minValue = boundsObj->Find("min")) {
                ParseVec3(*minValue, mesh.localBounds.min);
            }
            if (const JsonValue* maxValue = boundsObj->Find("max")) {
                ParseVec3(*maxValue, mesh.localBounds.max);
            }
        }
    }
}

void CaptureMeshCollider(const MeshColliderComponent& collider, JsonValue& data) {
    data = MakeObject();
    data.objectValue["enabled"] = MakeBool(collider.enabled);
    data.objectValue["isStatic"] = MakeBool(collider.isStatic);
    data.objectValue["isTrigger"] = MakeBool(collider.isTrigger);
    data.objectValue["debugDraw"] = MakeBool(collider.debugDraw);
    data.objectValue["useComplexCollision"] = MakeBool(collider.useComplexCollision);
}

void ApplyMeshCollider(MeshColliderComponent& collider, const JsonValue& data) {
    if (!data.IsObject()) {
        return;
    }
    if (const JsonValue* enabledField = data.Find("enabled")) {
        collider.enabled = ParseBoolLike(*enabledField, collider.enabled);
    }
    if (const JsonValue* staticValue = data.Find("isStatic")) {
        collider.isStatic = ParseBoolLike(*staticValue, collider.isStatic);
    }
    if (const JsonValue* triggerValue = data.Find("isTrigger")) {
        collider.isTrigger = ParseBoolLike(*triggerValue, collider.isTrigger);
    }
    if (const JsonValue* debugValue = data.Find("debugDraw")) {
        collider.debugDraw = ParseBoolLike(*debugValue, collider.debugDraw);
    }
    if (const JsonValue* complexValue = data.Find("useComplexCollision")) {
        collider.useComplexCollision = ParseBoolLike(*complexValue, collider.useComplexCollision);
    }
}

void CapturePhysicsShape(const PhysicsShapeComponent& shape, JsonValue& data) {
    data = MakeObject();
    data.objectValue["type"] = MakeString(PhysicsShapeTypeToString(shape.type));
    data.objectValue["extents"] = MakeVec3(shape.extents);
    data.objectValue["radius"] = MakeNumber(shape.radius);
    data.objectValue["height"] = MakeNumber(shape.height);
    data.objectValue["offset"] = MakeVec3(shape.offset);
}

void ApplyPhysicsShape(PhysicsShapeComponent& shape, const JsonValue& data) {
    if (!data.IsObject()) {
        return;
    }
    if (const JsonValue* typeValue = data.Find("type")) {
        if (typeValue->IsString()) {
            shape.type = ParsePhysicsShapeType(typeValue->AsString());
        }
    }
    if (const JsonValue* extentsValue = data.Find("extents")) {
        ParseVec3(*extentsValue, shape.extents);
    }
    if (const JsonValue* radiusValue = data.Find("radius")) {
        if (radiusValue->IsNumber()) {
            shape.radius = static_cast<float>(radiusValue->AsNumber());
        }
    }
    if (const JsonValue* heightValue = data.Find("height")) {
        if (heightValue->IsNumber()) {
            shape.height = static_cast<float>(heightValue->AsNumber());
        }
    }
    if (const JsonValue* offsetValue = data.Find("offset")) {
        ParseVec3(*offsetValue, shape.offset);
    }
}

void CaptureRigidBody(const RigidBodyComponent& body, JsonValue& data) {
    data = MakeObject();
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
}

void ApplyRigidBody(RigidBodyComponent& body, const JsonValue& data) {
    if (!data.IsObject()) {
        return;
    }
    if (const JsonValue* massValue = data.Find("mass")) {
        if (massValue->IsNumber()) {
            body.mass = static_cast<float>(massValue->AsNumber());
        }
    }
    if (const JsonValue* staticValue = data.Find("isStatic")) {
        body.isStatic = ParseBoolLike(*staticValue, body.isStatic);
    }
    if (const JsonValue* triggerValue = data.Find("isTrigger")) {
        body.isTrigger = ParseBoolLike(*triggerValue, body.isTrigger);
    }
    if (const JsonValue* gravityValue = data.Find("useGravity")) {
        body.useGravity = ParseBoolLike(*gravityValue, body.useGravity);
    }
    if (const JsonValue* freezeValue = data.Find("freezeRotation")) {
        body.freezeRotation = ParseBoolLike(*freezeValue, body.freezeRotation);
    }
    if (const JsonValue* linearDampingValue = data.Find("linearDamping")) {
        if (linearDampingValue->IsNumber()) {
            body.linearDamping = static_cast<float>(linearDampingValue->AsNumber());
        }
    }
    if (const JsonValue* angularDampingValue = data.Find("angularDamping")) {
        if (angularDampingValue->IsNumber()) {
            body.angularDamping = static_cast<float>(angularDampingValue->AsNumber());
        }
    }
    if (const JsonValue* frictionValue = data.Find("friction")) {
        if (frictionValue->IsNumber()) {
            body.friction = static_cast<float>(frictionValue->AsNumber());
        }
    }
    if (const JsonValue* restitutionValue = data.Find("restitution")) {
        if (restitutionValue->IsNumber()) {
            body.restitution = static_cast<float>(restitutionValue->AsNumber());
        }
    }
    if (const JsonValue* linearVelocityValue = data.Find("linearVelocity")) {
        ParseVec3(*linearVelocityValue, body.linearVelocity);
    }
    if (const JsonValue* angularVelocityValue = data.Find("angularVelocity")) {
        ParseVec3(*angularVelocityValue, body.angularVelocity);
    }
    if (const JsonValue* debugValue = data.Find("debugDraw")) {
        body.debugDraw = ParseBoolLike(*debugValue, body.debugDraw);
    }
}

void CaptureMaterial(const MaterialComponent& material, JsonValue& data) {
    data = MakeObject();
    data.objectValue["albedo"] = MakeVec3(material.albedo);
    data.objectValue["metallic"] = MakeNumber(material.metallic);
    data.objectValue["roughness"] = MakeNumber(material.roughness);
    data.objectValue["ao"] = MakeNumber(material.ao);
    data.objectValue["emissive"] = MakeVec3(material.emissive);
    data.objectValue["emissiveStrength"] = MakeNumber(material.emissiveStrength);
    data.objectValue["reflectionStrength"] = MakeNumber(material.reflectionStrength);
    data.objectValue["specular"] = MakeNumber(material.specular);
    data.objectValue["opacity"] = MakeNumber(material.opacity);
    data.objectValue["albedoTexturePath"] = MakeString(material.albedoTexturePath);
    data.objectValue["useAlbedoTexture"] = MakeBool(material.useAlbedoTexture);
    data.objectValue["normalTexturePath"] = MakeString(material.normalTexturePath);
    data.objectValue["useNormalTexture"] = MakeBool(material.useNormalTexture);
    data.objectValue["roughnessTexturePath"] = MakeString(material.roughnessTexturePath);
    data.objectValue["useRoughnessTexture"] = MakeBool(material.useRoughnessTexture);
    data.objectValue["metallicTexturePath"] = MakeString(material.metallicTexturePath);
    data.objectValue["useMetallicTexture"] = MakeBool(material.useMetallicTexture);
    data.objectValue["aoTexturePath"] = MakeString(material.aoTexturePath);
    data.objectValue["useAoTexture"] = MakeBool(material.useAoTexture);
    data.objectValue["useWorldAlignedTexture"] = MakeBool(material.useWorldAlignedTexture);
    data.objectValue["worldTextureScale"] = MakeNumber(material.worldTextureScale);
}

void ApplyMaterial(MaterialComponent& material, const JsonValue& data) {
    if (!data.IsObject()) {
        return;
    }
    if (const JsonValue* albedoValue = data.Find("albedo")) {
        ParseVec3(*albedoValue, material.albedo);
    }
    if (const JsonValue* metallicValue = data.Find("metallic")) {
        if (metallicValue->IsNumber()) {
            material.metallic = static_cast<float>(metallicValue->AsNumber());
        }
    }
    if (const JsonValue* roughnessValue = data.Find("roughness")) {
        if (roughnessValue->IsNumber()) {
            material.roughness = static_cast<float>(roughnessValue->AsNumber());
        }
    }
    if (const JsonValue* aoValue = data.Find("ao")) {
        if (aoValue->IsNumber()) {
            material.ao = static_cast<float>(aoValue->AsNumber());
        }
    }
    if (const JsonValue* emissiveValue = data.Find("emissive")) {
        ParseVec3(*emissiveValue, material.emissive);
    }
    if (const JsonValue* emissiveStrengthValue = data.Find("emissiveStrength")) {
        if (emissiveStrengthValue->IsNumber()) {
            material.emissiveStrength = static_cast<float>(emissiveStrengthValue->AsNumber());
        }
    }
    if (const JsonValue* reflectionValue = data.Find("reflectionStrength")) {
        if (reflectionValue->IsNumber()) {
            material.reflectionStrength = static_cast<float>(reflectionValue->AsNumber());
        }
    }
    if (const JsonValue* specularValue = data.Find("specular")) {
        if (specularValue->IsNumber()) {
            material.specular = static_cast<float>(specularValue->AsNumber());
        }
    }
    if (const JsonValue* opacityValue = data.Find("opacity")) {
        if (opacityValue->IsNumber()) {
            material.opacity = static_cast<float>(opacityValue->AsNumber());
        }
    }
    if (const JsonValue* albedoTextureValue = data.Find("albedoTexturePath")) {
        if (albedoTextureValue->IsString()) {
            material.albedoTexturePath = albedoTextureValue->AsString();
        }
    }
    if (const JsonValue* useAlbedoValue = data.Find("useAlbedoTexture")) {
        material.useAlbedoTexture = ParseBoolLike(*useAlbedoValue, material.useAlbedoTexture);
    }
    if (const JsonValue* normalTextureValue = data.Find("normalTexturePath")) {
        if (normalTextureValue->IsString()) {
            material.normalTexturePath = normalTextureValue->AsString();
        }
    }
    if (const JsonValue* useNormalValue = data.Find("useNormalTexture")) {
        material.useNormalTexture = ParseBoolLike(*useNormalValue, material.useNormalTexture);
    }
    if (const JsonValue* roughnessTextureValue = data.Find("roughnessTexturePath")) {
        if (roughnessTextureValue->IsString()) {
            material.roughnessTexturePath = roughnessTextureValue->AsString();
        }
    }
    if (const JsonValue* useRoughnessValue = data.Find("useRoughnessTexture")) {
        material.useRoughnessTexture = ParseBoolLike(*useRoughnessValue, material.useRoughnessTexture);
    }
    if (const JsonValue* metallicTextureValue = data.Find("metallicTexturePath")) {
        if (metallicTextureValue->IsString()) {
            material.metallicTexturePath = metallicTextureValue->AsString();
        }
    }
    if (const JsonValue* useMetallicValue = data.Find("useMetallicTexture")) {
        material.useMetallicTexture = ParseBoolLike(*useMetallicValue, material.useMetallicTexture);
    }
    if (const JsonValue* aoTextureValue = data.Find("aoTexturePath")) {
        if (aoTextureValue->IsString()) {
            material.aoTexturePath = aoTextureValue->AsString();
        }
    }
    if (const JsonValue* useAoValue = data.Find("useAoTexture")) {
        material.useAoTexture = ParseBoolLike(*useAoValue, material.useAoTexture);
    }
    if (const JsonValue* worldAlignedValue = data.Find("useWorldAlignedTexture")) {
        material.useWorldAlignedTexture = ParseBoolLike(*worldAlignedValue, material.useWorldAlignedTexture);
    }
    if (const JsonValue* worldScaleValue = data.Find("worldTextureScale")) {
        if (worldScaleValue->IsNumber()) {
            material.worldTextureScale = static_cast<float>(worldScaleValue->AsNumber());
        }
    }
}

void CaptureLight(const LightComponent& light, JsonValue& data) {
    data = MakeObject();
    data.objectValue["type"] = MakeString(LightTypeToString(light.type));
    data.objectValue["enabled"] = MakeBool(light.enabled);
    data.objectValue["color"] = MakeVec3(light.color);
    data.objectValue["intensity"] = MakeNumber(light.intensity);
    data.objectValue["range"] = MakeNumber(light.range);
    data.objectValue["innerConeAngle"] = MakeNumber(light.innerConeAngle);
    data.objectValue["outerConeAngle"] = MakeNumber(light.outerConeAngle);
    data.objectValue["direction"] = MakeVec3(light.direction);
    data.objectValue["castShadows"] = MakeBool(light.castShadows);
    data.objectValue["shadowAutoFit"] = MakeBool(light.shadowAutoFit);
    data.objectValue["shadowBias"] = MakeNumber(light.shadowBias);
    data.objectValue["shadowNormalBias"] = MakeNumber(light.shadowNormalBias);
    data.objectValue["shadowStrength"] = MakeNumber(light.shadowStrength);
    data.objectValue["shadowDistance"] = MakeNumber(light.shadowDistance);
    data.objectValue["shadowExtent"] = MakeNumber(light.shadowExtent);
    data.objectValue["shadowMaxDistance"] = MakeNumber(light.shadowMaxDistance);
}

void ApplyLight(LightComponent& light, const JsonValue& data) {
    if (!data.IsObject()) {
        return;
    }
    if (const JsonValue* typeValue = data.Find("type")) {
        if (typeValue->IsString()) {
            light.type = ParseLightType(typeValue->AsString());
        }
    }
    if (const JsonValue* enabledValue = data.Find("enabled")) {
        light.enabled = ParseBoolLike(*enabledValue, light.enabled);
    }
    if (const JsonValue* colorValue = data.Find("color")) {
        ParseVec3(*colorValue, light.color);
    }
    if (const JsonValue* intensityValue = data.Find("intensity")) {
        if (intensityValue->IsNumber()) {
            light.intensity = static_cast<float>(intensityValue->AsNumber());
        }
    }
    if (const JsonValue* rangeValue = data.Find("range")) {
        if (rangeValue->IsNumber()) {
            light.range = static_cast<float>(rangeValue->AsNumber());
        }
    }
    if (const JsonValue* innerValue = data.Find("innerConeAngle")) {
        if (innerValue->IsNumber()) {
            light.innerConeAngle = static_cast<float>(innerValue->AsNumber());
        }
    }
    if (const JsonValue* outerValue = data.Find("outerConeAngle")) {
        if (outerValue->IsNumber()) {
            light.outerConeAngle = static_cast<float>(outerValue->AsNumber());
        }
    }
    if (const JsonValue* directionValue = data.Find("direction")) {
        ParseVec3(*directionValue, light.direction);
    }
    if (const JsonValue* shadowsValue = data.Find("castShadows")) {
        light.castShadows = ParseBoolLike(*shadowsValue, light.castShadows);
    }
    if (const JsonValue* autoFitValue = data.Find("shadowAutoFit")) {
        light.shadowAutoFit = ParseBoolLike(*autoFitValue, light.shadowAutoFit);
    }
    if (const JsonValue* biasValue = data.Find("shadowBias")) {
        if (biasValue->IsNumber()) {
            light.shadowBias = static_cast<float>(biasValue->AsNumber());
        }
    }
    if (const JsonValue* normalBiasValue = data.Find("shadowNormalBias")) {
        if (normalBiasValue->IsNumber()) {
            light.shadowNormalBias = static_cast<float>(normalBiasValue->AsNumber());
        }
    }
    if (const JsonValue* strengthValue = data.Find("shadowStrength")) {
        if (strengthValue->IsNumber()) {
            light.shadowStrength = static_cast<float>(strengthValue->AsNumber());
        }
    }
    if (const JsonValue* distanceValue = data.Find("shadowDistance")) {
        if (distanceValue->IsNumber()) {
            light.shadowDistance = static_cast<float>(distanceValue->AsNumber());
        }
    }
    if (const JsonValue* extentValue = data.Find("shadowExtent")) {
        if (extentValue->IsNumber()) {
            light.shadowExtent = static_cast<float>(extentValue->AsNumber());
        }
    }
    if (const JsonValue* maxDistValue = data.Find("shadowMaxDistance")) {
        if (maxDistValue->IsNumber()) {
            light.shadowMaxDistance = static_cast<float>(maxDistValue->AsNumber());
        }
    }
}

void CaptureCamera(const CameraComponent& camera, JsonValue& data) {
    data = MakeObject();
    data.objectValue["fov"] = MakeNumber(camera.fov);
    data.objectValue["nearPlane"] = MakeNumber(camera.nearPlane);
    data.objectValue["farPlane"] = MakeNumber(camera.farPlane);
    data.objectValue["projection"] = MakeString(CameraProjectionToString(camera.projection));
    data.objectValue["isActive"] = MakeBool(camera.isActive);
    data.objectValue["isPrimary"] = MakeBool(camera.isPrimary || camera.isActive);
    data.objectValue["showEditorMesh"] = MakeBool(camera.showEditorMesh);
}

void ApplyCamera(CameraComponent& camera, const JsonValue& data) {
    if (!data.IsObject()) {
        return;
    }
    if (const JsonValue* fovValue = data.Find("fov")) {
        if (fovValue->IsNumber()) {
            camera.fov = static_cast<float>(fovValue->AsNumber());
        }
    }
    if (const JsonValue* nearValue = data.Find("nearPlane")) {
        if (nearValue->IsNumber()) {
            camera.nearPlane = static_cast<float>(nearValue->AsNumber());
        }
    }
    if (const JsonValue* farValue = data.Find("farPlane")) {
        if (farValue->IsNumber()) {
            camera.farPlane = static_cast<float>(farValue->AsNumber());
        }
    }
    if (const JsonValue* projectionValue = data.Find("projection")) {
        if (projectionValue->IsString()) {
            camera.projection = ParseCameraProjection(projectionValue->AsString());
        }
    }
    if (const JsonValue* activeValue = data.Find("isActive")) {
        camera.isActive = ParseBoolLike(*activeValue, camera.isActive);
    }
    if (const JsonValue* primaryValue = data.Find("isPrimary")) {
        camera.isPrimary = ParseBoolLike(*primaryValue, camera.isPrimary);
        if (!data.Find("isActive") && camera.isPrimary) {
            camera.isActive = true;
        }
    }
    if (const JsonValue* showMeshValue = data.Find("showEditorMesh")) {
        camera.showEditorMesh = ParseBoolLike(*showMeshValue, camera.showEditorMesh);
    }
}

void CaptureLuaScript(const LuaScriptComponent& lua, JsonValue& data) {
    data = MakeObject();
    if (!lua.scriptPath.empty()) {
        data.objectValue["path"] = MakeString(lua.scriptPath);
    }
    if (!lua.scriptSource.empty()) {
        data.objectValue["source"] = MakeString(lua.scriptSource);
    }
    data.objectValue["enabled"] = MakeBool(lua.enabled);
    JsonValue vars = MakeObject();
    for (const auto& [key, value] : lua.variables) {
        vars.objectValue.emplace(key, PropertyToJson(value));
    }
    data.objectValue["variables"] = std::move(vars);
    JsonValue authored = MakeArray();
    std::vector<std::string> authoredNames(
        lua.authoredVariables.begin(), lua.authoredVariables.end());
    std::sort(authoredNames.begin(), authoredNames.end());
    for (const std::string& name : authoredNames) {
        authored.arrayValue.push_back(MakeString(name));
    }
    data.objectValue["authoredVariables"] = std::move(authored);
}

void ApplyLuaScript(LuaScriptComponent& lua, const JsonValue& data, bool enabled) {
    lua.enabled = enabled;
    if (!data.IsObject()) {
        return;
    }
    if (const JsonValue* pathValue = data.Find("path")) {
        if (pathValue->IsString()) {
            lua.scriptPath = pathValue->AsString();
        }
    }
    if (const JsonValue* sourceValue = data.Find("source")) {
        if (sourceValue->IsString()) {
            lua.scriptSource = sourceValue->AsString();
        }
    }
    if (const JsonValue* enabledValue = data.Find("enabled")) {
        lua.enabled = ParseBoolLike(*enabledValue, lua.enabled);
    }
    if (const JsonValue* varsValue = data.Find("variables")) {
        if (varsValue->IsObject()) {
            lua.variables.clear();
            for (const auto& [key, entry] : varsValue->AsObject()) {
                lua.variables.emplace(key, JsonToProperty(entry));
            }
        }
    }
    lua.authoredVariables.clear();
    lua.authoredVariablesInitialized = false;
    if (const JsonValue* authoredValue = data.Find("authoredVariables")) {
        lua.authoredVariablesInitialized = true;
        if (authoredValue->IsArray()) {
            for (const JsonValue& entry : authoredValue->AsArray()) {
                if (entry.IsString()) {
                    lua.authoredVariables.insert(entry.AsString());
                }
            }
        }
    }
}

void CaptureAudioSource(const AudioSourceComponent& audio, JsonValue& data) {
    data = MakeObject();
    data.objectValue["clipPath"] = MakeString(audio.clipPath);
    data.objectValue["enabled"] = MakeBool(audio.enabled);
    data.objectValue["playOnStart"] = MakeBool(audio.playOnStart);
    data.objectValue["loop"] = MakeBool(audio.loop);
    data.objectValue["volume"] = MakeNumber(audio.volume);
}

void ApplyAudioSource(AudioSourceComponent& audio, const JsonValue& data, bool enabled) {
    audio.enabled = enabled;
    if (!data.IsObject()) {
        return;
    }
    if (const JsonValue* clipValue = data.Find("clipPath")) {
        if (clipValue->IsString()) {
            audio.clipPath = clipValue->AsString();
        }
    }
    if (const JsonValue* enabledValue = data.Find("enabled")) {
        audio.enabled = ParseBoolLike(*enabledValue, audio.enabled);
    }
    if (const JsonValue* playValue = data.Find("playOnStart")) {
        audio.playOnStart = ParseBoolLike(*playValue, audio.playOnStart);
    }
    if (const JsonValue* loopValue = data.Find("loop")) {
        audio.loop = ParseBoolLike(*loopValue, audio.loop);
    }
    if (const JsonValue* volumeValue = data.Find("volume")) {
        if (volumeValue->IsNumber()) {
            audio.volume = static_cast<float>(volumeValue->AsNumber());
        }
    }
}

void CaptureScriptComponent(const ScriptComponent& script, JsonValue& data) {
    data = MakeObject();
    JsonValue names = MakeArray();
    for (const auto& name : script.scriptNames) {
        names.arrayValue.push_back(MakeString(name));
    }
    data.objectValue["scriptNames"] = std::move(names);
}

void ApplyScriptComponent(ScriptComponent& script, const JsonValue& data) {
    if (!data.IsObject()) {
        return;
    }
    if (const JsonValue* namesValue = data.Find("scriptNames")) {
        if (namesValue->IsArray()) {
            script.scriptNames.clear();
            for (const auto& entry : namesValue->AsArray()) {
                if (entry.IsString()) {
                    script.scriptNames.push_back(entry.AsString());
                }
            }
        }
    }
}

void CaptureCharacter(const CharacterComponent& character, JsonValue& data) {
    data = MakeObject();
    if (!character.characterPath.empty()) {
        data.objectValue["characterPath"] = MakeString(character.characterPath);
    }
    if (!character.defaultAnimation.empty()) {
        data.objectValue["defaultAnimation"] = MakeString(character.defaultAnimation);
    }
    data.objectValue["playbackSpeed"] = MakeNumber(character.playbackSpeed);
    data.objectValue["runController"] = MakeBool(character.runController);
    if (!character.controllerScriptPath.empty()) {
        data.objectValue["controllerScriptPath"] = MakeString(character.controllerScriptPath);
    }
    JsonValue vars = MakeObject();
    for (const auto& [key, value] : character.controllerVariables) {
        vars.objectValue.emplace(key, PropertyToJson(value));
    }
    data.objectValue["controllerVariables"] = std::move(vars);
    JsonValue authored = MakeArray();
    std::vector<std::string> authoredNames(
        character.authoredControllerVariables.begin(),
        character.authoredControllerVariables.end());
    std::sort(authoredNames.begin(), authoredNames.end());
    for (const std::string& name : authoredNames) {
        authored.arrayValue.push_back(MakeString(name));
    }
    data.objectValue["authoredControllerVariables"] = std::move(authored);
    JsonValue materials = MakeArray();
    for (const std::string& path : character.materialOverrides) {
        materials.arrayValue.push_back(MakeString(path));
    }
    data.objectValue["materialOverrides"] = std::move(materials);
}

void ApplyCharacter(CharacterComponent& character, const JsonValue& data) {
    if (!data.IsObject()) {
        return;
    }
    if (const JsonValue* pathValue = data.Find("characterPath")) {
        if (pathValue->IsString()) {
            character.characterPath = pathValue->AsString();
        }
    }
    if (const JsonValue* animValue = data.Find("defaultAnimation")) {
        if (animValue->IsString()) {
            character.defaultAnimation = animValue->AsString();
        }
    }
    if (const JsonValue* speedValue = data.Find("playbackSpeed")) {
        if (speedValue->IsNumber()) {
            character.playbackSpeed = static_cast<float>(speedValue->AsNumber());
        }
    }
    if (const JsonValue* runValue = data.Find("runController")) {
        character.runController = ParseBoolLike(*runValue, character.runController);
    }
    if (const JsonValue* scriptValue = data.Find("controllerScriptPath")) {
        if (scriptValue->IsString()) {
            character.controllerScriptPath = scriptValue->AsString();
        }
    }
    if (const JsonValue* varsValue = data.Find("controllerVariables")) {
        if (varsValue->IsObject()) {
            character.controllerVariables.clear();
            for (const auto& [key, entry] : varsValue->AsObject()) {
                character.controllerVariables.emplace(key, JsonToProperty(entry));
            }
        }
    }
    character.authoredControllerVariables.clear();
    character.authoredControllerVariablesInitialized = false;
    if (const JsonValue* authoredValue = data.Find("authoredControllerVariables")) {
        character.authoredControllerVariablesInitialized = true;
        if (authoredValue->IsArray()) {
            for (const JsonValue& entry : authoredValue->AsArray()) {
                if (entry.IsString()) {
                    character.authoredControllerVariables.insert(entry.AsString());
                }
            }
        }
    }
    if (const JsonValue* materialsValue = data.Find("materialOverrides")) {
        if (materialsValue->IsArray()) {
            character.materialOverrides.clear();
            for (const JsonValue& entry : materialsValue->AsArray()) {
                if (entry.IsString()) {
                    character.materialOverrides.push_back(entry.AsString());
                }
            }
        }
    }
}

} // namespace

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

JsonValue MakeVec3(const Vec3& value) {
    JsonValue arr = MakeArray();
    arr.arrayValue.push_back(MakeNumber(value.x));
    arr.arrayValue.push_back(MakeNumber(value.y));
    arr.arrayValue.push_back(MakeNumber(value.z));
    return arr;
}

PropertyValue JsonToProperty(const JsonValue& value) {
    if (value.IsBool()) {
        return PropertyValue(value.AsBool());
    }
    if (value.IsNumber()) {
        return PropertyValue(value.AsNumber());
    }
    if (value.IsString()) {
        return PropertyValue(value.AsString());
    }
    if (value.IsArray()) {
        const auto& arr = value.AsArray();
        if (arr.size() == 2 && arr[0].IsNumber() && arr[1].IsNumber()) {
            return PropertyValue(Vec2(static_cast<float>(arr[0].AsNumber()), static_cast<float>(arr[1].AsNumber())));
        }
        if (arr.size() == 3 && arr[0].IsNumber() && arr[1].IsNumber() && arr[2].IsNumber()) {
            return PropertyValue(Vec3(static_cast<float>(arr[0].AsNumber()),
                                      static_cast<float>(arr[1].AsNumber()),
                                      static_cast<float>(arr[2].AsNumber())));
        }
        if (arr.size() == 4 && arr[0].IsNumber() && arr[1].IsNumber() && arr[2].IsNumber() && arr[3].IsNumber()) {
            return PropertyValue(Vec4(static_cast<float>(arr[0].AsNumber()),
                                      static_cast<float>(arr[1].AsNumber()),
                                      static_cast<float>(arr[2].AsNumber()),
                                      static_cast<float>(arr[3].AsNumber())));
        }
        PropertyValue::Array arrayOut;
        arrayOut.reserve(arr.size());
        for (const auto& entry : arr) {
            arrayOut.push_back(JsonToProperty(entry));
        }
        return PropertyValue(std::move(arrayOut));
    }
    if (value.IsObject()) {
        PropertyValue::Object obj;
        for (const auto& [key, entry] : value.AsObject()) {
            obj.emplace(key, JsonToProperty(entry));
        }
        return PropertyValue(std::move(obj));
    }
    return PropertyValue();
}

JsonValue PropertyToJson(const PropertyValue& value) {
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
        return MakeVec3(*value.AsVec3());
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
            arr.arrayValue.push_back(PropertyToJson(entry));
        }
        return arr;
    }
    if (value.IsObject()) {
        JsonValue obj = MakeObject();
        for (const auto& [key, entry] : *value.AsObject()) {
            obj.objectValue.emplace(key, PropertyToJson(entry));
        }
        return obj;
    }
    return MakeNull();
}

bool JsonEquals(const JsonValue& a, const JsonValue& b, float epsilon) {
    if (a.type != b.type) {
        if (a.IsNumber() && b.IsNumber()) {
            return std::abs(a.AsNumber() - b.AsNumber()) <= static_cast<double>(epsilon);
        }
        return false;
    }
    switch (a.type) {
        case JsonValue::Type::Null:
            return true;
        case JsonValue::Type::Bool:
            return a.boolValue == b.boolValue;
        case JsonValue::Type::Number:
            return std::abs(a.numberValue - b.numberValue) <= static_cast<double>(epsilon);
        case JsonValue::Type::String:
            return a.stringValue == b.stringValue;
        case JsonValue::Type::Array: {
            if (a.arrayValue.size() != b.arrayValue.size()) {
                return false;
            }
            for (size_t i = 0; i < a.arrayValue.size(); ++i) {
                if (!JsonEquals(a.arrayValue[i], b.arrayValue[i], epsilon)) {
                    return false;
                }
            }
            return true;
        }
        case JsonValue::Type::Object: {
            if (a.objectValue.size() != b.objectValue.size()) {
                return false;
            }
            for (const auto& [key, entry] : a.objectValue) {
                auto it = b.objectValue.find(key);
                if (it == b.objectValue.end() || !JsonEquals(entry, it->second, epsilon)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

const JsonValue* GetPath(const JsonValue& root, const std::string& path) {
    if (path.empty()) {
        return &root;
    }
    const JsonValue* current = &root;
    for (const auto& part : SplitPath(path)) {
        if (!current || !current->IsObject()) {
            return nullptr;
        }
        current = current->Find(part);
    }
    return current;
}

bool SetPath(JsonValue& root, const std::string& path, const JsonValue& value) {
    if (path.empty()) {
        root = value;
        return true;
    }
    const auto parts = SplitPath(path);
    if (parts.empty()) {
        root = value;
        return true;
    }
    if (!root.IsObject()) {
        root = MakeObject();
    }
    JsonValue* current = &root;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!current->IsObject()) {
            *current = MakeObject();
        }
        auto it = current->objectValue.find(parts[i]);
        if (it == current->objectValue.end() || !it->second.IsObject()) {
            current->objectValue[parts[i]] = MakeObject();
        }
        current = &current->objectValue[parts[i]];
    }
    if (!current->IsObject()) {
        *current = MakeObject();
    }
    current->objectValue[parts.back()] = value;
    return true;
}

void DiffJson(const JsonValue& current, const JsonValue& baseline,
              std::unordered_map<std::string, PropertyValue>& outDiff,
              const std::string& prefix) {
    if (current.IsObject() && baseline.IsObject()) {
        std::unordered_set<std::string> keys;
        for (const auto& [key, _] : current.objectValue) {
            keys.insert(key);
        }
        for (const auto& [key, _] : baseline.objectValue) {
            keys.insert(key);
        }
        for (const auto& key : keys) {
            const JsonValue* live = current.Find(key);
            const JsonValue* base = baseline.Find(key);
            std::string childPath = prefix.empty() ? key : (prefix + "." + key);
            if (live && base) {
                DiffJson(*live, *base, outDiff, childPath);
            } else if (live && !base) {
                outDiff[childPath] = JsonToProperty(*live);
            }
        }
        return;
    }
    if (!JsonEquals(current, baseline)) {
        std::string key = prefix.empty() ? std::string("value") : prefix;
        outDiff[key] = JsonToProperty(current);
    }
}

void MergeOverrides(JsonValue& data, const std::unordered_map<std::string, PropertyValue>& properties) {
    if (!data.IsObject()) {
        data = MakeObject();
    }
    for (const auto& [path, value] : properties) {
        SetPath(data, path, PropertyToJson(value));
    }
}

bool CaptureComponent(Entity entity, const std::string& type, PrefabComponentEntry& out) {
    if (!entity) {
        return false;
    }
    out.type = type;
    out.enabled = true;
    if (type == "MeshRenderer" && entity.HasComponent<MeshRendererComponent>()) {
        const auto& mesh = entity.GetComponent<MeshRendererComponent>();
        if (IsRuntimeGeneratedVisual(mesh)) {
            return false;
        }
        CaptureMeshRenderer(mesh, out.data);
        return true;
    }
    if (type == "MeshCollider" && entity.HasComponent<MeshColliderComponent>()) {
        const auto& collider = entity.GetComponent<MeshColliderComponent>();
        out.enabled = collider.enabled;
        CaptureMeshCollider(collider, out.data);
        return true;
    }
    if (type == "PhysicsShape" && entity.HasComponent<PhysicsShapeComponent>()) {
        CapturePhysicsShape(entity.GetComponent<PhysicsShapeComponent>(), out.data);
        return true;
    }
    if (type == "RigidBody" && entity.HasComponent<RigidBodyComponent>()) {
        CaptureRigidBody(entity.GetComponent<RigidBodyComponent>(), out.data);
        return true;
    }
    if (type == "Material" && entity.HasComponent<MaterialComponent>()) {
        CaptureMaterial(entity.GetComponent<MaterialComponent>(), out.data);
        return true;
    }
    if (type == "Light" && entity.HasComponent<LightComponent>()) {
        const auto& light = entity.GetComponent<LightComponent>();
        out.enabled = light.enabled;
        CaptureLight(light, out.data);
        return true;
    }
    if (type == "Camera" && entity.HasComponent<CameraComponent>()) {
        CaptureCamera(entity.GetComponent<CameraComponent>(), out.data);
        return true;
    }
    if (type == "LuaScript" && entity.HasComponent<LuaScriptComponent>()) {
        const auto& lua = entity.GetComponent<LuaScriptComponent>();
        out.enabled = lua.enabled;
        CaptureLuaScript(lua, out.data);
        return true;
    }
    if (type == "ScriptComponent" && entity.HasComponent<ScriptComponent>()) {
        CaptureScriptComponent(entity.GetComponent<ScriptComponent>(), out.data);
        return true;
    }
    if (type == "AudioSource" && entity.HasComponent<AudioSourceComponent>()) {
        const auto& audio = entity.GetComponent<AudioSourceComponent>();
        out.enabled = audio.enabled;
        CaptureAudioSource(audio, out.data);
        return true;
    }
    if (type == "Character" && entity.HasComponent<CharacterComponent>()) {
        CaptureCharacter(entity.GetComponent<CharacterComponent>(), out.data);
        return true;
    }
    return false;
}

bool CaptureFromEntity(Entity entity, std::vector<PrefabComponentEntry>& out) {
    out.clear();
    if (!entity) {
        return false;
    }
    for (const auto& type : SupportedTypes()) {
        PrefabComponentEntry entry;
        if (CaptureComponent(entity, type, entry)) {
            out.push_back(std::move(entry));
        }
    }
    return true;
}

bool ApplyComponentJson(Entity entity, const std::string& type, const JsonValue& data, bool enabled) {
    if (!entity) {
        return false;
    }
    if (type == "MeshRenderer") {
        if (!enabled) {
            if (entity.HasComponent<MeshRendererComponent>()) {
                entity.RemoveComponent<MeshRendererComponent>();
            }
            return true;
        }
        auto& mesh = entity.HasComponent<MeshRendererComponent>()
            ? entity.GetComponent<MeshRendererComponent>()
            : entity.AddComponent<MeshRendererComponent>();
        ApplyMeshRenderer(mesh, data);
        return true;
    }
    if (type == "MeshCollider") {
        auto& collider = entity.HasComponent<MeshColliderComponent>()
            ? entity.GetComponent<MeshColliderComponent>()
            : entity.AddComponent<MeshColliderComponent>();
        ApplyMeshCollider(collider, data);
        collider.enabled = enabled;
        if (data.IsObject()) {
            if (const JsonValue* enabledField = data.Find("enabled")) {
                collider.enabled = ParseBoolLike(*enabledField, enabled);
            }
        }
        return true;
    }
    if (type == "PhysicsShape") {
        if (!enabled) {
            return true;
        }
        auto& shape = entity.HasComponent<PhysicsShapeComponent>()
            ? entity.GetComponent<PhysicsShapeComponent>()
            : entity.AddComponent<PhysicsShapeComponent>();
        ApplyPhysicsShape(shape, data);
        return true;
    }
    if (type == "RigidBody") {
        if (!enabled) {
            return true;
        }
        auto& body = entity.HasComponent<RigidBodyComponent>()
            ? entity.GetComponent<RigidBodyComponent>()
            : entity.AddComponent<RigidBodyComponent>();
        ApplyRigidBody(body, data);
        return true;
    }
    if (type == "Material") {
        if (!enabled) {
            return true;
        }
        auto& material = entity.HasComponent<MaterialComponent>()
            ? entity.GetComponent<MaterialComponent>()
            : entity.AddComponent<MaterialComponent>();
        ApplyMaterial(material, data);
        return true;
    }
    if (type == "Light") {
        if (!enabled) {
            return true;
        }
        auto& light = entity.HasComponent<LightComponent>()
            ? entity.GetComponent<LightComponent>()
            : entity.AddComponent<LightComponent>();
        ApplyLight(light, data);
#ifndef HEPHAESTUS_GAME_RUNTIME
        if (entity.HasComponent<EditorIconComponent>()) {
            entity.GetComponent<EditorIconComponent>() = EditorIconComponent::ForLight(light.type);
        } else {
            entity.AddComponent<EditorIconComponent>(EditorIconComponent::ForLight(light.type));
        }
#endif
        return true;
    }
    if (type == "Camera") {
        if (!enabled) {
            return true;
        }
        auto& camera = entity.HasComponent<CameraComponent>()
            ? entity.GetComponent<CameraComponent>()
            : entity.AddComponent<CameraComponent>();
        ApplyCamera(camera, data);
        return true;
    }
    if (type == "LuaScript") {
        auto& lua = entity.HasComponent<LuaScriptComponent>()
            ? entity.GetComponent<LuaScriptComponent>()
            : entity.AddComponent<LuaScriptComponent>();
        ApplyLuaScript(lua, data, enabled);
        return true;
    }
    if (type == "ScriptComponent") {
        auto& script = entity.HasComponent<ScriptComponent>()
            ? entity.GetComponent<ScriptComponent>()
            : entity.AddComponent<ScriptComponent>();
        ApplyScriptComponent(script, data);
        return true;
    }
    if (type == "AudioSource") {
        auto& audio = entity.HasComponent<AudioSourceComponent>()
            ? entity.GetComponent<AudioSourceComponent>()
            : entity.AddComponent<AudioSourceComponent>();
        ApplyAudioSource(audio, data, enabled);
        return true;
    }
    if (type == "Character") {
        auto& character = entity.HasComponent<CharacterComponent>()
            ? entity.GetComponent<CharacterComponent>()
            : entity.AddComponent<CharacterComponent>();
        ApplyCharacter(character, data);
        return true;
    }
    return false;
}

bool ApplyEntry(Entity entity, const PrefabComponentEntry& entry) {
    return ApplyComponentJson(entity, entry.type, entry.data, entry.enabled);
}

bool HasComponent(Entity entity, const std::string& type) {
    if (!entity) {
        return false;
    }
    if (type == "MeshRenderer") return entity.HasComponent<MeshRendererComponent>();
    if (type == "MeshCollider") return entity.HasComponent<MeshColliderComponent>();
    if (type == "PhysicsShape") return entity.HasComponent<PhysicsShapeComponent>();
    if (type == "RigidBody") return entity.HasComponent<RigidBodyComponent>();
    if (type == "Material") return entity.HasComponent<MaterialComponent>();
    if (type == "Light") return entity.HasComponent<LightComponent>();
    if (type == "Camera") return entity.HasComponent<CameraComponent>();
    if (type == "LuaScript") return entity.HasComponent<LuaScriptComponent>();
    if (type == "ScriptComponent") return entity.HasComponent<ScriptComponent>();
    if (type == "AudioSource") return entity.HasComponent<AudioSourceComponent>();
    if (type == "Character") return entity.HasComponent<CharacterComponent>();
    return false;
}

bool AddComponent(Entity entity, const std::string& type) {
    if (!entity || HasComponent(entity, type)) {
        return false;
    }
    PrefabComponentEntry entry;
    entry.type = type;
    entry.enabled = true;
    entry.data = MakeObject();
    return ApplyEntry(entity, entry);
}

bool RemoveComponent(Entity entity, const std::string& type) {
    if (!entity || !HasComponent(entity, type)) {
        return false;
    }
    if (type == "MeshRenderer") entity.RemoveComponent<MeshRendererComponent>();
    else if (type == "MeshCollider") entity.RemoveComponent<MeshColliderComponent>();
    else if (type == "PhysicsShape") entity.RemoveComponent<PhysicsShapeComponent>();
    else if (type == "RigidBody") entity.RemoveComponent<RigidBodyComponent>();
    else if (type == "Material") entity.RemoveComponent<MaterialComponent>();
    else if (type == "Light") entity.RemoveComponent<LightComponent>();
    else if (type == "Camera") entity.RemoveComponent<CameraComponent>();
    else if (type == "LuaScript") entity.RemoveComponent<LuaScriptComponent>();
    else if (type == "ScriptComponent") entity.RemoveComponent<ScriptComponent>();
    else if (type == "AudioSource") entity.RemoveComponent<AudioSourceComponent>();
    else if (type == "Character") entity.RemoveComponent<CharacterComponent>();
    else return false;
    return true;
}

const std::vector<std::string>& SupportedTypes() {
    static const std::vector<std::string> kTypes = {
        "MeshRenderer", "MeshCollider", "PhysicsShape", "RigidBody",
        "Material", "Light", "Camera", "LuaScript", "ScriptComponent", "AudioSource", "Character"
    };
    return kTypes;
}

bool TransformsEqual(const TransformComponent& a, const TransformComponent& b, float epsilon) {
    auto veq = [epsilon](const Vec3& lhs, const Vec3& rhs) {
        return std::abs(lhs.x - rhs.x) <= epsilon &&
               std::abs(lhs.y - rhs.y) <= epsilon &&
               std::abs(lhs.z - rhs.z) <= epsilon;
    };
    return veq(a.position, b.position) && veq(a.rotation, b.rotation) && veq(a.scale, b.scale);
}

void CopyLocalTransform(TransformComponent& dst, const TransformComponent& src) {
    dst.position = src.position;
    dst.rotation = src.rotation;
    dst.scale = src.scale;
}

std::string LightTypeToString(LightType type) {
    switch (type) {
        case LightType::Directional: return "Directional";
        case LightType::Spot: return "Spot";
        default: return "Point";
    }
}

LightType ParseLightType(const std::string& str) {
    if (str == "Directional") return LightType::Directional;
    if (str == "Spot") return LightType::Spot;
    return LightType::Point;
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

PhysicsShapeType ParsePhysicsShapeType(const std::string& str) {
    if (str == "Sphere") return PhysicsShapeType::Sphere;
    if (str == "Capsule") return PhysicsShapeType::Capsule;
    if (str == "Cylinder") return PhysicsShapeType::Cylinder;
    if (str == "Mesh") return PhysicsShapeType::Mesh;
    return PhysicsShapeType::Box;
}

std::string CameraProjectionToString(CameraProjection projection) {
    return projection == CameraProjection::Orthographic ? "Orthographic" : "Perspective";
}

CameraProjection ParseCameraProjection(const std::string& str) {
    return str == "Orthographic" ? CameraProjection::Orthographic : CameraProjection::Perspective;
}

bool IsNodeOverrideEmpty(const PrefabNodeOverride& nodeOverride) {
    if (nodeOverride.hasTransform) {
        return false;
    }
    if (!nodeOverride.scriptVars.empty()) {
        return false;
    }
    for (const auto& [_, componentOverride] : nodeOverride.componentOverrides) {
        if (!componentOverride.properties.empty()) {
            return false;
        }
    }
    return true;
}

void PruneEmptyOverrides(PrefabInstanceComponent& instance) {
    for (auto nodeIt = instance.overrides.begin(); nodeIt != instance.overrides.end();) {
        auto& nodeOverride = nodeIt->second;
        for (auto compIt = nodeOverride.componentOverrides.begin();
             compIt != nodeOverride.componentOverrides.end();) {
            if (compIt->second.properties.empty()) {
                compIt = nodeOverride.componentOverrides.erase(compIt);
            } else {
                ++compIt;
            }
        }
        if (IsNodeOverrideEmpty(nodeOverride)) {
            nodeIt = instance.overrides.erase(nodeIt);
        } else {
            ++nodeIt;
        }
    }
}

} // namespace PrefabComponentIO
