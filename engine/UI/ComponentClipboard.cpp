#include "ComponentClipboard.h"

#include "../Scene/Entity.h"
#include "../Scene/PrefabComponentIO.h"
#include "../Scene/Scene.h"
#include "InspectorSelectionModel.h"

namespace Hephaestus {

ComponentClipboard& ComponentClipboard::Instance()
{
    static ComponentClipboard s_instance;
    return s_instance;
}

bool ComponentClipboard::CopyFrom(Entity entity, const std::string& type)
{
    m_hasComponent = false;
    m_entry = {};
    if (!entity) {
        return false;
    }
    std::string id = type;
    if (id == "Mesh Renderer") id = "MeshRenderer";
    else if (id == "Mesh Collider") id = "MeshCollider";
    else if (id == "Rigid Body") id = "RigidBody";
    else if (id == "Physics Shape") id = "PhysicsShape";
    else if (id == "Lua Scripts" || id == "Lua Script") id = "LuaScript";
    else if (id == "Scripts") id = "ScriptComponent";
    else if (id == "Audio Source") id = "AudioSource";

    if (!PrefabComponentIO::CaptureComponent(entity, id, m_entry)) {
        return false;
    }
    m_hasComponent = true;
    return true;
}

int ComponentClipboard::PasteComponent(Scene& scene, const std::vector<entt::entity>& entities) const
{
    if (!m_hasComponent) {
        return 0;
    }
    int applied = 0;
    for (entt::entity handle : entities) {
        Entity entity(handle, &scene);
        if (!entity) {
            continue;
        }
        if (!PrefabComponentIO::HasComponent(entity, m_entry.type) &&
            !InspectorEntityHasComponent(scene.GetRegistry(), handle, m_entry.type)) {
            BatchAddComponent(scene, { handle }, m_entry.type);
        }
        if (PrefabComponentIO::ApplyEntry(entity, m_entry)) {
            ++applied;
        }
    }
    return applied;
}

int ComponentClipboard::PasteValues(Scene& scene, const std::vector<entt::entity>& entities) const
{
    if (!m_hasComponent) {
        return 0;
    }
    int applied = 0;
    for (entt::entity handle : entities) {
        Entity entity(handle, &scene);
        if (!entity) {
            continue;
        }
        if (!PrefabComponentIO::HasComponent(entity, m_entry.type) &&
            !InspectorEntityHasComponent(scene.GetRegistry(), handle, m_entry.type)) {
            continue;
        }
        if (PrefabComponentIO::ApplyEntry(entity, m_entry)) {
            ++applied;
        }
    }
    return applied;
}

void ComponentClipboard::CopyNumber(double value)
{
    m_propertyKind = PropertyKind::Number;
    m_number = value;
}

void ComponentClipboard::CopyBool(bool value)
{
    m_propertyKind = PropertyKind::Bool;
    m_bool = value;
}

void ComponentClipboard::CopyString(const std::string& value)
{
    m_propertyKind = PropertyKind::String;
    m_string = value;
}

void ComponentClipboard::CopyVec3(const Vec3& value)
{
    m_propertyKind = PropertyKind::Vec3;
    m_vec3 = value;
}

} // namespace Hephaestus
