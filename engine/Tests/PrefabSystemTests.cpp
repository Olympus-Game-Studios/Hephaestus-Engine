#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Scene/PrefabAsset.h"
#include "../Scene/PrefabSerializer.h"
#include "../Scene/PrefabInstantiator.h"
#include "../Scene/PrefabOverrideSystem.h"
#include "../Scene/PrefabComponentIO.h"
#include "../Scene/SceneSerializer.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

bool Near(float a, float b, float epsilon = 0.001f) {
    return std::abs(a - b) <= epsilon;
}

Entity FindByTag(Scene& scene, const std::string& tag) {
    auto view = scene.GetEntitiesWith<TagComponent>();
    for (auto handle : view) {
        Entity entity(handle, &scene);
        if (entity && entity.GetComponent<TagComponent>().tag == tag) {
            return entity;
        }
    }
    return {};
}

Entity FindChildByTag(Scene& scene, Entity root, const std::string& tag) {
    if (!root || !root.HasComponent<TransformComponent>()) {
        return {};
    }
    for (entt::entity childHandle : root.GetComponent<TransformComponent>().children) {
        Entity child(childHandle, &scene);
        if (child && child.HasComponent<TagComponent>() &&
            child.GetComponent<TagComponent>().tag == tag) {
            return child;
        }
        Entity nested = FindChildByTag(scene, child, tag);
        if (nested) {
            return nested;
        }
    }
    return {};
}

bool BuildLampHierarchy(Scene& scene, Entity& outRoot, Entity& outShade) {
    outRoot = scene.CreateEntity("Lamp");
    if (!outRoot) {
        return false;
    }
    outRoot.GetComponent<TransformComponent>().position = Vec3(10.0f, 0.0f, 20.0f);
    outRoot.AddComponent<LightComponent>(LightComponent::CreatePoint(Vec3(1.0f, 0.9f, 0.7f), 1.0f, 12.0f));

    outShade = scene.CreateEntity("Shade");
    if (!outShade) {
        return false;
    }
    scene.SetParent(outShade, outRoot);
    outShade.GetComponent<TransformComponent>().position = Vec3(0.0f, 1.5f, 0.0f);
    auto& mesh = outShade.AddComponent<MeshRendererComponent>();
    mesh.meshAssetPath = "assets/meshes/cube.mesh";
    mesh.appliedMaterialPaths = { "assets/materials/Lamp.hematerial" };
    mesh.castShadows = false;
    mesh.drawOverWorld = true;
    auto& material = outShade.AddComponent<MaterialComponent>();
    material.albedo = Vec3(0.8f, 0.6f, 0.2f);
    auto& collider = outShade.AddComponent<MeshColliderComponent>();
    collider.enabled = true;
    auto& shape = outShade.AddComponent<PhysicsShapeComponent>();
    shape.type = PhysicsShapeType::Box;
    shape.extents = Vec3(0.4f, 0.2f, 0.4f);
    auto& body = outShade.AddComponent<RigidBodyComponent>();
    body.mass = 2.5f;
    body.SetMotion(RigidBodyMotion::Dynamic);
    auto& lua = outShade.AddComponent<LuaScriptComponent>();
    lua.scriptPath = "assets/scripts/Lamp.lua";
    lua.variables["glow"] = PropertyValue(1.0);
    lua.authoredVariables.insert("glow");
    lua.authoredVariablesInitialized = true;
    return true;
}

bool SavePrefabFromHierarchy(Scene& scene, Entity root, const fs::path& path, PrefabAsset& outAsset) {
    if (!PrefabOverrideSystem::CaptureHierarchy(scene, root, outAsset)) {
        return false;
    }
    std::string error;
    if (!PrefabSerializer::Serialize(outAsset, path, error)) {
        std::cerr << error << "\n";
        return false;
    }
    PrefabOverrideSystem::InvalidateAssetCache(path.generic_string());
    if (!PrefabSerializer::Deserialize(outAsset, path, error)) {
        std::cerr << error << "\n";
        return false;
    }
    return true;
}

Entity InstantiateAt(Scene& scene, const PrefabAsset& asset, const std::string& path, const Vec3& position) {
    PrefabInstanceComponent data;
    data.prefabPath = path;
    data.prefabGuid = asset.guid;
    data.rootNodeId = asset.rootNodeId;
    data.hasRootTransform = true;
    data.rootTransform.position = position;
    data.rootTransform.scale = Vec3(1, 1, 1);

    PrefabInstantiateOptions options;
    options.prefabPath = path;
    options.instanceData = &data;
    return PrefabInstantiator::Instantiate(scene, asset, options);
}

bool TestPrefabSaveLoad(const fs::path& outDir) {
    Scene scratch("Scratch");
    Entity root, shade;
    if (!BuildLampHierarchy(scratch, root, shade)) {
        return Check(false, "failed to build lamp hierarchy");
    }

    PrefabAsset asset;
    const fs::path prefabPath = outDir / "Lamp.hpprefab";
    if (!SavePrefabFromHierarchy(scratch, root, prefabPath, asset)) {
        return Check(false, "failed to save/load lamp prefab");
    }

    bool ok = true;
    ok &= Check(asset.nodes.size() >= 2, "prefab has root and child");
    ok &= Check(!asset.rootNodeId.empty(), "prefab has rootNodeId");

    const PrefabNode* shadeNode = nullptr;
    for (const auto& node : asset.nodes) {
        if (node.name == "Shade") {
            shadeNode = &node;
            break;
        }
    }
    ok &= Check(shadeNode != nullptr, "prefab captured Shade node");
    if (shadeNode) {
        bool hasMesh = false, hasLua = false, hasBody = false, hasShape = false, hasMat = false, hasCol = false;
        for (const auto& entry : shadeNode->components) {
            hasMesh |= entry.type == "MeshRenderer";
            hasLua |= entry.type == "LuaScript";
            hasBody |= entry.type == "RigidBody";
            hasShape |= entry.type == "PhysicsShape";
            hasMat |= entry.type == "Material";
            hasCol |= entry.type == "MeshCollider";
        }
        ok &= Check(hasMesh && hasLua && hasBody && hasShape && hasMat && hasCol,
                    "Shade captured mesh/material/physics/lua");
        ok &= Check(Near(shadeNode->localTransform.position.y, 1.5f), "child local transform captured");
    }

    const PrefabNode* rootNode = PrefabOverrideSystem::FindAssetNode(asset, asset.rootNodeId);
    ok &= Check(rootNode != nullptr, "root node present");
    if (rootNode) {
        bool hasLight = false;
        for (const auto& entry : rootNode->components) {
            hasLight |= entry.type == "Light";
        }
        ok &= Check(hasLight, "root captured Light");
        ok &= Check(Near(rootNode->localTransform.position.x, 0.0f), "root transform identity in prefab");
    }
    return ok;
}

bool TestSceneRoundtripAndTransforms(const fs::path& outDir) {
    Scene scratch("Scratch");
    Entity root, shade;
    if (!BuildLampHierarchy(scratch, root, shade)) {
        return Check(false, "failed to build lamp hierarchy");
    }
    PrefabAsset asset;
    const fs::path prefabPath = outDir / "LampScene.hpprefab";
    if (!SavePrefabFromHierarchy(scratch, root, prefabPath, asset)) {
        return Check(false, "failed to save prefab for scene test");
    }

    Scene scene("PrefabScene");
    Entity instance = InstantiateAt(scene, asset, prefabPath.generic_string(), Vec3(100.0f, 0.0f, 50.0f));
    if (!Check(static_cast<bool>(instance), "instantiate lamp")) {
        return false;
    }
    Entity liveShade = FindChildByTag(scene, instance, "Shade");
    if (!Check(static_cast<bool>(liveShade), "find shade child")) {
        return false;
    }
    liveShade.GetComponent<TransformComponent>().position = Vec3(0.0f, 3.5f, 0.0f);
    liveShade.GetComponent<LuaScriptComponent>().variables["glow"] = PropertyValue(4.0);
    instance.GetComponent<LightComponent>().intensity = 2500.0f;
    PrefabOverrideSystem::SyncEntityOverrides(scene, instance);
    PrefabOverrideSystem::SyncEntityOverrides(scene, liveShade);

    const auto& inst = instance.GetComponent<PrefabInstanceComponent>();
    bool ok = true;
    ok &= Check(Near(inst.rootTransform.position.x, 100.0f), "root placement stored on instance");
    ok &= Check(!PrefabOverrideSystem::IsPropertyOverridden(inst, inst.rootNodeId, "Transform", "position"),
                "root placement is not a transform override");
    ok &= Check(PrefabOverrideSystem::IsPropertyOverridden(inst, liveShade.GetComponent<PrefabNodeComponent>().nodeId,
                                                          "Transform", "position"),
                "child transform override recorded");
    ok &= Check(PrefabOverrideSystem::IsPropertyOverridden(inst, inst.rootNodeId, "Light", "intensity"),
                "light intensity override recorded");

    Entity plain = scene.CreateEntity("PlainNoShadow");
    plain.AddComponent<MeshRendererComponent>().castShadows = false;
    plain.GetComponent<MeshRendererComponent>().drawOverWorld = true;
    Entity plainDefault = scene.CreateEntity("PlainShadow");
    plainDefault.AddComponent<MeshRendererComponent>();

    PrefabOverrideSystem::SyncAllInstances(scene);
    const fs::path scenePath = outDir / "LampScene.hpscene";
    SceneSerializer serializer(scene);
    std::string error;
    if (!serializer.Serialize(scenePath, error)) {
        std::cerr << error << "\n";
        return Check(false, "serialize scene with prefab instance");
    }

    std::ifstream in(scenePath);
    std::string sceneText((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ok &= Check(sceneText.find("\"prefabInstances\"") != std::string::npos, "scene stores prefabInstances");
    ok &= Check(sceneText.find("\"rootTransform\"") != std::string::npos, "scene stores rootTransform");
    ok &= Check(sceneText.find("\"name\": \"Shade\"") == std::string::npos,
                "prefab children are not serialized as ordinary entities");

    Scene loaded("Loaded");
    if (!SceneSerializer::Deserialize(loaded, scenePath, error)) {
        std::cerr << error << "\n";
        return Check(false, "deserialize scene with prefab instance");
    }

    Entity loadedRoot = FindByTag(loaded, "Lamp");
    ok &= Check(static_cast<bool>(loadedRoot), "loaded instance root");
    if (loadedRoot) {
        ok &= Check(loadedRoot.HasComponent<PrefabInstanceComponent>(), "loaded root keeps instance component");
        ok &= Check(Near(loadedRoot.GetComponent<TransformComponent>().position.x, 100.0f),
                    "root placement survived reload");
        ok &= Check(Near(loadedRoot.GetComponent<LightComponent>().intensity, 2500.0f),
                    "light override survived reload");
        Entity loadedShade = FindChildByTag(loaded, loadedRoot, "Shade");
        ok &= Check(static_cast<bool>(loadedShade), "loaded shade child");
        if (loadedShade) {
            ok &= Check(Near(loadedShade.GetComponent<TransformComponent>().position.y, 3.5f),
                        "child transform override survived reload");
            ok &= Check(!loadedShade.GetComponent<MeshRendererComponent>().castShadows,
                        "prefab mesh castShadows off survived reload");
            ok &= Check(loadedShade.GetComponent<MeshRendererComponent>().drawOverWorld,
                        "prefab mesh drawOverWorld survived reload");
            auto glow = loadedShade.GetComponent<LuaScriptComponent>().variables.find("glow");
            ok &= Check(glow != loadedShade.GetComponent<LuaScriptComponent>().variables.end() &&
                        glow->second.IsNumber() && Near(static_cast<float>(*glow->second.AsNumber()), 4.0f),
                        "lua override survived reload");
        }
    }
    Entity loadedPlain = FindByTag(loaded, "PlainNoShadow");
    Entity loadedPlainDefault = FindByTag(loaded, "PlainShadow");
    ok &= Check(loadedPlain && !loadedPlain.GetComponent<MeshRendererComponent>().castShadows,
                "scene mesh castShadows off survived reload");
    ok &= Check(loadedPlainDefault && loadedPlainDefault.GetComponent<MeshRendererComponent>().castShadows,
                "scene mesh castShadows defaults on");
    ok &= Check(loadedPlain && loadedPlain.GetComponent<MeshRendererComponent>().drawOverWorld,
                "scene mesh drawOverWorld survived reload");
    ok &= Check(loadedPlainDefault && !loadedPlainDefault.GetComponent<MeshRendererComponent>().drawOverWorld,
                "scene mesh drawOverWorld defaults off");
    return ok;
}

bool TestRefreshKeepsOverride(const fs::path& outDir) {
    Scene scratch("Scratch");
    Entity root, shade;
    if (!BuildLampHierarchy(scratch, root, shade)) {
        return Check(false, "failed to build lamp hierarchy");
    }
    PrefabAsset asset;
    const fs::path prefabPath = outDir / "LampRefresh.hpprefab";
    if (!SavePrefabFromHierarchy(scratch, root, prefabPath, asset)) {
        return Check(false, "failed to save prefab for refresh test");
    }

    Scene scene("Refresh");
    Entity instance = InstantiateAt(scene, asset, prefabPath.generic_string(), Vec3(1, 2, 3));
    Entity liveShade = FindChildByTag(scene, instance, "Shade");
    if (!instance || !liveShade) {
        return Check(false, "instantiate for refresh");
    }
    liveShade.GetComponent<TransformComponent>().position = Vec3(0.0f, 9.0f, 0.0f);
    PrefabOverrideSystem::SyncEntityOverrides(scene, liveShade);

    PrefabNode* assetShade = nullptr;
    for (auto& node : asset.nodes) {
        if (node.name == "Shade") {
            assetShade = &node;
            break;
        }
    }
    if (!assetShade) {
        return Check(false, "find shade in asset");
    }
    assetShade->localTransform.scale = Vec3(2.0f, 2.0f, 2.0f);
    std::string error;
    if (!PrefabSerializer::Serialize(asset, prefabPath, error)) {
        std::cerr << error << "\n";
        return Check(false, "save modified prefab");
    }
    PrefabOverrideSystem::InvalidateAssetCache(prefabPath.generic_string());

    Entity refreshed = PrefabOverrideSystem::RefreshInstance(scene, instance, &error);
    if (!refreshed) {
        std::cerr << error << "\n";
        return Check(false, "refresh instance");
    }
    Entity refreshedShade = FindChildByTag(scene, refreshed, "Shade");
    bool ok = true;
    ok &= Check(static_cast<bool>(refreshedShade), "shade exists after refresh");
    if (refreshedShade) {
        ok &= Check(Near(refreshedShade.GetComponent<TransformComponent>().position.y, 9.0f),
                    "child override remains after refresh");
        ok &= Check(Near(refreshedShade.GetComponent<TransformComponent>().scale.x, 2.0f),
                    "new prefab default scale appears after refresh");
    }
    ok &= Check(Near(refreshed.GetComponent<TransformComponent>().position.x, 1.0f),
                "instance root placement preserved on refresh");
    return ok;
}

bool TestApplyAndRevert(const fs::path& outDir) {
    Scene scratch("Scratch");
    Entity root, shade;
    if (!BuildLampHierarchy(scratch, root, shade)) {
        return Check(false, "failed to build lamp hierarchy");
    }
    PrefabAsset asset;
    const fs::path prefabPath = outDir / "LampApply.hpprefab";
    if (!SavePrefabFromHierarchy(scratch, root, prefabPath, asset)) {
        return Check(false, "failed to save prefab for apply test");
    }

    Scene scene("Apply");
    Entity instanceA = InstantiateAt(scene, asset, prefabPath.generic_string(), Vec3(1, 0, 0));
    Entity instanceB = InstantiateAt(scene, asset, prefabPath.generic_string(), Vec3(2, 0, 0));
    if (!instanceA || !instanceB) {
        return Check(false, "instantiate A and B");
    }

    instanceA.GetComponent<LightComponent>().intensity = 2500.0f;
    PrefabOverrideSystem::SyncEntityOverrides(scene, instanceA);
    bool ok = true;
    ok &= Check(Near(instanceB.GetComponent<LightComponent>().intensity, 1.0f), "B still uses default");

    std::string error;
    if (!PrefabOverrideSystem::ApplyPropertyToPrefab(scene, instanceA, "Light", "intensity", error)) {
        std::cerr << error << "\n";
        return Check(false, "apply intensity to prefab");
    }

    Entity afterA = FindByTag(scene, "Lamp");
    Entity afterB;
    auto view = scene.GetEntitiesWith<PrefabInstanceComponent>();
    int count = 0;
    for (auto handle : view) {
        Entity entity(handle, &scene);
        ++count;
        if (Near(entity.GetComponent<TransformComponent>().position.x, 2.0f)) {
            afterB = entity;
        }
        if (Near(entity.GetComponent<TransformComponent>().position.x, 1.0f)) {
            afterA = entity;
        }
    }
    ok &= Check(count == 2, "two instances remain after apply");
    if (afterA) {
        ok &= Check(Near(afterA.GetComponent<LightComponent>().intensity, 2500.0f), "A has new default");
        const auto& instA = afterA.GetComponent<PrefabInstanceComponent>();
        ok &= Check(!PrefabOverrideSystem::IsPropertyOverridden(instA, instA.rootNodeId, "Light", "intensity"),
                    "A no longer stores intensity override");
    }
    if (afterB) {
        ok &= Check(Near(afterB.GetComponent<LightComponent>().intensity, 2500.0f),
                    "B received the new prefab default");
    }

    if (!afterA) {
        return ok;
    }
    afterA.GetComponent<LightComponent>().intensity = 10.0f;
    PrefabOverrideSystem::SyncEntityOverrides(scene, afterA);
    ok &= Check(PrefabOverrideSystem::RevertProperty(scene, afterA, "Light", "intensity"), "revert intensity");
    ok &= Check(Near(afterA.GetComponent<LightComponent>().intensity, 2500.0f), "revert restores prefab default");
    ok &= Check(!PrefabOverrideSystem::IsPropertyOverridden(afterA.GetComponent<PrefabInstanceComponent>(),
                                                           afterA.GetComponent<PrefabInstanceComponent>().rootNodeId,
                                                           "Light", "intensity"),
                "revert removes override");
    return ok;
}

bool TestLuaScriptApplyAndRevert(const fs::path& outDir) {
    Scene scratch("Scratch");
    Entity root, shade;
    if (!BuildLampHierarchy(scratch, root, shade)) {
        return Check(false, "failed to build hierarchy for Lua apply test");
    }
    PrefabAsset asset;
    const fs::path prefabPath = outDir / "LampLuaApply.hpprefab";
    if (!SavePrefabFromHierarchy(scratch, root, prefabPath, asset)) {
        return Check(false, "failed to save prefab for Lua apply test");
    }

    Scene scene("LuaApply");
    Entity instanceA = InstantiateAt(scene, asset, prefabPath.generic_string(), Vec3(1, 0, 0));
    Entity instanceB = InstantiateAt(scene, asset, prefabPath.generic_string(), Vec3(2, 0, 0));
    Entity shadeA = FindChildByTag(scene, instanceA, "Shade");
    if (!instanceA || !instanceB || !shadeA) {
        return Check(false, "instantiate Lua apply fixtures");
    }

    shadeA.GetComponent<LuaScriptComponent>().variables["glow"] = PropertyValue(4.0);
    PrefabOverrideSystem::SyncEntityOverrides(scene, shadeA);
    const std::string nodeId = shadeA.GetComponent<PrefabNodeComponent>().nodeId;
    bool ok = true;
    ok &= Check(PrefabOverrideSystem::IsPropertyOverridden(
                    instanceA.GetComponent<PrefabInstanceComponent>(), nodeId, "LuaScript", "glow"),
                "Lua variable override recorded");

    std::string error;
    if (!PrefabOverrideSystem::ApplyPropertyToPrefab(
            scene, shadeA, "LuaScript", "glow", error)) {
        std::cerr << error << "\n";
        return Check(false, "apply Lua variable to prefab");
    }

    Entity refreshedA;
    Entity refreshedB;
    auto instances = scene.GetEntitiesWith<PrefabInstanceComponent>();
    for (entt::entity handle : instances) {
        Entity entity(handle, &scene);
        if (Near(entity.GetComponent<TransformComponent>().position.x, 1.0f)) refreshedA = entity;
        if (Near(entity.GetComponent<TransformComponent>().position.x, 2.0f)) refreshedB = entity;
    }
    Entity refreshedShadeA = FindChildByTag(scene, refreshedA, "Shade");
    Entity refreshedShadeB = FindChildByTag(scene, refreshedB, "Shade");
    auto hasGlow = [](Entity entity, double expected) {
        if (!entity || !entity.HasComponent<LuaScriptComponent>()) return false;
        const auto& variables = entity.GetComponent<LuaScriptComponent>().variables;
        const auto value = variables.find("glow");
        return value != variables.end() && value->second.IsNumber() &&
            *value->second.AsNumber() == expected;
    };
    ok &= Check(hasGlow(refreshedShadeA, 4.0), "source instance receives applied Lua value");
    ok &= Check(hasGlow(refreshedShadeB, 4.0), "other instance receives applied Lua value");
    if (refreshedA && refreshedShadeA) {
        ok &= Check(!PrefabOverrideSystem::IsPropertyOverridden(
                        refreshedA.GetComponent<PrefabInstanceComponent>(),
                        refreshedShadeA.GetComponent<PrefabNodeComponent>().nodeId,
                        "LuaScript", "glow"),
                    "applied Lua override cleared");
    }

    PrefabAsset appliedAsset;
    if (!PrefabSerializer::Deserialize(appliedAsset, prefabPath, error)) {
        return Check(false, "reload applied Lua prefab");
    }
    bool authored = false;
    for (const PrefabNode& node : appliedAsset.nodes) {
        if (node.name != "Shade") continue;
        for (const PrefabComponentEntry& component : node.components) {
            if (component.type != "LuaScript") continue;
            if (const JsonValue* values = component.data.Find("authoredVariables")) {
                for (const JsonValue& value : values->AsArray()) {
                    authored = authored || (value.IsString() && value.AsString() == "glow");
                }
            }
        }
    }
    ok &= Check(authored, "applied prefab value is authored relative to script default");

    if (refreshedShadeA) {
        refreshedShadeA.GetComponent<LuaScriptComponent>().variables["glow"] = PropertyValue(9.0);
        PrefabOverrideSystem::SyncEntityOverrides(scene, refreshedShadeA);
        ok &= Check(PrefabOverrideSystem::RevertProperty(
                        scene, refreshedShadeA, "LuaScript", "glow"),
                    "revert Lua variable");
        ok &= Check(hasGlow(refreshedShadeA, 4.0), "Lua revert restores prefab value");
    }
    return ok;
}

bool TestLuaAuthoredSceneRoundtrip(const fs::path&) {
    Scene scene("LuaAuthored");
    Entity entity = scene.CreateEntity("Controller");
    auto& script = entity.AddComponent<LuaScriptComponent>();
    script.scriptPath = "assets/scripts/Controller.lua";
    script.variables["speed"] = PropertyValue(7.0);
    script.authoredVariables.insert("speed");
    script.authoredVariablesInitialized = true;

    const std::string snapshot = SceneSerializer(scene).SerializeToString();
    Scene loaded("LoadedLuaAuthored");
    bool ok = true;
    ok &= Check(SceneSerializer::DeserializeFromString(loaded, snapshot),
                "deserialize authored Lua scene snapshot");
    Entity loadedEntity = FindByTag(loaded, "Controller");
    ok &= Check(static_cast<bool>(loadedEntity), "authored Lua entity survives snapshot");
    if (loadedEntity && loadedEntity.HasComponent<LuaScriptComponent>()) {
        const auto& loadedScript = loadedEntity.GetComponent<LuaScriptComponent>();
        ok &= Check(loadedScript.authoredVariablesInitialized,
                    "authored metadata presence survives snapshot");
        ok &= Check(loadedScript.authoredVariables.count("speed") == 1,
                    "authored Lua key survives snapshot");
        const auto speed = loadedScript.variables.find("speed");
        ok &= Check(speed != loadedScript.variables.end() && speed->second.IsNumber() &&
                        *speed->second.AsNumber() == 7.0,
                    "authored Lua value survives snapshot");
    }
    return ok;
}

bool TestCreateFromSelectionAndUnpack(const fs::path& outDir) {
    Scene scene("Create");
    Entity root, shade;
    if (!BuildLampHierarchy(scene, root, shade)) {
        return Check(false, "failed to build hierarchy for create-from-selection");
    }

    const fs::path prefabPath = outDir / "FromSelection.hpprefab";
    Entity instance;
    std::string error;
    if (!PrefabOverrideSystem::CreateFromSelection(scene, root, prefabPath, &instance, error)) {
        std::cerr << error << "\n";
        return Check(false, "create prefab from selection");
    }

    bool ok = true;
    ok &= Check(static_cast<bool>(instance), "replacement instance created");
    ok &= Check(!FindByTag(scene, "Lamp") || FindByTag(scene, "Lamp").HasComponent<PrefabInstanceComponent>(),
                "selection replaced by prefab instance");
    if (instance) {
        ok &= Check(Near(instance.GetComponent<TransformComponent>().position.x, 10.0f),
                    "world placement preserved");
        ok &= Check(instance.HasComponent<LightComponent>(), "instance root has light");
        Entity liveShade = FindChildByTag(scene, instance, "Shade");
        ok &= Check(static_cast<bool>(liveShade), "instance has shade");
        if (liveShade) {
            ok &= Check(liveShade.HasComponent<MeshRendererComponent>(), "shade has mesh");
            ok &= Check(liveShade.HasComponent<RigidBodyComponent>(), "shade has rigid body");
            ok &= Check(liveShade.HasComponent<LuaScriptComponent>(), "shade has lua");
            liveShade.GetComponent<TransformComponent>().position = Vec3(0.0f, 4.0f, 0.0f);
            PrefabOverrideSystem::SyncEntityOverrides(scene, liveShade);
        }

        ok &= Check(PrefabOverrideSystem::UnpackInstance(scene, instance), "unpack instance");
        ok &= Check(!instance.HasComponent<PrefabInstanceComponent>(), "unpack removes instance component");
        ok &= Check(!instance.HasComponent<PrefabNodeComponent>(), "unpack removes node metadata");
        Entity unpackedShade = FindChildByTag(scene, instance, "Shade");
        ok &= Check(static_cast<bool>(unpackedShade), "unpacked hierarchy remains");
        if (unpackedShade) {
            ok &= Check(!unpackedShade.HasComponent<PrefabNodeComponent>(), "child metadata removed");
            ok &= Check(Near(unpackedShade.GetComponent<TransformComponent>().position.y, 4.0f),
                        "overridden values survive unpack");
            ok &= Check(unpackedShade.HasComponent<MeshRendererComponent>(), "components survive unpack");
        }
    }

    PrefabAsset loaded;
    if (!PrefabSerializer::Deserialize(loaded, prefabPath, error)) {
        std::cerr << error << "\n";
        return Check(false, "load created prefab");
    }
    bool foundLua = false;
    for (const auto& node : loaded.nodes) {
        for (const auto& entry : node.components) {
            foundLua |= entry.type == "LuaScript";
        }
    }
    ok &= Check(foundLua, "created prefab contains lua data");
    return ok;
}

bool TestCreatedPrefabPathsArePortable(const fs::path& outDir) {
    const fs::path project = outDir / "PortableProject";
    fs::create_directories(project / "assets" / "prefabs");
    PrefabOverrideSystem::SetProjectRoot(project);

    Scene scene("Portable");
    Entity root = scene.CreateEntity("Gun");
    Entity body = scene.CreateEntity("Body");
    scene.SetParent(body, root);
    const std::string absoluteMesh = (project / "assets" / "models" / "Gun.fbx").generic_string();
    body.AddComponent<MeshRendererComponent>().meshAssetPath = absoluteMesh;

    const fs::path prefabPath = project / "assets" / "prefabs" / "Gun.hpprefab";
    Entity instance;
    std::string error;
    bool ok = Check(PrefabOverrideSystem::CreateFromSelection(scene, root, prefabPath, &instance, error),
                    "create prefab inside a project: " + error);
    auto readText = [](const fs::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string prefabText = readText(prefabPath);
    ok &= Check(prefabText.find("\"assets/models/Gun.fbx\"") != std::string::npos &&
                    prefabText.find(project.generic_string()) == std::string::npos,
                "prefab stores mesh sources as project keys, not absolute paths");
    const auto* data = instance ? PrefabOverrideSystem::TryGetInstance(scene, instance) : nullptr;
    ok &= Check(data && PrefabOverrideSystem::ListOverrides(*data).empty(),
                "absolute live mesh path is not an override of its portable prefab key");

    const fs::path scenePath = project / "assets" / "Portable.hpscene";
    ok &= Check(SceneSerializer(scene).Serialize(scenePath, error), "save scene: " + error);
    const std::string sceneText = readText(scenePath);
    ok &= Check(sceneText.find("\"prefabPath\": \"assets/prefabs/Gun.hpprefab\"") != std::string::npos &&
                    sceneText.find(project.generic_string()) == std::string::npos,
                "scene stores the prefab instance path as a project key");

    PrefabOverrideSystem::InvalidateAssetCache();
    Scene loaded("Loaded");
    ok &= Check(SceneSerializer::Deserialize(loaded, scenePath, error), "load scene: " + error);
    Entity loadedRoot = FindByTag(loaded, "Gun");
    const auto* loadedData = loadedRoot ? PrefabOverrideSystem::TryGetInstance(loaded, loadedRoot) : nullptr;
    ok &= Check(loadedData && !loadedData->missingPrefab, "portable prefab path resolves from the project root");

    PrefabOverrideSystem::SetProjectRoot({});
    return ok;
}

bool TestMissingPrefab(const fs::path& outDir) {
    Scene scratch("Scratch");
    Entity root, shade;
    if (!BuildLampHierarchy(scratch, root, shade)) {
        return Check(false, "failed to build lamp hierarchy");
    }
    PrefabAsset asset;
    const fs::path prefabPath = outDir / "MissingSoon.hpprefab";
    if (!SavePrefabFromHierarchy(scratch, root, prefabPath, asset)) {
        return Check(false, "failed to save prefab for missing test");
    }

    Scene scene("Missing");
    InstantiateAt(scene, asset, prefabPath.generic_string(), Vec3(5, 0, 5));
    PrefabOverrideSystem::SyncAllInstances(scene);
    const fs::path scenePath = outDir / "Missing.hpscene";
    SceneSerializer serializer(scene);
    std::string error;
    if (!serializer.Serialize(scenePath, error)) {
        std::cerr << error << "\n";
        return Check(false, "serialize scene before deleting prefab");
    }

    std::error_code ec;
    fs::remove(prefabPath, ec);
    PrefabOverrideSystem::InvalidateAssetCache();

    Scene loaded("MissingLoaded");
    if (!SceneSerializer::Deserialize(loaded, scenePath, error)) {
        std::cerr << error << "\n";
        return Check(false, "scene load must not fail when prefab is missing");
    }

    bool foundMissing = false;
    auto view = loaded.GetEntitiesWith<PrefabInstanceComponent>();
    for (auto handle : view) {
        Entity entity(handle, &loaded);
        const auto& inst = entity.GetComponent<PrefabInstanceComponent>();
        foundMissing |= inst.missingPrefab;
    }
    return Check(foundMissing, "missing prefab instance is marked broken");
}

bool TestLegacyBlueprint(const fs::path& repoRoot) {
    const fs::path legacy = repoRoot / "assets/blueprints/Player.hpblueprint";
    PrefabAsset asset;
    std::string error;
    if (!PrefabSerializer::Deserialize(asset, legacy, error)) {
        std::cerr << error << "\n";
        return Check(false, "legacy Player.hpblueprint still loads");
    }
    bool ok = true;
    ok &= Check(!asset.nodes.empty(), "legacy prefab has nodes");
    bool hasPhysics = false;
    bool hasLua = false;
    for (const auto& node : asset.nodes) {
        for (const auto& entry : node.components) {
            hasPhysics |= entry.type == "PhysicsShape" || entry.type == "RigidBody";
            hasLua |= entry.type == "LuaScript";
        }
    }
    ok &= Check(hasPhysics, "legacy physics components loaded");
    ok &= Check(hasLua, "legacy script block promoted to LuaScript");
    return ok;
}

bool TestInstantiatePoseAndVars(const fs::path& outDir) {
    Scene scratch("Scratch");
    Entity root, shade;
    if (!BuildLampHierarchy(scratch, root, shade)) {
        return Check(false, "failed to build lamp hierarchy for pose/vars");
    }

    PrefabAsset asset;
    const fs::path prefabPath = outDir / "LampPoseVars.hpprefab";
    if (!SavePrefabFromHierarchy(scratch, root, prefabPath, asset)) {
        return Check(false, "failed to save lamp prefab for pose/vars");
    }

    Scene scene("PoseVars");
    PrefabInstantiateOptions options;
    options.prefabPath = prefabPath.generic_string();
    options.attachInstanceComponent = true;
    Entity instance = PrefabInstantiator::Instantiate(scene, asset, options);
    bool ok = true;
    ok &= Check(static_cast<bool>(instance), "instantiate returned root");
    if (!instance) {
        return ok;
    }

    const Vec3 posePos(4.0f, 1.5f, -8.0f);
    const Vec3 poseRotDeg(0.0f, 45.0f, 0.0f);
    ok &= Check(instance.HasComponent<TransformComponent>(), "root has transform");
    if (instance.HasComponent<TransformComponent>()) {
        auto& transform = instance.GetComponent<TransformComponent>();
        transform.position = posePos;
        transform.SetRotationDegrees(poseRotDeg);
        ok &= Check(Near(transform.position.x, 4.0f) && Near(transform.position.y, 1.5f) &&
                        Near(transform.position.z, -8.0f),
                    "root pose position applied");
        Vec3 deg = transform.GetRotationDegrees();
        ok &= Check(Near(deg.y, 45.0f), "root pose rotation degrees applied");
    }

    // Merge spawn vars onto every LuaScriptComponent in the instance.
    std::function<void(Entity)> mergeVars = [&](Entity entity) {
        if (!entity) {
            return;
        }
        if (entity.HasComponent<LuaScriptComponent>()) {
            auto& lua = entity.GetComponent<LuaScriptComponent>();
            lua.variables["health"] = PropertyValue(40.0);
            lua.variables["moveSpeed"] = PropertyValue(3.5);
        }
        if (!entity.HasComponent<TransformComponent>()) {
            return;
        }
        for (entt::entity childHandle : entity.GetComponent<TransformComponent>().children) {
            mergeVars(Entity(childHandle, &scene));
        }
    };
    mergeVars(instance);

    Entity shadeEntity = FindChildByTag(scene, instance, "Shade");
    ok &= Check(static_cast<bool>(shadeEntity), "shade child present");
    if (shadeEntity && shadeEntity.HasComponent<LuaScriptComponent>()) {
        const auto& vars = shadeEntity.GetComponent<LuaScriptComponent>().variables;
        auto health = vars.find("health");
        auto speed = vars.find("moveSpeed");
        auto glow = vars.find("glow");
        ok &= Check(health != vars.end() && health->second.IsNumber() &&
                        Near(static_cast<float>(*health->second.AsNumber()), 40.0f),
                    "spawn var health merged");
        ok &= Check(speed != vars.end() && speed->second.IsNumber() &&
                        Near(static_cast<float>(*speed->second.AsNumber()), 3.5f),
                    "spawn var moveSpeed merged");
        ok &= Check(glow != vars.end(), "prefab default glow var preserved");
    } else {
        ok &= Check(false, "shade has LuaScriptComponent for vars");
    }

    return ok;
}

bool TestHierarchySceneComponentTypes(const fs::path&) {
    bool ok = true;
    ok &= Check(PrefabComponentIO::IsHierarchySceneComponent("MeshRenderer"), "MeshRenderer is a hierarchy scene component");
    ok &= Check(PrefabComponentIO::IsHierarchySceneComponent("Light"), "Light is a hierarchy scene component");
    ok &= Check(PrefabComponentIO::IsHierarchySceneComponent("Camera"), "Camera is a hierarchy scene component");
    ok &= Check(PrefabComponentIO::IsHierarchySceneComponent("AudioSource"), "AudioSource is a hierarchy scene component");
    ok &= Check(!PrefabComponentIO::IsHierarchySceneComponent("RigidBody"), "RigidBody stays on the selected node");
    ok &= Check(!PrefabComponentIO::IsHierarchySceneComponent("Character"), "Character stays on the selected node");
    ok &= Check(!PrefabComponentIO::IsHierarchySceneComponent("LuaScript"), "LuaScript stays on the selected node");
    ok &= Check(!PrefabComponentIO::IsHierarchySceneComponent("PhysicsShape"), "PhysicsShape stays on the selected node");
    return ok;
}

bool MotionSynced(const RigidBodyComponent& body) {
    return body.isStatic == (body.motion == RigidBodyMotion::Static);
}

bool TestRigidBodyMotionInvariant(const fs::path& outDir) {
    bool ok = true;
    {
        const RigidBodyComponent built;
        ok &= Check(built.motion == RigidBodyMotion::Dynamic && MotionSynced(built), "construction is dynamic and synced");
        ok &= Check(MotionSynced(RigidBodyComponent::CreateStatic())
                        && RigidBodyComponent::CreateStatic().motion == RigidBodyMotion::Static
                        && RigidBodyComponent::CreateStatic().isStatic,
                    "CreateStatic is static");
        const RigidBodyComponent dynamic = RigidBodyComponent::CreateDynamic(4.0f);
        ok &= Check(dynamic.motion == RigidBodyMotion::Dynamic && !dynamic.isStatic && MotionSynced(dynamic),
                    "CreateDynamic is dynamic");
        const RigidBodyComponent kinematic = RigidBodyComponent::CreateKinematic();
        ok &= Check(kinematic.motion == RigidBodyMotion::Kinematic && !kinematic.isStatic && MotionSynced(kinematic),
                    "CreateKinematic is kinematic");
        RigidBodyComponent edited;
        edited.SetMotion(RigidBodyMotion::Static);
        ok &= Check(edited.motion == RigidBodyMotion::Static && edited.isStatic, "inspector SetMotion static");
        edited.SetMotion(RigidBodyMotion::Kinematic);
        ok &= Check(edited.motion == RigidBodyMotion::Kinematic && !edited.isStatic, "inspector SetMotion kinematic");
        edited.SetMotion(RigidBodyMotion::Dynamic);
        ok &= Check(edited.motion == RigidBodyMotion::Dynamic && !edited.isStatic, "inspector SetMotion dynamic");
    }

    auto applyBody = [](Entity entity, const std::string& json) {
        JsonValue data;
        std::string error;
        if (!JsonParser(json).Parse(data, error) || !data.IsObject()) {
            return false;
        }
        return PrefabComponentIO::ApplyComponentJson(entity, "RigidBody", data, true);
    };

    Scene authored("Authored");
    Entity componentSet = authored.CreateEntity("ComponentSet");
    ok &= Check(applyBody(componentSet, R"({"isStatic":true,"mass":2,"useGravity":true,"isTrigger":false})"),
                "component.set legacy static payload");
    if (componentSet.HasComponent<RigidBodyComponent>()) {
        const auto& body = componentSet.GetComponent<RigidBodyComponent>();
        ok &= Check(body.motion == RigidBodyMotion::Static && body.isStatic && body.layer == 0 && MotionSynced(body),
                    "legacy isStatic true becomes Static on layer 0");
    }
    ok &= Check(applyBody(componentSet, R"({"isStatic":false,"mass":3,"useGravity":true,"isTrigger":false})"),
                "component.set legacy dynamic payload");
    if (componentSet.HasComponent<RigidBodyComponent>()) {
        const auto& body = componentSet.GetComponent<RigidBodyComponent>();
        ok &= Check(body.motion == RigidBodyMotion::Dynamic && !body.isStatic && MotionSynced(body),
                    "legacy isStatic false becomes Dynamic");
    }
    ok &= Check(applyBody(componentSet, R"({"motion":"kinematic","isStatic":true,"mass":1,"isTrigger":false})"),
                "component.set contradictory motion");
    if (componentSet.HasComponent<RigidBodyComponent>()) {
        const auto& body = componentSet.GetComponent<RigidBodyComponent>();
        ok &= Check(body.motion == RigidBodyMotion::Kinematic && !body.isStatic && MotionSynced(body),
                    "explicit motion wins over isStatic");
        PrefabComponentEntry captured;
        ok &= Check(PrefabComponentIO::CaptureComponent(componentSet, "RigidBody", captured), "capture kinematic body");
        const JsonValue* savedStatic = captured.data.Find("isStatic");
        const JsonValue* savedMotion = captured.data.Find("motion");
        ok &= Check(savedStatic && savedStatic->IsBool() && !savedStatic->boolValue
                        && savedMotion && savedMotion->IsString() && savedMotion->AsString() == "kinematic",
                    "capture writes a matching isStatic mirror");
    }

    const fs::path scenePath = outDir / "OldRigidBody.hpscene";
    {
        std::ofstream sceneFile(scenePath);
        sceneFile << R"({
  "name": "OldRigidBodies",
  "entities": [
    {
      "id": 1,
      "name": "OldStatic",
      "components": {
        "RigidBody": { "isStatic": true, "mass": 5, "useGravity": true, "isTrigger": false }
      }
    },
    {
      "id": 2,
      "name": "OldDynamic",
      "components": {
        "RigidBody": { "isStatic": false, "mass": 2, "useGravity": true, "isTrigger": false }
      }
    }
  ]
})";
    }
    Scene loaded("OldScene");
    std::string error;
    ok &= Check(SceneSerializer::Deserialize(loaded, scenePath, error), "old scene loads through SceneSerializer: " + error);
    Entity oldStatic = FindByTag(loaded, "OldStatic");
    Entity oldDynamic = FindByTag(loaded, "OldDynamic");
    ok &= Check(static_cast<bool>(oldStatic) && static_cast<bool>(oldDynamic), "old scene entities exist");
    if (oldStatic && oldDynamic) {
        const auto& staticBody = oldStatic.GetComponent<RigidBodyComponent>();
        const auto& dynamicBody = oldDynamic.GetComponent<RigidBodyComponent>();
        ok &= Check(staticBody.motion == RigidBodyMotion::Static && staticBody.isStatic && staticBody.layer == 0
                        && MotionSynced(staticBody),
                    "old static scene body is Static on layer 0");
        ok &= Check(dynamicBody.motion == RigidBodyMotion::Dynamic && !dynamicBody.isStatic && dynamicBody.layer == 0
                        && MotionSynced(dynamicBody),
                    "old dynamic scene body is Dynamic on layer 0");
        const fs::path savedPath = outDir / "OldRigidBodySaved.hpscene";
        ok &= Check(SceneSerializer(loaded).Serialize(savedPath, error), "resave old scene: " + error);
        std::ifstream saved(savedPath);
        const std::string savedText((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        ok &= Check(savedText.find("\"motion\": \"static\"") != std::string::npos
                        && savedText.find("\"motion\": \"dynamic\"") != std::string::npos,
                    "resave writes motion");
        Scene roundTrip("RoundTrip");
        ok &= Check(SceneSerializer::Deserialize(roundTrip, savedPath, error), "saved scene reloads");
        Entity roundStatic = FindByTag(roundTrip, "OldStatic");
        ok &= Check(static_cast<bool>(roundStatic) && MotionSynced(roundStatic.GetComponent<RigidBodyComponent>())
                        && roundStatic.GetComponent<RigidBodyComponent>().motion == RigidBodyMotion::Static,
                    "scene round-trip stays synced");
        auto copies = loaded.DuplicateEntities({static_cast<entt::entity>(oldStatic)}, false);
        ok &= Check(!copies.empty() && copies[0].HasComponent<RigidBodyComponent>()
                        && copies[0].GetComponent<RigidBodyComponent>().motion == RigidBodyMotion::Static
                        && MotionSynced(copies[0].GetComponent<RigidBodyComponent>()),
                    "scene duplicate keeps motion and isStatic together");
    }

    const fs::path staticPrefab = outDir / "OldStatic.hpprefab";
    const fs::path dynamicPrefab = outDir / "OldDynamic.hpprefab";
    {
        std::ofstream file(staticPrefab);
        file << R"({
  "schema": "HephaestusPrefab",
  "assetVersion": 1,
  "guid": "old-static-body",
  "name": "OldStaticPrefab",
  "rootNodeId": "root",
  "nodes": [{
    "id": "root",
    "name": "OldStaticPrefab",
    "parentId": null,
    "transform": { "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1] },
    "components": [{
      "type": "RigidBody",
      "enabled": true,
      "data": { "mass": 0, "isStatic": true, "useGravity": false, "isTrigger": false }
    }],
    "exposed": []
  }]
})";
    }
    {
        std::ofstream file(dynamicPrefab);
        file << R"({
  "schema": "HephaestusPrefab",
  "assetVersion": 1,
  "guid": "old-dynamic-body",
  "name": "OldDynamicPrefab",
  "rootNodeId": "root",
  "nodes": [{
    "id": "root",
    "name": "OldDynamicPrefab",
    "parentId": null,
    "transform": { "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1] },
    "components": [{
      "type": "RigidBody",
      "enabled": true,
      "data": { "mass": 2, "isStatic": false, "useGravity": true, "isTrigger": false }
    }],
    "exposed": []
  }]
})";
    }

    PrefabAsset staticAsset;
    PrefabAsset dynamicAsset;
    ok &= Check(PrefabSerializer::Deserialize(staticAsset, staticPrefab, error), "load old static prefab: " + error);
    ok &= Check(PrefabSerializer::Deserialize(dynamicAsset, dynamicPrefab, error), "load old dynamic prefab: " + error);
    Scene prefabs("Prefabs");
    Entity staticInstance = InstantiateAt(prefabs, staticAsset, staticPrefab.generic_string(), Vec3(0, 0, 0));
    Entity dynamicInstance = InstantiateAt(prefabs, dynamicAsset, dynamicPrefab.generic_string(), Vec3(2, 0, 0));
    ok &= Check(static_cast<bool>(staticInstance) && static_cast<bool>(dynamicInstance), "instantiate old prefabs");
    if (staticInstance && dynamicInstance) {
        const auto& staticBody = staticInstance.GetComponent<RigidBodyComponent>();
        ok &= Check(staticBody.motion == RigidBodyMotion::Static && staticBody.isStatic && staticBody.layer == 0
                        && MotionSynced(staticBody),
                    "old prefab isStatic override loads as Static layer 0");
        dynamicInstance.GetComponent<RigidBodyComponent>().SetMotion(RigidBodyMotion::Static);
        PrefabOverrideSystem::SyncEntityOverrides(prefabs, dynamicInstance);
        const auto& instance = dynamicInstance.GetComponent<PrefabInstanceComponent>();
        const std::string nodeId = dynamicInstance.GetComponent<PrefabNodeComponent>().nodeId;
        ok &= Check(PrefabOverrideSystem::IsPropertyOverridden(instance, nodeId, "RigidBody", "motion")
                        || PrefabOverrideSystem::IsPropertyOverridden(instance, nodeId, "RigidBody", "isStatic"),
                    "prefab instance records the rigid-body override");
        const fs::path prefabScene = outDir / "OldPrefabInstance.hpscene";
        ok &= Check(SceneSerializer(prefabs).Serialize(prefabScene, error), "save prefab instance scene: " + error);
        Scene reloaded("ReloadedPrefabs");
        ok &= Check(SceneSerializer::Deserialize(reloaded, prefabScene, error), "reload prefab instance scene: " + error);
        Entity reloadedDynamic = FindByTag(reloaded, "OldDynamicPrefab");
        ok &= Check(static_cast<bool>(reloadedDynamic)
                        && reloadedDynamic.GetComponent<RigidBodyComponent>().motion == RigidBodyMotion::Static
                        && reloadedDynamic.GetComponent<RigidBodyComponent>().isStatic
                        && MotionSynced(reloadedDynamic.GetComponent<RigidBodyComponent>()),
                    "prefab instance override survives reload in sync");
        ok &= Check(applyBody(reloadedDynamic, R"({"motion":"kinematic","isStatic":true,"mass":1})"),
                    "apply contradictory motion on a prefab instance");
        ok &= Check(reloadedDynamic.GetComponent<RigidBodyComponent>().motion == RigidBodyMotion::Kinematic
                        && !reloadedDynamic.GetComponent<RigidBodyComponent>().isStatic
                        && MotionSynced(reloadedDynamic.GetComponent<RigidBodyComponent>()),
                    "prefab apply keeps motion authoritative");
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path repoRoot = (argc > 1) ? fs::path(argv[1]) : fs::current_path();
    const fs::path outDir = fs::temp_directory_path() / "hephaestus_prefab_tests";
    std::error_code ec;
    fs::remove_all(outDir, ec);
    fs::create_directories(outDir, ec);

    PrefabOverrideSystem::InvalidateAssetCache();

    int failed = 0;
    auto run = [&](const char* name, bool (*fn)(const fs::path&), const fs::path& arg) {
        std::cout << "[TEST] " << name << "\n";
        if (!fn(arg)) {
            std::cerr << "[FAIL] " << name << "\n";
            ++failed;
        } else {
            std::cout << "[PASS] " << name << "\n";
        }
        PrefabOverrideSystem::InvalidateAssetCache();
    };

    run("PrefabSaveLoad", TestPrefabSaveLoad, outDir);
    run("SceneRoundtripAndTransforms", TestSceneRoundtripAndTransforms, outDir);
    run("RefreshKeepsOverride", TestRefreshKeepsOverride, outDir);
    run("ApplyAndRevert", TestApplyAndRevert, outDir);
    run("LuaScriptApplyAndRevert", TestLuaScriptApplyAndRevert, outDir);
    run("LuaAuthoredSceneRoundtrip", TestLuaAuthoredSceneRoundtrip, outDir);
    run("CreateFromSelectionAndUnpack", TestCreateFromSelectionAndUnpack, outDir);
    run("CreatedPrefabPathsArePortable", TestCreatedPrefabPathsArePortable, outDir);
    run("MissingPrefab", TestMissingPrefab, outDir);
    run("LegacyBlueprint", TestLegacyBlueprint, repoRoot);
    run("InstantiatePoseAndVars", TestInstantiatePoseAndVars, outDir);
    run("HierarchySceneComponentTypes", TestHierarchySceneComponentTypes, outDir);
    run("RigidBodyMotionInvariant", TestRigidBodyMotionInvariant, outDir);

    if (failed > 0) {
        std::cerr << failed << " prefab test(s) failed\n";
        return 1;
    }
    std::cout << "All prefab tests passed\n";
    return 0;
}
