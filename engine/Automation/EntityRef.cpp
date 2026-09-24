#include "EntityRef.h"

#include "Scene/Scene.h"

namespace Hephaestus::Automation {

EntityRef EntityRefTable::Bind(Scene& scene, entt::entity handle)
{
    if (!scene.GetRegistry().valid(handle)) return {};

    for (const auto& [token, entry] : m_Entries) {
        if (entry.scene == &scene && entry.handle == handle) {
            return EntityRef{token};
        }
    }

    EntityRef ref{"entity-" + std::to_string(m_NextId++)};
    m_Entries.emplace(ref.value, Entry{&scene, handle});
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
    if (!activeScene || !activeScene->GetRegistry().valid(it->second.handle)) {
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
    if (m_ObservedScene == activeScene) return;
    m_ObservedScene = activeScene;
    Reset();
}

} // namespace Hephaestus::Automation
