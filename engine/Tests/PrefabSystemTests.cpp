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
    auto& material = outShade.AddComponent<MaterialComponent>();
    material.albedo = Vec3(0.8f, 0.6f, 0.2f);
    auto& collider = outShade.AddComponent<MeshColliderComponent>();
    collider.enabled = true;
    auto& shape = outShade.AddComponent<PhysicsShapeComponent>();
    shape.type = PhysicsShapeType::Box;
    shape.extents = Vec3(0.4f, 0.2f, 0.4f);
    auto& body = outShade.AddComponent<RigidBodyComponent>();
    body.mass = 2.5f;
    body.isStatic = false;
    auto& lua = outShade.AddComponent<LuaScriptComponent>();
    lua.scriptPath = "assets/scripts/Lamp.lua";
    lua.variables["glow"] = PropertyValue(1.0);
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
            auto glow = loadedShade.GetComponent<LuaScriptComponent>().variables.find("glow");
            ok &= Check(glow != loadedShade.GetComponent<LuaScriptComponent>().variables.end() &&
                        glow->second.IsNumber() && Near(static_cast<float>(*glow->second.AsNumber()), 4.0f),
                        "lua override survived reload");
        }
    }
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
    run("CreateFromSelectionAndUnpack", TestCreateFromSelectionAndUnpack, outDir);
    run("MissingPrefab", TestMissingPrefab, outDir);
    run("LegacyBlueprint", TestLegacyBlueprint, repoRoot);

    if (failed > 0) {
        std::cerr << failed << " prefab test(s) failed\n";
        return 1;
    }
    std::cout << "All prefab tests passed\n";
    return 0;
}
