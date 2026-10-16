#include "EntityRef.h"

#include "Scene/Scene.h"

#include <cstdint>

namespace Hephaestus::Automation {

EntityRef EntityRefTable::Bind(Scene& scene, entt::entity handle)
{
    if (!scene.GetRegistry().valid(handle)) return {};

    for (const auto& [token, entry] : m_Entries) {
        if (entry.scene == &scene && entry.sceneIdentity == scene.GetIdentity() &&
            entry.handle == handle &&
            entry.generation == scene.GetRuntimeGeneration()) {
            return EntityRef{token};
        }
    }

    EntityRef ref{"entity-" + std::to_string(m_NextId++)};
    m_Entries.emplace(ref.value, Entry{
        &scene, handle, scene.GetIdentity(), scene.GetRuntimeGeneration()});
    return ref;
}

ResolvedEntity EntityRefTable::Resolve(const EntityRef& ref, Scene* activeScene) const
{
    const auto it = m_Entries.find(ref.value);
    if (it == m_Entries.end()) {
        return {EntityResolutionStatus::UnknownRef, nullptr, entt::null};
    }
    if (it->second.scene != activeScene) {
        return {EntityResolutionStatus::SceneChanged, nullptr, entt::null};
    }
    if (!activeScene || it->second.sceneIdentity != activeScene->GetIdentity()) {
        return {EntityResolutionStatus::SceneChanged, nullptr, entt::null};
    }
    if (it->second.generation != activeScene->GetRuntimeGeneration()) {
        return {EntityResolutionStatus::Destroyed, activeScene, entt::null};
    }
    if (!activeScene->GetRegistry().valid(it->second.handle)) {
        return {EntityResolutionStatus::Destroyed, activeScene, entt::null};
    }
    return {EntityResolutionStatus::Resolved, activeScene, it->second.handle};
}

void EntityRefTable::Erase(const EntityRef& ref)
{
    m_Entries.erase(ref.value);
}

void EntityRefTable::Reset()
{
    m_Entries.clear();
}

void EntityRefTable::ObserveActiveScene(Scene* activeScene)
{
    const std::uint64_t identity = activeScene ? activeScene->GetIdentity() : 0;
    if (m_ObservedScene == activeScene && m_ObservedSceneIdentity == identity) return;
    m_ObservedScene = activeScene;
    m_ObservedSceneIdentity = identity;
    Reset();
}

} // namespace Hephaestus::Automation
