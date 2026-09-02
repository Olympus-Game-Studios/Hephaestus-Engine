#include "InspectorSelectionModel.h"

#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/PrefabComponentIO.h"
#include "../Scene/Scene.h"
#include "../Scripting/LuaScriptComponent.h"
#include "../Scripting/ScriptComponent.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace Hephaestus {

void AccumulateScalar(MixedScalar& acc, double value, double epsilon)
{
    if (!acc.present) {
        acc.present = true;
        acc.mixed = false;
        acc.value = value;
        return;
    }
    if (!acc.mixed && std::abs(acc.value - value) > epsilon) {
        acc.mixed = true;
    }
}

void AccumulateBool(MixedBool& acc, bool value)
{
    if (!acc.present) {
        acc.present = true;
        acc.mixed = false;
        acc.value = value;
        return;
    }
    if (!acc.mixed && acc.value != value) {
        acc.mixed = true;
    }
}

void AccumulateString(MixedString& acc, const std::string& value)
{
    if (!acc.present) {
        acc.present = true;
        acc.mixed = false;
        acc.value = value;
        return;
    }
    if (!acc.mixed && acc.value != value) {
        acc.mixed = true;
    }
}

MixedVec3Axes MixVec3(const std::vector<Vec3>& values, double epsilon)
{
    MixedVec3Axes mixed;
    for (const Vec3& v : values) {
        AccumulateScalar(mixed.x, v.x, epsilon);
        AccumulateScalar(mixed.y, v.y, epsilon);
        AccumulateScalar(mixed.z, v.z, epsilon);
    }
    return mixed;
}

void WriteVec3Axis(Vec3& v, int axis, float value)
{
    if (axis == 0) {
        v.x = value;
    } else if (axis == 1) {
        v.y = value;
    } else {
        v.z = value;
    }
}

const std::vector<InspectorComponentDesc>& InspectorAddableComponents()
{
    static const std::vector<InspectorComponentDesc> kTypes = {
        { "MeshRenderer", "Mesh Renderer", "Rendering", false },
        { "Light", "Light", "Lighting", false },
        { "Camera", "Camera", "Camera", false },
        { "Environment", "Environment", "Lighting", true },
        { "RigidBody", "Rigid Body", "Physics", false },
        { "PhysicsShape", "Physics Shape", "Physics", false },
        { "MeshCollider", "Mesh Collider", "Physics", false },
        { "AudioSource", "Audio Source", "Audio", false },
        { "LuaScript", "Lua Script", "Scripting", false },
        { "ScriptComponent", "Script Component", "Scripting", false },
    };
    return kTypes;
}

const InspectorComponentDesc* FindInspectorComponentDesc(const std::string& id)
{
    for (const auto& desc : InspectorAddableComponents()) {
        if (id == desc.id) {
            return &desc;
        }
    }
    return nullptr;
}

std::string InspectorDisplayName(const std::string& id)
{
    if (const InspectorComponentDesc* desc = FindInspectorComponentDesc(id)) {
        return desc->displayName;
    }
    if (id == "Mesh Renderer") return "Mesh Renderer";
    if (id == "Mesh Collider") return "Mesh Collider";
    if (id == "Rigid Body") return "Rigid Body";
    if (id == "Physics Shape") return "Physics Shape";
    if (id == "Lua Scripts") return "Lua Script";
    return id;
}

bool InspectorEntityHasComponent(const entt::registry& reg, entt::entity entity, const std::string& type)
{
    if (!reg.valid(entity)) {
        return false;
    }
    if (type == "MeshRenderer" || type == "Mesh Renderer") {
        return reg.all_of<MeshRendererComponent>(entity);
    }
    if (type == "MeshCollider" || type == "Mesh Collider") {
        return reg.all_of<MeshColliderComponent>(entity);
    }
    if (type == "PhysicsShape" || type == "Physics Shape") {
        return reg.all_of<PhysicsShapeComponent>(entity);
    }
    if (type == "RigidBody" || type == "Rigid Body") {
        return reg.all_of<RigidBodyComponent>(entity);
    }
    if (type == "Light") {
        return reg.all_of<LightComponent>(entity);
    }
    if (type == "Camera") {
        return reg.all_of<CameraComponent>(entity);
    }
    if (type == "LuaScript" || type == "Lua Scripts") {
        return reg.all_of<LuaScriptComponent>(entity);
    }
    if (type == "ScriptComponent" || type == "Scripts") {
        return reg.all_of<ScriptComponent>(entity);
    }
    if (type == "AudioSource" || type == "Audio Source") {
        return reg.all_of<AudioSourceComponent>(entity);
    }
    if (type == "Environment") {
        return reg.all_of<EnvironmentComponent>(entity);
    }
    if (type == "Material") {
        return reg.all_of<MaterialComponent>(entity);
    }
    if (type == "Transform") {
        return reg.all_of<TransformComponent>(entity);
    }
    return false;
}

std::vector<std::string> InspectorComponentsOn(const entt::registry& reg, entt::entity entity)
{
    static const char* kInspectable[] = {
        "MeshRenderer", "MeshCollider", "PhysicsShape", "RigidBody",
        "Light", "Camera", "LuaScript", "ScriptComponent", "AudioSource", "Environment"
    };
    std::vector<std::string> out;
    for (const char* type : kInspectable) {
        if (InspectorEntityHasComponent(reg, entity, type)) {
            out.emplace_back(type);
        }
    }
    return out;
}

InspectorSelectionModel InspectorSelectionModel::Resolve(
    const std::vector<entt::entity>& selected,
    entt::entity primary,
    bool pinned,
    entt::entity pinnedEntity,
    const entt::registry* registry)
{
    InspectorSelectionModel model;
    model.pinned = pinned;
    model.pinnedEntity = pinnedEntity;

    auto appendValid = [registry, &model](entt::entity entity) {
        if (entity == entt::null) {
            return;
        }
        if (registry && !registry->valid(entity)) {
            return;
        }
        if (std::find(model.entities.begin(), model.entities.end(), entity) == model.entities.end()) {
            model.entities.push_back(entity);
        }
    };

    if (pinned) {
        if (!registry || pinnedEntity == entt::null || !registry->valid(pinnedEntity)) {
            model.pinnedMissing = true;
            return model;
        }
        model.entities.push_back(pinnedEntity);
        model.primary = pinnedEntity;
        return model;
    }

    for (entt::entity entity : selected) {
        appendValid(entity);
    }
    if (model.entities.empty() && primary != entt::null) {
        appendValid(primary);
    }
    if (primary != entt::null &&
        std::find(model.entities.begin(), model.entities.end(), primary) != model.entities.end()) {
        model.primary = primary;
    } else if (!model.entities.empty()) {
        model.primary = model.entities.front();
    }
    return model;
}

std::vector<std::string> InspectorSelectionModel::SharedComponents(const entt::registry& reg) const
{
    if (entities.empty()) {
        return {};
    }
    std::vector<std::string> shared = InspectorComponentsOn(reg, entities.front());
    for (size_t i = 1; i < entities.size(); ++i) {
        std::vector<std::string> next = InspectorComponentsOn(reg, entities[i]);
        std::unordered_set<std::string> nextSet(next.begin(), next.end());
        shared.erase(std::remove_if(shared.begin(), shared.end(),
                                    [&](const std::string& type) {
                                        return nextSet.find(type) == nextSet.end();
                                    }),
                     shared.end());
    }
    return shared;
}

std::vector<std::string> InspectorSelectionModel::PartialComponents(const entt::registry& reg) const
{
    if (entities.size() < 2) {
        return {};
    }
    const std::vector<std::string> shared = SharedComponents(reg);
    std::unordered_set<std::string> sharedSet(shared.begin(), shared.end());
    std::vector<std::string> partial;
    std::unordered_set<std::string> seen;
    for (entt::entity entity : entities) {
        for (const std::string& type : InspectorComponentsOn(reg, entity)) {
            if (sharedSet.count(type) || seen.count(type)) {
                continue;
            }
            seen.insert(type);
            partial.push_back(type);
        }
    }
    return partial;
}

static void AutoFitPhysicsShape(Scene& scene, Entity entity)
{
    if (!entity.HasComponent<PhysicsShapeComponent>() || !entity.HasComponent<MeshRendererComponent>()) {
        return;
    }
    auto& shape = entity.GetComponent<PhysicsShapeComponent>();
    const auto& renderer = entity.GetComponent<MeshRendererComponent>();
    const Vec3 mn = renderer.localBounds.min;
    const Vec3 mx = renderer.localBounds.max;
    Vec3 half((mx.x - mn.x) * 0.5f, (mx.y - mn.y) * 0.5f, (mx.z - mn.z) * 0.5f);
    shape.type = PhysicsShapeType::Box;
    shape.extents = Vec3(std::max(half.x, 0.001f), std::max(half.y, 0.001f), std::max(half.z, 0.001f));
    shape.offset = Vec3(0.0f, 0.0f, 0.0f);
    (void)scene;
}

int CountComponentsInScene(Scene& scene, const std::string& type)
{
    auto& reg = scene.GetRegistry();
    int count = 0;
    auto view = reg.view<TransformComponent>();
    for (auto entity : view) {
        if (InspectorEntityHasComponent(reg, entity, type)) {
            ++count;
        }
    }
    if (type == "Environment") {
        count = 0;
        auto envView = reg.view<EnvironmentComponent>();
        for (auto entity : envView) {
            (void)entity;
            ++count;
        }
    }
    return count;
}

BatchComponentResult BatchAddComponent(Scene& scene, const std::vector<entt::entity>& entities,
                                       const std::string& type)
{
    BatchComponentResult result;
    const InspectorComponentDesc* desc = FindInspectorComponentDesc(type);
    const bool sceneUnique = desc && desc->sceneUnique;
    if (sceneUnique && CountComponentsInScene(scene, type) > 0) {
        result.skippedUnique = static_cast<int>(entities.size());
        return result;
    }

    for (entt::entity handle : entities) {
        Entity entity(handle, &scene);
        if (!entity) {
            ++result.failed;
            continue;
        }
        if (PrefabComponentIO::HasComponent(entity, type) ||
            InspectorEntityHasComponent(scene.GetRegistry(), handle, type)) {
            ++result.skippedExisting;
            continue;
        }
        if (sceneUnique && result.added > 0) {
            ++result.skippedUnique;
            continue;
        }
        if (!PrefabComponentIO::AddComponent(entity, type) &&
            !InspectorEntityHasComponent(scene.GetRegistry(), handle, type)) {
            if (type == "AudioSource") {
                entity.AddComponent<AudioSourceComponent>();
            } else if (type == "Environment") {
                entity.AddComponent<EnvironmentComponent>();
            } else {
                ++result.failed;
                continue;
            }
        }
        if (type == "MeshRenderer" && !entity.HasComponent<MaterialComponent>()) {
            entity.AddComponent<MaterialComponent>();
        }
        if (type == "RigidBody" && !entity.HasComponent<PhysicsShapeComponent>()) {
            entity.AddComponent<PhysicsShapeComponent>();
            AutoFitPhysicsShape(scene, entity);
        }
        ++result.added;
    }
    return result;
}

BatchComponentResult BatchRemoveComponent(Scene& scene, const std::vector<entt::entity>& entities,
                                          const std::string& type)
{
    BatchComponentResult result;
    for (entt::entity handle : entities) {
        Entity entity(handle, &scene);
        if (!entity) {
            ++result.failed;
            continue;
        }
        if (!PrefabComponentIO::HasComponent(entity, type) &&
            !InspectorEntityHasComponent(scene.GetRegistry(), handle, type)) {
            ++result.skippedExisting;
            continue;
        }
        if (PrefabComponentIO::RemoveComponent(entity, type)) {
            if (type == "MeshRenderer" && entity.HasComponent<MaterialComponent>()) {
                entity.RemoveComponent<MaterialComponent>();
            }
            ++result.added;
            continue;
        }
        if (type == "AudioSource" && entity.HasComponent<AudioSourceComponent>()) {
            entity.RemoveComponent<AudioSourceComponent>();
            ++result.added;
            continue;
        }
        if (type == "Environment" && entity.HasComponent<EnvironmentComponent>()) {
            entity.RemoveComponent<EnvironmentComponent>();
            ++result.added;
            continue;
        }
        ++result.failed;
    }
    return result;
}

} // namespace Hephaestus
